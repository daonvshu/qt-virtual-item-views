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
/// Above this row count the native vertical header cannot mirror per-row heights
/// cheaply (QHeaderView keeps an O(rows) position cache plus a Section per row
/// here), so per-row mirroring is disabled; the widget header (v0.5) removes the
/// limit. One million rows cost about 8 MB of mirror state.
constexpr qsizetype kRowHeaderMirrorLimit = 1000000;

/// Magic/version of the table level header state (HeaderGeometry state plus the
/// frozen pane sets, §31/§32).
constexpr quint32 kTableStateMagic = 0x56495654; // 'VIVT'
constexpr quint32 kTableStateVersion = 1;

/// Framework owned clipping container of a pane (§31). It paints nothing, so a
/// business row widget keeps its own background; Qt clips the children of a
/// widget to its rect, which is exactly what keeps the scrollable columns from
/// painting under a frozen pane. Masks cannot do this, because a mask does not
/// clip child widgets.
class PaneClipHost : public QWidget
{
public:
    explicit PaneClipHost(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivPaneClipHost"));
        setFocusPolicy(Qt::NoFocus);
    }
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
    // Cells are children of the viewport, but they must be unbound while the
    // adapter is still alive.
    recycleAllCells();
    if (m_ownHorizontalHeader)
        delete m_horizontalHeader;
    if (m_ownVerticalHeader)
        delete m_verticalHeader;
    if (m_ownTableAdapter)
        delete m_tableAdapter;
    if (m_ownCellAdapter)
        delete m_cellAdapter;
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

void VirtualTableView::ensureHeaders()
{
    if (!m_horizontalHeader) {
        m_horizontalHeader = new NativeHeaderView(Qt::Horizontal, this);
        m_ownHorizontalHeader = true;
        m_horizontalHeader->setGeometryModel(m_columns);
        m_horizontalHeader->setLabelModel(model());
        m_horizontalHeader->setSortInteractionEnabled(m_sortingEnabled);
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
    if (m_ownHorizontalHeader && m_horizontalHeader)
        delete m_horizontalHeader->headerWidget();
    m_horizontalHeader = header;
    m_ownHorizontalHeader = true;
    m_horizontalHeader->setGeometryModel(m_columns);
    m_horizontalHeader->setLabelModel(model());
    layoutHeaderWidgets();
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
    if (m_ownVerticalHeader && m_verticalHeader)
        delete m_verticalHeader->headerWidget();
    m_verticalHeader = header;
    m_ownVerticalHeader = true;
    m_verticalHeader->setGeometryModel(m_rowHeaders);
    m_verticalHeader->setLabelModel(model());
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
    if (m_horizontalHeader) {
        QWidget *widget = m_horizontalHeader->headerWidget();
        // The scrollable header sits between the frozen panes (§31).
        const int leftWidth = headerHeight > 0 ? m_panes.frozenLeftWidth() : 0;
        const int rightWidth = headerHeight > 0 ? m_panes.frozenRightWidth() : 0;
        widget->setGeometry(viewportRect.x() + leftWidth, viewportRect.y() - headerHeight,
                            qMax(0, viewportRect.width() - leftWidth - rightWidth), headerHeight);
        widget->setVisible(headerHeight > 0);
    }
    if (m_frozenLeftHeader) {
        const int width = m_panes.frozenLeftWidth();
        QWidget *widget = m_frozenLeftHeader->headerWidget();
        widget->setGeometry(viewportRect.x(), viewportRect.y() - headerHeight, qMax(0, width),
                            headerHeight);
        widget->setVisible(headerHeight > 0 && width > 0);
        widget->raise();
    }
    if (m_frozenRightHeader) {
        const int width = m_panes.frozenRightWidth();
        QWidget *widget = m_frozenRightHeader->headerWidget();
        widget->setGeometry(viewportRect.x() + viewportRect.width() - width,
                            viewportRect.y() - headerHeight, qMax(0, width), headerHeight);
        widget->setVisible(headerHeight > 0 && width > 0);
        widget->raise();
    }
    // A widget based header derives its own coordinates from the geometry.
    const QPoint origin = viewportRect.topLeft();
    if (m_horizontalHeader)
        m_horizontalHeader->setViewportOrigin(origin);
    if (m_frozenLeftHeader)
        m_frozenLeftHeader->setViewportOrigin(origin);
    if (m_frozenRightHeader)
        m_frozenRightHeader->setViewportOrigin(origin);
    if (m_verticalHeader) {
        QWidget *widget = m_verticalHeader->headerWidget();
        widget->setGeometry(viewportRect.x() - rowHeaderWidth, viewportRect.y(), rowHeaderWidth,
                            viewportRect.height());
        widget->setVisible(rowHeaderWidth > 0);
    }
    m_headersLaidOut = true;
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
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (parent.isValid())
        return;
    m_columns->setSectionCount(columnCount());
}

void VirtualTableView::onColumnsRemoved(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (parent.isValid())
        return;
    m_columns->setSectionCount(columnCount());
}

void VirtualTableView::onColumnsMoved(const QModelIndex &parent, int start, int end,
                                      const QModelIndex &destinationParent, int destinationColumn)
{
    if (parent.isValid() || destinationParent.isValid()) {
        m_columns->setSectionCount(columnCount());
        return;
    }
    m_columns->moveLogicalSections(start, end - start + 1, destinationColumn);
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
    // The scrollable pane owns the horizontal window; frozen columns are always
    // visible and are reported by visibleColumnLogicalIndexes().
    return m_columns->visibleVisualRange(qMax(1, m_panes.scrollableWidth()));
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

void VirtualTableView::moveColumn(int fromLogicalIndex, int toLogicalIndex)
{
    const int fromVisual = m_columns->visualIndex(fromLogicalIndex);
    const int toVisual = m_columns->visualIndex(toLogicalIndex);
    if (fromVisual < 0 || toVisual < 0)
        return;
    m_columns->moveSection(fromVisual, toVisual);
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
    syncHeaderPanes();
    syncPaneSeparatorLines();
    emit columnGeometryChanged();
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
    QVector<int> leftColumns;
    QVector<int> rightColumns;
    QVector<int> scrollableColumns;
    for (const TablePane &pane : m_panes.panes()) {
        switch (pane.type) {
        case TablePane::Type::FrozenLeft:
            leftColumns = pane.logicalColumns;
            break;
        case TablePane::Type::FrozenRight:
            rightColumns = pane.logicalColumns;
            break;
        case TablePane::Type::Scrollable:
            scrollableColumns = pane.logicalColumns;
            break;
        }
    }

    const auto adopt = [this](HeaderViewInterface *&header, const QVector<int> &columns) {
        if (columns.isEmpty()) {
            if (header) {
                header->headerWidget()->hide();
                header->headerWidget()->deleteLater();
                header = nullptr;
            }
            return;
        }
        if (!header) {
            // The pane header is another renderer of the same geometry (§31), and
            // it is of the same kind as the installed horizontal header so a
            // widget based header can render the frozen panes as well.
            header = createHorizontalPaneHeader();
            header->setGeometryModel(m_columns);
            header->setLabelModel(model());
            header->setSortInteractionEnabled(m_sortingEnabled);
        }
        header->setPaneFilter(columns, true);
    };
    adopt(m_frozenLeftHeader, leftColumns);
    adopt(m_frozenRightHeader, rightColumns);

    if (leftColumns.isEmpty() && rightColumns.isEmpty())
        m_horizontalHeader->clearPaneFilter();
    else
        m_horizontalHeader->setPaneFilter(scrollableColumns, false);
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

void VirtualTableView::syncPaneSeparatorLines()
{
    // One line per existing boundary (left pane | scrollable | right pane).
    QVector<int> boundaries;
    const QRect scrollable = m_panes.paneRect(TablePane::Type::Scrollable);
    if (m_panes.frozenLeftWidth() > 0)
        boundaries.append(scrollable.left());
    if (m_panes.frozenRightWidth() > 0)
        boundaries.append(scrollable.right() + 1);

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
        const bool leftBoundary = m_panes.frozenLeftWidth() > 0 && i == 0;
        const int x = lineOriginX + (leftBoundary ? boundaries.at(i) - lineWidth : boundaries.at(i));
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
    // The row-number strip consumes the same vertical offset as the body, so the
    // numbers stay glued to their rows while scrolling.
    m_rowHeaders->setViewportOffset(verticalOffset());
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
    for (auto it = m_explicitRowHeights.constBegin(); it != m_explicitRowHeights.constEnd(); ++it) {
        const qsizetype row = viewItemForIndex(it.key());
        if (row < 0)
            continue;
        if (m_rowHeaders->storedSectionSize(int(row)) != it.value())
            pending.append({row, it.value()});
    }
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
        || version != kTableStateVersion || int(columnStateSize) > state.size()) {
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

    m_panes.setFrozenColumns(frozenLeft);
    m_panes.setFrozenRightColumns(frozenRight);
    updatePaneLayout();
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
    if (m_ownTableAdapter && m_tableAdapter)
        delete m_tableAdapter;
    m_tableAdapter = adapter;
    m_ownTableAdapter = takeOwnership;
    setAdapter(adapter, false); // the kernel uses the same adapter
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
    m_materializationMode = mode;
    relayout();
}

void VirtualTableView::setCellAdapter(CellWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_cellAdapter == adapter) {
        m_ownCellAdapter = m_ownCellAdapter || takeOwnership;
        return;
    }
    recycleAllCells();
    if (m_ownCellAdapter)
        delete m_cellAdapter;
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
    const QRect rowRect = m_rowLayout->itemRect(row, verticalOffset());
    if (rowRect.height() <= 0)
        return QRect();
    return QRect(column.viewportX, rowRect.y(), column.width, rowRect.height());
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

bool VirtualTableView::isCellPinned(const QPersistentModelIndex &index, const QWidget *widget) const
{
    if (index.isValid() && isItemPinned(QModelIndex(index)))
        return true;
    // A focused editor, an active IME composition or a popup must survive.
    return hasFocusWithin(widget);
}

void VirtualTableView::materializeItems(const VisibleRange &rows)
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
    for (qsizetype row = rows.first; row >= 0 && row <= rows.last; ++row) {
        for (int column : columns) {
            const QModelIndex index = model()->index(int(row), column);
            if (!index.isValid())
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

void VirtualTableView::ensureCellPaneClipHost()
{
    if (!m_panes.hasFrozenColumns()) {
        if (!m_cellClipHost)
            return;
        // Nothing is frozen any more: hand the cells back to the viewport and
        // drop the container, so an unused feature changes nothing at all.
        const QList<QWidget *> cells =
            m_cellClipHost->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *cell : cells)
            cell->setParent(viewport());
        m_cellClipHost->hide();
        m_cellClipHost->setParent(nullptr);
        m_cellClipHost->deleteLater();
        m_cellClipHost = nullptr;
        return;
    }

    if (!m_cellClipHost)
        m_cellClipHost = new PaneClipHost(viewport());
    const QRect rect = m_panes.paneRect(TablePane::Type::Scrollable);
    if (m_cellClipHost->geometry() != rect)
        m_cellClipHost->setGeometry(rect);
    m_cellClipHost->setVisible(!rect.isEmpty());
}

void VirtualTableView::updateCellGeometry()
{
    ensureCellPaneClipHost();
    const QRect scrollableRect = m_panes.hasFrozenColumns()
        ? m_panes.paneRect(TablePane::Type::Scrollable)
        : QRect();

    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        QWidget *widget = it.value();
        const QModelIndex index = it.key();
        if (!index.isValid()) {
            widget->hide();
            continue;
        }
        const QRect rect = cellRect(index.row(), index.column());
        const bool frozen = m_panes.isFrozenColumn(index.column());
        // Scrollable cells live in the clip host: Qt clips a widget to its
        // parent, so they can never paint under a frozen pane (§31) and no cell
        // has to repaint or change its background.
        QWidget *parent = (m_cellClipHost && !frozen) ? static_cast<QWidget *>(m_cellClipHost)
                                                      : viewport();
        if (widget->parentWidget() != parent)
            widget->setParent(parent);

        // Only the scrollable cells are clipped to the scrollable pane; frozen
        // cells live outside of it by definition.
        const QRect visible = (scrollableRect.isNull() || frozen) ? rect : rect.intersected(scrollableRect);
        if (rect.isValid() && !visible.isEmpty()) {
            const QPoint origin = parent == viewport() ? QPoint(0, 0) : scrollableRect.topLeft();
            widget->setGeometry(rect.translated(-origin));
            if (!widget->isVisible())
                widget->show();
            if (frozen)
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
    return rowIndex.siblingAtColumn(column);
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
                                                      QWidget *scrollablePaneHost) const
{
    TableRowLayoutContext context;
    context.m_geometry = m_columns;
    context.m_panes = &m_panes;
    context.m_scrollablePaneHost = scrollablePaneHost;
    context.m_columnCount = columnCount();
    context.m_viewportRect = viewportRect;
    context.m_horizontalOffset = m_columns->viewportOffset();
    context.m_columnOverscan = m_columnOverscan;
    context.m_visibleColumns = m_panes.visibleScrollableRange();
    return context;
}

/// Column hosts of \a rowWidget: they live directly in the row widget (frozen
/// columns, or no frozen columns at all) or in the framework's pane clip host.
QList<ColumnHost *> VirtualTableView::rowColumnHosts(QWidget *rowWidget) const
{
    QList<ColumnHost *> hosts =
        rowWidget->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
    if (QWidget *clipHost = m_rowClipHosts.value(rowWidget, nullptr)) {
        const QList<ColumnHost *> clipped =
            clipHost->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
        hosts.append(clipped);
    }
    return hosts;
}

/// Ensures the scrollable pane clip host of \a rowWidget and returns it (null
/// when nothing is frozen, in which case the column hosts keep their parent).
QWidget *VirtualTableView::ensureRowPaneClipHost(QWidget *rowWidget, const QRect &rowRect)
{
    QWidget *clipHost = m_rowClipHosts.value(rowWidget, nullptr);
    if (!m_panes.hasFrozenColumns()) {
        if (!clipHost)
            return nullptr;
        // Frozen columns are gone: give the hosts back to the row widget and drop
        // the container, so an unused feature changes nothing at all.
        const QList<ColumnHost *> clipped =
            clipHost->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
        for (ColumnHost *host : clipped)
            host->setParent(rowWidget);
        clipHost->hide();
        clipHost->setParent(nullptr);
        clipHost->deleteLater();
        m_rowClipHosts.remove(rowWidget);
        return nullptr;
    }

    if (!clipHost) {
        clipHost = new PaneClipHost(rowWidget);
        m_rowClipHosts.insert(rowWidget, clipHost);
    }
    const QRect local = m_panes.paneRect(TablePane::Type::Scrollable).translated(-rowRect.topLeft());
    if (clipHost->geometry() != local)
        clipHost->setGeometry(local);
    clipHost->setVisible(!local.isEmpty());
    return clipHost;
}

void VirtualTableView::applyColumnLayout(const MaterializedItem &item)
{
    if (!item.widget)
        return;

    QWidget *clipHost = ensureRowPaneClipHost(item.widget, item.geometry);
    const TableRowLayoutContext context = layoutContext(item.geometry, clipHost);
    // Everything below works in the row widget's own coordinates: the row widget
    // covers the viewport, so its origin is the row rect (see columnX()).
    const QRect localViewport(0, 0, item.geometry.width(), item.geometry.height());
    const QRect localScrollable = m_panes.hasFrozenColumns()
        ? m_panes.paneRect(TablePane::Type::Scrollable).translated(-item.geometry.topLeft())
        : QRect();
    const QPoint scrollableOrigin = m_panes.hasFrozenColumns()
        ? m_panes.paneRect(TablePane::Type::Scrollable).topLeft() - item.geometry.topLeft()
        : QPoint();

    // Framework-managed column hosts (§27).
    const QList<ColumnHost *> hosts = rowColumnHosts(item.widget);
    if (!hosts.isEmpty()) {
        for (ColumnHost *host : hosts) {
            const ColumnGeometry geometry = context.column(host->logicalColumn());
            if (!geometry.isValid() || geometry.hidden) {
                host->setVisible(false);
                continue;
            }
            const bool frozen = context.isColumnFrozen(geometry.logicalIndex);
            // Scrollable columns live in the clip host: Qt clips a widget to its
            // parent, so they can never paint under a frozen pane (§31) and no
            // widget has to repaint or change its background.
            QWidget *parent = (clipHost && !frozen) ? clipHost : item.widget;
            if (host->parentWidget() != parent)
                host->setParent(parent);

            const QRect hostRect(context.columnX(geometry.logicalIndex), 0, geometry.width,
                                 context.viewportRect().height());
            // Only the scrollable columns are clipped to the scrollable pane; a
            // frozen column lives outside of it by definition.
            const QRect visible = (localScrollable.isNull() || frozen)
                ? hostRect
                : hostRect.intersected(localScrollable);
            const bool intersects = !visible.isEmpty() && hostRect.intersects(localViewport);
            const QPoint origin = parent == item.widget ? QPoint(0, 0) : scrollableOrigin;
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
    const int viewportWidth = viewport()->width();
    const qint64 start = geometry.contentX;
    const qint64 end = start + geometry.width;
    const qint64 offset = m_columns->viewportOffset();
    if (start < offset)
        setHorizontalOffset(start);
    else if (end > offset + viewportWidth)
        setHorizontalOffset(end - viewportWidth);
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
    return QRect(column.viewportX, line.y(), column.width, line.height());
}

} // namespace viv
