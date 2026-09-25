#include <virtualitemviews/virtualtableview.h>

#include <virtualitemviews/virtualheaderview.h>

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QWheelEvent>
#include <QDebug>

#include <limits>

namespace viv {

namespace {

/// Offset of one band of the row-number strip: the content y its top edge shows. The
/// strip sits on its pane rect, so a band below the frozen rows is shifted by the pane's
/// y - that is what keeps the numbers glued to their rows (§31 row direction).
qint64 rowStripOffset(const ItemPane &pane, qint64 verticalOffset, qint64 contentExtent)
{
    switch (pane.type) {
    case ItemPane::Type::FrozenTop:
        return 0;
    case ItemPane::Type::FrozenBottom:
        return qMax<qint64>(0, contentExtent - pane.viewportRect.height());
    case ItemPane::Type::Scrollable:
        return verticalOffset + qMax<qint64>(0, pane.viewportRect.y());
    }
    return verticalOffset;
}

/// Above this row count the native vertical header cannot mirror per-row heights
/// cheaply (QHeaderView keeps an O(rows) position cache plus a Section per row
/// here), so per-row mirroring is disabled; the widget header (v0.5) removes the
/// limit. One million rows cost about 8 MB of mirror state.
constexpr qsizetype kRowHeaderMirrorLimit = 1000000;

/// Magic/version of the table level header state (HeaderGeometry state plus the
/// frozen pane sets, §31/§32).
constexpr quint32 kTableStateMagic = 0x56495654; // 'VIVT'
/// 1: column state + frozen column sets. 2: adds the frozen row counts (§31 row
/// direction); a version 1 state still restores (its rows then default to 0).
constexpr quint32 kTableStateVersion = 2;
constexpr quint32 kTableStateVersionWithFrozenRows = 2;

/// Framework owned clipping container of a pane (§31). It paints nothing, so a
/// business row widget keeps its own background; Qt clips the children of a
/// widget to its rect, which is exactly what keeps the scrollable columns from
/// painting under a frozen pane. Masks cannot do this, because a mask does not
/// clip child widgets.
///
/// With several scroll groups (§43 "advanced panes") every scrolling pane has its
/// own container: the desktop-wide rule "one clip per scrolling pane" is what
/// keeps two groups from painting over each other.
class PaneClipHost : public QWidget
{
public:
    /// Property that carries the pane index of the container (diagnostics and the
    /// row mode lookup, which cannot afford a stale widget pointer key).
    static const char *paneIndexProperty() { return "vivPaneIndex"; }

    explicit PaneClipHost(int paneIndex, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivPaneClipHost"));
        setProperty(paneIndexProperty(), paneIndex);
        setFocusPolicy(Qt::NoFocus);
    }

    int paneIndex() const { return property(paneIndexProperty()).toInt(); }
};

/// The vertical line between two panes in the body (§31). It is a 1 px overlay:
/// the row widgets/cells cover the viewport, so a line painted by the viewport
/// itself would be hidden behind them. Input passes through.
class PaneSeparatorLine : public QWidget
{
public:
    explicit PaneSeparatorLine(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivPaneSeparatorLine"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
    }

    void setSeparator(const PaneSeparatorStyle &style, const QColor &styleSeparatorColor)
    {
        if (m_style.width == style.width && m_style.color == style.color
            && m_style.lineStyle == style.lineStyle && m_resolvedColor == styleSeparatorColor) {
            return;
        }
        m_style = style;
        m_resolvedColor = styleSeparatorColor;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        NativeHeaderView::drawPaneSeparator(&painter, rect(), m_style, m_resolvedColor);
    }

private:
    PaneSeparatorStyle m_style;
    QColor m_resolvedColor;
};

} // namespace

VirtualTableView::VirtualTableView(QWidget *parent)
    : VirtualItemView(parent)
{
    // Tables select whole rows by default (§45: row selection).
    setSelectionBehavior(SelectionBehavior::SelectRows);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_columns = new HeaderGeometry(Qt::Horizontal, this);
    m_rowHeaders = new HeaderGeometry(Qt::Vertical, this);
    m_panes.setGeometry(m_columns);

    m_rowLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_rowLayout, true);

    // The recycler creates both row widgets and cell widgets, so the table owns
    // the factory and dispatches on the current materialization mode.
    recycler()->setFactory([this](WidgetType type, QWidget *parent) -> QWidget * {
        if (m_materializationMode == MaterializationMode::CellWidgets && m_cellAdapter)
            return m_cellAdapter->createCellWidget(type, parent);
        return m_tableAdapter ? m_tableAdapter->createWidget(type, parent) : nullptr;
    });

    ensureHeaders();

    connect(m_columns, &HeaderGeometry::geometryChanged, this,
            &VirtualTableView::onHeaderGeometryChanged);
    // The pane layout caches every column x, so it follows the offset - this
    // covers plain scroll bar drags as well as setHorizontalOffset().
    connect(m_columns, &HeaderGeometry::offsetChanged, this,
            [this](qint64) { updatePaneLayout(); });
    connect(m_rowHeaders, &HeaderGeometry::sectionResized, this,
            &VirtualTableView::onVerticalSectionResized);
    connect(m_columns, &HeaderGeometry::sortIndicatorChanged, this,
            &VirtualTableView::onSortIndicatorChanged);
}

VirtualTableView::~VirtualTableView()
{
    // Teardown order matters. Cells are unbound first (they need the cell
    // adapter), then every derived pane renderer - a widget pane header borrows
    // the primary header's adapter, so it must be gone before that adapter is
    // destroyed - then the primary renderers (which may own the header adapter),
    // and only then the adapters themselves.
    recycleAllCells();
    for (HeaderViewInterface *&paneHeader : m_paneHeaders) {
        deleteHeader(paneHeader);
    }
    m_paneHeaders.clear();
    deleteHeader(m_frozenTopRowsHeader);
    deleteHeader(m_frozenBottomRowsHeader);
    if (m_ownHorizontalHeader)
        deleteHeader(m_horizontalHeader);
    if (m_ownVerticalHeader)
        deleteHeader(m_verticalHeader);
    if (m_ownTableAdapter)
        delete m_tableAdapter;
    if (m_ownCellAdapter)
        delete m_cellAdapter;
    if (m_ownSpanProvider)
        delete m_spanProvider;
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

void VirtualTableView::deleteHeader(HeaderViewInterface *&header)
{
    if (!header)
        return;
    // Delete through the interface: HeaderViewInterface does not require the
    // renderer to be the QWidget itself (a composed renderer would leak its
    // wrapper when only the widget is deleted), and the virtual destructor is
    // what releases the widget.
    delete header;
    header = nullptr;
}

void VirtualTableView::ensureHeaders()
{
    if (!m_horizontalHeader) {
        m_horizontalHeader = new NativeHeaderView(Qt::Horizontal, this);
        m_ownHorizontalHeader = true;
        m_horizontalHeader->setGeometryModel(m_columns);
        m_horizontalHeader->setLabelModel(model());
        m_horizontalHeader->setSortInteractionEnabled(m_sortingEnabled);
        applyHeaderAnimationSettings();
    }
    if (!m_verticalHeader) {
        m_verticalHeader = new NativeHeaderView(Qt::Vertical, this);
        m_ownVerticalHeader = true;
        m_verticalHeader->setGeometryModel(m_rowHeaders);
        m_verticalHeader->setLabelModel(model());
    }
    if (auto *native = qobject_cast<QHeaderView *>(m_verticalHeader->headerWidget())) {
        // A user drag on the row-number strip writes an explicit row height.
        connect(native, &QHeaderView::sectionResized, this,
                &VirtualTableView::onVerticalHeaderUserResized, Qt::UniqueConnection);
    }
}

void VirtualTableView::setHorizontalHeader(HeaderViewInterface *header)
{
    if (!header) {
        auto *native = new NativeHeaderView(Qt::Horizontal, this);
        native->setGeometryModel(m_columns);
        native->setLabelModel(model());
        header = native;
    }
    if (m_horizontalHeader == header)
        return;
    // The derived pane renderers were cloned from the installed header (a widget
    // header hands them its adapter), so they have to be destroyed before the
    // header that owns that adapter.
    for (HeaderViewInterface *&paneHeader : m_paneHeaders) {
        deleteHeader(paneHeader);
    }
    m_paneHeaders.clear();
    if (m_ownHorizontalHeader && m_horizontalHeader)
        deleteHeader(m_horizontalHeader);
    m_horizontalHeader = header;
    m_ownHorizontalHeader = true;
    m_horizontalHeader->setGeometryModel(m_columns);
    m_horizontalHeader->setLabelModel(model());
    // The header is part of the view, so the pane rects and the viewport origin mean
    // the same thing for it as for the body. A renderer created by the application
    // is usually parentless - a top level window - and would then be placed in
    // screen coordinates (off by its frame margins, and not clipped by the view).
    m_horizontalHeader->headerWidget()->setParent(this);
    applyHeaderAnimationSettings();
    layoutHeaderWidgets();
    syncHeaderPanes();
}

void VirtualTableView::setVerticalHeader(HeaderViewInterface *header)
{
    if (!header) {
        auto *native = new NativeHeaderView(Qt::Vertical, this);
        native->setGeometryModel(m_rowHeaders);
        native->setLabelModel(model());
        header = native;
    }
    if (m_verticalHeader == header)
        return;
    deleteHeader(m_frozenTopRowsHeader);
    deleteHeader(m_frozenBottomRowsHeader);
    if (m_ownVerticalHeader && m_verticalHeader)
        deleteHeader(m_verticalHeader);
    m_verticalHeader = header;
    m_ownVerticalHeader = true;
    m_verticalHeader->setGeometryModel(m_rowHeaders);
    m_verticalHeader->setLabelModel(model());
    m_verticalHeader->headerWidget()->setParent(this);
    syncVerticalPaneHeaders();
    layoutHeaderWidgets();
}

void VirtualTableView::setHorizontalHeaderVisible(bool visible)
{
    if (m_horizontalHeaderVisible == visible)
        return;
    m_horizontalHeaderVisible = visible;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::setVerticalHeaderVisible(bool visible)
{
    if (m_verticalHeaderVisible == visible)
        return;
    m_verticalHeaderVisible = visible;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::setHeaderHeight(int height)
{
    const int clamped = qMax(0, height);
    if (m_headerHeight == clamped)
        return;
    m_headerHeight = clamped;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::setVerticalHeaderWidth(int width)
{
    const int clamped = qMax(0, width);
    if (m_verticalHeaderWidth == clamped)
        return;
    m_verticalHeaderWidth = clamped;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::layoutHeaderWidgets()
{
    const int headerHeight = (m_horizontalHeaderVisible && m_horizontalHeader) ? m_headerHeight : 0;
    const int rowHeaderWidth = (m_verticalHeaderVisible && m_verticalHeader) ? m_verticalHeaderWidth : 0;

    setViewportMargins(rowHeaderWidth, headerHeight, 0, 0);

    const QRect viewportRect = viewport()->geometry();
    // Every pane has its own header renderer, positioned on the pane rectangle
    // (§43): the primary pane's header is m_horizontalHeader, the others live in
    // m_paneHeaders indexed by pane index.
    const QVector<TablePane> &panes = m_panes.panes();
    for (int paneIndex = 0;
         paneIndex < m_paneHeaders.size() && paneIndex < panes.size(); ++paneIndex) {
        HeaderViewInterface *header = m_paneHeaders.at(paneIndex);
        if (!header)
            continue;
        const QRect paneRect = panes.at(paneIndex).viewportRect;
        QWidget *widget = header->headerWidget();
        widget->setGeometry(viewportRect.x() + paneRect.x(), viewportRect.y() - headerHeight,
                            qMax(0, paneRect.width()), headerHeight);
        widget->setVisible(headerHeight > 0 && paneRect.width() > 0);
        widget->raise();
    }
    if (m_horizontalHeader) {
        // The primary (scrolling) pane: it is the one the scroll bar drives.
        QRect primaryRect = m_panes.paneRect(TablePane::Type::Scrollable);
        for (const TablePane &pane : panes) {
            if (pane.type == TablePane::Type::Scrollable) {
                primaryRect = pane.viewportRect;
                break;
            }
        }
        QWidget *widget = m_horizontalHeader->headerWidget();
        widget->setGeometry(viewportRect.x() + primaryRect.x(), viewportRect.y() - headerHeight,
                            qMax(0, primaryRect.width()), headerHeight);
        widget->setVisible(headerHeight > 0);
    }
    // A widget based header derives its own coordinates from the geometry.
    const QPoint origin = viewportRect.topLeft();
    if (m_horizontalHeader)
        m_horizontalHeader->setViewportOrigin(origin);
    for (HeaderViewInterface *header : m_paneHeaders) {
        if (header)
            header->setViewportOrigin(origin);
    }
    if (m_verticalHeader) {
        layoutVerticalHeaderStrips();
    }
    m_headersLaidOut = true;
}

void VirtualTableView::layoutVerticalHeaderStrips()
{
    if (!m_verticalHeader)
        return;
    // The row-number strip mirrors the row panes: every band is its own renderer, placed
    // on its pane rectangle, so the numbers stay glued to their rows even when some rows
    // are frozen (§31 row direction).
    const int rowHeaderWidth = (m_verticalHeaderVisible && m_verticalHeader)
        ? m_verticalHeaderWidth
        : 0;
    const QRect viewportRect = viewport()->geometry();
    const QVector<ItemPane> panes = itemPanes();
    for (const ItemPane &pane : panes) {
        HeaderViewInterface *header = m_verticalHeader;
        if (pane.type == ItemPane::Type::FrozenTop)
            header = m_frozenTopRowsHeader;
        else if (pane.type == ItemPane::Type::FrozenBottom)
            header = m_frozenBottomRowsHeader;
        if (!header)
            continue;
        QWidget *strip = header->headerWidget();
        // The pane rects are viewport relative, the strips live in the view: the viewport's
        // own origin is the bridge (with one pane - nothing frozen - the pane rect *is* the
        // viewport rect, so this covers both cases).
        const QRect rect = pane.viewportRect;
        strip->setGeometry(viewportRect.x() - rowHeaderWidth, viewportRect.y() + rect.y(),
                           rowHeaderWidth, rect.height());
        strip->setVisible(rowHeaderWidth > 0 && rect.height() > 0);
        if (header != m_verticalHeader)
            strip->raise();
    }
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void VirtualTableView::setModel(QAbstractItemModel *model)
{
    QAbstractItemModel *previous = VirtualItemView::model();
    if (previous == model)
        return;
    if (previous)
        disconnect(previous, nullptr, this, nullptr);

    VirtualItemView::setModel(model);
    // Persistent cell indexes of the old model are invalid now.
    recycleAllCells();
    connectColumnSignals(model);
    if (model) {
        // Connected *after* the kernel's own handlers, so the row widgets are
        // released first and the cells are still bound to a valid index here.
        connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                &VirtualTableView::onRowsAboutToBeRemovedForCells);
        connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this,
                &VirtualTableView::onColumnsAboutToBeRemovedForCells);
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this,
                &VirtualTableView::onModelAboutToBeResetForCells);
    }
    if (m_horizontalHeader)
        m_horizontalHeader->setLabelModel(model);
    if (m_verticalHeader)
        m_verticalHeader->setLabelModel(model);

    m_columns->setSectionCount(model ? model->columnCount() : 0);
    m_rowHeaders->setDefaultSectionSize(uniformItemHeight() > 0 ? uniformItemHeight()
                                                               : estimatedItemHeight());
    m_explicitRowHeights.clear();
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::connectColumnSignals(QAbstractItemModel *model)
{
    if (!model)
        return;
    connect(model, &QAbstractItemModel::columnsInserted, this, &VirtualTableView::onColumnsInserted);
    connect(model, &QAbstractItemModel::columnsRemoved, this, &VirtualTableView::onColumnsRemoved);
    connect(model, &QAbstractItemModel::columnsMoved, this, &VirtualTableView::onColumnsMoved);
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        m_columns->setSectionCount(columnCount());
    });
}

void VirtualTableView::onColumnsInserted(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid())
        return;
    // The inserted columns carry the default state; every column after them keeps
    // its own width / visibility / explicit size, and the frozen sets, the pane
    // specs and the sort indicator follow the columns they name.
    const int count = qMax(0, last - first + 1);
    m_columns->insertLogicalSections(first, count);
    m_panes.insertLogicalColumns(first, count);
}

void VirtualTableView::onColumnsRemoved(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid())
        return;
    const int count = qMax(0, last - first + 1);
    m_columns->removeLogicalSections(first, count);
    m_panes.removeLogicalColumns(first, count);
}

void VirtualTableView::onColumnsMoved(const QModelIndex &parent, int start, int end,
                                      const QModelIndex &destinationParent, int destinationColumn)
{
    if (parent.isValid() || destinationParent.isValid()) {
        m_columns->setSectionCount(columnCount());
        return;
    }
    const int count = qMax(0, end - start + 1);
    m_columns->moveLogicalSections(start, count, destinationColumn);
    m_panes.moveLogicalColumns(start, count, destinationColumn);
}

// ---------------------------------------------------------------------------
// Identity mapping (rows)
// ---------------------------------------------------------------------------

qsizetype VirtualTableView::viewItemCount() const
{
    QAbstractItemModel *m = model();
    return m ? qMax<qsizetype>(0, m->rowCount()) : 0;
}

QModelIndex VirtualTableView::viewIndex(qsizetype item, int column) const
{
    QAbstractItemModel *m = model();
    if (!m || item < 0 || item >= m->rowCount())
        return QModelIndex();
    return m->index(int(item), column);
}

qsizetype VirtualTableView::viewItemForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !model() || index.parent().isValid())
        return -1;
    if (index.row() < 0 || index.row() >= model()->rowCount())
        return -1;
    return index.row();
}

bool VirtualTableView::isLayoutParent(const QModelIndex &parent) const
{
    return !parent.isValid();
}

QModelIndex VirtualTableView::indexForNavigation(qsizetype item, const QModelIndex &current) const
{
    const QModelIndex target = viewIndex(item);
    if (!target.isValid() || !current.isValid())
        return target;
    // Vertical navigation keeps the current column.
    return target.siblingAtColumn(qBound(0, current.column(), qMax(0, columnCount() - 1)));
}

// ---------------------------------------------------------------------------
// Columns: geometry queries and state (HeaderGeometry is the authority)
// ---------------------------------------------------------------------------

int VirtualTableView::columnCount() const
{
    QAbstractItemModel *m = model();
    return m ? m->columnCount() : 0;
}

ColumnGeometry VirtualTableView::columnGeometry(int logicalIndex) const
{
    ColumnGeometry geometry = m_columns->columnGeometry(logicalIndex);
    // Pane aware (§31): frozen columns keep their own x, the scrollable ones are
    // shifted by the horizontal offset.
    const int x = m_panes.columnViewportX(logicalIndex);
    if (geometry.isValid() && x >= 0)
        geometry.viewportX = x;
    return geometry;
}

int VirtualTableView::columnWidth(int logicalIndex) const
{
    return m_columns->sectionSize(logicalIndex);
}

VisibleRange VirtualTableView::visibleColumns() const
{
    // The scrolling panes own the horizontal windows; frozen columns are always
    // visible and are reported by visibleColumnLogicalIndexes(). With several
    // scroll groups (§43) the range is the union of their windows - it may then
    // contain frozen or hidden indices in between, exactly like
    // HeaderGeometry::visibleVisualRange().
    return m_panes.visibleScrollableRange();
}

QVector<int> VirtualTableView::visibleColumnLogicalIndexes() const
{
    return layoutContext(QRect(0, 0, viewport()->width(), viewport()->height())).columnsToLayout();
}

void VirtualTableView::setColumnOverscan(int columns)
{
    const int clamped = qMax(0, columns);
    if (m_columnOverscan == clamped)
        return;
    m_columnOverscan = clamped;
    updateColumnLayout();
}

void VirtualTableView::setColumnWidth(int logicalIndex, int width)
{
    m_columns->resizeSection(logicalIndex, width);
}

void VirtualTableView::setColumnHidden(int logicalIndex, bool hidden)
{
    m_columns->setSectionHidden(logicalIndex, hidden);
}

bool VirtualTableView::isColumnHidden(int logicalIndex) const
{
    return m_columns->isSectionHidden(logicalIndex);
}

void VirtualTableView::moveColumn(int fromLogicalIndex, int toLogicalIndex, MoveAnimation animation)
{
    const int fromVisual = m_columns->visualIndex(fromLogicalIndex);
    const int toVisual = m_columns->visualIndex(toLogicalIndex);
    if (fromVisual < 0 || toVisual < 0)
        return;
    // A programmatic reorder is immediate unless the caller asks for the visual
    // transition (§23): code that sets an order should not get an animation nobody
    // requested, and the flag is consumed by the very next relayout of each renderer.
    const bool animate = animation == MoveAnimation::Animate;
    if (animate)
        requestSectionMoveAnimation(true);
    m_columns->moveSection(fromVisual, toVisual);
    if (animate)
        requestSectionMoveAnimation(false);
}

void VirtualTableView::setDefaultColumnWidth(int width)
{
    m_columns->setDefaultSectionSize(width);
}

int VirtualTableView::defaultColumnWidth() const
{
    return m_columns->defaultSectionSize();
}

void VirtualTableView::setColumnMinimumWidth(int width)
{
    m_columns->setMinimumSectionSize(width);
}

void VirtualTableView::setColumnMaximumWidth(int width)
{
    m_columns->setMaximumSectionSize(width);
}

void VirtualTableView::setStretchLastColumn(bool stretch)
{
    m_columns->setStretchLastSection(stretch);
}

bool VirtualTableView::stretchLastColumn() const
{
    return m_columns->stretchLastSection();
}

// ---------------------------------------------------------------------------
// Horizontal scrolling
// ---------------------------------------------------------------------------

qint64 VirtualTableView::horizontalOffset() const
{
    return m_columns->viewportOffset();
}

qint64 VirtualTableView::maximumHorizontalOffset() const
{
    // Only the scrollable pane scrolls: frozen columns are never hidden by the
    // horizontal offset (§31).
    return m_panes.maximumOffset();
}

qint64 VirtualTableView::horizontalContentExtent() const
{
    return m_columns->totalExtent();
}

void VirtualTableView::setHorizontalOffset(qint64 offset)
{
    const qint64 clamped = qBound<qint64>(qint64(0), offset, maximumHorizontalOffset());
    if (clamped == m_columns->viewportOffset())
        return;
    m_columns->setViewportOffset(clamped);
    // The offset change only shifts the header; the row widgets have to be told.
    updateColumnLayout();
    syncHorizontalScrollBar();
    emit horizontalOffsetChanged(clamped);
    emit columnGeometryChanged();
}

void VirtualTableView::scrollByHorizontalPixels(qint64 pixels)
{
    if (pixels == 0)
        return;
    setHorizontalOffset(m_columns->viewportOffset() + pixels);
}

void VirtualTableView::setHorizontalWheelPixels(int pixels)
{
    m_horizontalWheelPixels = qMax(1, pixels);
}

// ---------------------------------------------------------------------------
// Frozen columns (§31)
// ---------------------------------------------------------------------------

void VirtualTableView::setFrozenColumns(const QVector<int> &logicalColumns)
{
    if (m_panes.frozenColumns() == logicalColumns)
        return;
    m_panes.setFrozenColumns(logicalColumns);
    updatePaneLayout();
    emit columnGeometryChanged();
}

void VirtualTableView::setFrozenRightColumns(const QVector<int> &logicalColumns)
{
    if (m_panes.frozenRightColumns() == logicalColumns)
        return;
    m_panes.setFrozenRightColumns(logicalColumns);
    updatePaneLayout();
    emit columnGeometryChanged();
}

void VirtualTableView::clearFrozenColumns()
{
    if (!m_panes.hasFrozenColumns())
        return;
    m_panes.setFrozenColumns(QVector<int>());
    m_panes.setFrozenRightColumns(QVector<int>());
    updatePaneLayout();
    emit columnGeometryChanged();
}

void VirtualTableView::setPaneSeparatorStyle(const PaneSeparatorStyle &style)
{
    if (m_paneSeparatorStyle.width == style.width && m_paneSeparatorStyle.color == style.color
        && m_paneSeparatorStyle.lineStyle == style.lineStyle) {
        return;
    }
    m_paneSeparatorStyle = style;
    // One look for both directions: the row pane boundary gets the same width, pen style and
    // (optional) explicit colour, so a table never shows two different kinds of boundary line.
    setItemPaneSeparatorStyle(style);
    syncHeaderPanes();
    syncPaneSeparatorLines();
    emit columnGeometryChanged();
}

void VirtualTableView::setHeaderAnimationEnabled(bool enabled)
{
    if (m_headerAnimationEnabled == enabled)
        return;
    m_headerAnimationEnabled = enabled;
    applyHeaderAnimationSettings();
}

void VirtualTableView::setHeaderAnimationDuration(int ms)
{
    const int clamped = qMax(0, ms);
    if (m_headerAnimationDuration == clamped)
        return;
    m_headerAnimationDuration = clamped;
    applyHeaderAnimationSettings();
}

void VirtualTableView::applyHeaderAnimationSettings()
{
    // The primary header plus every pane renderer (§43): the setting is a property
    // of the view, not of one renderer.
    if (m_horizontalHeader) {
        m_horizontalHeader->setSectionAnimationEnabled(m_headerAnimationEnabled);
        m_horizontalHeader->setSectionAnimationDuration(m_headerAnimationDuration);
    }
    for (HeaderViewInterface *header : m_paneHeaders) {
        if (!header)
            continue;
        header->setSectionAnimationEnabled(m_headerAnimationEnabled);
        header->setSectionAnimationDuration(m_headerAnimationDuration);
    }
}

void VirtualTableView::requestSectionMoveAnimation(bool animated)
{
    // Every renderer of the view gets the request, and the one whose order does not
    // change keeps it until the next move clears it - the flag is one-shot, so it can
    // never leak into an unrelated change.
    if (m_horizontalHeader)
        m_horizontalHeader->setSectionMoveAnimated(animated);
    for (HeaderViewInterface *header : m_paneHeaders) {
        if (header)
            header->setSectionMoveAnimated(animated);
    }
}

void VirtualTableView::updatePaneLayout()
{
    const bool changed = m_panes.update(viewport()->width(), viewport()->height());
    syncHeaderPanes();
    syncPaneSeparatorLines();
    if (changed) {
        // Which rows/columns belong to the window changed (the scrollable pane
        // shrank or grew) and the header panes moved.
        layoutHeaderWidgets();
        if (m_materializationMode == MaterializationMode::CellWidgets)
            markDirty();
    }
    // Every column x may have moved: rows, cells and the scroll bar follow.
    updateColumnLayout();
    syncHorizontalScrollBar();
}

void VirtualTableView::syncHeaderPanes()
{
    const QVector<TablePane> &panes = m_panes.panes();
    // One header renderer per pane (§43 "advanced panes"), indexed by pane index.
    // The primary (scrolling) pane keeps the installed horizontal header.
    for (int index = panes.size(); index < m_paneHeaders.size(); ++index) {
        deleteHeader(m_paneHeaders[index]);
    }
    m_paneHeaders.resize(panes.size());
    int primaryIndex = -1;
    for (int index = 0; index < panes.size(); ++index) {
        if (panes.at(index).type != TablePane::Type::Scrollable)
            continue;
        primaryIndex = index;
        break;
    }

    bool anyOtherPane = false;
    for (int paneIndex = 0; paneIndex < panes.size(); ++paneIndex) {
        HeaderViewInterface *&header = m_paneHeaders[paneIndex];
        // The primary pane keeps the installed horizontal header, and a pane
        // without columns shows nothing: an entry that was a pane renderer before
        // the pane list changed must not stay behind as a second header.
        if (paneIndex == primaryIndex || panes.at(paneIndex).logicalColumns.isEmpty()) {
            if (header) {
                deleteHeader(header);
            }
            continue;
        }
        const TablePane &pane = panes.at(paneIndex);
        if (!header) {
            // The pane header is another renderer of the same geometry (§31),
            // and it is of the same kind as the installed horizontal header so a
            // widget based header can render every pane as well.
            header = createHorizontalPaneHeader();
            header->setGeometryModel(m_columns);
            header->setLabelModel(model());
            header->setSortInteractionEnabled(m_sortingEnabled);
        }
        // A frozen pane is pinned (offset 0); a scrolling pane of a group other
        // than the primary one follows its own group offset (§43 "advanced
        // panes"), so its header stays aligned with the body.
        header->setPaneFilter(pane.logicalColumns, pane.isFrozen());
        header->setPaneOffset(pane.isFrozen() ? 0 : m_panes.groupOffset(pane.scrollGroup));
        anyOtherPane = true;
    }
    if (!m_horizontalHeader)
        return;
    if (primaryIndex >= 0 && anyOtherPane)
        m_horizontalHeader->setPaneFilter(panes.at(primaryIndex).logicalColumns, false);
    else
        m_horizontalHeader->clearPaneFilter();
    applyHeaderAnimationSettings();
}

HeaderViewInterface *VirtualTableView::createHorizontalPaneHeader()
{
    if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(m_horizontalHeader)) {
        auto *header = new VirtualHeaderView(Qt::Horizontal, this);
        header->setAdapter(widgetHeader->adapter());
        header->setSectionOverscan(widgetHeader->sectionOverscan());
        return header;
    }
    return new NativeHeaderView(Qt::Horizontal, this);
}

HeaderViewInterface *VirtualTableView::createVerticalPaneHeader()
{
    if (!m_verticalHeader)
        return nullptr;
    // Only a native strip can be cloned; a custom renderer cannot be asked to reproduce
    // itself, so the strip then stays single (documented in docs/row-freezing.md).
    if (!qobject_cast<QHeaderView *>(m_verticalHeader->headerWidget()))
        return nullptr;
    auto *header = new NativeHeaderView(Qt::Vertical, this);
    header->setGeometryModel(m_rowHeaders);
    header->setLabelModel(model());
    connect(header, &QHeaderView::sectionResized, this,
            &VirtualTableView::onVerticalHeaderUserResized, Qt::UniqueConnection);
    return header;
}

void VirtualTableView::syncVerticalPaneHeaders()
{
    if (!m_verticalHeader)
        return;
    const QVector<ItemPane> panes = itemPanes();
    HeaderViewInterface *&top = m_frozenTopRowsHeader;
    HeaderViewInterface *&bottom = m_frozenBottomRowsHeader;
    const bool wantsTop = panes.size() > 1 && itemPaneRect(ItemPane::Type::FrozenTop).height() > 0;
    const bool wantsBottom = panes.size() > 1
        && itemPaneRect(ItemPane::Type::FrozenBottom).height() > 0;

    const auto drop = [this](HeaderViewInterface *&header) {
        deleteHeader(header);
    };
    if (!wantsTop)
        drop(top);
    else if (!top)
        top = createVerticalPaneHeader();
    if (!wantsBottom)
        drop(bottom);
    else if (!bottom)
        bottom = createVerticalPaneHeader();

    // The installed strip goes back to following the geometry when nothing is frozen:
    // an unused feature must change nothing at all.
    if (panes.size() <= 1) {
        if (qobject_cast<QHeaderView *>(m_verticalHeader->headerWidget()))
            m_verticalHeader->setPaneOffset(HeaderViewInterface::kFollowGeometryOffset);
    }
}

void VirtualTableView::syncPaneSeparatorLines()
{
    // One line per pane boundary (§43: pane count - 1).
    QVector<int> boundaries;
    QVector<bool> insidePrecedingPane;
    const QVector<TablePane> &panes = m_panes.panes();
    for (int index = 0; index + 1 < panes.size(); ++index) {
        const TablePane &before = panes.at(index);
        const TablePane &after = panes.at(index + 1);
        if (before.viewportRect.width() <= 0 || after.viewportRect.width() <= 0)
            continue;
        boundaries.append(before.viewportRect.right() + 1);
        // A frozen pane keeps the hair line inside itself, so the line stays
        // continuous with the pane it belongs to (the default left boundary);
        // every other boundary sits on the first pixel of the following pane.
        insidePrecedingPane.append(before.type != TablePane::Type::Scrollable
                                   && after.type == TablePane::Type::Scrollable);
    }

    while (m_paneSeparatorLines.size() > boundaries.size()) {
        QWidget *line = m_paneSeparatorLines.takeLast();
        line->hide();
        line->setParent(nullptr); // leave nothing behind while it is deleted
        line->deleteLater();
    }
    // The line is a child of the view (not of the viewport): it has to cover the
    // header strip too, and the header widgets are siblings of the viewport.
    while (m_paneSeparatorLines.size() < boundaries.size())
        m_paneSeparatorLines.append(new PaneSeparatorLine(this));

    const QColor styleColor = boundaries.isEmpty()
        ? QColor()
        : NativeHeaderView::sectionSeparatorColor(this);
    // The line covers the header strip *and* the body, so it works for every
    // header renderer (native or widget based) and looks continuous.
    const int headerTop = viewport()->geometry().y()
        - ((m_horizontalHeaderVisible && m_horizontalHeader) ? m_headerHeight : 0);
    const int lineTop = qMax(0, headerTop);
    const int lineHeight = qMax(0, viewport()->geometry().y() + viewport()->height() - lineTop);
    const int lineOriginX = viewport()->geometry().x();
    const int band = qMax(0, m_paneSeparatorStyle.width);
    for (int i = 0; i < boundaries.size(); ++i) {
        auto *line = static_cast<PaneSeparatorLine *>(m_paneSeparatorLines.at(i));
        line->setSeparator(m_paneSeparatorStyle, styleColor);
        // Same rule as the header: the band lies inside the frozen pane, so the
        // two lines are continuous. A dashed line still needs a 1 px band to
        // draw on.
        const int lineWidth = m_paneSeparatorStyle.lineStyle == Qt::SolidLine ? band : qMax(1, band);
        const int x = lineOriginX + (insidePrecedingPane.at(i) ? boundaries.at(i) - lineWidth
                                                              : boundaries.at(i));
        line->setGeometry(x, lineTop, lineWidth, lineHeight);
        line->setVisible(m_paneSeparatorStyle.isVisible() && lineHeight > 0);
    }
    // A pane header created after the line would sit above it, and the lines
    // have to cover the header strip.
    raisePaneSeparatorLines();
}

void VirtualTableView::raisePaneSeparatorLines()
{
    // The items are (re)created by every materialization pass, so the lines have
    // to be lifted above them again. They are 1 px wide, paint nothing else and
    // let input through, so they can sit on top of everything.
    for (QWidget *line : m_paneSeparatorLines)
        line->raise();
}

void VirtualTableView::syncHorizontalScrollBar()
{
    QScrollBar *bar = horizontalScrollBar();
    const int viewportWidth = viewport()->width();
    const qint64 maximum = maximumHorizontalOffset();

    const QSignalBlocker blocker(bar);
    bar->setRange(0, int(qMin<qint64>(maximum, qint64(std::numeric_limits<int>::max()))));
    bar->setPageStep(qMax(1, viewportWidth));
    bar->setSingleStep(qMax(1, viewportWidth / 20));
    const int value = int(qBound<qint64>(qint64(0), m_columns->viewportOffset(), maximum));
    bar->setValue(value);
    if (m_columns->viewportOffset() != value)
        m_columns->setViewportOffset(value);
}

// ---------------------------------------------------------------------------
// Row heights and the vertical header
// ---------------------------------------------------------------------------

int VirtualTableView::rowHeight(qsizetype row) const
{
    return m_rowLayout ? m_rowLayout->itemSize(row) : 0;
}

void VirtualTableView::setRowSizePolicy(RowSizePolicy policy)
{
    if (m_rowSizePolicy == policy)
        return;
    m_rowSizePolicy = policy;
    markDirty();
}

void VirtualTableView::setRowHeight(qsizetype row, int height)
{
    if (!m_rowLayout || row < 0 || row >= viewItemCount())
        return;
    const int clamped = qMax(1, height);
    if (rowHeight(row) == clamped && hasExplicitRowHeight(row))
        return;
    // A uniform-height table whose row is resized has to become variable: all
    // other rows keep their height (that height becomes the estimate).
    if (itemHeightMode() == ItemHeightMode::Uniform && uniformItemHeight() > 0)
        setEstimatedItemHeight(uniformItemHeight());
    if (itemHeightMode() == ItemHeightMode::Uniform)
        setItemHeightMode(ItemHeightMode::Variable);
    m_rowLayout->setItemSize(row, clamped);
    const QModelIndex index = viewIndex(row);
    if (index.isValid())
        m_explicitRowHeights.insert(QPersistentModelIndex(index), clamped);
    updateRowHeaderGeometry();
    // Deferred: setRowHeight() may be called from a header signal, and the
    // relayout rebuilds the materialized set.
    markDirty();
    emit rowHeightChanged(row, clamped);
}

void VirtualTableView::clearRowHeight(qsizetype row)
{
    const QModelIndex index = viewIndex(row);
    if (!index.isValid())
        return;
    const QPersistentModelIndex persistent(index);
    if (!m_explicitRowHeights.contains(persistent))
        return;
    m_explicitRowHeights.remove(persistent);
    // Dropping the marker has to give the row its measured / estimated size back
    // *now*: waiting for the next materialization would leave the row at the
    // height the user just cleared. The visual position stays stable.
    if (m_rowLayout) {
        int restored = 0;
        for (const MaterializedItem &item : materializedItems()) {
            if (item.index == persistent) {
                restored = measuredHeightOf(item);
                break;
            }
        }
        if (restored <= 0)
            restored = qMax(1, estimateItemSize(row));
        if (m_rowLayout->itemSize(row) != restored) {
            const ScrollAnchor anchor = captureAnchor();
            m_rowLayout->setItemSize(row, restored);
            setPendingAnchor(anchor);
        }
    }
    updateRowHeaderGeometry();
    markDirty();
}

bool VirtualTableView::hasExplicitRowHeight(qsizetype row) const
{
    const QModelIndex index = viewIndex(row);
    return index.isValid() && m_explicitRowHeights.contains(QPersistentModelIndex(index));
}

bool VirtualTableView::canMeasureItem(qsizetype item) const
{
    if (m_rowSizePolicy == RowSizePolicy::MeasuredWins)
        return true;
    return !hasExplicitRowHeight(item);
}

void VirtualTableView::onVerticalSectionResized(int logicalIndex, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    if (logicalIndex < 0 || qsizetype(logicalIndex) >= viewItemCount())
        return;
    if (m_rowHeaderUpdateActive)
        return; // programmatic mirroring of the layout into the geometry
    if (rowHeight(logicalIndex) == newSize)
        return;
    // The user dragged the row boundary: the row height becomes explicit.
    setRowHeight(logicalIndex, newSize);
}

void VirtualTableView::onVerticalHeaderUserResized(int row, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    if (m_rowHeaderUpdateActive)
        return; // programmatic mirroring, not a user gesture
    if (m_verticalHeader) {
        if (auto *native = dynamic_cast<NativeHeaderView *>(m_verticalHeader->headerWidget())) {
            if (native->isApplyingGeometry())
                return; // the geometry is being pushed into the header
        }
    }
    if (row < 0 || qsizetype(row) >= viewItemCount())
        return;
    setRowHeight(row, newSize);
}

void VirtualTableView::updateRowHeaderOffset()
{
    if (!m_rowHeaders)
        return;
    // Row panes can appear or disappear with a single call (setFrozenRows()), so the
    // strips are reconciled first, then placed, then given their offsets (§31).
    syncVerticalPaneHeaders();
    // The frozen bands change the pane rectangles, so the strips are placed first (§31).
    layoutVerticalHeaderStrips();
    const QVector<ItemPane> panes = itemPanes();
    if (panes.size() <= 1) {
        // Unchanged: one strip, the geometry carries the offset, so a custom renderer
        // that reads HeaderGeometry keeps working.
        m_rowHeaders->setViewportOffset(verticalOffset());
        return;
    }
    // Frozen rows: every band shows a different content range, and one geometry offset
    // cannot express three. Each strip therefore owns its offset (an explicit pane
    // offset wins over the geometry, see NativeHeaderView::setPaneOffset()), while the
    // geometry keeps the scrolling band's mapping for custom renderers.
    m_rowHeaders->setViewportOffset(verticalOffset() + frozenTopExtent());
    for (const ItemPane &pane : panes) {
        HeaderViewInterface *header = m_verticalHeader;
        if (pane.type == ItemPane::Type::FrozenTop)
            header = m_frozenTopRowsHeader;
        else if (pane.type == ItemPane::Type::FrozenBottom)
            header = m_frozenBottomRowsHeader;
        if (header)
            header->setPaneOffset(rowStripOffset(pane, verticalOffset(), contentExtent()));
    }
}

void VirtualTableView::updateRowHeaderGeometry()
{
    if (!m_rowHeaders)
        return;
    ensureHeaders();
    if (m_rowHeaderUpdateActive)
        return;
    m_rowHeaderUpdateActive = true;

    const qsizetype count = viewItemCount();
    const bool variable = itemHeightMode() == ItemHeightMode::Variable;
    if (variable && count > kRowHeaderMirrorLimit) {
        // Per-row heights cannot be mirrored into a native header at this scale
        // without an O(rows) structure; the row-number strip is disabled and the
        // widget header (v0.5) will lift this limitation.
        if (!m_verticalHeaderDisabled) {
            m_verticalHeaderDisabled = true;
            qWarning("VirtualItemViews: vertical header disabled for %lld variable-height rows; "
                     "use uniform row heights or a widget header (v0.5).",
                     qint64(count));
            setVerticalHeaderVisible(false);
        }
        m_rowHeaderUpdateActive = false;
        return;
    }

    const int defaultSize = (!variable && uniformItemHeight() > 0) ? uniformItemHeight()
                                                                  : estimatedItemHeight();
    if (m_rowHeaders->defaultSectionSize() != defaultSize)
        m_rowHeaders->setDefaultSectionSize(defaultSize);
    // The row header has to be able to express every height the kernel can produce (it
    // clamps to 1 px). With the geometry's default minimum of 24 px the mirrored sizes
    // would be clamped and the row numbers would drift away from their rows.
    if (m_rowHeaders->minimumSectionSize() != 1)
        m_rowHeaders->setMinimumSectionSize(1);

    // Mirror per-row heights only when they are needed: variable-height rows, or
    // rows the user resized explicitly. A uniform table keeps an empty geometry
    // (default section size = row height) and costs no memory.
    const bool mirror = count > 0 && count <= kRowHeaderMirrorLimit
        && (variable || !m_explicitRowHeights.isEmpty());
    if (!mirror) {
        // Uniform heights: the default section size is exact and cheap, so no
        // per-row mirroring is needed (the header keeps the sections it gets
        // from the model).
        if (m_rowHeaders->sectionCount() != 0)
            m_rowHeaders->setSectionCount(0);
        m_rowHeaderUpdateActive = false;
        return;
    }
    if (m_rowHeaders->sectionCount() != int(count))
        m_rowHeaders->setSectionCount(int(count));

    // Collect the differences first: applying them emits sectionResized, and the
    // resulting handler may relayout and rebuild the materialized set.
    QVector<QPair<qsizetype, int>> pending;
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        const int size = rowHeight(row);
        if (size > 0 && m_rowHeaders->storedSectionSize(int(row)) != size)
            pending.append({row, size});
    }
    // m_explicitRowHeights is a *policy* marker, never a second truth: under
    // RowSizePolicy::MeasuredWins a measurement may legally diverge from the
    // height the user set, and the strip has to follow the committed layout (the
    // body) rather than write the stale explicit value back.
    for (const QPair<qsizetype, int> &entry : pending) {
        if (m_rowHeaders->storedSectionSize(int(entry.first)) != entry.second)
            m_rowHeaders->resizeSection(int(entry.first), entry.second);
    }
    m_rowHeaderUpdateActive = false;
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------

void VirtualTableView::setSortingEnabled(bool enabled)
{
    if (m_sortingEnabled == enabled)
        return;
    m_sortingEnabled = enabled;
    ensureHeaders();
    m_horizontalHeader->setSortInteractionEnabled(enabled);
    if (!enabled)
        m_columns->setSortIndicator(-1, Qt::AscendingOrder);
}

void VirtualTableView::sortByColumn(int logicalIndex, Qt::SortOrder order)
{
    QAbstractItemModel *m = model();
    if (!m || logicalIndex < 0 || logicalIndex >= columnCount())
        return;
    m_sortGuard = true;
    m_columns->setSortIndicator(logicalIndex, order);
    m_sortGuard = false;
    emit sortIndicatorRequested(logicalIndex, order);
    m->sort(logicalIndex, order);
}

void VirtualTableView::setSortIndicator(int logicalIndex, Qt::SortOrder order)
{
    m_columns->setSortIndicator(logicalIndex, order);
}

void VirtualTableView::onSortIndicatorChanged(int logicalIndex, Qt::SortOrder order)
{
    if (m_sortGuard || !m_sortingEnabled || logicalIndex < 0)
        return;
    QAbstractItemModel *m = model();
    if (!m)
        return;
    emit sortIndicatorRequested(logicalIndex, order);
    m->sort(logicalIndex, order);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

QByteArray VirtualTableView::saveHeaderState() const
{
    // Table level state: the column state of HeaderGeometry (single source of
    // truth, §32) plus the frozen pane sets, which are a table concept (§31).
    const QByteArray columnState = m_columns->saveState();

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << kTableStateMagic << kTableStateVersion;
    stream << quint32(columnState.size());
    stream.writeRawData(columnState.constData(), int(columnState.size()));
    const QVector<int> panes[2] = {m_panes.frozenColumns(), m_panes.frozenRightColumns()};
    for (const QVector<int> &columns : panes) {
        stream << qint32(columns.size());
        for (int logical : columns)
            stream << qint32(logical);
    }
    // v2: which rows are pinned is user state as well (§31 row direction), so it travels
    // with the column state.
    stream << qint32(frozenRows()) << qint32(frozenBottomRows());
    return state;
}

bool VirtualTableView::restoreHeaderState(const QByteArray &state)
{
    QDataStream stream(state);
    stream.setVersion(QDataStream::Qt_5_15);

    quint32 magic = 0;
    quint32 version = 0;
    quint32 columnStateSize = 0;
    stream >> magic >> version >> columnStateSize;
    if (stream.status() != QDataStream::Ok || magic != kTableStateMagic
        || version < 1 || version > kTableStateVersionWithFrozenRows
        || int(columnStateSize) > state.size()) {
        // Not a table level state: accept a bare HeaderGeometry state so a state
        // saved before the pane sets existed keeps working.
        return m_columns->restoreState(state);
    }

    QByteArray columnState(int(columnStateSize), Qt::Uninitialized);
    if (stream.readRawData(columnState.data(), int(columnStateSize)) != int(columnStateSize))
        return false;
    if (!m_columns->restoreState(columnState))
        return false;

    QVector<int> frozenLeft;
    QVector<int> frozenRight;
    for (QVector<int> *columns : {&frozenLeft, &frozenRight}) {
        qint32 count = 0;
        stream >> count;
        if (stream.status() != QDataStream::Ok || count < 0 || count > columnCount())
            return false;
        for (qint32 i = 0; i < count; ++i) {
            qint32 logical = -1;
            stream >> logical;
            if (stream.status() != QDataStream::Ok || logical < 0 || logical >= columnCount())
                return false;
            columns->append(int(logical));
        }
    }

    // Version 1 has no frozen row counts: those rows simply stay unfrozen.
    qint32 frozenTopRows = 0;
    qint32 frozenBottomRows = 0;
    if (version >= kTableStateVersionWithFrozenRows) {
        stream >> frozenTopRows >> frozenBottomRows;
        if (stream.status() != QDataStream::Ok || frozenTopRows < 0 || frozenBottomRows < 0)
            return false;
    }

    m_panes.setFrozenColumns(frozenLeft);
    m_panes.setFrozenRightColumns(frozenRight);
    setFrozenRows(int(frozenTopRows));
    setFrozenBottomRows(int(frozenBottomRows));
    updatePaneLayout();
    flushPendingRelayout();
    emit columnGeometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Adapter
// ---------------------------------------------------------------------------

void VirtualTableView::setTableAdapter(TableWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_tableAdapter == adapter) {
        m_ownTableAdapter = m_ownTableAdapter || takeOwnership;
        return;
    }
    // setAdapter() recycles the row widgets through the *old* adapter and drops
    // the pool, so the old adapter may only be deleted after that call. Deleting
    // it first would let the kernel call unbindWidget() on a freed object.
    TableWidgetAdapter *previous = m_ownTableAdapter ? m_tableAdapter : nullptr;
    if (previous == adapter)
        previous = nullptr;
    m_ownTableAdapter = false;
    m_tableAdapter = adapter;
    setAdapter(adapter, false); // the kernel uses the same adapter
    delete previous;
    m_ownTableAdapter = takeOwnership;
}

TableWidgetAdapter *VirtualTableView::tableAdapter() const
{
    return m_tableAdapter;
}

// ---------------------------------------------------------------------------
// Materialization mode (Row Widget Mode / Cell Widget Mode)
// ---------------------------------------------------------------------------

void VirtualTableView::setMaterializationMode(MaterializationMode mode)
{
    if (m_materializationMode == mode)
        return;
    // Both directions have to release the widgets owned by the old mode: the
    // kernel does not recycle row widgets while cell mode is active.
    recycleAllCells();
    recycleAllItems();
    // Row widgets and cell widgets live in the same recycler, and both use
    // WidgetType 0 by default: a pool entry of the old mode must never be handed
    // out as a widget of the new mode.
    if (recycler())
        recycler()->clear();
    m_materializationMode = mode;
    relayout();
}

void VirtualTableView::setCellAdapter(CellWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_cellAdapter == adapter) {
        m_ownCellAdapter = m_ownCellAdapter || takeOwnership;
        return;
    }
    // Recycle (unbind with the old adapter) and drop the pool *before* the old
    // adapter is deleted - same reasoning as setAdapter()/setTableAdapter().
    recycleAllCells();
    if (recycler())
        recycler()->clear();
    if (m_ownCellAdapter) {
        delete m_cellAdapter;
        m_cellAdapter = nullptr;
    }
    m_cellAdapter = adapter;
    m_ownCellAdapter = takeOwnership;
    relayout();
}

bool VirtualTableView::usesItemWidgets() const
{
    return m_materializationMode == MaterializationMode::RowWidgets;
}

QVector<int> VirtualTableView::columnsForCellMaterialization() const
{
    // Frozen columns are always materialized, the scrollable ones inside the
    // visible window widened by the overscan (§31).
    return m_panes.columnsForLayout(m_columnOverscan);
}

QRect VirtualTableView::cellRect(qsizetype row, int logicalColumn) const
{
    if (!m_rowLayout || !m_columns)
        return QRect();
    const ColumnGeometry column = columnGeometry(logicalColumn);
    if (!column.isValid() || column.hidden || column.width <= 0)
        return QRect();
    // Pane aware: a frozen row is pinned, a scrolling one follows the offset
    // (§31 row direction, docs/row-freezing.md).
    const QRect rowRect = geometryForViewRow(row);
    if (rowRect.height() <= 0)
        return QRect();
    return QRect(column.viewportX, rowRect.y(), column.width, rowRect.height());
}

// ---------------------------------------------------------------------------
// Spans (§43 "spans", see docs/spans.md)
// ---------------------------------------------------------------------------

int VirtualTableView::itemPaneSeparatorLeftExtension() const
{
    // The row-number strip is outside the viewport: the row pane boundary lines cross it
    // just like the column boundary lines cross the header strip.
    return (m_verticalHeaderVisible && m_verticalHeader) ? qMax(0, m_verticalHeaderWidth) : 0;
}

QColor VirtualTableView::itemPaneSeparatorColor() const
{
    // Exactly what the column boundary uses: the colour the current style paints a section
    // separator with (probed by rendering one). A custom style therefore keeps both
    // directions in step, and an explicit colour in the separator style still wins.
    return NativeHeaderView::sectionSeparatorColor(this);
}

void VirtualTableView::setPanes(const QVector<TablePaneSpec> &panes)
{
    if (m_panes.paneSpecs() == panes)
        return;
    m_panes.setPaneSpecs(panes);
    // The clip containers are keyed by pane index, so they are dropped and
    // recreated for the new list before the layout runs.
    syncCellPaneClipHosts();
    for (const MaterializedItem &item : materializedItems()) {
        if (item.widget)
            dropStaleRowPaneClipHosts(item.widget, QSet<int>());
    }
    updatePaneLayout();
    markDirty();
}

void VirtualTableView::setHorizontalOffset(int scrollGroup, qint64 offset)
{
    if (scrollGroup < 0)
        return;
    if (scrollGroup == m_panes.primaryScrollGroup()) {
        // The primary group is carried by the committed header geometry (header,
        // scroll bar and every geometry query), so it is set through the view.
        setHorizontalOffset(offset);
        return;
    }
    const qint64 clamped = qBound<qint64>(qint64(0), offset, maximumHorizontalOffset(scrollGroup));
    if (m_panes.groupOffset(scrollGroup) == clamped)
        return;
    m_panes.setGroupOffset(scrollGroup, clamped);
    updatePaneLayout();
    emit horizontalOffsetChanged(clamped);
}

QVector<QRect> VirtualTableView::paneSeparatorRects() const
{
    QVector<QRect> rects;
    rects.reserve(m_paneSeparatorLines.size());
    for (QWidget *line : m_paneSeparatorLines) {
        if (line->isVisible())
            rects.append(line->geometry());
    }
    return rects;
}

void VirtualTableView::setSpanProvider(TableSpanProvider *provider, bool takeOwnership)
{
    if (m_spanProvider != provider) {
        if (m_ownSpanProvider)
            delete m_spanProvider;
        m_spanProvider = provider;
        m_ownSpanProvider = provider && takeOwnership;
    } else if (takeOwnership) {
        m_ownSpanProvider = true;
    }
    // Which cells exist and where they sit changed.
    markDirty();
    updateColumnLayout();
}

void VirtualTableView::setSpan(int row, int column, int rowSpan, int columnSpan)
{
    QAbstractItemModel *tableModel = model();
    if (!tableModel)
        return;
    const QModelIndex anchor = tableModel->index(row, column);
    if (!anchor.isValid())
        return;
    auto *map = dynamic_cast<TableSpanMap *>(m_spanProvider);
    if (!map) {
        map = new TableSpanMap;
        setSpanProvider(map, true);
    }
    map->setSpan(anchor, rowSpan, columnSpan);
    markDirty();
    updateColumnLayout();
}

void VirtualTableView::removeSpan(int row, int column)
{
    auto *map = dynamic_cast<TableSpanMap *>(m_spanProvider);
    QAbstractItemModel *tableModel = model();
    if (!map || !tableModel)
        return;
    map->removeSpan(tableModel->index(row, column));
    markDirty();
    updateColumnLayout();
}

void VirtualTableView::clearSpans()
{
    auto *map = dynamic_cast<TableSpanMap *>(m_spanProvider);
    if (!map)
        return;
    map->clearSpans();
    markDirty();
    updateColumnLayout();
}

TableSpan VirtualTableView::spanAt(const QModelIndex &index) const
{
    if (!m_spanProvider || !index.isValid())
        return TableSpan();
    return m_spanProvider->spanAt(index);
}

QModelIndex VirtualTableView::anchorIndex(const QModelIndex &index) const
{
    if (!m_spanProvider || !index.isValid())
        return index;
    return m_spanProvider->anchorOf(index);
}

bool VirtualTableView::isSpanCovered(const QModelIndex &index) const
{
    if (!index.isValid())
        return false;
    const QModelIndex anchor = anchorIndex(index);
    return anchor.isValid() && anchor != index;
}

QRect VirtualTableView::spanRect(const QModelIndex &index) const
{
    QAbstractItemModel *tableModel = model();
    if (!m_columns || !m_rowLayout || !tableModel || !index.isValid())
        return QRect();
    // Only an anchor owns pixels: a covered cell has no rectangle of its own.
    if (anchorIndex(index) != index)
        return QRect();

    const TableSpan span = spanAt(index);
    const int lastRow = qMin(index.row() + qMax(1, span.rowSpan) - 1,
                             tableModel->rowCount(index.parent()) - 1);

    // Columns: walk the visual order from the anchor, inside its pane only. A
    // hidden column contributes no width (no compensation), a pane boundary ends
    // the merge (§31/§43).
    const int paneIndex = m_panes.paneIndexOfColumn(index.column());
    if (paneIndex < 0)
        return QRect();
    const int visual = m_columns->visualIndex(index.column());
    if (visual < 0)
        return QRect();
    int taken = 0;
    bool hasColumn = false;
    int left = -1;
    int right = -1;
    for (int candidate = visual;
         candidate < m_columns->sectionCount() && taken < qMax(1, span.columnSpan);
         ++candidate) {
        const int logical = m_columns->logicalIndex(candidate);
        if (logical < 0)
            break;
        ++taken;
        const ColumnGeometry geometry = columnGeometry(logical);
        // A hidden column contributes no width and does not end the merge, so it
        // is skipped before the pane boundary is checked.
        if (!geometry.isValid() || geometry.hidden || geometry.width <= 0)
            continue;
        if (m_panes.paneIndexOfColumn(logical) != paneIndex)
            break;
        // A column scrolled (partly) out of the viewport has a negative x: it
        // still contributes, the parent clips the result.
        if (!hasColumn) {
            hasColumn = true;
            left = geometry.viewportX;
            right = geometry.viewportX + geometry.width;
        } else {
            left = qMin(left, geometry.viewportX);
            right = qMax(right, geometry.viewportX + geometry.width);
        }
    }
    if (!hasColumn || right <= left)
        return QRect();

    const QRect first = m_rowLayout->itemRect(index.row(), verticalOffset());
    if (first.height() <= 0)
        return QRect();
    const QRect last = lastRow >= index.row()
        ? m_rowLayout->itemRect(lastRow, verticalOffset())
        : QRect();
    const int bottom = last.height() > 0 ? last.bottom() : first.bottom();
    return QRect(left, first.top(), right - left, bottom - first.top() + 1);
}

QRect VirtualTableView::cellRect(const QModelIndex &index) const
{
    if (!index.isValid())
        return QRect();
    // A covered cell has no rect: hit testing and layout always use the anchor.
    if (anchorIndex(index) != index)
        return QRect();
    return spanRect(index);
}

QWidget *VirtualTableView::createCellWidget(const QPersistentModelIndex &index)
{
    if (!m_cellAdapter)
        return nullptr;

    const QModelIndex modelIndex(index);
    const WidgetType type = m_cellAdapter->cellWidgetType(modelIndex);
    QWidget *widget = recycler()->acquire(type);
    if (!widget)
        return nullptr;

    if (widget->parentWidget() != viewport())
        widget->setParent(viewport());

    // Never show a cell before it has been bound to its new index.
    widget->hide();
    m_cellAdapter->bindCellWidget(widget, modelIndex);
    m_cellTypes.insert(widget, type);
    return widget;
}

void VirtualTableView::recycleCell(const QPersistentModelIndex &index, QWidget *widget)
{
    if (!widget || !m_cellAdapter)
        return;
    m_cellAdapter->unbindCellWidget(widget, QModelIndex(index));
    widget->hide();
    const WidgetType type = m_cellTypes.take(widget);
    recycler()->recycle(type, widget);
}

void VirtualTableView::recycleAllCells()
{
    if (m_cells.isEmpty()) {
        m_cellTypes.clear();
        return;
    }
    if (m_cellAdapter) {
        for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
            recycleCell(it.key(), it.value());
    }
    m_cells.clear();
    m_cellTypes.clear();
}

// ---------------------------------------------------------------------------
// Cell lifecycle (Cell Widget Mode)
// ---------------------------------------------------------------------------

void VirtualTableView::onRowsAboutToBeRemovedForCells(const QModelIndex &parent, int first, int last)
{
    recycleCellsInRowRange(parent, first, last);
}

void VirtualTableView::onColumnsAboutToBeRemovedForCells(const QModelIndex &parent, int first,
                                                         int last)
{
    recycleCellsInColumnRange(parent, first, last);
}

void VirtualTableView::onModelAboutToBeResetForCells()
{
    // The persistent index of every cell dies with the model reset, so the
    // widgets are unbound while the model still describes them.
    recycleAllCells();
}

void VirtualTableView::recycleCellsInRowRange(const QModelIndex &parent, int first, int last)
{
    if (m_cells.isEmpty() || !m_cellAdapter)
        return;
    QHash<QPersistentModelIndex, QWidget *> kept;
    kept.reserve(m_cells.size());
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const QModelIndex index = it.key();
        const bool removed = index.isValid() && index.parent() == parent
            && index.row() >= first && index.row() <= last;
        if (removed)
            recycleCell(it.key(), it.value());
        else
            kept.insert(it.key(), it.value());
    }
    m_cells = kept;
}

void VirtualTableView::recycleCellsInColumnRange(const QModelIndex &parent, int first, int last)
{
    if (m_cells.isEmpty() || !m_cellAdapter)
        return;
    QHash<QPersistentModelIndex, QWidget *> kept;
    kept.reserve(m_cells.size());
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const QModelIndex index = it.key();
        const bool removed = index.isValid() && (!parent.isValid() || index.parent() == parent)
            && index.column() >= first && index.column() <= last;
        if (removed)
            recycleCell(it.key(), it.value());
        else
            kept.insert(it.key(), it.value());
    }
    m_cells = kept;
}

bool VirtualTableView::isCellPinned(const QPersistentModelIndex &index, const QWidget *widget) const
{
    if (index.isValid() && isItemPinned(QModelIndex(index)))
        return true;
    // A focused editor, an active IME composition or a popup must survive.
    return hasFocusWithin(widget);
}

void VirtualTableView::materializeItems(const VisibleRange &rows)
{
    materializeItemRanges(QVector<VisibleRange>{rows});
}

void VirtualTableView::materializeItemRanges(const QVector<VisibleRange> &ranges)
{
    if (m_materializationMode != MaterializationMode::CellWidgets || !m_cellAdapter || !model()) {
        recycleAllCells();
        return;
    }
    if (m_cellMaterializationActive)
        return;
    m_cellMaterializationActive = true;

    const QVector<int> columns = columnsForCellMaterialization();
    QHash<QPersistentModelIndex, QWidget *> next;
    // Every range contributes: the scrolling window and, with frozen rows (§31 row
    // direction), the frozen rows as well.
    for (const VisibleRange &range : ranges) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            for (int column : columns) {
                const QModelIndex index = model()->index(int(row), column);
                if (!index.isValid())
                    continue;
                // Merged cells are one target: only the anchor owns a widget, the
                // cells it covers are never materialized (§43 "spans").
                if (isSpanCovered(index))
                    continue;
                const QPersistentModelIndex persistent(index);
                QWidget *widget = m_cells.value(persistent, nullptr);
                if (!widget) {
                    widget = createCellWidget(persistent);
                    if (!widget)
                        continue;
                }
                next.insert(persistent, widget);
            }
        }
    }

    // Keep pinned cells (focus/IME/popup/explicit pin) even outside the window,
    // recycle everything else.
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (next.contains(it.key()))
            continue;
        if (isCellPinned(it.key(), it.value())) {
            next.insert(it.key(), it.value());
            continue;
        }
        recycleCell(it.key(), it.value());
    }

    m_cells = next;
    updateCellGeometry();
    m_cellMaterializationActive = false;
}

quint64 VirtualTableView::cellClipKey(ItemPane::Type rowPane, int columnPaneIndex)
{
    return (quint64(quint32(int(rowPane))) << 32) | quint64(quint32(columnPaneIndex + 1));
}

void VirtualTableView::syncCellPaneClipHosts()
{
    // One container per (row pane, column pane) intersection: several column groups
    // scroll independently (§43) and the frozen rows are a boundary of their own
    // (§31 row direction), so a cell is clipped by the intersection of the two.
    const QVector<ItemPane> rowPanes = itemPanes();
    const QVector<TablePane> columnPanes = m_panes.panes();
    const bool rowsActive = rowPanes.size() > 1;
    const bool columnsActive = columnPanes.size() > 1;

    QHash<quint64, QWidget *> wanted;
    for (const ItemPane &rowPane : rowPanes) {
        for (int columnIndex = 0; columnIndex < columnPanes.size(); ++columnIndex) {
            const TablePane &columnPane = columnPanes.at(columnIndex);
            // Untouched behaviour when nothing is frozen vertically: only the
            // scrolling column panes get a container, frozen columns keep the
            // viewport as parent (they never move).
            if (!rowsActive && (columnPane.type != TablePane::Type::Scrollable
                                || !columnsActive)) {
                continue;
            }
            const QRect rect = rowPane.viewportRect.intersected(columnPane.viewportRect);
            if (rect.isEmpty())
                continue;
            const quint64 key = cellClipKey(rowPane.type, columnIndex);
            QWidget *host = m_cellClipHosts.take(key);
            if (!host)
                host = new PaneClipHost(columnIndex, viewport());
            if (host->geometry() != rect)
                host->setGeometry(rect);
            host->setVisible(true);
            wanted.insert(key, host);
        }
    }

    // Whatever is left over belongs to a pane intersection that is gone: hand the
    // cells back to the viewport and drop the container, so an unused feature
    // changes nothing at all.
    for (auto it = m_cellClipHosts.begin(); it != m_cellClipHosts.end(); ++it) {
        QWidget *host = it.value();
        const QList<QWidget *> cells =
            host->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *cell : cells)
            cell->setParent(viewport());
        host->hide();
        host->setParent(nullptr);
        host->deleteLater();
    }
    m_cellClipHosts = wanted;
}

void VirtualTableView::updateCellGeometry()
{
    syncCellPaneClipHosts();

    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        QWidget *widget = it.value();
        const QModelIndex index = it.key();
        if (!index.isValid()) {
            widget->hide();
            continue;
        }
        // Span aware: an anchor cell widget covers its whole merged area.
        const QRect rect = cellRect(index);
        const int paneIndex = m_panes.paneIndexOfColumn(index.column());
        const ItemPane::Type rowPane = itemPaneForRow(viewItemForIndex(index));
        const bool frozen = m_panes.isFrozenColumn(index.column());
        // Cells live in the clip container of their own (row pane, column pane)
        // intersection: Qt clips a widget to its parent, so a cell can never paint
        // under a frozen pane (§31) or into the pane of another scroll group (§43),
        // and no cell has to repaint or change its background.
        QWidget *clipHost = m_cellClipHosts.value(cellClipKey(rowPane, paneIndex), nullptr);
        QWidget *parent = clipHost ? clipHost : viewport();
        if (widget->parentWidget() != parent)
            widget->setParent(parent);

        // A cell that does not own a container is not clipped at all (a frozen cell
        // in a view without frozen rows).
        const QRect hostRect = clipHost ? clipHost->geometry() : QRect();
        const QRect visible = hostRect.isNull() ? rect : rect.intersected(hostRect);
        if (rect.isValid() && !visible.isEmpty()) {
            const QPoint origin = hostRect.isNull() ? QPoint(0, 0) : hostRect.topLeft();
            widget->setGeometry(rect.translated(-origin));
            if (!widget->isVisible())
                widget->show();
            // A frozen cell (either direction) goes above the scrolling cells, so it
            // stays visible when a scrolling cell passes behind it.
            if (frozen || isRowFrozen(viewItemForIndex(index)))
                widget->raise();
        } else {
            widget->hide();
        }
    }
}

void VirtualTableView::rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    if (m_materializationMode != MaterializationMode::CellWidgets) {
        VirtualItemView::rebindItemsInRange(topLeft, bottomRight);
        return;
    }
    if (!m_cellAdapter)
        return;
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const QModelIndex index = it.key();
        if (!index.isValid() || index.parent() != topLeft.parent())
            continue;
        if (index.row() < topLeft.row() || index.row() > bottomRight.row())
            continue;
        if (index.column() < topLeft.column() || index.column() > bottomRight.column())
            continue;
        m_cellAdapter->bindCellWidget(it.value(), index);
    }
}

QModelIndex VirtualTableView::indexAt(const QPoint &viewportPos) const
{
    const QModelIndex rowIndex = VirtualItemView::indexAt(viewportPos);
    if (!rowIndex.isValid() || !m_columns)
        return rowIndex;
    const int column = columnAtViewportX(viewportPos.x());
    if (column < 0 || m_columns->isSectionHidden(column))
        return rowIndex;
    // A merged area is one hit target: the anchor owns it (§43 "spans").
    return anchorIndex(rowIndex.siblingAtColumn(column));
}

int VirtualTableView::columnAtViewportX(int viewportX) const
{
    if (!m_columns || viewportX < 0)
        return -1;
    const QVector<TablePane> panes = m_panes.panes();
    if (panes.isEmpty()) {
        // The pane layout has not run yet (no resize): fall back to the flat
        // mapping of the committed geometry.
        const int column = m_columns->sectionAtOffset(m_columns->viewportOffset() + viewportX);
        return (column >= 0 && !m_columns->isSectionHidden(column)) ? column : -1;
    }
    // Candidates only: the frozen columns plus the scrollable window, so the hit
    // test stays cheap for a table with thousands of columns.
    const QVector<int> candidates = m_panes.columnsForLayout(0);
    for (int logical : candidates) {
        if (logical < 0 || m_columns->isSectionHidden(logical))
            continue;
        const int x = m_panes.columnViewportX(logical);
        if (x < 0)
            continue;
        // A pane is a hard boundary: a scrolled column may stick out of its own
        // pane (under a frozen pane, or into the pane of another scroll group),
        // and what is drawn there belongs to the neighbour.
        const QRect paneRect = m_panes.paneAt(m_panes.paneIndexOfColumn(logical)).viewportRect;
        if (viewportX < paneRect.x() || viewportX >= paneRect.x() + paneRect.width())
            continue;
        const int width = m_columns->sectionSize(logical);
        if (width > 0 && viewportX >= x && viewportX < x + width)
            return logical;
    }
    return -1;
}

QWidget *VirtualTableView::cellWidget(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    return m_cells.value(QPersistentModelIndex(index), nullptr);
}

QModelIndex VirtualTableView::cellIndexForWidget(const QWidget *widget) const
{
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (it.value() == widget)
            return QModelIndex(it.key());
    }
    return QModelIndex();
}

QList<QModelIndex> VirtualTableView::materializedCellIndexes() const
{
    QList<QModelIndex> indexes;
    indexes.reserve(m_cells.size());
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        indexes.append(QModelIndex(it.key()));
    return indexes;
}

VirtualViewStats VirtualTableView::stats() const
{
    VirtualViewStats result = VirtualItemView::stats();
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        result.materializedItems = m_cells.size();
        qsizetype pinned = 0;
        for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
            if (isCellPinned(it.key(), it.value()))
                ++pinned;
        }
        result.pinnedWidgets = pinned;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Kernel hooks
// ---------------------------------------------------------------------------

TableRowLayoutContext VirtualTableView::layoutContext(const QRect &viewportRect,
                                                      const QHash<int, QWidget *> &paneHosts,
                                                      const QModelIndex &rowIndex) const
{
    TableRowLayoutContext context;
    context.m_geometry = m_columns;
    context.m_panes = &m_panes;
    context.m_paneHosts = paneHosts;
    context.m_scrollablePaneHost = paneHosts.value(m_panes.primaryPaneIndex(), nullptr);
    context.m_columnCount = columnCount();
    context.m_viewportRect = viewportRect;
    context.m_horizontalOffset = m_columns->viewportOffset();
    context.m_columnOverscan = m_columnOverscan;
    context.m_visibleColumns = m_panes.visibleScrollableRange();

    // §43 "spans": tell the adapter what the framework decided for this row.
    // A merged rectangle is reported in row widget coordinates and clipped to
    // the row: Row Widget Mode owns one widget per row, so a cross-row merge can
    // only be honoured by the business (see docs/spans.md).
    if (m_spanProvider && rowIndex.isValid()) {
        const QModelIndex row = rowIndex.siblingAtColumn(0);
        const QVector<int> columns = context.columnsToLayout();
        for (int logical : columns) {
            const QModelIndex cell = row.siblingAtColumn(logical);
            if (!cell.isValid())
                continue;
            if (anchorIndex(cell) != cell) {
                context.m_spans.m_covered.insert(logical);
                continue;
            }
            const TableSpan span = spanAt(cell);
            if (!span.isMerged())
                continue;
            QRect merged = spanRect(cell);
            if (merged.isEmpty())
                continue;
            merged.translate(-viewportRect.topLeft());
            merged.setHeight(qMin(merged.height(), viewportRect.height()));
            context.m_spans.m_spans.insert(logical, span);
            context.m_spans.m_rects.insert(logical, merged);
        }
    }
    return context;
}

/// Clip containers of \a rowWidget: one per scrolling pane (§43 "advanced
/// panes"). They are found through the pane index property instead of a widget
/// pointer key, so a recycled row widget can never leave a stale pointer behind.
QVector<QWidget *> VirtualTableView::rowPaneClipHosts(QWidget *rowWidget) const
{
    QVector<QWidget *> hosts;
    if (!rowWidget)
        return hosts;
    const QList<QWidget *> children = rowWidget->findChildren<QWidget *>(
        QStringLiteral("vivPaneClipHost"), Qt::FindDirectChildrenOnly);
    hosts.reserve(children.size());
    for (QWidget *child : children)
        hosts.append(child);
    return hosts;
}

QWidget *VirtualTableView::rowPaneClipHost(QWidget *rowWidget, int paneIndex) const
{
    if (!rowWidget || paneIndex < 0)
        return nullptr;
    for (QWidget *child : rowPaneClipHosts(rowWidget)) {
        if (static_cast<PaneClipHost *>(child)->paneIndex() == paneIndex)
            return child;
    }
    return nullptr;
}

/// Column hosts of \a rowWidget: they live directly in the row widget (frozen
/// columns, or no frozen columns at all) or in one of the framework's pane clip
/// containers.
QList<ColumnHost *> VirtualTableView::rowColumnHosts(QWidget *rowWidget) const
{
    QList<ColumnHost *> hosts =
        rowWidget->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *clipHost : rowPaneClipHosts(rowWidget)) {
        const QList<ColumnHost *> clipped =
            clipHost->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
        hosts.append(clipped);
    }
    return hosts;
}

/// Ensures the clip container of one scrolling pane and returns it.
QWidget *VirtualTableView::ensureRowPaneClipHost(QWidget *rowWidget, int paneIndex,
                                                 const QRect &paneLocalRect)
{
    QWidget *clipHost = rowPaneClipHost(rowWidget, paneIndex);
    if (!clipHost)
        clipHost = new PaneClipHost(paneIndex, rowWidget);
    if (clipHost->geometry() != paneLocalRect)
        clipHost->setGeometry(paneLocalRect);
    clipHost->setVisible(!paneLocalRect.isEmpty());
    return clipHost;
}

void VirtualTableView::dropStaleRowPaneClipHosts(QWidget *rowWidget, const QSet<int> &scrollingPanes)
{
    if (!rowWidget)
        return;
    const QVector<QWidget *> hosts = rowPaneClipHosts(rowWidget);
    for (QWidget *clipHost : hosts) {
        const int paneIndex = static_cast<PaneClipHost *>(clipHost)->paneIndex();
        if (scrollingPanes.contains(paneIndex))
            continue;
        // Hand the hosts back to the row widget and drop the container, so an
        // unused feature changes nothing at all.
        const QList<ColumnHost *> clipped =
            clipHost->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
        for (ColumnHost *host : clipped)
            host->setParent(rowWidget);
        clipHost->hide();
        clipHost->setParent(nullptr);
        clipHost->deleteLater();
    }
}

void VirtualTableView::applyColumnLayout(const MaterializedItem &item)
{
    if (!item.widget)
        return;

    // One clip container per scrolling pane (§43 "advanced panes"): several
    // groups scroll independently, so a single container for the primary pane
    // would let a second group paint over its neighbour.
    QHash<int, QWidget *> paneHosts;
    QSet<int> scrollingPanes;
    if (panesNeedClipping()) {
        const QVector<TablePane> panes = m_panes.panes();
        for (int paneIndex = 0; paneIndex < panes.size(); ++paneIndex) {
            const TablePane &pane = panes.at(paneIndex);
            if (pane.type != TablePane::Type::Scrollable)
                continue;
            const QRect local = pane.viewportRect.translated(-item.geometry.topLeft());
            paneHosts.insert(paneIndex,
                             ensureRowPaneClipHost(item.widget, paneIndex, local));
            scrollingPanes.insert(paneIndex);
        }
    }
    dropStaleRowPaneClipHosts(item.widget, scrollingPanes);

    const TableRowLayoutContext context
        = layoutContext(item.geometry, paneHosts, QModelIndex(item.index));
    // Everything below works in the row widget's own coordinates: the row widget
    // covers the viewport, so its origin is the row rect (see columnX()).
    const QRect localViewport(0, 0, item.geometry.width(), item.geometry.height());

    // Framework-managed column hosts (§27).
    const QList<ColumnHost *> hosts = rowColumnHosts(item.widget);
    if (!hosts.isEmpty()) {
        for (ColumnHost *host : hosts) {
            const int logicalColumn = host->logicalColumn();
            const ColumnGeometry geometry = context.column(logicalColumn);
            if (!geometry.isValid() || geometry.hidden) {
                host->setVisible(false);
                continue;
            }
            // §43 "spans": the columns a merged area covers have no widget of
            // their own, the anchor host takes over their rectangle.
            if (context.spans().isCovered(logicalColumn)) {
                host->setVisible(false);
                continue;
            }
            const bool frozen = context.isColumnFrozen(geometry.logicalIndex);
            // Scrollable columns live in the clip container of their own pane: Qt
            // clips a widget to its parent, so they can never paint under a frozen
            // pane (§31) or into the pane of another scroll group (§43), and no
            // widget has to repaint or change its background.
            QWidget *parent = item.widget;
            QRect localPaneRect;
            if (!frozen) {
                parent = context.paneHostForColumn(geometry.logicalIndex);
                if (parent) {
                    localPaneRect =
                        context.paneRectAt(context.paneIndex(geometry.logicalIndex))
                            .translated(-item.geometry.topLeft());
                } else {
                    parent = item.widget;
                }
            }
            if (host->parentWidget() != parent)
                host->setParent(parent);

            QRect hostRect(context.columnX(geometry.logicalIndex), 0, geometry.width,
                           context.viewportRect().height());
            if (context.spans().spanOf(logicalColumn).isMerged()) {
                const QRect merged = context.spans().rect(logicalColumn);
                if (!merged.isEmpty())
                    hostRect = merged;
            }
            // Only the scrollable columns are clipped to their pane; a frozen
            // column lives outside of every scrolling pane by definition.
            const QRect visible = localPaneRect.isNull() ? hostRect
                                                        : hostRect.intersected(localPaneRect);
            const bool intersects = !visible.isEmpty() && hostRect.intersects(localViewport);
            const QPoint origin = localPaneRect.isNull() ? QPoint(0, 0) : localPaneRect.topLeft();
            host->setGeometry(hostRect.translated(-origin));
            host->setVisible(intersects);
            if (!intersects)
                continue;

            if (frozen)
                host->raise();
        }
    }

    if (m_tableAdapter)
        m_tableAdapter->layoutRowWidget(item.widget, QModelIndex(item.index), context);
}

void VirtualTableView::updateColumnLayout()
{
    if (m_columnUpdateActive)
        return;
    m_columnUpdateActive = true;
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        updateCellGeometry();
    } else {
        // Snapshot: the adapter hook or a ColumnHost may trigger a relayout.
        const QList<MaterializedItem> items = materializedItems();
        for (const MaterializedItem &item : items)
            applyColumnLayout(item);
    }
    m_columnUpdateActive = false;
}

void VirtualTableView::onHeaderGeometryChanged()
{
    // Frozen widths (or hidden/order) may have changed: refresh the panes first.
    updatePaneLayout();
    if (m_horizontalHeader)
        m_horizontalHeader->headerWidget()->update();
    syncHorizontalScrollBar();
    updateColumnLayout();
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        // Column geometry changes also change which cells belong to the
        // materialized region, so the cell set has to be recomputed.
        markDirty();
    }
    emit columnGeometryChanged();
}

void VirtualTableView::afterMaterialize()
{
    VirtualItemView::afterMaterialize();
    updateRowHeaderOffset();
    updateRowHeaderGeometry();
    updateColumnLayout();
    raisePaneSeparatorLines();
    syncHorizontalScrollBar();
}

void VirtualTableView::resizeEvent(QResizeEvent *event)
{
    layoutHeaderWidgets();
    // The scrollable pane width changed with the viewport.
    updatePaneLayout();
    VirtualItemView::resizeEvent(event);
    syncHorizontalScrollBar();
}

void VirtualTableView::showEvent(QShowEvent *event)
{
    ensureHeaders();
    layoutHeaderWidgets();
    updatePaneLayout();
    VirtualItemView::showEvent(event);
    syncHorizontalScrollBar();
}

void VirtualTableView::changeEvent(QEvent *event)
{
    VirtualItemView::changeEvent(event);
    // The default separator colour is probed from the style, so a style or
    // palette change has to refresh the body lines (the header line resolves the
    // colour while painting).
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
        syncPaneSeparatorLines();
        break;
    default:
        break;
    }
}

void VirtualTableView::scrollContentsBy(int dx, int dy)
{
    if (dx != 0 && !m_columnUpdateActive) {
        // Header and body consume the same offset: they cannot drift (§45.5).
        m_columns->setViewportOffset(horizontalScrollBar()->value());
        emit horizontalOffsetChanged(m_columns->viewportOffset());
    }
    VirtualItemView::scrollContentsBy(dx, dy);
}

void VirtualTableView::wheelEvent(QWheelEvent *event)
{
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    const int horizontal = !pixel.isNull() ? pixel.x() : angle.x();
    const bool shiftHorizontal = horizontal == 0 && (event->modifiers() & Qt::ShiftModifier);

    if (horizontal != 0 || shiftHorizontal) {
        qint64 delta = 0;
        if (!pixel.isNull()) {
            // High resolution input: a horizontal wheel/trackpad pan uses x, a
            // Shift+wheel keeps the vertical delta.
            delta = horizontal != 0 ? pixel.x() : pixel.y();
        } else {
            delta = qint64(horizontal != 0 ? angle.x() : angle.y()) * m_horizontalWheelPixels / 120;
        }
        if (delta != 0) {
            scrollByHorizontalPixels(-delta);
            event->accept();
            return;
        }
    }
    VirtualItemView::wheelEvent(event);
}

bool VirtualTableView::handleItemKeyPress(QKeyEvent *event)
{
    const QModelIndex current = currentIndex();
    if (!current.isValid())
        return false;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const int step = event->key() == Qt::Key_Left ? -1 : 1;
        int column = current.column() + step;
        while (column >= 0 && column < columnCount() && isColumnHidden(column))
            column += step;
        if (column < 0 || column >= columnCount())
            return true;
        setCurrentIndex(current.siblingAtColumn(column));
        scrollToColumn(column);
        return true;
    }
    default:
        break;
    }
    return false;
}

void VirtualTableView::scrollToColumn(int logicalIndex)
{
    const ColumnGeometry geometry = m_columns->columnGeometry(logicalIndex);
    if (!geometry.isValid() || geometry.hidden)
        return;

    // Which pane shows the column decides what "make it visible" means: a frozen
    // column is always visible, and a column of another scroll group must move
    // that group (the primary one is driven by the header geometry and the scroll
    // bar, the others by setHorizontalOffset(group, ...)).
    const int paneIndex = m_panes.paneIndexOfColumn(logicalIndex);
    if (paneIndex < 0)
        return;
    const TablePane pane = m_panes.paneAt(paneIndex);
    if (pane.isFrozen() || pane.viewportRect.width() <= 0)
        return;

    const int scrollGroup = pane.scrollGroup;
    const qint64 groupOffset = m_panes.groupOffset(scrollGroup);
    const int viewportX = m_panes.columnViewportX(logicalIndex);
    if (viewportX < 0)
        return;
    // The pane packs its own columns from its own left edge, so the number that
    // matters is the x inside the pane, not the flat content x.
    const qint64 localStart = qint64(viewportX) - pane.viewportRect.x() + groupOffset;
    const qint64 localEnd = localStart + geometry.width;
    const qint64 paneWidth = pane.viewportRect.width();

    qint64 wanted = groupOffset;
    if (localStart < groupOffset)
        wanted = localStart;
    else if (localEnd > groupOffset + paneWidth)
        wanted = localEnd - paneWidth;
    wanted = qBound<qint64>(0, wanted, m_panes.maximumGroupOffset(scrollGroup));
    if (wanted == groupOffset)
        return;

    if (scrollGroup == m_panes.primaryScrollGroup())
        setHorizontalOffset(wanted);
    else
        setHorizontalOffset(scrollGroup, wanted);
}

// ---------------------------------------------------------------------------
// Drag & drop (§38): row drops and cell drops
// ---------------------------------------------------------------------------

VirtualItemView::DropTarget VirtualTableView::resolveDropTarget(const QPoint &viewportPos) const
{
    DropTarget target = VirtualItemView::resolveDropTarget(viewportPos);
    if (!target.isValid() || !m_columns)
        return target;
    // Row semantics put the whole row on the model, so the insertion stays
    // between rows (column -1). Item semantics resolve the cell under the
    // cursor and the drop lands inside that column.
    if (selectionBehavior() == SelectionBehavior::SelectRows)
        return target;
    const int column = columnAtViewportX(viewportPos.x());
    if (column >= 0 && !m_columns->isSectionHidden(column))
        target.column = column;
    // A merged area is one drop target: the column of a covered cell folds back
    // to its anchor (§43 "spans").
    if (target.column >= 0 && target.row >= 0 && model() && m_spanProvider) {
        const QModelIndex anchor
            = anchorIndex(model()->index(target.row, target.column, target.parent));
        if (anchor.isValid())
            target.column = anchor.column();
    }
    return target;
}

QRect VirtualTableView::resolveDropIndicatorRect(const DropTarget &target) const
{
    const QRect line = VirtualItemView::resolveDropIndicatorRect(target);
    if (line.isEmpty() || target.column < 0)
        return line;
    // A cell drop marks the cell: the same insertion line, narrowed to the
    // column the drop lands in (frozen columns included - their viewport x
    // ignores the horizontal offset, like their geometry).
    const ColumnGeometry column = columnGeometry(target.column);
    if (!column.isValid() || column.hidden || column.width <= 0)
        return line;
    // An anchored merge marks the whole merged rectangle (which is clipped to
    // the pane by spanRect()), not just its first column.
    if (model() && m_spanProvider) {
        const QModelIndex anchor = anchorIndex(model()->index(target.row, target.column, target.parent));
        const QRect merged = anchor.isValid() ? spanRect(anchor) : QRect();
        if (!merged.isEmpty())
            return QRect(merged.x(), line.y(), merged.width(), line.height());
    }
    return QRect(column.viewportX, line.y(), column.width, line.height());
}

} // namespace viv
