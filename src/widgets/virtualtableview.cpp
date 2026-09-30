#include <virtualitemviews/virtualtableview.h>

#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/reorderabletablemodel.h>

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QStyleOptionHeader>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPointer>
#include <QRegion>
#include <QScrollBar>
#include <QSet>
#include <QHeaderView>
#include <QShowEvent>
#include <QWheelEvent>
#include <QDebug>

#include <algorithm>
#include <limits>
#include <utility>

namespace viv {

// A shared build has to export this public constant (see virtualitemview.cpp): a consumer
// that odr-uses it links against `__imp_...`, and MinGW/GCC only emits the symbol in a TU
// that odr-uses it. The pane-offset sentinel used to be addressed by the removed
// nativeheader implementation.
namespace {
[[maybe_unused]] const void *const volatile kExportedConstants[] = {
    &HeaderViewInterface::kFollowGeometryOffset,
};
} // namespace

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
/// Above kRowHeaderMirrorLimit a table without measured per-row heights still keeps its
/// row-number strip as long as the only per-row states are the handful of explicit heights
/// the user set: those are sparse size overrides, not one state per row (§12 of the
/// vertical-header decision).
constexpr int kMaxSparseRowHeights = 4096;

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
        VirtualTableView::drawPaneSeparator(&painter, rect(), m_style, m_resolvedColor);
    }

private:
    PaneSeparatorStyle m_style;
    QColor m_resolvedColor;
};

class ColumnSpacingHost : public QWidget
{
public:
    explicit ColumnSpacingHost(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivColumnSpacingHost"));
        setFocusPolicy(Qt::NoFocus);
    }

    void setContent(QWidget *content)
    {
        m_content = content;
        if (content) {
            content->setParent(this);
            content->show();
        }
        layoutContent();
    }

    QWidget *content() const { return m_content; }
    void setHeaderContent(QWidget *content)
    {
        m_headerContent = content;
        if (content) {
            content->setParent(this);
            content->show();
        }
        layoutContent();
    }
    QWidget *headerContent() const { return m_headerContent; }
    int bodyTop() const { return m_bodyTop; }
    void setLineColors(const QColor &vertical, const QColor &horizontal)
    {
        m_verticalColor = vertical;
        m_horizontalColor = horizontal;
        syncHorizontalLines();
        update();
    }
    void setLineWidths(int vertical, int horizontal)
    {
        m_verticalWidth = vertical;
        m_horizontalWidth = horizontal;
        layoutContent();
        update();
    }
    void setHorizontalGridLinesVisible(bool visible)
    {
        m_horizontalGridLinesVisible = visible;
        layoutContent();
        update();
    }
    void setEdges(bool left, bool right, int bodyTop)
    {
        m_left = left;
        m_right = right;
        m_bodyTop = bodyTop;
        layoutContent();
        update();
    }
    void setRowGaps(const QVector<QRect> &gaps, const QVector<int> &horizontalLines,
                    bool verticalThrough)
    {
        m_rowGaps = gaps;
        m_horizontalLineYs = horizontalLines;
        m_verticalThrough = verticalThrough;
        syncHorizontalLines();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        layoutContent();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(0, 0, width(), m_bodyTop), palette().brush(QPalette::Button));
        painter.fillRect(QRect(0, m_bodyTop, width(), height() - m_bodyTop),
                         palette().brush(QPalette::Base));
        if (m_left)
            painter.fillRect(QRect(0, 0, m_verticalWidth, m_bodyTop), m_verticalColor);
        if (m_right)
            painter.fillRect(QRect(width() - m_verticalWidth, 0, m_verticalWidth, m_bodyTop), m_verticalColor);
        if (m_horizontalGridLinesVisible && m_bodyTop > 0)
            painter.fillRect(QRect(0, m_bodyTop - m_horizontalWidth, width(),
                                   m_horizontalWidth), m_horizontalColor);
        int from = m_bodyTop;
        for (const QRect &gap : m_rowGaps) {
            if (!m_verticalThrough && gap.top() > from)
                drawBodyEdges(&painter, from, gap.top() - from);
            if (!m_verticalThrough)
                from = qMax(from, gap.bottom() + 1);
        }
        if (from < height())
            drawBodyEdges(&painter, from, height() - from);
        if (m_horizontalGridLinesVisible && !m_verticalThrough
            && m_horizontalLineYs.isEmpty()) {
            for (const QRect &gap : m_rowGaps) {
                for (int y : {gap.top(), gap.bottom()}) {
                    if (y < m_bodyTop || y >= height())
                        continue;
                    if (m_left)
                        painter.fillRect(QRect(0, y, m_verticalWidth, m_horizontalWidth), m_verticalColor);
                    if (m_right)
                        painter.fillRect(QRect(width() - m_verticalWidth, y,
                                               m_verticalWidth, m_horizontalWidth), m_verticalColor);
                }
            }
        }
    }

private:
    void layoutContent()
    {
        if (m_content)
            m_content->setGeometry(m_left ? m_verticalWidth : 0, m_bodyTop,
                                   qMax(0, width() - (m_left ? m_verticalWidth : 0)
                                        - (m_right ? m_verticalWidth : 0)),
                                   qMax(0, height() - m_bodyTop));
        if (m_headerContent)
            m_headerContent->setGeometry(m_left ? m_verticalWidth : 0, 0,
                                         qMax(0, width() - (m_left ? m_verticalWidth : 0)
                                              - (m_right ? m_verticalWidth : 0)),
                                         qMax(0, m_bodyTop - (m_horizontalGridLinesVisible
                                             ? m_horizontalWidth : 0)));
        syncHorizontalLines();
    }

    void drawBodyEdges(QPainter *painter, int y, int h)
    {
        if (m_left)
            painter->fillRect(QRect(0, y, m_verticalWidth, h), m_verticalColor);
        if (m_right)
            painter->fillRect(QRect(width() - m_verticalWidth, y, m_verticalWidth, h), m_verticalColor);
    }

    void syncHorizontalLines()
    {
        const int count = m_horizontalLineYs.size();
        while (m_horizontalLines.size() > count)
            delete m_horizontalLines.takeLast();
        while (m_horizontalLines.size() < count) {
            auto *line = new QWidget(this);
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_horizontalLines.append(line);
        }
        for (int i = 0; i < count; ++i) {
            QWidget *line = m_horizontalLines.at(i);
            line->setGeometry(0, m_horizontalLineYs.at(i), width(), m_horizontalWidth);
            QPalette colors = line->palette();
            colors.setColor(QPalette::Window, m_horizontalColor);
            line->setPalette(colors);
            line->setAutoFillBackground(true);
            line->show();
            line->raise();
        }
    }

    QWidget *m_content = nullptr;
    QWidget *m_headerContent = nullptr;
    QColor m_verticalColor;
    QColor m_horizontalColor;
    int m_verticalWidth = 1;
    int m_horizontalWidth = 1;
    bool m_left = true;
    bool m_right = true;
    int m_bodyTop = 0;
    QVector<QRect> m_rowGaps;
    QVector<int> m_horizontalLineYs;
    QVector<QWidget *> m_horizontalLines;
    bool m_verticalThrough = true;
    bool m_horizontalGridLinesVisible = true;
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
            [this](qint64) { updatePaneLayoutForScroll(); });
    connect(m_rowHeaders, &HeaderGeometry::sectionResized, this,
            &VirtualTableView::onVerticalSectionResized);
    connect(m_columns, &HeaderGeometry::sortIndicatorChanged, this,
            &VirtualTableView::onSortIndicatorChanged);
}

VirtualTableView::~VirtualTableView()
{
    for (QWidget *widget : m_columnSpacingWidgets)
        delete widget;
    qDeleteAll(m_columnSpacingPool);
    for (QWidget *widget : m_rowGridLines)
        delete widget;
    qDeleteAll(m_rowGridLinePool);
    // Teardown order matters. Cells are unbound first (they need the cell
    // adapter), then the materialized rows: the *base* destructor would release
    // them, but by then this class has already deleted the owned table adapter -
    // unbindWidget() on a freed adapter is a use-after-free (P0-1 of the second
    // review: it crashed in VirtualItemView::recycleItem() under MSVC Debug). So the
    // rows and the pool go first, while every adapter is still alive; the base pass
    // then finds nothing left to release.
    recycleAllCells();
    recycleAllItems();
    if (WidgetRecycler *pool = recycler())
        pool->clear();
    // Every derived pane renderer next - a widget pane header borrows the primary
    // header's adapter, so it must be gone before that adapter is destroyed - then the
    // primary renderers (which may own the header adapter), and only then the adapters
    // themselves.
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
        m_horizontalHeader = createDefaultHorizontalHeader();
        m_ownHorizontalHeader = true;
        m_horizontalHeader->setGeometryModel(m_columns);
        m_horizontalHeader->setLabelModel(model());
        m_horizontalHeader->setSortInteractionEnabled(m_sortingEnabled);
        applyHeaderAnimationSettings();
        watchHeaderVisualGeometry(m_horizontalHeader);
        // The table owns the stretch target (see updatePaneLayout()): telling the renderer
        // where the viewport starts is what marks it as table driven, and it has to happen
        // before the first layout pass resizes its widget - a renderer that does not know
        // it is driven keeps the geometry's target on its own width instead.
        m_horizontalHeader->setViewportOrigin(viewport()->geometry().topLeft());
    }
    if (!m_verticalHeader) {
        m_verticalHeader = createDefaultVerticalHeader();
        m_ownVerticalHeader = true;
        if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(m_verticalHeader)) {
            connect(widgetHeader, &VirtualHeaderView::adapterAboutToChange, this,
                    &VirtualTableView::dropFrozenRowHeaders, Qt::UniqueConnection);
            connect(widgetHeader, &VirtualHeaderView::adapterChanged, this,
                    &VirtualTableView::rebuildFrozenRowHeaders, Qt::UniqueConnection);
        }
        m_verticalHeader->setGeometryModel(m_rowHeaders);
        m_verticalHeader->setLabelModel(model());
        m_verticalHeader->headerWidget()->setParent(this);
        watchRowStrip(m_verticalHeader);
    }
    applyHeaderGestureSettings();
}

void VirtualTableView::setHorizontalHeader(HeaderViewInterface *header)
{
    if (!header) {
        auto *defaultHeader = createDefaultHorizontalHeader();
        defaultHeader->setGeometryModel(m_columns);
        defaultHeader->setLabelModel(model());
        header = defaultHeader;
    } else if (header->orientation() != Qt::Horizontal) {
        // A renderer of the wrong orientation would be laid out against the other
        // axis' geometry (P2-4): refuse it instead of showing a broken header.
        qWarning("VirtualTableView::setHorizontalHeader(): the renderer is not horizontal; ignored");
        return;
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
    // A widget header owns (or at least holds) the adapter that the derived pane renderers
    // borrow, so a later adapter replacement has to tear them down first - they would
    // otherwise keep a pointer to an adapter that was just deleted (P0-1 of the third
    // review).
    if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(m_horizontalHeader)) {
        connect(widgetHeader, &VirtualHeaderView::adapterAboutToChange, this,
                &VirtualTableView::dropDerivedPaneHeaders, Qt::UniqueConnection);
        connect(widgetHeader, &VirtualHeaderView::adapterChanged, this,
                &VirtualTableView::rebuildDerivedPaneHeaders, Qt::UniqueConnection);
    }
    m_horizontalHeader->setGeometryModel(m_columns);
    m_horizontalHeader->setLabelModel(model());
    watchHeaderVisualGeometry(m_horizontalHeader);
    // The header is part of the view, so the pane rects and the viewport origin mean
    // the same thing for it as for the body. A renderer created by the application
    // is usually parentless - a top level window - and would then be placed in
    // screen coordinates (off by its frame margins, and not clipped by the view).
    m_horizontalHeader->headerWidget()->setParent(this);
    m_horizontalHeader->setViewportOrigin(viewport()->geometry().topLeft());
    applyHeaderAnimationSettings();
    applyHeaderGestureSettings();
    syncHeaderPanes();
    layoutHeaderWidgets();
}

void VirtualTableView::setVerticalHeader(HeaderViewInterface *header)
{
    if (!header) {
        auto *defaultStrip = createDefaultVerticalHeader();
        defaultStrip->setGeometryModel(m_rowHeaders);
        defaultStrip->setLabelModel(model());
        header = defaultStrip;
    } else if (header->orientation() != Qt::Vertical) {
        qWarning("VirtualTableView::setVerticalHeader(): the renderer is not vertical; ignored");
        return;
    }
    if (m_verticalHeader == header)
        return;
    deleteHeader(m_frozenTopRowsHeader);
    deleteHeader(m_frozenBottomRowsHeader);
    if (m_ownVerticalHeader && m_verticalHeader)
        deleteHeader(m_verticalHeader);
    m_verticalHeader = header;
    m_ownVerticalHeader = true;
    if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(m_verticalHeader)) {
        connect(widgetHeader, &VirtualHeaderView::adapterAboutToChange, this,
                &VirtualTableView::dropFrozenRowHeaders, Qt::UniqueConnection);
        connect(widgetHeader, &VirtualHeaderView::adapterChanged, this,
                &VirtualTableView::rebuildFrozenRowHeaders, Qt::UniqueConnection);
    }
    m_verticalHeader->setGeometryModel(m_rowHeaders);
    m_verticalHeader->setLabelModel(model());
    m_verticalHeader->headerWidget()->setParent(this);
    watchRowStrip(m_verticalHeader);
    applyHeaderGestureSettings();
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
    const int rowHeaderWidth
        = (isVerticalHeaderShown() && m_verticalHeader) ? m_verticalHeaderWidth : 0;

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
    // Belt and braces for the stretch target: a renderer that was resized before it learned
    // that the table drives it may have written its own width to the geometry, so the owner
    // re-asserts it once the layout is settled (a no-op when it is already right).
    m_columns->setStretchExtent(viewport()->width());
    if (m_verticalHeader) {
        layoutVerticalHeaderStrips();
    }
    applyGridLineVisibilityToHeaders();
    m_headersLaidOut = true;
}

void VirtualTableView::layoutVerticalHeaderStrips()
{
    if (!m_verticalHeader)
        return;
    // The row-number strip mirrors the row panes: every band is its own renderer, placed
    // on its pane rectangle, so the numbers stay glued to their rows even when some rows
    // are frozen (§31 row direction).
    const int rowHeaderWidth
        = (isVerticalHeaderShown() && m_verticalHeader) ? m_verticalHeaderWidth : 0;
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
        // A widget strip derives its section positions from the geometry, so it needs to
        // know where the viewport starts inside the view - exactly like the column header
        // (a pane strip with an explicit offset ignores it).
        header->setViewportOrigin(viewportRect.topLeft());
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
    // A model the application installed - not the one this view instantiated for the row
    // drag - latches "the application owns the model": from then on the view never
    // instantiates one, whether it was set before or after the drag was switched on.
    if (model && model != m_internalModel)
        m_applicationModelSeen = true;
    if (previous)
        disconnect(previous, nullptr, this, nullptr);

    const quint64 previousChangeSerial = modelChangeSerial();
    VirtualItemView::setModel(model);
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    // Persistent cell indexes of the old model are invalid now.
    recycleAllCells();
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    QPointer<QAbstractItemModel> activeModel(this->model());
    const quint64 changeSerial = previousChangeSerial + 1;
    const bool hadModel = activeModel;
    const auto requestIsCurrent = [this, changeSerial, hadModel, &activeModel]() {
        return modelChangeSerial() == changeSerial && this->model() == activeModel.data()
            && (!hadModel || activeModel);
    };
    connectColumnSignals(activeModel.data());
    if (activeModel) {
        // Connected *after* the kernel's own handlers, so the row widgets are
        // released first and the cells are still bound to a valid index here.
        connect(activeModel.data(), &QAbstractItemModel::rowsAboutToBeRemoved, this,
                &VirtualTableView::onRowsAboutToBeRemovedForCells);
        connect(activeModel.data(), &QAbstractItemModel::columnsAboutToBeRemoved, this,
                &VirtualTableView::onColumnsAboutToBeRemovedForCells);
        connect(activeModel.data(), &QAbstractItemModel::modelAboutToBeReset, this,
                &VirtualTableView::onModelAboutToBeResetForCells);
    }
    if (m_horizontalHeader)
        m_horizontalHeader->setLabelModel(activeModel.data());
    if (!requestIsCurrent())
        return;
    if (m_verticalHeader)
        m_verticalHeader->setLabelModel(activeModel.data());
    if (!requestIsCurrent())
        return;
    // The derived renderers cache the label model as well (they are another renderer of the
    // same geometry, one per pane / frozen row band), and they are usually already
    // materialized: without this they would keep the *old* model - and its old titles - after
    // a model switch that happens to have the same column count (P1 of the fourth review).
    for (HeaderViewInterface *paneHeader : m_paneHeaders) {
        if (paneHeader)
            paneHeader->setLabelModel(activeModel.data());
        if (!requestIsCurrent())
            return;
    }
    if (m_frozenTopRowsHeader)
        m_frozenTopRowsHeader->setLabelModel(activeModel.data());
    if (!requestIsCurrent())
        return;
    if (m_frozenBottomRowsHeader)
        m_frozenBottomRowsHeader->setLabelModel(activeModel.data());
    if (!requestIsCurrent())
        return;

    m_columns->setSectionCount(columnCount());
    if (!requestIsCurrent())
        return;
    m_rowHeaders->setSectionCount(0);
    if (!requestIsCurrent())
        return;
    m_rowHeaders->setDefaultSectionSize(uniformItemHeight() > 0 ? uniformItemHeight()
                                                               : estimatedItemHeight());
    if (!requestIsCurrent())
        return;
    m_explicitRowHeights.clear();
    layoutHeaderWidgets();
    if (!requestIsCurrent())
        return;
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
        m_explicitRowHeights.clear();
        m_rowHeaders->setSectionCount(0);
    });
    connect(model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &parent, int first, int last) {
                if (!managesVisibleRows() && !parent.isValid())
                    m_rowHeaders->insertLogicalSections(first, last - first + 1);
            });
    connect(model, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &parent, int first, int last) {
                if (!managesVisibleRows() && !parent.isValid())
                    m_rowHeaders->removeLogicalSections(first, last - first + 1);
            });
    connect(model, &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &source, int first, int last,
                   const QModelIndex &destination, int row) {
                if (!managesVisibleRows() && !source.isValid() && !destination.isValid())
                    m_rowHeaders->moveLogicalSectionSizes(first, last - first + 1, row);
            });
    // Row identity changes also require the visible labels to be rebound.
    const auto refreshRowStrips = [this]() {
        if (m_verticalHeader)
            m_verticalHeader->refreshSectionLabels();
        for (HeaderViewInterface *strip : {m_frozenTopRowsHeader, m_frozenBottomRowsHeader}) {
            if (strip)
                strip->refreshSectionLabels();
        }
    };
    connect(model, &QAbstractItemModel::rowsMoved, this, refreshRowStrips);
    connect(model, &QAbstractItemModel::rowsInserted, this, refreshRowStrips);
    connect(model, &QAbstractItemModel::rowsRemoved, this, refreshRowStrips);
    // A layout change rebuilds every row size from the estimate (the kernel's contract: sizes
    // keyed by row cannot be trusted after a reorder - see docs/history/model-signals.md). The heights
    // the *user* set are not those derived sizes: they are attached to their row through
    // m_explicitRowHeights (a persistent index), so they are re-applied here. Under
    // RowSizePolicy::MeasuredWins the measurement still wins (canMeasureItem()).
    connect(model, &QAbstractItemModel::layoutChanged, this,
            [this](const QList<QPersistentModelIndex> &, QAbstractItemModel::LayoutChangeHint) {
                m_rowHeaders->setSectionCount(0);
                reapplyExplicitRowHeights();
            });
}

void VirtualTableView::reapplyExplicitRowHeights()
{
    reapplyExplicitRowHeightsInRange(0, viewItemCount() - 1);
}

void VirtualTableView::reapplyExplicitRowHeightsInRange(qsizetype first, qsizetype last)
{
    if (m_explicitRowHeights.isEmpty() || !m_rowLayout)
        return;
    const qsizetype count = viewItemCount();
    bool applied = false;
    for (auto it = m_explicitRowHeights.cbegin(); it != m_explicitRowHeights.cend(); ++it) {
        const qsizetype row = viewItemForIndex(it.key());
        if (row >= first && row <= last && row < count) {
            m_rowLayout->setItemSize(row, it.value());
            applied = true;
        }
    }
    if (applied) {
        updateRowHeaderGeometry();
        markDirty();
    }
}

void VirtualTableView::rekeyPersistentTableState()
{
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const QPointer<QAbstractItemModel> activeModel(model());
    rekeyPersistentState();
    QHash<QPersistentModelIndex, int> heights;
    for (auto it = m_explicitRowHeights.cbegin(); it != m_explicitRowHeights.cend(); ++it) {
        if (isPersistentRowStateValid(it.key()))
            heights.insert(it.key(), it.value());
    }
    m_explicitRowHeights.swap(heights);

    QHash<QPersistentModelIndex, QWidget *> cells;
    QHash<QPersistentModelIndex, QWidget *> invalidCells;
    for (auto it = m_cells.cbegin(); it != m_cells.cend(); ++it) {
        if (it.key().isValid())
            cells.insert(it.key(), it.value());
        else
            invalidCells.insert(it.key(), it.value());
    }
    m_cells.swap(cells);
    if (!invalidCells.isEmpty())
        ++m_cellLifecycleSerial;
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    for (auto it = invalidCells.constBegin(); it != invalidCells.constEnd(); ++it) {
        if (activeAdapter && m_cellAdapter == activeAdapter)
            recycleCell(it.key(), it.value());
        else
            discardCellWidget(it.value());
    }
    if (modelChangeSerial() != modelSerial || viewMappingSerial() != mappingSerial
        || model() != activeModel.data())
        return;
    if (m_spanProvider)
        m_spanProvider->modelStructureChanged();
}

void VirtualTableView::onColumnsInserted(const QModelIndex &parent, int first, int last)
{
    if (!isColumnSchemaParent(parent))
        return;
    // The inserted columns carry the default state; every column after them keeps
    // its own width / visibility / explicit size, and the frozen sets, the pane
    // specs and the sort indicator follow the columns they name.
    //
    // The pane state is remapped *before* the geometry: the geometry emits
    // geometryChanged synchronously and that is what rebuilds the pane cache
    // (updatePaneLayout()). Doing it the other way round left the cache one structure
    // change behind, so paneOfColumn()/paneIndexOfColumn()/columnViewportX() answered
    // with the layout from before the insert (P1-1 of the second review).
    const int count = qMax(0, last - first + 1);
    // A column structure change keeps every row identity, so no row widget is recycled -
    // but the business row widget usually builds one ColumnHost per column, and that schema
    // just changed. bindWidget() is the only hook the business gets for it; the pane and
    // geometry update below then positions the hosts it (re)created (P1-2 of the second
    // review).
    const quint64 modelSerial = modelChangeSerial();
    const QPointer<QAbstractItemModel> activeModel(model());
    rebindMaterializedRows();
    if (!activeModel || modelChangeSerial() != modelSerial || model() != activeModel.data()
        || !isColumnSchemaParent(parent))
        return;
    m_panes.insertLogicalColumns(first, count);
    // The remap renames the sorted column, which HeaderGeometry reports through
    // sortIndicatorChanged - a signal that normally means "the user asked for a sort".
    // The model already inserted the column, so there is nothing to sort here.
    m_sortGuard = true;
    m_columns->insertLogicalSections(first, count);
    m_sortGuard = false;
}

void VirtualTableView::onColumnsRemoved(const QModelIndex &parent, int first, int last)
{
    if (!isColumnSchemaParent(parent))
        return;
    const int count = qMax(0, last - first + 1);
    const quint64 modelSerial = modelChangeSerial();
    const QPointer<QAbstractItemModel> activeModel(model());
    rebindMaterializedRows();
    if (!activeModel || modelChangeSerial() != modelSerial || model() != activeModel.data()
        || !isColumnSchemaParent(parent))
        return;
    m_panes.removeLogicalColumns(first, count);
    m_sortGuard = true;
    m_columns->removeLogicalSections(first, count);
    m_sortGuard = false;
}

void VirtualTableView::onColumnsMoved(const QModelIndex &parent, int start, int end,
                                      const QModelIndex &destinationParent, int destinationColumn)
{
    if (!isColumnSchemaParent(parent) || !isColumnSchemaParent(destinationParent)) {
        m_columns->setSectionCount(columnCount());
        return;
    }
    const int count = qMax(0, end - start + 1);
    const quint64 modelSerial = modelChangeSerial();
    const QPointer<QAbstractItemModel> activeModel(model());
    rebindMaterializedRows();
    if (!activeModel || modelChangeSerial() != modelSerial || model() != activeModel.data()
        || !isColumnSchemaParent(parent) || !isColumnSchemaParent(destinationParent))
        return;
    m_panes.moveLogicalColumns(start, count, destinationColumn);
    m_sortGuard = true;
    m_columns->moveLogicalSections(start, count, destinationColumn);
    m_sortGuard = false;
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
    return m ? m->columnCount(columnSchemaParent()) : 0;
}

QModelIndex VirtualTableView::columnSchemaParent() const
{
    return QModelIndex();
}

bool VirtualTableView::isColumnSchemaParent(const QModelIndex &parent) const
{
    return parent == columnSchemaParent();
}

ColumnGeometry VirtualTableView::columnGeometry(int logicalIndex) const
{
    ColumnGeometry geometry = m_columns->columnGeometry(logicalIndex);
    // Pane aware (§31): frozen columns keep their own x, the scrollable ones are
    // shifted by the horizontal offset.
    const int x = m_panes.columnViewportX(logicalIndex);
    if (geometry.isValid() && m_panes.paneIndexOfColumn(logicalIndex) >= 0)
        geometry.viewportX = x;
    return geometry;
}

int VirtualTableView::columnWidth(int logicalIndex) const
{
    return m_columns->sectionSize(logicalIndex);
}

void VirtualTableView::setColumnSpacing(int pixels)
{
    if (m_columns)
        m_columns->setSectionSpacing(pixels);
}

void VirtualTableView::setColumnSpacingFactory(ColumnSpacingFactory factory,
                                               ColumnSpacingBinder binder)
{
    for (QWidget *widget : m_columnSpacingWidgets)
        delete widget;
    m_columnSpacingWidgets.clear();
    qDeleteAll(m_columnSpacingPool);
    m_columnSpacingPool.clear();
    m_columnSpacingFactory = std::move(factory);
    m_columnSpacingBinder = std::move(binder);
    syncColumnSpacingWidgets();
}

void VirtualTableView::setHeaderColumnSpacingFactory(ColumnSpacingFactory factory,
                                                     ColumnSpacingBinder binder)
{
    for (QWidget *widget : m_columnSpacingWidgets)
        delete widget;
    m_columnSpacingWidgets.clear();
    qDeleteAll(m_columnSpacingPool);
    m_columnSpacingPool.clear();
    m_headerColumnSpacingFactory = std::move(factory);
    m_headerColumnSpacingBinder = std::move(binder);
    syncColumnSpacingWidgets();
}

void VirtualTableView::setVerticalSpacingLineThroughRowSpacing(bool enabled)
{
    if (m_verticalSpacingLineThroughRowSpacing == enabled)
        return;
    m_verticalSpacingLineThroughRowSpacing = enabled;
    syncColumnSpacingWidgets();
}

void VirtualTableView::setHorizontalSpacingLineThroughColumnSpacing(bool enabled)
{
    if (m_horizontalSpacingLineThroughColumnSpacing == enabled)
        return;
    m_horizontalSpacingLineThroughColumnSpacing = enabled;
    syncColumnSpacingWidgets();
}

void VirtualTableView::setVerticalGridLinesVisible(bool visible)
{
    if (m_verticalGridLinesVisible == visible)
        return;
    m_verticalGridLinesVisible = visible;
    applyGridLineVisibilityToHeaders();
    syncColumnSpacingWidgets();
}

void VirtualTableView::setHorizontalGridLinesVisible(bool visible)
{
    if (m_horizontalGridLinesVisible == visible)
        return;
    m_horizontalGridLinesVisible = visible;
    applyGridLineVisibilityToHeaders();
    relayout();
    syncRowGridLines();
    syncColumnSpacingWidgets();
}

void VirtualTableView::setVerticalGridLineColor(const QColor &color)
{
    if (m_verticalGridLineColor == color)
        return;
    m_verticalGridLineColor = color;
    applyGridLineVisibilityToHeaders();
    syncColumnSpacingWidgets();
}

void VirtualTableView::setHorizontalGridLineColor(const QColor &color)
{
    if (m_horizontalGridLineColor == color)
        return;
    m_horizontalGridLineColor = color;
    applyGridLineVisibilityToHeaders();
    relayout();
    syncRowGridLines();
    syncColumnSpacingWidgets();
}

void VirtualTableView::setVerticalGridLineWidth(int pixels)
{
    pixels = qMax(1, pixels);
    if (m_verticalGridLineWidth == pixels)
        return;
    m_verticalGridLineWidth = pixels;
    applyGridLineVisibilityToHeaders();
    syncColumnSpacingWidgets();
}

void VirtualTableView::setHorizontalGridLineWidth(int pixels)
{
    pixels = qMax(1, pixels);
    if (m_horizontalGridLineWidth == pixels)
        return;
    m_horizontalGridLineWidth = pixels;
    applyGridLineVisibilityToHeaders();
    relayout();
    syncRowGridLines();
    syncColumnSpacingWidgets();
}

QColor VirtualTableView::resolvedVerticalGridLineColor() const
{
    return m_verticalGridLineColor.isValid() ? m_verticalGridLineColor : sectionSeparatorColor(this);
}

QColor VirtualTableView::resolvedHorizontalGridLineColor() const
{
    return m_horizontalGridLineColor.isValid() ? m_horizontalGridLineColor : sectionSeparatorColor(this);
}

void VirtualTableView::applyGridLineVisibilityToHeaders()
{
    const QColor verticalColor = resolvedVerticalGridLineColor();
    const QColor horizontalColor = resolvedHorizontalGridLineColor();
    const auto configure = [this, &verticalColor, &horizontalColor](HeaderViewInterface *header,
                                bool horizontal, bool sectionVisible, bool crossVisible) {
        if (auto *widget = dynamic_cast<VirtualHeaderView *>(header)) {
            widget->setSectionSeparatorsVisible(sectionVisible);
            widget->setCrossAxisSeparatorVisible(crossVisible);
            widget->setSectionSeparatorColor(horizontal ? verticalColor : horizontalColor);
            widget->setCrossAxisSeparatorColor(horizontal ? horizontalColor : verticalColor);
            widget->setSectionSeparatorWidth(horizontal ? m_verticalGridLineWidth : m_horizontalGridLineWidth);
            widget->setCrossAxisSeparatorWidth(horizontal ? m_horizontalGridLineWidth : m_verticalGridLineWidth);
        }
    };
    configure(m_horizontalHeader, true, m_verticalGridLinesVisible, m_horizontalGridLinesVisible);
    for (HeaderViewInterface *header : m_paneHeaders)
        configure(header, true, m_verticalGridLinesVisible, m_horizontalGridLinesVisible);
    configure(m_verticalHeader, false, m_horizontalGridLinesVisible, m_verticalGridLinesVisible);
    configure(m_frozenTopRowsHeader, false, m_horizontalGridLinesVisible, m_verticalGridLinesVisible);
    configure(m_frozenBottomRowsHeader, false, m_horizontalGridLinesVisible, m_verticalGridLinesVisible);
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

void VirtualTableView::setColumnStretchFactor(int logicalIndex, qreal factor)
{
    m_columns->setSectionStretchFactor(logicalIndex, factor);
}

qreal VirtualTableView::columnStretchFactor(int logicalIndex) const
{
    return m_columns->sectionStretchFactor(logicalIndex);
}

void VirtualTableView::setColumnDragEnabled(bool enabled)
{
    if (m_columnDragEnabled == enabled)
        return;
    m_columnDragEnabled = enabled;
    applyHeaderGestureSettings();
}

void VirtualTableView::setVerticalHeaderDragEnabled(bool enabled)
{
    if (m_verticalHeaderDragEnabled == enabled)
        return;
    m_verticalHeaderDragEnabled = enabled;
    // The row side needs a model that records the order (see the header's doc comment).
    // Without one - and without the application ever having installed one - the view
    // instantiates the base class so the gesture is usable at all; an application model
    // always wins, whenever it arrives.
    if (enabled && !model() && !m_applicationModelSeen) {
        auto *internal = new ReorderableTableModel(this);
        m_internalModel = internal;
        setModel(internal);
    }
    applyHeaderGestureSettings();
}

void VirtualTableView::setColumnResizeEnabled(bool enabled)
{
    if (m_columnResizeEnabled == enabled)
        return;
    m_columnResizeEnabled = enabled;
    applyHeaderGestureSettings();
}

void VirtualTableView::setVerticalHeaderResizeEnabled(bool enabled)
{
    if (m_verticalHeaderResizeEnabled == enabled)
        return;
    m_verticalHeaderResizeEnabled = enabled;
    applyHeaderGestureSettings();
}

void VirtualTableView::applyHeaderGestureSettings()
{
    const auto apply = [](HeaderViewInterface *header, bool dragEnabled, bool resizeEnabled) {
        auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(header);
        if (!widgetHeader)
            return;
        widgetHeader->setSectionDragEnabled(dragEnabled);
        widgetHeader->setSectionResizeEnabled(resizeEnabled);
    };
    // The column side: the installed header and every pane clone (a drag inside a frozen
    // pane reorders like one in the scrolling pane).
    apply(m_horizontalHeader, m_columnDragEnabled, m_columnResizeEnabled);
    for (HeaderViewInterface *paneHeader : m_paneHeaders)
        apply(paneHeader, m_columnDragEnabled, m_columnResizeEnabled);
    // The row side: the strip and the frozen-row bands, which are strips of their own.
    apply(m_verticalHeader, m_verticalHeaderDragEnabled, m_verticalHeaderResizeEnabled);
    apply(m_frozenTopRowsHeader, m_verticalHeaderDragEnabled, m_verticalHeaderResizeEnabled);
    apply(m_frozenBottomRowsHeader, m_verticalHeaderDragEnabled, m_verticalHeaderResizeEnabled);
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
    refreshHoveredIndex();
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

void VirtualTableView::setColumnFollowsHeaderVisual(bool follows)
{
    if (m_columnFollowsHeaderVisual == follows)
        return;
    m_columnFollowsHeaderVisual = follows;
    // Turning it off has to put the body back on the committed geometry *now*: a
    // running header animation sends frames that are simply ignored, so nothing
    // else would ever correct the column hosts left on an intermediate position.
    if (!follows)
        updateColumnLayout();
    visualColumnGeometryChanged();
}

void VirtualTableView::watchHeaderVisualGeometry(HeaderViewInterface *header)
{
    if (!header)
        return;
    // A renderer without visual state (the native adapter) ignores the callback, so
    // this is one line on every header and nothing happens until a widget header
    // actually moves its sections. The renderer is a child of this view and dies
    // with it, so capturing `this` cannot outlive the view.
    header->setVisualGeometryCallback([this]() { onHeaderVisualGeometryFrame(); });
}

bool VirtualTableView::columnVisualX(int logicalIndex, int *viewportX) const
{
    if (!m_columnFollowsHeaderVisual)
        return false;
    // The installed header renders the primary pane, the derived renderers the
    // frozen / extra scroll panes (§43). A column belongs to exactly one of them, so
    // the first renderer that has a visual state for it decides.
    if (m_horizontalHeader && m_horizontalHeader->hasVisualSectionGeometry()
        && m_horizontalHeader->sectionVisualX(logicalIndex, viewportX))
        return true;
    for (HeaderViewInterface *header : m_paneHeaders) {
        if (header && header->hasVisualSectionGeometry()
            && header->sectionVisualX(logicalIndex, viewportX))
            return true;
    }
    return false;
}

void VirtualTableView::onHeaderVisualGeometryFrame()
{
    if (m_visualGeometryFrameActive)
        return;
    m_visualGeometryFrameActive = true;
    if (m_columnFollowsHeaderVisual)
        updateVisualColumnGeometry();
    visualColumnGeometryChanged();
    if (m_rowFollowsHeaderVisual)
        updateVisualRowGeometry();
    m_visualGeometryFrameActive = false;
}

void VirtualTableView::updateVisualColumnGeometry()
{
    // Deliberately *not* updateColumnLayout(): the committed geometry did not
    // change and the headings already exist, so only x has to be corrected. An
    // application's layoutRowWidget() is a per-change hook, not a per-frame one, so
    // it is not re-run here (the framework-managed column hosts are moved directly).
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        updateCellGeometry();
        return;
    }
    const QList<MaterializedItem> items = materializedItems();
    for (const MaterializedItem &item : items)
        applyColumnLayout(item, false);
}

void VirtualTableView::setRowFollowsHeaderVisual(bool follows)
{
    if (m_rowFollowsHeaderVisual == follows)
        return;
    m_rowFollowsHeaderVisual = follows;
    // Turning it off has to put the rows back on the committed layout *now*: the running
    // strip will not send another frame.
    if (!follows)
        updateVisualRowGeometry();
}

VirtualHeaderView *VirtualTableView::visualRowStrip() const
{
    // Only the installed strip (the scrolling band) drives the rows: a frozen band's rows
    // are pinned at the edge, so letting them follow a preview would move them off it.
    auto *strip = dynamic_cast<VirtualHeaderView *>(m_verticalHeader);
    if (!strip || !strip->hasVisualSectionGeometry())
        return nullptr;
    if (strip->headerWidget()->isHidden())
        return nullptr;
    return strip;
}

void VirtualTableView::updateVisualRowGeometry()
{
    VirtualHeaderView *strip = visualRowStrip();
    if (!strip && !m_rowVisualOffsetsActive)
        return;                       // nothing is dragging and nothing was offset
    m_rowVisualOffsetsActive = strip != nullptr;

    for (const MaterializedItem &item : materializedItems()) {
        QWidget *widget = item.widget;
        const qsizetype row = viewItemForIndex(item.index);
        if (!widget || row < 0 || isRowFrozen(row))
            continue;                 // frozen rows stay where the layout pinned them
        // The kernel puts the widget at `geometry - the pane's origin`; the parent is the
        // scrolling pane's clip container (or the viewport when nothing is clipped).
        QWidget *parent = widget->parentWidget();
        const int parentY = parent && parent != viewport() ? parent->y() : 0;
        int visualY = item.geometry.y();
        if (strip) {
            // The strip reports the y its section is *drawn* at, in viewport coordinates
            // (the interface's report hook is axis aware).
            int reported = 0;
            if (strip->sectionVisualX(int(row), &reported))
                visualY = reported;
        }
        widget->move(widget->x(), visualY - parentY);

    }
}

void VirtualTableView::updatePaneLayout()
{
    // A stretching column is measured against the width the columns have to cover, so the
    // geometry is told the viewport width *before* the pane split reads the section sizes
    // (a pane's extent is the sum of its sections). Rewriting those sizes emits geometry
    // signals, and a listener may come back here: the pass below is what the signal is
    // about, so the re-entered call does nothing and the outer one finishes the job.
    if (m_paneLayoutActive)
        return;
    m_paneLayoutActive = true;
    m_columns->setStretchExtent(viewport()->width());
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
    m_paneLayoutActive = false;
}

void VirtualTableView::rebindMaterializedRows()
{
    // Cell Widget Mode does not need this: a cell widget carries its own persistent index, so
    // a structural change materializes/drops cells through the normal window logic.
    if (m_materializationMode == MaterializationMode::CellWidgets)
        return;
    // Same helper the dataChanged path uses - it only touches materialized rows and keeps the
    // bind bookkeeping (diagnostics/log) consistent.
    rebindItemsInModelRange(QModelIndex(), 0, std::numeric_limits<int>::max());
}

void VirtualTableView::updatePaneLayoutForScroll()
{
    // Only the offsets changed: refresh each pane's window with a binary search
    // instead of rebuilding the whole pane/column cache (which is O(total
    // columns)). The structural pass stays in updatePaneLayout().
    const bool windowMoved = m_panes.refreshScrollWindows();
    // The pane headers follow the offset of their own scroll group.
    syncHeaderPanes();
    // Every column x may have moved: the row widgets (or the cells) follow.
    updateColumnLayout();
    refreshHoveredIndex();
    // Row widgets cover the viewport and only re-position their columns, so a
    // horizontal scroll does not need a materialization pass there. A cell window
    // does: which cells exist changed.
    if (windowMoved && m_materializationMode == MaterializationMode::CellWidgets)
        markDirty();
}

void VirtualTableView::syncHeaderPanes()
{
    const QVector<TablePane> &panes = m_panes.panes();
    int terminalColumn = -1;
    for (int i = panes.size() - 1; i >= 0; --i) {
        if (!panes.at(i).logicalColumns.isEmpty()) {
            terminalColumn = panes.at(i).logicalColumns.last();
            break;
        }
    }
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
            if (!header)
                continue;   // a renderer we cannot reproduce (see createHorizontalPaneHeader())
            header->setGeometryModel(m_columns);
            header->setLabelModel(model());
            header->setSortInteractionEnabled(m_sortingEnabled);
            // Same contract as the installed header: the clone is table driven from the
            // moment it exists, so it never puts the geometry's stretch target on its own
            // (frozen) pane width.
            header->setViewportOrigin(viewport()->geometry().topLeft());
            // A drag inside a frozen pane previews on *that* renderer, so the body
            // has to follow its sections just like the primary header's.
            watchHeaderVisualGeometry(header);
        }
        // A frozen pane is pinned (offset 0); a scrolling pane of a group other
        // than the primary one follows its own group offset (§43 "advanced
        // panes"), so its header stays aligned with the body.
        header->setPaneFilter(pane.logicalColumns, pane.isFrozen());
        if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(header))
            widgetHeader->setPaneTerminalColumn(terminalColumn);
        header->setPaneOffset(pane.isFrozen() ? 0 : m_panes.groupOffset(pane.scrollGroup));
        anyOtherPane = true;
    }
    if (!m_horizontalHeader)
        return;
    if (primaryIndex >= 0 && anyOtherPane) {
        if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(m_horizontalHeader))
            widgetHeader->setPaneTerminalColumn(terminalColumn);
        m_horizontalHeader->setPaneFilter(panes.at(primaryIndex).logicalColumns, false);
        // ... and it packs its own columns too, exactly like the pane clones above. A
        // pane's columns are not necessarily a contiguous slice of the committed order
        // (a frozen column set is a set, so it can sit behind scrollable columns), and
        // only the "own packing" path derives the x and the materialization window from
        // the pane's own column list. Without this the primary header reads the flat
        // committed x: a section then lands on the slot of another column and whole
        // columns stop being materialized at all.
        m_horizontalHeader->setPaneOffset(
            panes.at(primaryIndex).isFrozen()
                ? 0
                : m_panes.groupOffset(panes.at(primaryIndex).scrollGroup));
    } else {
        m_horizontalHeader->clearPaneFilter();
    }
    applyHeaderAnimationSettings();
    applyHeaderGestureSettings();
    applyGridLineVisibilityToHeaders();
}

void VirtualTableView::dropDerivedPaneHeaders()
{
    // The derived pane renderers borrow the primary header's adapter: their sections have to
    // be unbound through that adapter *before* it is replaced (and deleted), so they go away
    // here and are rebuilt by rebuildDerivedPaneHeaders().
    for (HeaderViewInterface *&paneHeader : m_paneHeaders)
        deleteHeader(paneHeader);
    m_paneHeaders.clear();
}

void VirtualTableView::rebuildDerivedPaneHeaders()
{
    // The primary header has a new adapter: recreate the pane renderers so they borrow the
    // new one (they were dropped by dropDerivedPaneHeaders()).
    syncHeaderPanes();
    layoutHeaderWidgets();
}

void VirtualTableView::dropFrozenRowHeaders()
{
    deleteHeader(m_frozenTopRowsHeader);
    deleteHeader(m_frozenBottomRowsHeader);
}

void VirtualTableView::rebuildFrozenRowHeaders()
{
    syncVerticalPaneHeaders();
    layoutVerticalHeaderStrips();
}

HeaderViewInterface *VirtualTableView::createHorizontalPaneHeader()
{
    if (auto *widgetHeader = dynamic_cast<VirtualHeaderView *>(m_horizontalHeader)) {
        auto *header = new VirtualHeaderView(Qt::Horizontal, this);
        header->setAdapter(widgetHeader->adapter());
        header->setSectionOverscan(widgetHeader->sectionOverscan());
        return header;
    }
    // A renderer this table cannot reproduce (an application's own HeaderViewInterface
    // that is not a VirtualHeaderView) leaves the panes without their own renderer: the
    // installed one keeps showing its own pane, honestly, instead of a look-alike.
    return nullptr;
}

HeaderViewInterface *VirtualTableView::createDefaultHorizontalHeader()
{
    // The default renderer is a widget header with a label-only adapter (so the
    // out-of-the-box header looks like the style-painted one but supports
    // everything a widget header supports: the resize cursor, drag reordering, the
    // section transition and the body following it, and pane clones).
    return new LabelHeaderView(Qt::Horizontal, this);
}

HeaderViewInterface *VirtualTableView::createVerticalPaneHeader()
{
    if (!m_verticalHeader)
        return nullptr;
    // A widget strip is cloned the same way the column header clones its panes: the
    // band gets another renderer of the same kind, borrowing the installed adapter.
    if (auto *widgetStrip = dynamic_cast<VirtualHeaderView *>(m_verticalHeader)) {
        auto *header = new VirtualHeaderView(Qt::Vertical, this);
        header->setAdapter(widgetStrip->adapter());
        header->setSectionOverscan(widgetStrip->sectionOverscan());
        header->setGeometryModel(m_rowHeaders);
        header->setLabelModel(model());
        watchRowStrip(header);
        return header;
    }
    // A renderer this table cannot reproduce (an application's own HeaderViewInterface)
    // leaves the strip single - documented in docs/history/row-freezing.md.
    return nullptr;
}

HeaderViewInterface *VirtualTableView::createDefaultVerticalHeader()
{
    // Same renderer as the column header, other axis: a label-only widget strip. Only the
    // rows its window shows own a widget, so a ten-million-row uniform table costs
    // O(visible + overscan) - and the strip supports the same gestures as the column
    // header: drag a row boundary to set the row height, drag a row number to move the row
    // (reported through rowMoveRequested() and applied with model()->moveRows()).
    return new LabelHeaderView(Qt::Vertical, this);
}
void VirtualTableView::watchRowStrip(HeaderViewInterface *strip)
{
    auto *widgetStrip = dynamic_cast<VirtualHeaderView *>(strip);
    if (!widgetStrip)
        return;
    // The order of the rows is the model's, not the strip's: a committed drag reports
    // where the row should land instead of moving the row geometry (which only mirrors
    // the model).
    widgetStrip->setSectionOrderExternal(true);
    connect(widgetStrip, &VirtualHeaderView::sectionMoveRequested, this,
            &VirtualTableView::moveRowsForStripDrag, Qt::UniqueConnection);
    // ... and its preview frames keep the body's rows in step with the numbers
    // (setRowFollowsHeaderVisual(), on by default).
    widgetStrip->setVisualGeometryCallback([this]() { onHeaderVisualGeometryFrame(); });
}

void VirtualTableView::moveRowsForStripDrag(int fromRow, int toRow)
{
    const qsizetype count = viewItemCount();
    if (fromRow < 0 || toRow < 0 || fromRow >= count || toRow >= count || fromRow == toRow)
        return;
    emit rowMoveRequested(fromRow, toRow);
    QAbstractItemModel *m = model();
    if (!m)
        return;
    // moveRows() inserts *before* the destination child, so a move towards the end has
    // to name the row after the target (same convention as beginMoveRows()).
    const int destination = toRow > fromRow ? toRow + 1 : toRow;
    // The table's rows are the model's top-level rows (the table has no root index).
    m->moveRows(QModelIndex(), fromRow, 1, QModelIndex(), destination);
}

bool VirtualTableView::isSpanValid(const QModelIndex &, const TableSpan &) const
{
    return true;
}

QRect VirtualTableView::spanRowRect(qsizetype row) const
{
    return m_rowLayout ? m_rowLayout->itemRect(row, verticalOffset()) : QRect();
}

int VirtualTableView::leadingCellInset(const QModelIndex &) const
{
    return 0;
}

QRect VirtualTableView::rowGridLineExclusion(int) const
{
    return QRect();
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
    applyHeaderGestureSettings();
    applyGridLineVisibilityToHeaders();
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
        : VirtualTableView::sectionSeparatorColor(this);
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

    // QScrollBar only offers an int range while the logical offset is 64-bit: the
    // mapper compresses the logical space into the bar (and keeps the precision
    // window on the current position), exactly like the vertical scroll bar.
    m_horizontalMapper.setExtents(maximum + viewportWidth, viewportWidth);

    const QSignalBlocker blocker(bar);
    bar->setRange(0, m_horizontalMapper.scrollRange());
    bar->setPageStep(qMax(1, viewportWidth));
    bar->setSingleStep(qMax(1, viewportWidth / 20));
    const int value = m_horizontalMapper.toScrollBarValue(m_columns->viewportOffset());
    m_horizontalMapper.setAnchor(m_columns->viewportOffset(), value);
    bar->setValue(value);
    // The raw offset is never written back: only the mapped value is, and only
    // when the round trip cannot represent the offset exactly.
    const qint64 mapped = m_horizontalMapper.toLogicalOffset(value);
    if (m_columns->viewportOffset() != mapped)
        m_columns->setViewportOffset(mapped);
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
    // offset wins over the geometry, see setPaneOffset()), while the
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
    // Beyond the mirror limit the strip survives as long as the per-row state stays a
    // handful of *explicit* heights: those are sparse size overrides at any scale. What
    // the limit really protects against is a state per row, i.e. a model whose rows were
    // *measured* into different heights - that stays unsupported and is documented.
    const bool sparseSizes = count > kRowHeaderMirrorLimit
        && !m_explicitRowHeights.isEmpty()
        && m_explicitRowHeights.size() <= kMaxSparseRowHeights;
    const auto *blockSizes = m_rowLayout
        ? dynamic_cast<const BlockSizeIndex *>(m_rowLayout->sizeIndex()) : nullptr;
    const bool noSizeExceptions = blockSizes && blockSizes->explicitSizeCount() == 0;
    const bool supported = sparseSizes || noSizeExceptions
        || !(variable && count > kRowHeaderMirrorLimit);
    if (supported != m_verticalHeaderSupported) {
        m_verticalHeaderSupported = supported;
        if (!supported) {
            // Per-row heights cannot be mirrored into a native header at this scale
            // without an O(rows) structure. Only the *effective* support changes: the
            // request stays as the application left it, so the strip comes back on its
            // own when the model shrinks or the heights become uniform again (P2-9).
            qWarning("VirtualItemViews: vertical header hidden for %lld variable-height rows; "
                     "use uniform row heights or a widget header (v0.5).",
                     qint64(count));
        }
        // Hiding/showing the strip is a layout change like setVerticalHeaderVisible().
        layoutHeaderWidgets();
        relayout();
    }
    if (!supported) {
        // The strip is hidden, but the row geometry stays truthful: a uniform count with no
        // stale sparse override (the pass that turns the support off must not leave the
        // state of the last supported pass behind).
        if (m_rowHeaders->sectionCount() != int(count))
            m_rowHeaders->setSectionCount(int(count));
        m_rowHeaders->clearExplicitSectionSizes();
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
    // rows the user resized explicitly. A uniform table keeps "count + default size"
    // (HeaderGeometry's uniform representation: O(1) whatever the row count is) and the
    // few explicitly resized rows are sparse size overrides - which is what lets a
    // ten-million-row uniform table keep a row-number strip (§5/§12 of the
    // vertical-header decision) instead of materialising one Section per row.
    //
    // A *variable* height model is the case that still needs per-row states for every
    // measured row, so it keeps the mirror limit.
    const bool mirror = count > 0 && count <= kRowHeaderMirrorLimit
        && (variable || !m_explicitRowHeights.isEmpty());
    if (!mirror) {
        // Uniform heights: "count + default size" is the whole geometry, and the rows the
        // user resized are sparse overrides. Starting from a clean slate keeps an override
        // that is no longer backed by an explicit height from staying behind.
        if (m_rowHeaders->sectionCount() != int(count))
            m_rowHeaders->setSectionCount(int(count));
        m_rowHeaders->clearExplicitSectionSizes();
        for (auto it = m_explicitRowHeights.constBegin(); it != m_explicitRowHeights.constEnd();
             ++it) {
            const qsizetype row = viewItemForIndex(it.key());
            if (row < 0 || row >= count)
                continue;
            // The committed height, not the stored request: under
            // RowSizePolicy::MeasuredWins a measurement may win over it.
            const int size = rowHeight(row);
            if (size > 0)
                m_rowHeaders->resizeSection(int(row), size);
        }
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
    for (auto it = m_explicitRowHeights.constBegin(); it != m_explicitRowHeights.constEnd();
         ++it) {
        const qsizetype row = viewItemForIndex(it.key());
        if (row < 0 || row >= count)
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
    QPointer<QAbstractItemModel> m(model());
    if (!m || logicalIndex < 0 || logicalIndex >= columnCount())
        return;
    m_sortGuard = true;
    m_columns->setSortIndicator(logicalIndex, order);
    m_sortGuard = false;
    if (!m || m.data() != model())
        return;
    emit sortIndicatorRequested(logicalIndex, order);
    if (m && m.data() == model())
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
    QPointer<QAbstractItemModel> m(model());
    if (!m)
        return;
    emit sortIndicatorRequested(logicalIndex, order);
    if (m && m.data() == model())
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
    // The size is an unsigned 32-bit field of the stream, so it is checked against
    // both the buffer and int *before* anything is read with it: casting a huge value
    // to int first would turn it into a negative length (P2-3).
    if (stream.status() != QDataStream::Ok || magic != kTableStateMagic
        || version < 1 || version > kTableStateVersionWithFrozenRows
        || qint64(columnStateSize) > qint64(state.size())
        || columnStateSize > quint32(std::numeric_limits<int>::max())) {
        // Not a table level state: accept a bare HeaderGeometry state so a state
        // saved before the pane sets existed keeps working.
        return m_columns->restoreState(state);
    }

    // Parse and validate the whole state first: a corrupt tail must not leave the
    // column geometry of the view changed (the frozen pane sets and the frozen row
    // counts are parsed *after* the column state would have been applied, so this
    // used to return false with the columns already moved).
    QByteArray columnState(int(columnStateSize), Qt::Uninitialized);
    if (stream.readRawData(columnState.data(), int(columnStateSize)) != int(columnStateSize))
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

    // Everything parsed: commit once. HeaderGeometry::restoreState() validates its
    // own stream before touching a section, so a state we accepted here is applied
    // completely or not at all.
    // The restore may replace the sort state, which the geometry reports through
    // sortIndicatorChanged - the same signal a user click produces. The model is not to be
    // sorted here: a restored state says what the state *was*, not that the user asked for a
    // sort now (P1 of the third review).
    m_sortGuard = true;
    const bool restored = m_columns->restoreState(columnState);
    m_sortGuard = false;
    if (!restored)
        return false;
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

QRect VirtualTableView::dragPixmapRect(const QModelIndex &index) const
{
    // Per-cell drags preview the dragged cell; a row drag (SelectionBehavior::SelectRows) keeps the
    // whole row, which is exactly what Row Widget Mode materializes. Cell Widget Mode has no row
    // widget to cut down, so it keeps the "no preview" behaviour.
    if (!index.isValid() || m_materializationMode != MaterializationMode::RowWidgets
        || selectionBehavior() != SelectionBehavior::SelectItems) {
        return QRect();
    }
    const qsizetype item = viewItemForIndex(index);
    QWidget *row = item >= 0 ? widgetForIndex(viewIndex(item, 0)) : nullptr;
    if (!row)
        return QRect();
    // The column geometry is viewport relative and the row widget covers the viewport: the cell's
    // rectangle inside the row widget differs from the geometry by the row widget's own origin.
    // (The same mapping the framework uses to place the column hosts, so it also holds for a row
    // widget that lays its cells out itself.)
    const ColumnGeometry column = columnGeometry(index.column());
    if (!column.isValid() || column.hidden || column.width <= 0)
        return QRect();
    const QPoint rowOrigin = row->geometry().topLeft();   // viewport coordinates
    return QRect(column.viewportX - rowOrigin.x(), 0, column.width, row->height());
}

void VirtualTableView::setAdapter(WidgetAdapter *adapter, bool takeOwnership)
{
    // Both entry points (the typed one below and a call through a VirtualItemView *) end up
    // here, so the table invariant "the kernel adapter and the table adapter are the same
    // object" holds no matter how the call was written (P1 of the fourth review).
    auto *tableAdapter = dynamic_cast<TableWidgetAdapter *>(adapter);
    if (adapter && !tableAdapter) {
        qWarning("VirtualTableView::setAdapter(): the adapter is not a TableWidgetAdapter; the "
                 "installed adapter is kept (a table creates and lays out its rows through "
                 "TableWidgetAdapter)");
        return;
    }
    setTableAdapter(tableAdapter, takeOwnership);
}

void VirtualTableView::setTableAdapter(TableWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_tableAdapter == adapter && VirtualItemView::adapter() == adapter) {
        m_ownTableAdapter = m_ownTableAdapter || takeOwnership;
        return;
    }
    const quint64 changeSerial = ++m_tableAdapterChangeSerial;
    TableWidgetAdapter *const previous = m_tableAdapter;
    const bool previousOwned = m_ownTableAdapter;
    // Keep the old factory in place while the base returns its widgets through the old adapter.
    if (!installAdapter(adapter, false, false))
        return;
    if (m_tableAdapterChangeSerial != changeSerial || VirtualItemView::adapter() != adapter)
        return;
    m_tableAdapter = adapter;
    m_ownTableAdapter = takeOwnership;
    if (previousOwned && previous != adapter)
        delete previous;
    if (m_tableAdapterChangeSerial != changeSerial || VirtualItemView::adapter() != adapter)
        return;
    relayout();
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
    const quint64 changeSerial = ++m_cellConfigurationSerial;
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    // Both directions have to release the widgets owned by the old mode: the
    // kernel does not recycle row widgets while cell mode is active.
    recycleAllCells();
    if (m_cellConfigurationSerial != changeSerial || modelChangeSerial() != modelSerial
        || viewMappingSerial() != mappingSerial)
        return;
    recycleAllItems();
    if (m_cellConfigurationSerial != changeSerial || modelChangeSerial() != modelSerial
        || viewMappingSerial() != mappingSerial)
        return;
    // Row widgets and cell widgets live in the same recycler, and both use
    // WidgetType 0 by default: a pool entry of the old mode must never be handed
    // out as a widget of the new mode.
    if (recycler())
        recycler()->clear();
    if (m_cellConfigurationSerial != changeSerial || modelChangeSerial() != modelSerial
        || viewMappingSerial() != mappingSerial)
        return;
    m_materializationMode = mode;
    relayout();
}

void VirtualTableView::setCellAdapter(CellWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_cellAdapter == adapter) {
        m_ownCellAdapter = m_ownCellAdapter || takeOwnership;
        return;
    }
    const quint64 changeSerial = ++m_cellConfigurationSerial;
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    // Recycle (unbind with the old adapter) and drop the pool *before* the old
    // adapter is deleted - same reasoning as setAdapter()/setTableAdapter().
    recycleAllCells();
    if (m_cellConfigurationSerial != changeSerial || modelChangeSerial() != modelSerial
        || viewMappingSerial() != mappingSerial)
        return;
    if (recycler())
        recycler()->clear();
    if (m_cellConfigurationSerial != changeSerial || modelChangeSerial() != modelSerial
        || viewMappingSerial() != mappingSerial)
        return;
    if (m_ownCellAdapter) {
        CellWidgetAdapter *previous = m_cellAdapter;
        m_cellAdapter = nullptr;
        m_ownCellAdapter = false;
        delete previous;
        if (m_cellConfigurationSerial != changeSerial || modelChangeSerial() != modelSerial
            || viewMappingSerial() != mappingSerial)
            return;
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
    // (§31 row direction, docs/history/row-freezing.md).
    const QRect rowRect = geometryForViewRow(row);
    if (rowRect.height() <= 0)
        return QRect();
    return QRect(column.viewportX, rowRect.y(), column.width, rowRect.height());
}

// ---------------------------------------------------------------------------
// Spans (§43 "spans", see docs/history/spans.md)
// ---------------------------------------------------------------------------

int VirtualTableView::itemPaneSeparatorLeftExtension() const
{
    // The row-number strip is outside the viewport: the row pane boundary lines cross it
    // just like the column boundary lines cross the header strip.
    return (isVerticalHeaderShown() && m_verticalHeader) ? qMax(0, m_verticalHeaderWidth) : 0;
}

QColor VirtualTableView::itemPaneSeparatorColor() const
{
    return resolvedHorizontalGridLineColor();
}

QColor headerSectionSeparatorColor(const QWidget *context)
{
    // Render a small section with the current style and read the pixel of its right edge:
    // that is exactly the separator the style draws between two sections.
    static constexpr int kProbeWidth = 8;
    static constexpr int kProbeHeight = 8;
    QImage probe(kProbeWidth, kProbeHeight, QImage::Format_ARGB32_Premultiplied);
    probe.fill(Qt::transparent);
    {
        QPainter painter(&probe);
        painter.setRenderHint(QPainter::Antialiasing, false);
        QStyleOptionHeader option;
        if (context)
            option.initFrom(context);
        option.state |= QStyle::State_Horizontal | QStyle::State_Enabled;
        option.orientation = Qt::Horizontal;
        option.position = QStyleOptionHeader::Middle; // draws the separator
        option.rect = QRect(0, 0, kProbeWidth, kProbeHeight);
        const QWidget *styleSource = context;
        const_cast<QStyle *>(styleSource ? styleSource->style() : QApplication::style())
            ->drawControl(QStyle::CE_HeaderSection, &option, &painter,
                          const_cast<QWidget *>(styleSource));
    }

    const QColor separator = probe.pixelColor(kProbeWidth - 1, kProbeHeight / 2);
    if (separator.isValid() && separator.alpha() > 0)
        return separator;
    // Fall back to a palette role when the style draws nothing there.
    return context ? context->palette().color(QPalette::Mid) : QColor(160, 160, 160);
}

void VirtualTableView::drawPaneSeparator(QPainter *painter, const QRect &rect,
                                         const PaneSeparatorStyle &style,
                                         const QColor &styleSeparatorColor)
{
    if (!painter || rect.isEmpty() || !style.isVisible())
        return;
    const QColor color = style.effectiveColor(styleSeparatorColor);
    if (!color.isValid())
        return;
    if (style.lineStyle == Qt::SolidLine) {
        painter->fillRect(rect, color);
        return;
    }
    QPen pen(color, qMax(1, style.width), style.lineStyle);
    painter->save();
    painter->setPen(pen);
    const int x = rect.center().x();
    painter->drawLine(x, rect.top(), x, rect.bottom());
    painter->restore();
}

QColor VirtualTableView::sectionSeparatorColor(const QWidget *context)
{
    // The pane lines and the default section widgets share this one probe
    // (headerSectionSeparatorColor()); the view keeps the static entry point it has always
    // exported.
    return headerSectionSeparatorColor(context);
}
void VirtualTableView::setPanes(const QVector<TablePaneSpec> &panes)
{
    // The layout normalizes the list (a column belongs to one pane, a scroll group
    // is never negative), so the "nothing changed" test compares what it stores
    // rather than what came in: passing the same broken list twice is a no-op.
    const QVector<TablePaneSpec> previous = m_panes.paneSpecs();
    m_panes.setPaneSpecs(panes);
    if (m_panes.paneSpecs() == previous)
        return;
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
    // Only the offset of that group changed, so the cheap path applies: refresh each pane's
    // visible window with a binary search over the cached prefix sums instead of rebuilding
    // the whole pane/column cache (P1-9 of the second review - a scrolling group that is not
    // the primary one used to pay a full O(total columns) pass per step).
    updatePaneLayoutForScroll();
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
    const QModelIndex anchor = viewIndex(row, column);
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
    if (!map)
        return;
    map->removeSpan(viewIndex(row, column));
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
    const TableSpan span = m_spanProvider->spanAt(index);
    return isSpanValid(index, span) ? span : TableSpan();
}

QModelIndex VirtualTableView::anchorIndex(const QModelIndex &index) const
{
    if (!m_spanProvider || !index.isValid() || index.model() != model())
        return index;
    const int targetVisual = m_columns->visualIndex(index.column());
    if (targetVisual < 0)
        return index;
    const int parentColumns = model()->columnCount(index.parent());

    const auto covers = [this, &index, targetVisual, parentColumns](const QModelIndex &anchor) {
        if (!anchor.isValid() || anchor.model() != index.model()
            || anchor.parent() != index.parent())
            return false;
        const TableSpan span = m_spanProvider->spanAt(anchor);
        const int anchorVisual = m_columns->visualIndex(anchor.column());
        if (!span.isMerged() || !isSpanValid(anchor, span) || anchorVisual < 0
            || index.row() < anchor.row() || index.row() - anchor.row() >= span.rowSpan
            || targetVisual < anchorVisual || targetVisual - anchorVisual >= span.columnSpan)
            return false;
        const int pane = m_panes.paneIndexOfColumn(anchor.column());
        if (pane < 0)
            return false;
        // spanRect() stops at a pane boundary or a column absent from this parent.
        for (int visual = anchorVisual; visual <= targetVisual; ++visual) {
            const int logical = m_columns->logicalIndex(visual);
            const ColumnGeometry geometry = columnGeometry(logical);
            if (geometry.isValid() && !geometry.hidden && geometry.width > 0) {
                if (logical >= parentColumns || m_panes.paneIndexOfColumn(logical) != pane)
                    return false;
            }
        }
        return true;
    };

    const QModelIndex providerAnchor = m_spanProvider->anchorOf(index);
    if (covers(providerAnchor))
        return providerAnchor;

    // The provider's default reverse lookup walks model columns. After a visual
    // reorder, search the bounded visual predecessors that spanRect() can draw.
    const TableSpan maximum = m_spanProvider->maximumSpan();
    if (!maximum.isMerged())
        return index;
    const int firstRow = qMax(0, index.row() - maximum.rowSpan + 1);
    const int firstVisual = qMax(0, targetVisual - maximum.columnSpan + 1);
    for (int row = index.row(); row >= firstRow; --row) {
        for (int visual = targetVisual; visual >= firstVisual; --visual) {
            const int logical = m_columns->logicalIndex(visual);
            const QModelIndex candidate = model()->index(row, logical, index.parent());
            if (covers(candidate))
                return candidate;
        }
    }
    return index;
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
    if (!isSpanValid(index, span))
        return QRect();
    const int lastModelRow = int(qMin(qint64(index.row()) + qMax(1, span.rowSpan) - 1,
                                      qint64(tableModel->rowCount(index.parent()) - 1)));
    const qsizetype firstViewRow = viewItemForIndex(index);
    const QModelIndex lastIndex = tableModel->index(lastModelRow, index.column(), index.parent());
    const qsizetype lastViewRow = viewItemForIndex(lastIndex);
    if (firstViewRow < 0 || lastViewRow < firstViewRow)
        return QRect();

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
    const int parentColumns = tableModel->columnCount(index.parent());
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
        if (logical >= parentColumns || m_panes.paneIndexOfColumn(logical) != paneIndex)
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

    const QRect first = spanRowRect(firstViewRow);
    if (first.height() <= 0)
        return QRect();
    const QRect last = lastViewRow >= firstViewRow
        ? spanRowRect(lastViewRow)
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
    if (!m_cellAdapter || !model())
        return nullptr;

    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 configurationSerial = m_cellConfigurationSerial;
    const quint64 lifecycleSerial = m_cellLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(model());
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    const auto requestIsCurrent = [this, &index, &activeModel, activeAdapter, modelSerial,
                                   mappingSerial, configurationSerial, lifecycleSerial]() {
        return activeModel && model() == activeModel.data()
            && modelChangeSerial() == modelSerial && viewMappingSerial() == mappingSerial
            && m_cellConfigurationSerial == configurationSerial
            && m_cellLifecycleSerial == lifecycleSerial && m_cellAdapter == activeAdapter
            && m_materializationMode == MaterializationMode::CellWidgets && index.isValid();
    };
    const QModelIndex modelIndex(index);
    const WidgetType type = activeAdapter->cellWidgetType(modelIndex);
    if (!requestIsCurrent() || QModelIndex(index) != modelIndex)
        return nullptr;
    QWidget *widget = recycler()->acquire(type);
    if (!widget)
        return nullptr;
    QPointer<QWidget> guardedWidget(widget);
    if (!requestIsCurrent() || QModelIndex(index) != modelIndex) {
        recycler()->discard(guardedWidget.data());
        return nullptr;
    }

    if (widget->parentWidget() != viewport())
        widget->setParent(viewport());
    if (!guardedWidget || !requestIsCurrent() || QModelIndex(index) != modelIndex) {
        recycler()->discard(guardedWidget.data());
        return nullptr;
    }

    // Never show a cell before it has been bound to its new index.
    widget->hide();
    if (!guardedWidget || !requestIsCurrent() || QModelIndex(index) != modelIndex) {
        recycler()->discard(guardedWidget.data());
        return nullptr;
    }
    m_cellTypes.insert(widget, type);
    m_cells.insert(index, widget);
    const auto bindingIsCurrent = [this, &index, &modelIndex, &guardedWidget,
                                   &requestIsCurrent, widget]() {
        if (requestIsCurrent() && QModelIndex(index) == modelIndex
            && guardedWidget && m_cells.value(index) == widget)
            return true;
        if (m_cells.value(index) == widget) {
            m_cells.remove(index);
            m_cellTypes.remove(widget);
            recycler()->discard(guardedWidget.data());
        }
        return false;
    };
    activeAdapter->bindCellWidget(widget, modelIndex);
    if (!bindingIsCurrent())
        return nullptr;
    prepareHoverTracking(widget);
    if (!bindingIsCurrent())
        return nullptr;
    activeAdapter->visualStateChanged(widget, modelIndex);
    if (!bindingIsCurrent())
        return nullptr;
    return widget;
}

void VirtualTableView::recycleCell(const QPersistentModelIndex &index, QWidget *widget)
{
    if (!widget || !m_cellAdapter)
        return;
    const WidgetType type = m_cellTypes.take(widget);
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    QPointer<QWidget> guardedWidget(widget);
    activeAdapter->unbindCellWidget(widget, QModelIndex(index));
    if (m_visualStateScope == VisualStateScope::Cell)
        clearVisualTransition(QModelIndex(index));
    if (!guardedWidget)
        return;
    if (m_cellAdapter != activeAdapter) {
        recycler()->discard(guardedWidget.data());
        return;
    }
    recycler()->recycle(type, guardedWidget.data());
}

void VirtualTableView::discardCellWidget(QWidget *widget)
{
    m_cellTypes.remove(widget);
    recycler()->discard(widget);
}

void VirtualTableView::recycleAllCells()
{
    if (m_cells.isEmpty())
        return;
    ++m_cellLifecycleSerial;
    QHash<QPersistentModelIndex, QWidget *> cells;
    cells.swap(m_cells);
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
        if (activeAdapter && m_cellAdapter == activeAdapter)
            recycleCell(it.key(), it.value());
        else
            discardCellWidget(it.value());
    }
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
    ++m_cellLifecycleSerial;
    QHash<QPersistentModelIndex, QWidget *> kept;
    QHash<QPersistentModelIndex, QWidget *> removedCells;
    kept.reserve(m_cells.size());
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        QModelIndex branch = it.key();
        while (branch.isValid() && branch.parent() != parent)
            branch = branch.parent();
        const bool removed = branch.isValid() && branch.row() >= first
            && branch.row() <= last;
        if (removed)
            removedCells.insert(it.key(), it.value());
        else
            kept.insert(it.key(), it.value());
    }
    m_cells.swap(kept);
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    for (auto it = removedCells.constBegin(); it != removedCells.constEnd(); ++it) {
        if (m_cellAdapter == activeAdapter)
            recycleCell(it.key(), it.value());
        else
            discardCellWidget(it.value());
    }
}

void VirtualTableView::recycleCellsInColumnRange(const QModelIndex &parent, int first, int last)
{
    if (m_cells.isEmpty() || !m_cellAdapter)
        return;
    ++m_cellLifecycleSerial;
    QHash<QPersistentModelIndex, QWidget *> kept;
    QHash<QPersistentModelIndex, QWidget *> removedCells;
    kept.reserve(m_cells.size());
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const QModelIndex index = it.key();
        const bool removed = index.isValid() && (!parent.isValid() || index.parent() == parent)
            && index.column() >= first && index.column() <= last;
        if (removed)
            removedCells.insert(it.key(), it.value());
        else
            kept.insert(it.key(), it.value());
    }
    m_cells.swap(kept);
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    for (auto it = removedCells.constBegin(); it != removedCells.constEnd(); ++it) {
        if (m_cellAdapter == activeAdapter)
            recycleCell(it.key(), it.value());
        else
            discardCellWidget(it.value());
    }
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
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 configurationSerial = m_cellConfigurationSerial;
    const quint64 lifecycleSerial = m_cellLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(model());
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    const auto requestIsCurrent = [this, &activeModel, activeAdapter, modelSerial,
                                   mappingSerial, configurationSerial, lifecycleSerial]() {
        return activeModel && model() == activeModel.data()
            && modelChangeSerial() == modelSerial && viewMappingSerial() == mappingSerial
            && m_cellConfigurationSerial == configurationSerial
            && m_cellLifecycleSerial == lifecycleSerial && m_cellAdapter == activeAdapter
            && m_materializationMode == MaterializationMode::CellWidgets;
    };
    const auto abortStalePass = [this, &requestIsCurrent]() {
        if (requestIsCurrent())
            return false;
        m_cellMaterializationActive = false;
        abortMaterializationPass();
        return true;
    };

    const QVector<int> columns = columnsForCellMaterialization();
    if (abortStalePass())
        return;
    QHash<QPersistentModelIndex, QWidget *> next;
    // Every range contributes: the scrolling window and, with frozen rows (§31 row
    // direction), the frozen rows as well.
    for (const VisibleRange &range : ranges) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            for (int column : columns) {
                const QModelIndex index = viewIndex(row, column);
                if (abortStalePass())
                    return;
                if (!index.isValid())
                    continue;
                // Merged cells are one target: only the anchor owns a widget, the
                // cells it covers are never materialized (§43 "spans"). A visible
                // covered cell still needs its anchor when that row is beyond the
                // vertical overscan window.
                const QModelIndex anchor = anchorIndex(index);
                if (abortStalePass())
                    return;
                if (!anchor.isValid())
                    continue;
                const QPersistentModelIndex persistent(anchor);
                if (next.contains(persistent))
                    continue;
                QWidget *widget = m_cells.value(persistent, nullptr);
                if (!widget) {
                    widget = createCellWidget(persistent);
                    if (abortStalePass())
                        return;
                    if (!widget)
                        continue;
                }
                next.insert(persistent, widget);
            }
        }
    }

    for (const QPersistentModelIndex &pinned : explicitPinnedIndexes()) {
        const QModelIndex cell = pinned;
        const qsizetype row = viewItemForIndex(cell);
        if (row < 0 || viewIndex(row, cell.column()) != cell)
            continue;
        const QModelIndex anchor = anchorIndex(cell);
        if (abortStalePass())
            return;
        if (!anchor.isValid())
            continue;
        const QPersistentModelIndex key(anchor);
        if (next.contains(key))
            continue;
        QWidget *widget = m_cells.value(key, nullptr);
        if (!widget)
            widget = createCellWidget(key);
        if (abortStalePass())
            return;
        if (widget)
            next.insert(key, widget);
    }

    // Keep pinned cells (focus/IME/popup/explicit pin) even outside the window,
    // recycle everything else.
    QList<QPersistentModelIndex> obsolete;
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (next.contains(it.key()))
            continue;
        if (isCellPinned(it.key(), it.value())) {
            next.insert(it.key(), it.value());
            continue;
        }
        obsolete.append(it.key());
    }
    for (const QPersistentModelIndex &key : obsolete) {
        QWidget *widget = m_cells.take(key);
        recycleCell(key, widget);
        if (abortStalePass())
            return;
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
        const qsizetype viewRow = viewItemForIndex(index);
        if (viewRow < 0) {
            widget->move(-qMax(1, widget->width()), -qMax(1, widget->height()));
            continue;
        }
        // Span aware: an anchor cell widget covers its whole merged area.
        QRect rect = cellRect(index);
        const int inset = qBound(0, leadingCellInset(index), rect.width());
        rect.adjust(inset, 0, 0, 0);
        // ... and follows its column while the header draws it away from the
        // committed position (§23/§24), like the row widget mode hosts do.
        int visualX = 0;
        if (columnVisualX(index.column(), &visualX))
            rect.translate(visualX - m_columns->columnGeometry(index.column()).viewportX, 0);
        const int paneIndex = m_panes.paneIndexOfColumn(index.column());
        const ItemPane::Type rowPane = itemPaneForRow(viewRow);
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
            if (frozen || isRowFrozen(viewRow))
                widget->raise();
        } else {
            if (isCellPinned(it.key(), widget))
                widget->move(-qMax(1, widget->width()), -qMax(1, widget->height()));
            else
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
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 configurationSerial = m_cellConfigurationSerial;
    const quint64 lifecycleSerial = m_cellLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(model());
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    const auto current = [this, &activeModel, activeAdapter, modelSerial, mappingSerial,
                          configurationSerial, lifecycleSerial]() {
        return activeModel && model() == activeModel.data()
            && modelChangeSerial() == modelSerial && viewMappingSerial() == mappingSerial
            && m_cellConfigurationSerial == configurationSerial
            && m_cellLifecycleSerial == lifecycleSerial && m_cellAdapter == activeAdapter
            && m_materializationMode == MaterializationMode::CellWidgets;
    };
    const QList<QPersistentModelIndex> indexes = m_cells.keys();
    for (const QPersistentModelIndex &persistent : indexes) {
        if (!current())
            return;
        const QModelIndex index = persistent;
        if (!index.isValid() || index.parent() != topLeft.parent())
            continue;
        if (index.row() < topLeft.row() || index.row() > bottomRight.row())
            continue;
        if (index.column() < topLeft.column() || index.column() > bottomRight.column())
            continue;
        QPointer<QWidget> widget(m_cells.value(persistent));
        if (!widget)
            continue;
        activeAdapter->bindCellWidget(widget.data(), index);
        if (!current())
            return;
        if (!widget || m_cells.value(persistent) != widget.data())
            continue;
        activeAdapter->visualStateChanged(widget.data(), index);
        if (!current())
            return;
    }
}

QModelIndex VirtualTableView::indexAt(const QPoint &viewportPos) const
{
    const QModelIndex rowIndex = VirtualItemView::indexAt(viewportPos);
    if (!rowIndex.isValid() || !m_columns)
        return rowIndex;
    const int column = columnAtViewportX(viewportPos.x());
    if (column < 0 || m_columns->isSectionHidden(column))
        return QModelIndex();
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

void VirtualTableView::setVisualStateScope(VisualStateScope scope)
{
    if (m_visualStateScope == scope)
        return;
    m_visualStateScope = scope;
    clearVisualTransitions();
    refreshVisualStates();
}

VirtualItemView::VisualState VirtualTableView::visualState(const QModelIndex &index) const
{
    if (m_visualStateScope == VisualStateScope::Cell || !index.isValid()
        || index.model() != model())
        return VirtualItemView::visualState(index);
    const QModelIndex hovered = hoveredIndex();
    const bool hoveredRow = hovered.isValid() && hovered.parent() == index.parent()
        && hovered.row() == index.row();
    const bool selectedRow = selectionModel()
        && selectionModel()->rowIntersectsSelection(index.row(), index.parent());
    return animatedVisualState(index.siblingAtColumn(0), hoveredRow, selectedRow);
}

void VirtualTableView::refreshVisualStates()
{
    if (m_materializationMode == MaterializationMode::RowWidgets) {
        VirtualItemView::refreshVisualStates();
        return;
    }
    notifyCellVisualStates(QModelIndex());
}

void VirtualTableView::refreshVisualState(const QModelIndex &index)
{
    if (m_materializationMode == MaterializationMode::RowWidgets) {
        VirtualItemView::refreshVisualState(index);
        return;
    }
    if (!m_cellAdapter || !index.isValid())
        return;
    if (m_visualStateScope == VisualStateScope::Cell) {
        auto it = m_cells.constFind(QPersistentModelIndex(index));
        if (it != m_cells.constEnd())
            m_cellAdapter->visualStateChanged(it.value(), QModelIndex(it.key()));
        return;
    }
    notifyCellVisualStates(index);
}

void VirtualTableView::notifyCellVisualStates(const QModelIndex &row)
{
    if (!m_cellAdapter)
        return;
    const bool filterRow = row.isValid();
    const QPersistentModelIndex target(row);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 configurationSerial = m_cellConfigurationSerial;
    const quint64 lifecycleSerial = m_cellLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(model());
    CellWidgetAdapter *const activeAdapter = m_cellAdapter;
    const QList<QPersistentModelIndex> indexes = m_cells.keys();
    for (const QPersistentModelIndex &persistent : indexes) {
        if (!activeModel || model() != activeModel.data()
            || modelChangeSerial() != modelSerial || viewMappingSerial() != mappingSerial
            || m_cellConfigurationSerial != configurationSerial
            || m_cellLifecycleSerial != lifecycleSerial || m_cellAdapter != activeAdapter
            || m_materializationMode != MaterializationMode::CellWidgets)
            return;
        const QModelIndex cell = persistent;
        if (!cell.isValid() || (filterRow
            && (!target.isValid() || cell.parent() != target.parent()
                || cell.row() != target.row())))
            continue;
        QPointer<QWidget> widget(m_cells.value(persistent));
        if (widget)
            activeAdapter->visualStateChanged(widget.data(), cell);
    }
}

QModelIndex VirtualTableView::indexForWidget(const QWidget *widget) const
{
    if (m_materializationMode == MaterializationMode::CellWidgets)
        return cellIndexForWidget(widget);
    return VirtualItemView::indexForWidget(widget);
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
    context.m_leadingCellInset = rowIndex.isValid() ? leadingCellInset(rowIndex) : 0;
    context.m_viewportRect = viewportRect;
    context.m_horizontalOffset = m_columns->viewportOffset();
    context.m_columnOverscan = m_columnOverscan;
    context.m_visibleColumns = m_panes.visibleScrollableRange();

    // §43 "spans": tell the adapter what the framework decided for this row.
    // A merged rectangle is reported in row widget coordinates and clipped to
    // the row: Row Widget Mode owns one widget per row, so a cross-row merge can
    // only be honoured by the business (see docs/history/spans.md).
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
            const int inset = qBound(0, leadingCellInset(cell), merged.width());
            merged.adjust(inset, 0, 0, 0);
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

void VirtualTableView::applyColumnLayout(const MaterializedItem &item, bool notifyAdapter)
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
            const qsizetype viewRow = viewItemForIndex(item.index);
            if (viewRow < 0 || !viewIndex(viewRow, logicalColumn).isValid()) {
                host->setVisible(false);
                continue;
            }
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

            // The header may be drawing this column away from its committed position
            // (§23/§24: a drag preview, or a transition settling a move). Only x is
            // taken from the renderer - width, pane, span and clipping keep coming
            // from the committed geometry, which stays authoritative.
            const int committedX = context.columnX(geometry.logicalIndex);
            int columnX = committedX;
            int visualX = 0;
            if (columnVisualX(geometry.logicalIndex, &visualX))
                columnX = visualX - context.viewportRect().x()
                    + (geometry.logicalIndex == 0 ? leadingCellInset(item.index) : 0);
            QRect hostRect(columnX, 0, geometry.width, context.viewportRect().height());
            if (context.spans().spanOf(logicalColumn).isMerged()) {
                // A merged area follows the column that owns it (§43).
                const QRect merged
                    = context.spans().rect(logicalColumn).translated(columnX - committedX, 0);
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

    if (notifyAdapter && m_tableAdapter)
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
    syncColumnSpacingWidgets();
}

void VirtualTableView::syncRowGridLines()
{
    QHash<qsizetype, QRect> desired;
    const QRect viewportRect = viewport()->geometry();
    if (m_horizontalGridLinesVisible && viewportRect.width() > 0) {
        for (const VisibleRange &range : visibleItemRanges()) {
            for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                if (row + 1 >= viewItemCount()
                    || (m_rowLayout && m_rowLayout->spacingAfter(row) > 0))
                    continue;
                const QRect rowRect = geometryForViewRow(row);
                if (!rowRect.intersects(viewport()->rect()))
                    continue;
                const int y = viewportRect.y() + rowRect.bottom() - m_horizontalGridLineWidth + 1;
                const QRect lineRect(viewportRect.x(), y, viewportRect.width(),
                                     m_horizontalGridLineWidth);
                const QRect paneRect = itemPaneRect(itemPaneForRow(row))
                    .translated(viewportRect.topLeft());
                const QRect clipped = lineRect.intersected(paneRect);
                if (!clipped.isEmpty())
                    desired.insert(row, clipped);
            }
        }
    }
    for (auto it = m_rowGridLines.begin(); it != m_rowGridLines.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->hide();
        m_rowGridLinePool.append(it.value());
        it = m_rowGridLines.erase(it);
    }
    const QColor color = resolvedHorizontalGridLineColor();
    const QVector<int> visibleColumns = m_spanProvider ? m_panes.columnsForLayout(1) : QVector<int>();
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        QWidget *line = m_rowGridLines.value(it.key(), nullptr);
        if (!line) {
            line = m_rowGridLinePool.isEmpty() ? new QWidget(this) : m_rowGridLinePool.takeLast();
            line->setObjectName(QStringLiteral("vivRowGridLine"));
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setAutoFillBackground(true);
            m_rowGridLines.insert(it.key(), line);
        }
        QPalette colors = line->palette();
        colors.setColor(QPalette::Window, color);
        line->setPalette(colors);
        line->setGeometry(it.value());
        QRegion mask(line->rect());
        const QRect excluded = rowGridLineExclusion(itemDepth(viewIndex(it.key())));
        if (!excluded.isEmpty())
            mask -= excluded.translated(viewportRect.x() - line->x(), 0);
        if (m_spanProvider && model()) {
            for (int column : visibleColumns) {
                const QModelIndex above = viewIndex(it.key(), column);
                const QModelIndex below = viewIndex(it.key() + 1, column);
                const QModelIndex anchor = anchorIndex(above);
                if (!anchor.isValid() || anchor != anchorIndex(below))
                    continue;
                const QRect merged = spanRect(anchor);
                mask -= QRect(viewportRect.x() + merged.x() - line->x(), 0,
                              merged.width(), line->height());
            }
        }
        if (mask == QRegion(line->rect()))
            line->clearMask();
        else
            line->setMask(mask);
        line->show();
        line->raise();
    }
}

void VirtualTableView::syncColumnSpacingWidgets()
{
    QHash<int, QRect> desired;
    QHash<int, QRect> fullRects;
    const int spacing = columnSpacing();
    const QColor verticalColor = resolvedVerticalGridLineColor();
    const QColor horizontalColor = resolvedHorizontalGridLineColor();
    const QRect viewportRect = viewport()->geometry();
    int last = -1;
    if ((spacing > 0 || m_verticalGridLinesVisible) && m_columns
        && viewport()->height() > 0) {
        const int headerTop = m_horizontalHeaderVisible && m_horizontalHeader
            ? viewportRect.y() - m_headerHeight : viewportRect.y();
        const int bottom = viewportRect.y() + viewportRect.height();
        const QVector<TablePane> &panes = m_panes.panes();
        for (int i = panes.size() - 1; i >= 0; --i) {
            if (!panes.at(i).logicalColumns.isEmpty()) {
                last = panes.at(i).logicalColumns.last();
                break;
            }
        }
        for (int logical : m_panes.columnsForLayout(1)) {
            if (m_columns->isSectionHidden(logical))
                continue;
            const int paneIndex = m_panes.paneIndexOfColumn(logical);
            const int x = m_panes.columnViewportX(logical);
            if (paneIndex < 0)
                continue;
            const QRect pane = m_panes.paneAt(paneIndex).viewportRect;
            const bool terminal = logical == last;
            const int columnEnd = x + m_columns->sectionSize(logical);
            if (terminal && (!m_verticalGridLinesVisible || columnEnd >= pane.right() + 1))
                continue;
            const int boundarySpacing = terminal ? 0 : spacing;
            const int verticalBand = m_verticalGridLinesVisible ? m_verticalGridLineWidth : 1;
            const QRect full(viewportRect.x() + columnEnd - verticalBand,
                             headerTop, boundarySpacing + verticalBand, bottom - headerTop);
            const QRect clipped = full.intersected(
                QRect(viewportRect.x() + pane.x(), headerTop, pane.width(), bottom - headerTop));
            if (!clipped.isEmpty()) {
                desired.insert(logical, clipped);
                fullRects.insert(logical, full);
            }
        }
    }
    for (auto it = m_columnSpacingWidgets.begin(); it != m_columnSpacingWidgets.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->hide();
        m_columnSpacingPool.append(it.value());
        it = m_columnSpacingWidgets.erase(it);
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        QWidget *widget = m_columnSpacingWidgets.value(it.key(), nullptr);
        if (!widget) {
            widget = m_columnSpacingPool.isEmpty()
                ? new ColumnSpacingHost(this) : m_columnSpacingPool.takeLast();
            m_columnSpacingWidgets.insert(it.key(), widget);
        }
        auto *host = static_cast<ColumnSpacingHost *>(widget);
        const bool terminal = it.key() == last;
        if (spacing > 0 && !terminal && m_columnSpacingFactory && !host->content())
            host->setContent(m_columnSpacingFactory(it.key(), host));
        if (spacing > 0 && !terminal && m_headerColumnSpacingFactory && !host->headerContent())
            host->setHeaderContent(m_headerColumnSpacingFactory(it.key(), host));
        host->setLineColors(verticalColor, horizontalColor);
        host->setLineWidths(m_verticalGridLineWidth, m_horizontalGridLineWidth);
        host->setHorizontalGridLinesVisible(m_horizontalGridLinesVisible);
        if (!terminal && host->content() && m_columnSpacingBinder)
            m_columnSpacingBinder(host->content(), it.key());
        if (!terminal && host->headerContent() && m_headerColumnSpacingBinder)
            m_headerColumnSpacingBinder(host->headerContent(), it.key());
        const QRect full = fullRects.value(it.key());
        host->setEdges(m_verticalGridLinesVisible && it.value().left() == full.left(),
                       m_verticalGridLinesVisible && it.value().right() == full.right(),
                       viewport()->geometry().y() - it.value().y());
        widget->setGeometry(it.value());
        if (m_spanProvider && model()) {
            QRegion mask(widget->rect());
            const int paneIndex = m_panes.paneIndexOfColumn(it.key());
            const QVector<int> &paneColumns = m_panes.paneAt(paneIndex).logicalColumns;
            const int visual = m_columns->visualIndex(it.key());
            const auto next = std::lower_bound(paneColumns.cbegin(), paneColumns.cend(), visual,
                [this](int logical, int targetVisual) {
                    return m_columns->visualIndex(logical) < targetVisual;
                });
            if (next != paneColumns.cend() && next + 1 != paneColumns.cend()) {
                const int rightColumn = *(next + 1);
                for (const VisibleRange &range : visibleItemRanges()) {
                    for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                        const QModelIndex left = viewIndex(row, it.key());
                        const QModelIndex right = viewIndex(row, rightColumn);
                        const QModelIndex anchor = anchorIndex(left);
                        if (!anchor.isValid() || anchor != anchorIndex(right)
                            || m_panes.paneIndexOfColumn(anchor.column()) != paneIndex)
                            continue;
                        const QRect merged = spanRect(anchor)
                            .intersected(itemPaneRect(itemPaneForRow(row)));
                        if (!merged.isEmpty())
                            mask -= QRect(0, viewportRect.y() + merged.y() - widget->y(),
                                          widget->width(), merged.height());
                    }
                }
            }
            widget->setMask(mask);
        } else {
            widget->clearMask();
        }
        QVector<QRect> localGaps;
        for (const QRect &gap : rowSpacingWidgetRectsInView()) {
            const QRect local = gap.translated(-widget->pos());
            if (local.intersects(widget->rect()))
                localGaps.append(local);
        }
        std::sort(localGaps.begin(), localGaps.end(),
                  [](const QRect &a, const QRect &b) { return a.top() < b.top(); });
        QVector<int> horizontalLines;
        if (m_horizontalGridLinesVisible && m_horizontalSpacingLineThroughColumnSpacing
            && spacing > 0) {
            for (const VisibleRange &range : visibleItemRanges()) {
                for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                    const QRect rowRect = geometryForViewRow(row);
                    if (!rowRect.intersects(viewport()->rect()))
                        continue;
                    const int spacing = m_rowLayout ? m_rowLayout->spacingAfter(row) : 0;
                    const int first = viewportRect.y() + rowRect.bottom()
                        + (spacing > 0 ? 1 : 1 - m_horizontalGridLineWidth) - widget->y();
                    const QRect paneRect = itemPaneRect(itemPaneForRow(row));
                    const int firstInViewport = first + widget->y() - viewportRect.y();
                    if (first >= host->bodyTop() && first < host->height()
                        && firstInViewport >= paneRect.top()
                        && firstInViewport + m_horizontalGridLineWidth <= paneRect.bottom() + 1)
                        horizontalLines.append(first);
                    if (spacing > 0) {
                        const int last = first + qMax(0, spacing - m_horizontalGridLineWidth);
                        const int lastInViewport = last + widget->y() - viewportRect.y();
                        if (last >= host->bodyTop() && last < host->height()
                            && lastInViewport >= paneRect.top()
                            && lastInViewport + m_horizontalGridLineWidth <= paneRect.bottom() + 1)
                            horizontalLines.append(last);
                    }
                }
            }
        }
        host->setRowGaps(localGaps, horizontalLines, m_verticalSpacingLineThroughRowSpacing);
        widget->show();
        widget->raise();
    }
    raisePaneSeparatorLines();
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
    syncRowGridLines();
    updateColumnLayout();
    raisePaneSeparatorLines();
    syncHorizontalScrollBar();
}

void VirtualTableView::applyRowSpacingOverrides()
{
    if (m_rowHeaders)
        m_rowHeaders->setSectionSpacing(rowSpacing());
}

void VirtualTableView::configureRowSpacingWidget(QWidget *widget) const
{
    if (widget) {
        widget->setProperty("vivFillSpacingBackground", !m_verticalSpacingLineThroughRowSpacing);
        widget->setProperty("vivShowSpacingLines", m_horizontalGridLinesVisible);
        widget->setProperty("vivSpacingLineWidth", m_horizontalGridLineWidth);
    }
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
        applyGridLineVisibilityToHeaders();
        relayout();
        syncRowGridLines();
        syncColumnSpacingWidgets();
        break;
    default:
        break;
    }
}

void VirtualTableView::scrollContentsBy(int dx, int dy)
{
    if (dx != 0 && !m_columnUpdateActive) {
        // Header and body consume the same offset: they cannot drift (§45.5). The
        // bar holds a compressed value, so it goes through the same mapper the
        // sync uses.
        m_columns->setViewportOffset(m_horizontalMapper.toLogicalOffset(horizontalScrollBar()->value()));
        emit horizontalOffsetChanged(m_columns->viewportOffset());
    }
    // A purely horizontal scroll does not need the kernel's vertical pass (it only
    // reads the vertical scroll bar and relayouts): dx has already updated the
    // header and the pane layout, which repositions the row widgets. Cell Widget
    // Mode is the exception - there the horizontal window, and therefore the set
    // of cells to materialize, changes with dx.
    if (dy != 0 || m_materializationMode == MaterializationMode::CellWidgets)
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
        while (column >= 0 && column < columnCount()
               && (isColumnHidden(column) || !current.siblingAtColumn(column).isValid()))
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
