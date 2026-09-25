#include <virtualitemviews/virtualitemview.h>

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/layoutpolicy.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <limits>
#include <numeric>

namespace viv {

namespace {
/// Drop indicator of the kernel: a thin bar in the palette's highlight colour.
/// It is painted by itself instead of relying on autoFillBackground(), which
/// style sheet based styles ignore.
class DropIndicatorWidget : public QWidget
{
public:
    explicit DropIndicatorWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivDropIndicator"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    /// A line fills the whole rect; a frame draws a hollow rectangle ("drop into
    /// this item", the tree's OnItem indicator).
    void setFrame(bool frame)
    {
        if (m_frame == frame)
            return;
        m_frame = frame;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QColor color = palette().color(QPalette::Highlight);
        if (!m_frame) {
            painter.fillRect(rect(), color);
            return;
        }
        painter.setPen(color);
        const int thickness = qMin(2, qMin(width(), height()));
        for (int inset = 0; inset < thickness; ++inset) {
            const QRect outline = rect().adjusted(inset, inset, -inset - 1, -inset - 1);
            if (outline.isValid())
                painter.drawRect(outline);
        }
    }

private:
    bool m_frame = false;
};

/// Framework owned clipping container of the scrolling row pane (§31, row
/// direction). It paints nothing, so a business item widget keeps its own
/// background; Qt clips the children of a widget to its rect, which is what keeps
/// a row that scrolled behind the frozen band from showing through it.
class ItemPaneClipHost : public QWidget
{
public:
    explicit ItemPaneClipHost(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivItemPaneClipHost"));
        setFocusPolicy(Qt::NoFocus);
    }
};

/// The line between two row panes (§31). The item widgets cover the viewport, so a
/// line painted by the viewport itself would be hidden behind them; this overlay
/// sits above them and lets input through.
class ItemPaneSeparatorLine : public QWidget
{
public:
    explicit ItemPaneSeparatorLine(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivItemPaneSeparatorLine"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
    }

    void setSeparator(const PaneSeparatorStyle &style, const QColor &styleSeparatorColor)
    {
        m_style = style;
        m_resolvedColor = style.effectiveColor(styleSeparatorColor);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_style.isVisible() || m_resolvedColor.alpha() == 0)
            return;
        QPainter painter(this);
        if (m_style.lineStyle == Qt::SolidLine) {
            painter.fillRect(rect(), m_resolvedColor);
            return;
        }
        QPen pen(m_resolvedColor);
        pen.setStyle(m_style.lineStyle);
        pen.setWidth(1);
        painter.setPen(pen);
        const int y = rect().top() + (qMax(0, rect().height() - 1)) / 2;
        painter.drawLine(rect().left(), y, rect().right(), y);
    }

private:
    PaneSeparatorStyle m_style;
    QColor m_resolvedColor;
};
} // namespace

namespace {
constexpr int kDefaultOverscan = 2;
constexpr int kDefaultWheelScrollItems = 3;
/// Automatic height measurement must converge: a widget whose size hint depends
/// on its own geometry could otherwise trigger an endless relayout loop.
constexpr int kMaxConsecutiveMeasurePasses = 8;

/// QMouseEvent::position() only exists in Qt 6; Qt 5 delivers QPoint.
inline QPoint mousePosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/// Same for the drag/drop events (QDropEvent carries the position the same way).
inline QPoint dropPosition(const QDropEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}
} // namespace

VirtualItemView::VirtualItemView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    // v0.1 lists fill the viewport width; horizontal virtualization arrives
    // with VirtualTableView.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(false);

    m_overscanBefore = kDefaultOverscan;
    m_overscanAfter = kDefaultOverscan;
    m_wheelScrollItems = kDefaultWheelScrollItems;

    m_recycler = new WidgetRecycler(viewport());
    m_recycler->setFactory([this](WidgetType type, QWidget *parentWidget) {
        return m_adapter ? m_adapter->createWidget(type, parentWidget) : nullptr;
    });
}

VirtualItemView::~VirtualItemView()
{
    // Materialized widgets have to be released while the adapter and the
    // recycler are both alive: business code stops its timers / async requests
    // in unbindWidget(), and a pooled widget must not outlive the factory that
    // created it. Only then may the owned collaborators go away.
    recycleAllItems();
    if (m_recycler)
        m_recycler->clear();
    disconnectModel(m_model.data());
    if (m_ownSelectionModel) {
        delete m_selectionModel.data();
        m_selectionModel = nullptr;
        m_ownSelectionModel = false;
    }
    if (m_ownAdapter) {
        delete m_adapter;
        m_adapter = nullptr;
        m_ownAdapter = false;
    }
    if (m_ownLayout) {
        delete m_layout;
        m_layout = nullptr;
        m_ownLayout = false;
    }
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void VirtualItemView::setModel(QAbstractItemModel *model)
{
    if (m_model.data() == model)
        return;

    disconnectModel(m_model.data());
    // A drag or drop in flight refers to the outgoing model (§38).
    finishDrag();
    recycleAllItems();
    m_explicitPinned.clear();
    cancelPendingAnchor();
    m_scrollOffset = 0;
    m_scrollMapper.resetAnchor();

    m_model = model;
    connectModel(model);

    // Invariant: selectionModel()->model() == model(), or there is no selection
    // model at all. A selection model of the outgoing model cannot address the
    // new one, so it is detached (an external one is not deleted).
    if (m_ownSelectionModel) {
        delete m_selectionModel.data();
        m_selectionModel = nullptr;
        m_ownSelectionModel = false;
    } else if (m_selectionModel && m_selectionModel->model() != model) {
        m_selectionModel = nullptr;
    }
    if (!m_selectionModel && model) {
        m_selectionModel = new QItemSelectionModel(model, this);
        m_ownSelectionModel = true;
    }

    resetLayoutForNewModel();
    relayout();
}

void VirtualItemView::connectModel(QAbstractItemModel *model)
{
    if (!model)
        return;

    connect(model, &QAbstractItemModel::dataChanged, this, &VirtualItemView::onDataChanged);
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted,
            this, &VirtualItemView::onRowsAboutToBeInserted);
    connect(model, &QAbstractItemModel::rowsInserted, this, &VirtualItemView::onRowsInserted);
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, &VirtualItemView::onRowsAboutToBeRemoved);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &VirtualItemView::onRowsRemoved);
    connect(model, &QAbstractItemModel::rowsAboutToBeMoved,
            this, &VirtualItemView::onRowsAboutToBeMoved);
    connect(model, &QAbstractItemModel::rowsMoved, this, &VirtualItemView::onRowsMoved);
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged,
            this, &VirtualItemView::onLayoutAboutToBeChanged);
    connect(model, &QAbstractItemModel::layoutChanged, this, &VirtualItemView::onLayoutChanged);
    connect(model, &QAbstractItemModel::modelAboutToBeReset,
            this, &VirtualItemView::onModelAboutToBeReset);
    connect(model, &QAbstractItemModel::modelReset, this, &VirtualItemView::onModelReset);
}

void VirtualItemView::disconnectModel(QAbstractItemModel *model)
{
    if (!model)
        return;
    disconnect(model, nullptr, this, nullptr);
}

void VirtualItemView::resetLayoutForNewModel()
{
    if (!m_layout)
        return;
    const qsizetype count = viewItemCount();
    m_layout->resetItems(count, estimateItemSize(count > 0 ? count - 1 : 0));
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void VirtualItemView::setSelectionModel(QItemSelectionModel *selectionModel)
{
    if (m_selectionModel.data() == selectionModel)
        return;
    if (selectionModel && m_model && selectionModel->model() != m_model.data()) {
        qWarning("VirtualItemView::setSelectionModel(): the selection model belongs to another "
                 "model; the current selection model is kept");
        return;
    }
    if (m_ownSelectionModel) {
        delete m_selectionModel.data();
        m_ownSelectionModel = false;
    }
    m_selectionModel = selectionModel;
}

QModelIndex VirtualItemView::currentIndex() const
{
    return m_selectionModel ? m_selectionModel->currentIndex() : QModelIndex();
}

void VirtualItemView::setSelectionMode(SelectionMode mode)
{
    if (m_selectionMode == mode)
        return;
    m_selectionMode = mode;
    if (!m_selectionModel)
        return;

    switch (mode) {
    case SelectionMode::NoSelection:
        m_selectionModel->clearSelection();
        break;
    case SelectionMode::SingleSelection: {
        // Keep only the current index selected.
        const QModelIndex current = m_selectionModel->currentIndex();
        m_selectionModel->clearSelection();
        if (current.isValid())
            m_selectionModel->select(current, QItemSelectionModel::ClearAndSelect);
        break;
    }
    case SelectionMode::MultiSelection:
    case SelectionMode::ExtendedSelection:
        break;
    }
}

void VirtualItemView::setSelectionBehavior(SelectionBehavior behavior)
{
    m_selectionBehavior = behavior;
}

void VirtualItemView::setCurrentIndex(const QModelIndex &index)
{
    if (!m_selectionModel)
        return;
    if (!index.isValid()) {
        m_selectionModel->clearCurrentIndex();
        return;
    }
    switch (m_selectionMode) {
    case SelectionMode::NoSelection:
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
        break;
    default:
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | rowFlags());
        pinCurrentIndex(index);
        m_selectionAnchor = QPersistentModelIndex(index);
        break;
    }
}

void VirtualItemView::activateIndex(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    setCurrentIndex(index);
    emit clicked(index);
    emit activated(index);
}

void VirtualItemView::pinCurrentIndex(const QModelIndex &index)
{
    if (!m_selectionModel || !index.isValid())
        return;
    // QItemSelectionModel::select() with the Rows/Columns flag selects the whole
    // row/column and moves the current index to its first cell; re-assert the
    // current cell so that cell navigation keeps working.
    if (rowFlags() != QItemSelectionModel::NoUpdate)
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
}

QItemSelectionModel::SelectionFlags VirtualItemView::rowFlags() const
{
    return m_selectionBehavior == SelectionBehavior::SelectRows ? QItemSelectionModel::Rows
                                                                : QItemSelectionModel::NoUpdate;
}

void VirtualItemView::appendLifecycleLog(const QString &entry)
{
    if (!m_lifecycleLogEnabled)
        return;
    m_lifecycleLog.append(entry);
    while (m_lifecycleLog.size() > kLifecycleLogCapacity)
        m_lifecycleLog.removeFirst();
}

void VirtualItemView::setLifecycleLoggingEnabled(bool enabled)
{
    m_lifecycleLogEnabled = enabled;
    if (!enabled)
        m_lifecycleLog.clear();
}

void VirtualItemView::clearLifecycleLog()
{
    m_lifecycleLog.clear();
}

// ---------------------------------------------------------------------------
// Adapter / layout
// ---------------------------------------------------------------------------

void VirtualItemView::setAdapter(WidgetAdapter *adapter, bool takeOwnership)
{
    if (m_adapter == adapter) {
        m_ownAdapter = m_ownAdapter || takeOwnership;
        return;
    }
    // Order matters: hand the materialized widgets back through the *old*
    // adapter, drop every pooled widget (a widget class of another adapter must
    // never be handed out through the new adapter's WidgetType namespace), and
    // only then let the old adapter die.
    recycleAllItems();
    if (m_recycler)
        m_recycler->clear();
    if (m_ownAdapter) {
        delete m_adapter;
        m_adapter = nullptr;
    }
    m_adapter = adapter;
    m_ownAdapter = takeOwnership;
    relayout();
}

void VirtualItemView::setLayoutPolicy(LayoutPolicy *policy, bool takeOwnership)
{
    if (m_layout == policy) {
        m_ownLayout = m_ownLayout || takeOwnership;
        return;
    }
    recycleAllItems();
    if (m_ownLayout)
        delete m_layout;
    m_layout = policy;
    m_ownLayout = policy ? takeOwnership : false;
    if (m_layout) {
        m_layout->setCrossExtent(viewport()->width());
        rebuildSizeIndex();
    }
    relayout();
}

// ---------------------------------------------------------------------------
// Item size model (shared by the List and the Table kernel)
// ---------------------------------------------------------------------------

void VirtualItemView::setItemHeightMode(ItemHeightMode mode)
{
    if (m_heightMode == mode)
        return;
    m_heightMode = mode;
    m_measurePasses = 0;
    rebuildSizeIndex();
    relayout();
}

void VirtualItemView::setUniformItemHeight(int height)
{
    const int clamped = qMax(0, height);
    if (m_uniformItemHeight == clamped && m_heightMode == ItemHeightMode::Uniform)
        return;
    m_uniformItemHeight = clamped;
    m_heightMode = ItemHeightMode::Uniform;
    m_measurePasses = 0;
    rebuildSizeIndex();
    relayout();
}

void VirtualItemView::setEstimatedItemHeight(int height)
{
    const int clamped = qMax(1, height);
    if (m_estimatedItemHeight == clamped)
        return;
    m_estimatedItemHeight = clamped;
    if (m_heightMode == ItemHeightMode::Variable) {
        rebuildSizeIndex();
        relayout();
    }
}

void VirtualItemView::setAutoMeasureItemHeight(bool enabled)
{
    if (m_autoMeasure == enabled)
        return;
    m_autoMeasure = enabled;
    m_measurePasses = 0;
    markDirty();
}

void VirtualItemView::rebuildSizeIndex()
{
    if (!m_layout)
        return;

    if (m_heightMode == ItemHeightMode::Uniform)
        m_layout->setSizeIndex(new FixedSizeIndex(0, m_uniformItemHeight), true);
    else
        m_layout->setSizeIndex(new BlockSizeIndex(0, m_estimatedItemHeight), true);

    resetLayoutForNewModel();
}

int VirtualItemView::estimateItemSize(qsizetype item) const
{
    if (m_heightMode == ItemHeightMode::Uniform)
        return m_uniformItemHeight;

    const QModelIndex index = viewIndex(item);
    if (index.isValid() && m_adapter) {
        const QSize hint = m_adapter->estimatedSize(index);
        if (hint.height() > 0)
            return hint.height();
    }
    return m_estimatedItemHeight;
}

int VirtualItemView::measuredHeightOf(const MaterializedItem &item) const
{
    if (!item.widget)
        return 0;
    const int width = item.geometry.width() > 0 ? item.geometry.width() : viewport()->width();
    if (item.widget->hasHeightForWidth())
        return item.widget->heightForWidth(width);
    return item.widget->sizeHint().height();
}

void VirtualItemView::afterMaterialize()
{
    if (!m_autoMeasure || !m_layout || materializedItems().isEmpty())
        return;

    if (m_heightMode == ItemHeightMode::Uniform) {
        if (m_uniformItemHeight > 0)
            return;
        // "Fit to first item" mode: measure once and switch to that height.
        const int measured = measuredHeightOf(materializedItems().first());
        if (measured <= 0)
            return;
        // Captured before the size changes: afterwards the same pixel offset can
        // belong to a different item.
        const ScrollAnchor anchor = captureAnchor();
        m_uniformItemHeight = measured;
        m_layout->setItemSize(0, measured);
        setPendingAnchor(anchor);
        markDirty();
        return;
    }

    // The anchor has to describe what the user is looking at *now*: measuring
    // changes the offsets, so taking it after the loop would anchor whatever item
    // ends up at the old pixel offset (P1-2).
    const ScrollAnchor anchor = captureAnchor();

    bool changed = false;
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0 || !canMeasureItem(row))
            continue;
        const int measured = measuredHeightOf(item);
        if (measured <= 0 || measured == m_layout->itemSize(row))
            continue;
        m_layout->setItemSize(row, measured);
        changed = true;
    }

    if (!changed) {
        m_measurePasses = 0;
        return;
    }
    if (++m_measurePasses > kMaxConsecutiveMeasurePasses) {
        // Give up on this oscillation and keep the last measured heights.
        m_measurePasses = 0;
        return;
    }
    // Heights above the viewport changed: keep the top item in place.
    setPendingAnchor(anchor);
    markDirty();
}

int VirtualItemView::itemDepth(const QModelIndex &index) const
{
    int depth = 0;
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        ++depth;
    return depth;
}

bool VirtualItemView::handleItemKeyPress(QKeyEvent *event)
{
    Q_UNUSED(event);
    return false;
}

bool VirtualItemView::canMeasureItem(qsizetype item) const
{
    Q_UNUSED(item);
    return true;
}

bool VirtualItemView::usesItemWidgets() const
{
    return true;
}

void VirtualItemView::materializeItems(const VisibleRange &rows)
{
    Q_UNUSED(rows);
}

void VirtualItemView::materializeItemRanges(const QVector<VisibleRange> &ranges)
{
    materializeItems(ranges.isEmpty() ? VisibleRange() : ranges.first());
}

void VirtualItemView::rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    rebindItemsInModelRange(topLeft.parent(), topLeft.row(), bottomRight.row());
}

QModelIndex VirtualItemView::indexForNavigation(qsizetype item, const QModelIndex &current) const
{
    Q_UNUSED(current);
    return viewIndex(item);
}

// ---------------------------------------------------------------------------
// Scrolling helpers
// ---------------------------------------------------------------------------

void VirtualItemView::setOverscan(int before, int after)
{
    const int clampedBefore = qMax(0, before);
    const int clampedAfter = qMax(0, after);
    if (m_overscanBefore == clampedBefore && m_overscanAfter == clampedAfter)
        return;
    m_overscanBefore = clampedBefore;
    m_overscanAfter = clampedAfter;
    markDirty();
}

void VirtualItemView::setWheelScrollMode(WheelScrollMode mode)
{
    m_wheelScrollMode = mode;
    syncScrollBars();
}

void VirtualItemView::setWheelScrollPixels(int pixels)
{
    m_wheelScrollPixels = qMax(1, pixels);
    m_wheelScrollMode = WheelScrollMode::Pixels;
    syncScrollBars();
}

void VirtualItemView::setWheelScrollItems(int items)
{
    m_wheelScrollItems = qMax(1, items);
    m_wheelScrollMode = WheelScrollMode::Items;
    syncScrollBars();
}

int VirtualItemView::viewportMainExtent() const
{
    if (m_layout && m_layout->orientation() == Qt::Horizontal)
        return viewport()->width();
    return viewport()->height();
}

qint64 VirtualItemView::contentExtent() const
{
    return m_layout ? m_layout->contentExtent() : 0;
}

qint64 VirtualItemView::maximumVerticalOffset() const
{
    return qMax<qint64>(0, contentExtent() - qint64(viewportMainExtent()));
}

VisibleRange VirtualItemView::coreVisibleRange() const
{
    VisibleRange range;
    if (!m_layout)
        return range;

    const qsizetype count = m_layout->itemCount();
    const qint64 viewExtent = viewportMainExtent();
    if (count <= 0 || viewExtent <= 0 || m_layout->contentExtent() <= 0)
        return range;

    // The scrolling pane starts below the frozen band, so the visible content
    // window is shifted by its height (§31 row direction). Frozen rows are always
    // visible and are reported by visibleItemRanges(), not by this window.
    const qint64 top = m_scrollOffset + frozenTopExtent();
    const qint64 bottom = m_scrollOffset + viewExtent - frozenBottomExtent() - 1;
    if (bottom < top)
        return range; // the frozen bands cover the whole viewport

    qsizetype first = qMin(m_layout->indexAtOffset(top), count - 1);
    qsizetype last = qMin(m_layout->indexAtOffset(bottom), count - 1);
    first = qMax<qsizetype>(0, first);
    last = qMax<qsizetype>(first, last);
    range.first = first;
    range.last = last;
    return range;
}

VisibleRange VirtualItemView::visibleItemRange() const
{
    return coreVisibleRange();
}

QRect VirtualItemView::geometryForViewRow(qsizetype row) const
{
    if (!m_layout)
        return QRect();
    return m_layout->itemRect(row, itemPaneScrollOffset(itemPaneForRow(row)));
}

// ---------------------------------------------------------------------------
// Row panes (§31 row direction, docs/row-freezing.md)
// ---------------------------------------------------------------------------

int VirtualItemView::frozenRows() const
{
    if (!m_layout || m_frozenRows <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    const int viewHeight = viewport()->height();
    qsizetype rows = qBound<qsizetype>(qsizetype(0), m_frozenRows, count);
    if (viewHeight > 0)
        rows = qMin(rows, rowsFittingFromTop(viewHeight));
    return int(rows);
}

int VirtualItemView::frozenBottomRows() const
{
    if (!m_layout || m_frozenBottomRows <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    qsizetype rows = qBound<qsizetype>(qsizetype(0), m_frozenBottomRows,
                                       count - qsizetype(frozenRows()));
    const qint64 remaining = qint64(viewport()->height()) - frozenTopExtent();
    if (remaining <= 0)
        return 0;
    rows = qMin(rows, rowsFittingFromBottom(remaining));
    return int(rows);
}

void VirtualItemView::setFrozenRows(int count)
{
    const int clamped = qMax(0, count);
    if (m_frozenRows == clamped)
        return;
    m_frozenRows = clamped;
    markDirty();
}

void VirtualItemView::setFrozenBottomRows(int count)
{
    const int clamped = qMax(0, count);
    if (m_frozenBottomRows == clamped)
        return;
    m_frozenBottomRows = clamped;
    markDirty();
}

bool VirtualItemView::isRowFrozen(qsizetype row) const
{
    if (row < 0 || !m_layout)
        return false;
    const qsizetype count = m_layout->itemCount();
    const qsizetype top = frozenRows();
    if (row < top)
        return true;
    const qsizetype bottom = frozenBottomRows();
    return bottom > 0 && row >= count - bottom;
}

qint64 VirtualItemView::frozenTopExtent() const
{
    const qsizetype rows = frozenRows();
    if (rows <= 0 || !m_layout)
        return 0;
    return qMin<qint64>(m_layout->offsetOf(rows), m_layout->contentExtent());
}

qint64 VirtualItemView::frozenBottomExtent() const
{
    const qsizetype rows = frozenBottomRows();
    if (rows <= 0 || !m_layout)
        return 0;
    return qMax<qint64>(0, m_layout->contentExtent() - m_layout->offsetOf(m_layout->itemCount() - rows));
}

qsizetype VirtualItemView::rowsFittingFromTop(qint64 extent) const
{
    if (!m_layout || extent <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    const qint64 content = m_layout->contentExtent();
    if (extent >= content)
        return count;
    // The row containing the boundary may stick out of the pane, so only the rows
    // that end before it belong to the pane.
    return qBound<qsizetype>(qsizetype(0), m_layout->indexAtOffset(extent), count);
}

qsizetype VirtualItemView::rowsFittingFromBottom(qint64 extent) const
{
    if (!m_layout || extent <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    const qint64 content = m_layout->contentExtent();
    if (extent >= content)
        return count;
    const qsizetype row = m_layout->indexAtOffset(content - extent);
    if (row < 0)
        return 0;
    // Same rule from the other edge: the row the boundary falls into is not part of
    // the pane.
    return qBound<qsizetype>(qsizetype(0), count - row - 1, count);
}

ItemPane::Type VirtualItemView::itemPaneForRow(qsizetype row) const
{
    if (isRowFrozen(row)) {
        const qsizetype top = frozenRows();
        if (row < top)
            return ItemPane::Type::FrozenTop;
        return ItemPane::Type::FrozenBottom;
    }
    return ItemPane::Type::Scrollable;
}

qint64 VirtualItemView::itemPaneScrollOffset(ItemPane::Type type) const
{
    switch (type) {
    case ItemPane::Type::FrozenTop:
        return 0;
    case ItemPane::Type::Scrollable:
        return m_scrollOffset;
    case ItemPane::Type::FrozenBottom:
        // "Scrolled to the very bottom" pins the last rows to the bottom edge; the
        // value is signed on purpose, so it also works when the content is shorter
        // than the viewport.
        return contentExtent() - qint64(viewportMainExtent());
    }
    return m_scrollOffset;
}

QVector<ItemPane> VirtualItemView::itemPanes() const
{
    QVector<ItemPane> panes;
    if (!m_layout)
        return panes;
    const qsizetype count = m_layout->itemCount();
    const int viewHeight = viewport()->height();
    const int viewWidth = viewport()->width();
    if (count <= 0 || viewHeight <= 0)
        return panes;

    const qsizetype topRows = frozenRows();
    const qsizetype bottomRows = frozenBottomRows();
    const qint64 topExtent = qMin<qint64>(frozenTopExtent(), viewHeight);
    const qint64 bottomExtent = qMin<qint64>(frozenBottomExtent(), viewHeight - topExtent);

    if (topRows > 0 && topExtent > 0) {
        ItemPane top;
        top.type = ItemPane::Type::FrozenTop;
        top.viewportRect = QRect(0, 0, viewWidth, int(topExtent));
        top.firstRow = 0;
        top.lastRow = topRows - 1;
        panes.append(top);
    }

    ItemPane scrolling;
    scrolling.type = ItemPane::Type::Scrollable;
    scrolling.viewportRect = QRect(0, int(topExtent), viewWidth,
                                   int(viewHeight - topExtent - bottomExtent));
    scrolling.firstRow = topRows;
    scrolling.lastRow = count - bottomRows - 1;
    panes.append(scrolling);

    if (bottomRows > 0 && bottomExtent > 0) {
        ItemPane bottom;
        bottom.type = ItemPane::Type::FrozenBottom;
        bottom.viewportRect = QRect(0, int(viewHeight - bottomExtent), viewWidth, int(bottomExtent));
        bottom.firstRow = count - bottomRows;
        bottom.lastRow = count - 1;
        panes.append(bottom);
    }
    return panes;
}

QRect VirtualItemView::itemPaneRect(ItemPane::Type type) const
{
    for (const ItemPane &pane : itemPanes()) {
        if (pane.type == type)
            return pane.viewportRect;
    }
    return QRect();
}

ItemPane::Type VirtualItemView::itemPaneAtY(int y) const
{
    const QVector<ItemPane> panes = itemPanes();
    for (const ItemPane &pane : panes) {
        if (y >= pane.viewportRect.y()
            && y < pane.viewportRect.y() + pane.viewportRect.height()) {
            return pane.type;
        }
    }
    // Outside every pane (a degenerate viewport): fall back to the pane that owns
    // the edge the point is closest to.
    if (!panes.isEmpty() && y < panes.first().viewportRect.y())
        return panes.first().type;
    return panes.isEmpty() ? ItemPane::Type::Scrollable : panes.last().type;
}

QVector<VisibleRange> VirtualItemView::visibleItemRanges() const
{
    QVector<VisibleRange> ranges;
    if (frozenRows() > 0)
        ranges.append(VisibleRange{0, qsizetype(frozenRows()) - 1});
    const VisibleRange window = visibleItemRange();
    if (window.isValid())
        ranges.append(window);
    if (frozenBottomRows() > 0) {
        const qsizetype count = m_layout ? m_layout->itemCount() : 0;
        ranges.append(VisibleRange{count - qsizetype(frozenBottomRows()), count - 1});
    }
    return ranges;
}

void VirtualItemView::setItemPaneSeparatorStyle(const PaneSeparatorStyle &style)
{
    if (m_itemPaneSeparatorStyle == style)
        return;
    m_itemPaneSeparatorStyle = style;
    syncItemPanes();
}

QColor VirtualItemView::itemPaneSeparatorColor() const
{
    // No style specific separator to probe in the kernel: the palette's mid colour is the
    // honest default for a plain list. A table overrides this with the colour its style
    // paints section separators with, which is what its column boundary uses as well.
    return palette().color(QPalette::Mid);
}

QVector<QRect> VirtualItemView::itemPaneSeparatorRects() const
{
    QVector<QRect> rects;
    rects.reserve(m_itemPaneSeparatorLines.size());
    for (QWidget *line : m_itemPaneSeparatorLines) {
        if (line->isVisible())
            rects.append(line->geometry());
    }
    return rects;
}

void VirtualItemView::syncItemPanes()
{
    // The clip container only exists while something is frozen: an unused feature
    // changes nothing at all (the item widgets keep the viewport as parent).
    const QVector<ItemPane> panes = itemPanes();
    const QRect scrollRect = itemPaneRect(ItemPane::Type::Scrollable);
    if (panes.size() <= 1) {
        if (m_scrollPaneHost) {
            const QList<QWidget *> children =
                m_scrollPaneHost->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget *child : children)
                child->setParent(viewport());
            m_scrollPaneHost->hide();
            m_scrollPaneHost->setParent(nullptr);
            m_scrollPaneHost->deleteLater();
            m_scrollPaneHost = nullptr;
        }
    } else {
        if (!m_scrollPaneHost)
            m_scrollPaneHost = new ItemPaneClipHost(viewport());
        if (m_scrollPaneHost->geometry() != scrollRect)
            m_scrollPaneHost->setGeometry(scrollRect);
        m_scrollPaneHost->setVisible(!scrollRect.isEmpty());
    }

    // One line per pane boundary (top|scrolling and scrolling|bottom). The band
    // lies inside the pane above the boundary, so the lines of neighbouring panes
    // stay continuous.
    QVector<int> boundaries;
    for (int index = 0; index + 1 < panes.size(); ++index) {
        const ItemPane &before = panes.at(index);
        const ItemPane &after = panes.at(index + 1);
        if (before.viewportRect.height() <= 0 || after.viewportRect.height() <= 0)
            continue;
        boundaries.append(before.viewportRect.bottom() + 1);
    }
    while (m_itemPaneSeparatorLines.size() > boundaries.size()) {
        QWidget *line = m_itemPaneSeparatorLines.takeLast();
        line->hide();
        line->setParent(nullptr);
        line->deleteLater();
    }
    while (m_itemPaneSeparatorLines.size() < boundaries.size())
        // A child of the view, not of the viewport: the line also crosses whatever the
        // subclass keeps outside the viewport on the left (the row-number strip), exactly
        // like the column boundary lines cross the header strip.
        m_itemPaneSeparatorLines.append(new ItemPaneSeparatorLine(this));

    const QColor styleColor = itemPaneSeparatorColor();
    const QRect viewportRect = viewport()->geometry();
    const int leftExtension = qMax(0, itemPaneSeparatorLeftExtension());
    const int band = qMax(0, m_itemPaneSeparatorStyle.width);
    const int lineWidth = m_itemPaneSeparatorStyle.lineStyle == Qt::SolidLine ? band
                                                                             : qMax(1, band);
    for (int i = 0; i < boundaries.size(); ++i) {
        auto *line = static_cast<ItemPaneSeparatorLine *>(m_itemPaneSeparatorLines.at(i));
        line->setSeparator(m_itemPaneSeparatorStyle, styleColor);
        line->setGeometry(viewportRect.x() - leftExtension, viewportRect.y() + boundaries.at(i) - lineWidth,
                          viewportRect.width() + leftExtension, lineWidth);
        line->setVisible(m_itemPaneSeparatorStyle.isVisible() && lineWidth > 0);
    }
    raiseItemPaneSeparatorLines();
}

void VirtualItemView::raiseItemPaneSeparatorLines() const
{
    // The items are (re)created by every materialization pass, so the boundary lines
    // have to be lifted above them again. They are 1 px overlays that let input
    // through, so they can sit on top of everything.
    for (QWidget *line : m_itemPaneSeparatorLines)
        line->raise();
}

void VirtualItemView::applyItemPaneGeometry(MaterializedItem &item, qsizetype row)
{
    QWidget *widget = item.widget;
    if (!widget)
        return;

    QWidget *parent = viewport();
    QPoint origin;
    bool frozen = false;
    if (m_scrollPaneHost) {
        frozen = row >= 0 && isRowFrozen(row);
        if (!frozen) {
            // Scrollable rows live in the clip container: Qt clips a widget to its
            // parent, so a row that scrolled behind the frozen band can never be
            // seen through it.
            parent = m_scrollPaneHost;
            origin = itemPaneRect(ItemPane::Type::Scrollable).topLeft();
        }
    }
    if (widget->parentWidget() != parent)
        widget->setParent(parent);
    widget->setGeometry(item.geometry.translated(-origin));
    if (!widget->isVisible())
        widget->show();
    if (frozen)
        widget->raise();
}

qsizetype VirtualItemView::visibleItemCount() const
{
    const VisibleRange range = coreVisibleRange();
    if (range.isValid())
        return range.count();
    return (m_layout && m_layout->itemCount() > 0) ? 1 : 0;
}

qint64 VirtualItemView::wheelStepPixels() const
{
    if (m_wheelScrollMode == WheelScrollMode::Pixels)
        return qint64(qMax(1, m_wheelScrollPixels));

    int reference = 0;
    if (m_layout && m_layout->itemCount() > 0) {
        const qsizetype row = qMin(m_layout->indexAtOffset(m_scrollOffset), m_layout->itemCount() - 1);
        reference = m_layout->itemSize(row);
    }
    if (reference <= 0)
        reference = 32;
    return qint64(reference) * qint64(qMax(1, m_wheelScrollItems));
}

qint64 VirtualItemView::scrollBarSingleStepPixels() const
{
    if (m_wheelScrollMode == WheelScrollMode::Pixels)
        return qMax<qint64>(1, qint64(m_wheelScrollPixels) / 3);

    int reference = 0;
    if (m_layout && m_layout->itemCount() > 0) {
        const qsizetype row = qMin(m_layout->indexAtOffset(m_scrollOffset), m_layout->itemCount() - 1);
        reference = m_layout->itemSize(row);
    }
    return qMax<qint64>(1, reference > 0 ? reference : 32);
}

void VirtualItemView::setVerticalOffset(qint64 offset)
{
    const qint64 clamped = qBound<qint64>(qint64(0), offset, maximumVerticalOffset());
    if (clamped == m_scrollOffset)
        return;
    m_scrollOffset = clamped;
    const int value = m_scrollMapper.toScrollBarValue(m_scrollOffset);
    m_scrollMapper.setAnchor(m_scrollOffset, value);
    relayout();
}

void VirtualItemView::scrollByPixels(qint64 pixels)
{
    if (pixels == 0)
        return;
    setVerticalOffset(m_scrollOffset + pixels);
}

void VirtualItemView::scrollTo(const QModelIndex &index, ScrollHint hint)
{
    if (!m_layout)
        return;
    const qsizetype row = viewItemForIndex(index);
    if (row < 0)
        return;

    const qint64 viewExtent = viewportMainExtent();
    const qint64 start = m_layout->offsetOf(row);
    const qint64 size = m_layout->itemSize(row);
    qint64 offset = m_scrollOffset;

    // A frozen row is pinned: nothing to scroll for it (§31 row direction). The
    // scrolling rows are visible inside the scrolling pane, which starts below the
    // frozen band, so its height - not the viewport height - is what counts.
    if (isRowFrozen(row))
        return;
    const qint64 paneTop = frozenTopExtent();
    const qint64 paneExtent = qMax<qint64>(0, viewExtent - paneTop - frozenBottomExtent());

    switch (hint) {
    case EnsureVisible:
        if (start < offset + paneTop)
            offset = start - paneTop;
        else if (start + size > offset + paneTop + paneExtent)
            offset = start + size - paneTop - paneExtent;
        break;
    case PositionAtTop:
        offset = start - paneTop;
        break;
    case PositionAtBottom:
        offset = start + size - paneTop - paneExtent;
        break;
    case PositionAtCenter:
        offset = start + size / 2 - paneTop - paneExtent / 2;
        break;
    }

    m_scrollOffset = qBound<qint64>(qint64(0), offset, maximumVerticalOffset());
    relayout();
}

void VirtualItemView::setItemPinned(const QModelIndex &index, bool pinned)
{
    if (!index.isValid())
        return;
    const QPersistentModelIndex persistent(index);
    if (pinned) {
        m_explicitPinned.insert(persistent);
        if (m_lifecycleLogEnabled)
            appendLifecycleLog(QStringLiteral("pin row=%1").arg(index.row()));
        checkPinLimit();
        // A pinned item has to be materialized even when it is outside the
        // overscan window, so schedule a pass.
        markDirty();
    } else {
        m_explicitPinned.remove(persistent);
        if (m_lifecycleLogEnabled)
            appendLifecycleLog(QStringLiteral("unpin row=%1").arg(index.row()));
        markDirty();
    }
}

bool VirtualItemView::isItemPinned(const QModelIndex &index) const
{
    return index.isValid() && m_explicitPinned.contains(QPersistentModelIndex(index));
}

void VirtualItemView::pinWidget(QWidget *widget)
{
    if (!widget)
        return;
    const QModelIndex index = indexForWidget(widget);
    if (!index.isValid()) {
        qWarning("VirtualItemViews: pinWidget() called for a widget that is not a materialized item");
        return;
    }
    setItemPinned(index, true);
}

void VirtualItemView::unpinWidget(QWidget *widget)
{
    if (!widget)
        return;
    const QModelIndex index = indexForWidget(widget);
    if (!index.isValid()) {
        qWarning("VirtualItemViews: unpinWidget() called for a widget that is not a materialized item");
        return;
    }
    setItemPinned(index, false);
}

void VirtualItemView::setMaxPinnedItems(int max)
{
    m_maxPinnedItems = max;
    m_pinLimitWarned = false;
    checkPinLimit();
}

void VirtualItemView::checkPinLimit()
{
    if (m_maxPinnedItems <= 0)
        return;
    const qsizetype pinned = m_explicitPinned.size();
    if (pinned <= m_maxPinnedItems) {
        m_pinLimitWarned = false;
        return;
    }
    if (m_pinLimitWarned)
        return;
    m_pinLimitWarned = true;
    qWarning("VirtualItemViews: %lld items are pinned, above the configured limit of %d. "
             "Pinned items keep their QWidget alive and are not recycled.",
             qint64(pinned), m_maxPinnedItems);
}

VirtualViewStats VirtualItemView::stats() const
{
    VirtualViewStats result;
    result.logicalItems = viewItemCount();
    result.materializedItems = m_items.size();
    result.pooledWidgets = m_recycler ? m_recycler->pooledCount() : 0;
    result.pinnedWidgets = pinnedItemCount();
    result.createCount = quint64(m_recycler ? qMax<qsizetype>(0, m_recycler->createdCount()) : 0);
    result.bindCount = m_bindCount;
    result.recycleCount = m_recycleCount;
    return result;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

QModelIndex VirtualItemView::indexAt(const QPoint &viewportPos) const
{
    if (!m_layout)
        return QModelIndex();
    // Fold the point into the pane it belongs to (§31 row direction): a frozen pane
    // does not scroll, the bottom one is pinned to the bottom edge.
    const ItemPane::Type pane = itemPaneAtY(viewportPos.y());
    const qsizetype row = m_layout->itemAtPoint(viewportPos, itemPaneScrollOffset(pane));
    if (row < 0 || row >= viewItemCount())
        return QModelIndex();
    return viewIndex(row);
}

QRect VirtualItemView::visualRect(const QModelIndex &index) const
{
    if (!m_layout)
        return QRect();
    const qsizetype row = viewItemForIndex(index);
    if (row < 0)
        return QRect();
    return m_layout->itemRect(row, itemPaneScrollOffset(itemPaneForRow(row)));
}

QWidget *VirtualItemView::widgetForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    const auto it = m_itemLookup.constFind(QPersistentModelIndex(index));
    if (it == m_itemLookup.constEnd())
        return nullptr;
    return m_items.at(it.value()).widget;
}

QModelIndex VirtualItemView::indexForWidget(const QWidget *widget) const
{
    for (const MaterializedItem &item : m_items) {
        if (item.widget == widget)
            return QModelIndex(item.index);
    }
    return QModelIndex();
}

qsizetype VirtualItemView::pinnedItemCount() const
{
    qsizetype count = 0;
    for (const MaterializedItem &item : m_items) {
        if (item.pinned)
            ++count;
    }
    return count;
}

qsizetype VirtualItemView::pooledWidgetCount() const
{
    return m_recycler ? m_recycler->pooledCount() : 0;
}

qsizetype VirtualItemView::createdWidgetCount() const
{
    return m_recycler ? m_recycler->createdCount() : 0;
}

qsizetype VirtualItemView::destroyedWidgetCount() const
{
    return m_recycler ? m_recycler->destroyedCount() : 0;
}

void VirtualItemView::rebuildLookup()
{
    m_itemLookup.clear();
    m_itemLookup.reserve(m_items.size());
    for (qsizetype i = 0; i < m_items.size(); ++i)
        m_itemLookup.insert(m_items.at(i).index, i);
}

// ---------------------------------------------------------------------------
// Materialization
// ---------------------------------------------------------------------------

void VirtualItemView::markDirty()
{
    scheduleRelayout();
}

void VirtualItemView::scheduleRelayout()
{
    m_relayoutScheduled = true;
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_relayoutScheduled)
            return;
        m_relayoutScheduled = false;
        relayout();
    }, Qt::QueuedConnection);
}

void VirtualItemView::flushPendingRelayout()
{
    if (!m_relayoutScheduled)
        return;
    m_relayoutScheduled = false;
    relayout();
}

void VirtualItemView::relayout()
{
    if (m_inRelayout)
        return;

    // A view that materializes its own widgets (table cell mode) does not need
    // a WidgetAdapter: the kernel only computes ranges for it.
    if (!m_layout || (!m_adapter && usesItemWidgets())) {
        recycleAllItems();
        syncItemPanes();
        syncScrollBars();
        return;
    }

    // Safety net: a model may emit coarser signals than expected (proxy
    // models, custom models). The layout must always match the model.
    const qsizetype expectedCount = viewItemCount();
    if (m_layout->itemCount() != expectedCount)
        m_layout->resetItems(expectedCount, estimateItemSize(expectedCount > 0 ? expectedCount - 1 : 0));

    if (m_anchorPending)
        applyPendingAnchor();

    m_inRelayout = true;

    const qint64 viewExtent = viewportMainExtent();
    m_scrollOffset = qBound<qint64>(qint64(0), m_scrollOffset, maximumVerticalOffset());

    const qsizetype count = m_layout->itemCount();
    qsizetype firstRow = -1;
    qsizetype lastRow = -1;
    if (count > 0 && viewExtent > 0) {
        if (m_layout->contentExtent() <= 0) {
            // Nothing measurable yet (for example the first pass of a
            // "fit to first item" layout): materialize just enough rows to
            // measure, never the whole model.
            firstRow = 0;
            lastRow = qMin<qsizetype>(count - 1, qMax<qsizetype>(2, m_overscanAfter));
        } else {
            const VisibleRange visible = coreVisibleRange();
            const VisibleRange window = VisibleRange::expanded(visible.first, visible.last,
                                                              m_overscanBefore, m_overscanAfter,
                                                              count);
            // The overscan must not reach into the frozen panes: those rows have their
            // own range and would otherwise be materialized twice.
            const qsizetype top = qsizetype(frozenRows());
            const qsizetype bottom = qsizetype(frozenBottomRows());
            firstRow = qBound<qsizetype>(top, window.first, count - bottom - 1);
            lastRow = qBound<qsizetype>(firstRow, window.last, count - bottom - 1);
        }
    }

    // Materialization ranges: the scrolling window plus the frozen rows, which are
    // always on screen (§31 row direction) - exactly like frozen columns. The
    // ranges are disjoint: the window starts below the frozen top rows and ends
    // above the frozen bottom ones.
    QVector<VisibleRange> ranges;
    if (frozenRows() > 0)
        ranges.append(VisibleRange{0, qsizetype(frozenRows()) - 1});
    if (firstRow >= 0 && lastRow >= firstRow)
        ranges.append(VisibleRange{firstRow, lastRow});
    if (frozenBottomRows() > 0)
        ranges.append(VisibleRange{count - qsizetype(frozenBottomRows()), count - 1});

    // ---- decide what to reuse, then recycle what is obsolete --------------
    if (!usesItemWidgets()) {
        // The view materializes its own widgets (table cell mode): it only needs
        // the ranges, not row widgets.
        materializeItemRanges(ranges);
        m_inRelayout = false;
        syncScrollBars();
        afterMaterialize();
        emit virtualizationUpdated();
        return;
    }

    // ---- decide what to reuse, then recycle what is obsolete --------------
    QHash<QPersistentModelIndex, qsizetype> existing;
    existing.reserve(m_items.size());
    for (qsizetype i = 0; i < m_items.size(); ++i)
        existing.insert(m_items.at(i).index, i);

    QVector<bool> claimed(int(m_items.size()), false);
    QList<qsizetype> desiredRows;
    QList<qsizetype> reuseIndex;
    for (const VisibleRange &range : ranges) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            const QModelIndex index = viewIndex(row);
            if (!index.isValid())
                continue;
            const QPersistentModelIndex persistent(index);
            const auto it = existing.constFind(persistent);
            if (it != existing.constEnd() && !claimed.at(it.value())) {
                claimed[it.value()] = true;
                desiredRows.append(row);
                reuseIndex.append(it.value());
                continue;
            }
            desiredRows.append(row);
            reuseIndex.append(-1);
        }
    }

    // Recycling before creating lets the pool serve the incoming rows, so
    // steady state scrolling performs no allocation at all.
    QList<MaterializedItem> keptPinned;
    keptPinned.reserve(m_items.size());
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (claimed.at(i))
            continue;
        MaterializedItem item = m_items.at(i);
        item.pinned = isPinnedItem(item);
        if (item.pinned) {
            const qsizetype row = viewItemForIndex(item.index);
            item.geometry = row >= 0 && row < m_layout->itemCount()
                ? geometryForViewRow(row)
                : item.geometry;
            keptPinned.append(item);
            continue;
        }
        recycleItem(item);
        // Drop the dangling pointer from the source list as well.
        m_items[i].widget = nullptr;
    }

    // ---- materialize ------------------------------------------------------
    QList<MaterializedItem> next;
    QList<qsizetype> nextRows;
    next.reserve(desiredRows.size() + keptPinned.size());
    nextRows.reserve(desiredRows.size() + keptPinned.size());

    for (qsizetype k = 0; k < desiredRows.size(); ++k) {
        const qsizetype row = desiredRows.at(k);
        MaterializedItem item;
        if (reuseIndex.at(k) >= 0) {
            item = m_items.at(reuseIndex.at(k));
        } else {
            const QModelIndex index = viewIndex(row);
            if (!index.isValid())
                continue;
            item = createItem(QPersistentModelIndex(index));
            if (!item.widget)
                continue;
        }
        item.geometry = geometryForViewRow(row);
        item.pinned = isPinnedItem(item);
        next.append(item);
        nextRows.append(row);
    }

    for (MaterializedItem &item : keptPinned) {
        const qsizetype row = viewItemForIndex(item.index);
        next.append(item);
        nextRows.append(row >= 0 ? row : std::numeric_limits<qsizetype>::max());
    }

    // Deterministic order: top-to-bottom.
    QVector<qsizetype> order(int(next.size()));
    std::iota(order.begin(), order.end(), qsizetype(0));
    std::stable_sort(order.begin(), order.end(), [&nextRows](qsizetype lhs, qsizetype rhs) {
        return nextRows.at(lhs) < nextRows.at(rhs);
    });

    m_items.clear();
    m_items.reserve(next.size());
    for (qsizetype i : order)
        m_items.append(next.at(i));
    rebuildLookup();

    // ---- apply geometry ---------------------------------------------------
    // The row panes decide the parent (the scrolling rows are clipped into their
    // pane) and, for the frozen rows, lift them above the scrolling pane.
    syncItemPanes();
    for (MaterializedItem &item : m_items)
        applyItemPaneGeometry(item, viewItemForIndex(item.index));
    raiseItemPaneSeparatorLines();

    m_inRelayout = false;

    syncScrollBars();
    afterMaterialize();
    emit virtualizationUpdated();
}

MaterializedItem VirtualItemView::createItem(const QPersistentModelIndex &index)
{
    MaterializedItem item;
    item.index = index;
    item.type = m_adapter->widgetType(index);

    const qsizetype reuseBefore = m_recycler->reuseCount();
    QWidget *widget = m_recycler->acquire(item.type);
    if (!widget)
        return item;

    if (m_lifecycleLogEnabled) {
        appendLifecycleLog(QStringLiteral("%1 type=%2 row=%3")
                               .arg(m_recycler->reuseCount() == reuseBefore
                                        ? QStringLiteral("create")
                                        : QStringLiteral("reuse"))
                               .arg(item.type)
                               .arg(index.row()));
    }

    item.widget = widget;
    if (widget->parentWidget() != viewport())
        widget->setParent(viewport());
    // Never show a widget before it has been bound to its new index.
    widget->hide();
    m_adapter->bindWidget(widget, QModelIndex(index));
    ++m_bindCount;
    if (m_lifecycleLogEnabled)
        appendLifecycleLog(QStringLiteral("bind row=%1").arg(index.row()));
    return item;
}

void VirtualItemView::recycleItem(MaterializedItem &item)
{
    if (!item.widget)
        return;
    m_adapter->unbindWidget(item.widget, QModelIndex(item.index));
    if (m_lifecycleLogEnabled)
        appendLifecycleLog(QStringLiteral("unbind row=%1").arg(item.index.row()));
    item.widget->hide();
    m_recycler->recycle(item.type, item.widget);
    ++m_recycleCount;
    if (m_lifecycleLogEnabled)
        appendLifecycleLog(QStringLiteral("recycle type=%1").arg(item.type));
    item.widget = nullptr;
}

void VirtualItemView::recycleAllItems()
{
    if (m_adapter) {
        for (MaterializedItem &item : m_items)
            recycleItem(item);
    }
    m_items.clear();
    m_itemLookup.clear();
}

bool VirtualItemView::hasFocusWithin(const QWidget *widget) const
{
    if (!widget)
        return false;
    const QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == widget || widget->isAncestorOf(focus)))
        return true;
    // A popup (QComboBox, context menu, ...) is a top-level window whose owner
    // chain still contains the item widget.
    const QWidget *popup = QApplication::activePopupWidget();
    if (popup && (popup == widget || widget->isAncestorOf(popup)))
        return true;
    return false;
}

bool VirtualItemView::isPinnedItem(const MaterializedItem &item) const
{
    if (!item.widget)
        return false;
    if (m_explicitPinned.contains(item.index))
        return true;
    // An open editor, an active IME composition or a popup must not be
    // recycled: in all three cases the widget (or one of its children) owns the
    // focus.
    return hasFocusWithin(item.widget);
}

void VirtualItemView::rebindItemsInModelRange(const QModelIndex &parent, int first, int last)
{
    if (!m_adapter)
        return;
    for (MaterializedItem &item : m_items) {
        const QModelIndex index = item.index;
        if (!index.isValid() || index.parent() != parent)
            continue;
        const int row = index.row();
        if (row < first || row > last)
            continue;
        m_adapter->bindWidget(item.widget, index);
        ++m_bindCount;
        if (m_lifecycleLogEnabled)
            appendLifecycleLog(QStringLiteral("rebind row=%1").arg(row));
    }
}

void VirtualItemView::recycleItemsInModelRange(const QModelIndex &parent, int first, int last)
{
    if (m_items.isEmpty())
        return;

    QList<MaterializedItem> kept;
    kept.reserve(m_items.size());
    for (MaterializedItem &item : m_items) {
        const QModelIndex index = item.index;
        const bool removed = index.isValid() && index.parent() == parent
            && index.row() >= first && index.row() <= last;
        if (!removed) {
            kept.append(item);
            continue;
        }
        m_explicitPinned.remove(item.index);
        recycleItem(item);
    }
    m_items = kept;
    rebuildLookup();
}

// ---------------------------------------------------------------------------
// Scroll anchor
// ---------------------------------------------------------------------------

ScrollAnchor VirtualItemView::captureAnchor() const
{
    ScrollAnchor anchor;
    if (!m_layout || m_layout->itemCount() == 0)
        return anchor;

    // The anchor is the first item of the *scrolling* pane, whose top edge sits
    // frozenTopExtent() pixels below the viewport top (§31 row direction).
    const qint64 contentOffset = m_scrollOffset + frozenTopExtent();
    const qsizetype row = m_layout->indexAtOffset(contentOffset);
    if (row < 0 || row >= m_layout->itemCount())
        return anchor;

    const QModelIndex index = viewIndex(row);
    if (!index.isValid())
        return anchor;

    anchor.index = QPersistentModelIndex(index);
    anchor.offsetInsideItem = int(contentOffset - m_layout->offsetOf(row));
    return anchor;
}

void VirtualItemView::setPendingAnchor(const ScrollAnchor &anchor)
{
    if (!anchor.isValid())
        return;
    m_pendingAnchor = anchor;
    m_anchorPending = true;
}

void VirtualItemView::cancelPendingAnchor()
{
    m_pendingAnchor = ScrollAnchor();
    m_anchorPending = false;
}

void VirtualItemView::applyPendingAnchor()
{
    m_anchorPending = false;
    if (!m_pendingAnchor.isValid() || !m_layout)
        return;

    const qsizetype row = viewItemForIndex(m_pendingAnchor.index);
    if (row < 0)
        return; // the anchored item disappeared: keep the numeric offset

    // Inverse of captureAnchor(): the scrolling pane starts below the frozen band.
    const qint64 contentOffset = m_layout->offsetOf(row) + m_pendingAnchor.offsetInsideItem;
    m_scrollOffset = qMax<qint64>(0, contentOffset - frozenTopExtent());
}

// ---------------------------------------------------------------------------
// Scrollbars
// ---------------------------------------------------------------------------

void VirtualItemView::syncScrollBars()
{
    QScrollBar *bar = verticalScrollBar();
    const int viewExtent = viewportMainExtent();
    const qint64 extent = m_layout ? m_layout->contentExtent() : 0;

    m_scrollMapper.setExtents(extent, viewExtent);

    const QSignalBlocker blocker(bar);
    bar->setRange(0, m_scrollMapper.scrollRange());
    const int value = m_scrollMapper.toScrollBarValue(m_scrollOffset);
    // Re-centre the compression window on the current viewport position so that
    // precision stays maximal where the user is looking.
    m_scrollMapper.setAnchor(m_scrollOffset, value);
    bar->setPageStep(qMax(1, viewExtent));
    bar->setSingleStep(int(qBound<qint64>(qint64(1), scrollBarSingleStepPixels(),
                                          qint64(std::numeric_limits<int>::max()))));
    bar->setValue(value);
}

void VirtualItemView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    if (m_inRelayout)
        return;

    const int value = verticalScrollBar()->value();
    m_scrollOffset = m_scrollMapper.toLogicalOffset(value);
    m_scrollMapper.setAnchor(m_scrollOffset, value);
    relayout();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void VirtualItemView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().brush(QPalette::Base));
}

void VirtualItemView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    if (m_layout)
        m_layout->setCrossExtent(viewport()->width());
    if (m_recycler)
        m_recycler->trim();
    relayout();
}

void VirtualItemView::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    if (m_layout)
        m_layout->setCrossExtent(viewport()->width());
    relayout();
}

void VirtualItemView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const QModelIndex index = indexAt(mousePosition(event));
    m_dragStartPos = mousePosition(event);
    m_pressedIndex = index.isValid() ? QPersistentModelIndex(index) : QPersistentModelIndex();
    updateSelectionForClick(index, event->modifiers());
    event->accept();
}

void VirtualItemView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    stopDragAutoscroll();
    const QModelIndex index = indexAt(mousePosition(event));
    const bool sameIndex = index.isValid() && m_pressedIndex.isValid()
        && QModelIndex(m_pressedIndex) == index;
    m_pressedIndex = QPersistentModelIndex();
    event->accept();
    if (sameIndex)
        emit clicked(index);
}

void VirtualItemView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    const QModelIndex index = indexAt(mousePosition(event));
    event->accept();
    if (!index.isValid())
        return;
    emit doubleClicked(index);
    emit activated(index);
}

// ---------------------------------------------------------------------------
// Drag & drop (§38)
// ---------------------------------------------------------------------------
//
// The kernel owns the interaction only: which item may start a drag (model
// flags), how the payload is built (model->mimeData()), where a drop lands
// (resolveDropTarget()) and how the gesture is drawn (drop indicator,
// autoscroll). Insertion, move and rejection remain in the model
// (canDropMimeData()/dropMimeData()), so no DnD logic reaches the Recycler.

void VirtualItemView::setDragEnabled(bool enabled)
{
    if (m_dragEnabled == enabled)
        return;
    m_dragEnabled = enabled;
    // Qt only delivers drag & drop events to a widget that accepts drops. The
    // widget under the cursor is the viewport, so it is the one that opts in;
    // QAbstractScrollArea then forwards the events to the view (like mouse
    // events), which is what the handlers below expect.
    viewport()->setAcceptDrops(enabled);
    if (!enabled)
        finishDrag();
}

bool VirtualItemView::canStartDrag(const QModelIndex &index) const
{
    if (!m_dragEnabled || !m_model || !index.isValid())
        return false;
    if (!(m_model->flags(index) & Qt::ItemIsDragEnabled))
        return false;
    return (dragDropActions() & (Qt::CopyAction | Qt::MoveAction | Qt::LinkAction))
        != Qt::IgnoreAction;
}

Qt::DropActions VirtualItemView::dragDropActions() const
{
    if (m_dragDropActions != Qt::IgnoreAction)
        return m_dragDropActions;
    if (m_model) {
        const Qt::DropActions supported = m_model->supportedDragActions();
        if (supported != Qt::IgnoreAction)
            return supported;
        const Qt::DropActions drop = m_model->supportedDropActions();
        if (drop != Qt::IgnoreAction)
            return drop;
    }
    return Qt::MoveAction | Qt::CopyAction;
}

void VirtualItemView::setDragDropActions(Qt::DropActions actions)
{
    m_dragDropActions = actions;
}

void VirtualItemView::setDropIndicatorShown(bool shown)
{
    m_dropIndicatorShown = shown;
    if (!shown)
        hideDropIndicator();
}

void VirtualItemView::setDefaultDropAction(Qt::DropAction action)
{
    m_defaultDropAction = action;
}

Qt::DropAction VirtualItemView::startDrag(const QModelIndex &index)
{
    if (!m_model)
        return Qt::IgnoreAction;

    QModelIndex dragIndex = index;
    if (!dragIndex.isValid()) {
        dragIndex = currentIndex();
        if (!dragIndex.isValid() && m_pressedIndex.isValid())
            dragIndex = QModelIndex(m_pressedIndex);
    }
    if (!canStartDrag(dragIndex))
        return Qt::IgnoreAction;

    // The selection is the payload when the dragged item is part of it.
    QModelIndexList indexes;
    if (m_selectionModel && m_selectionModel->isSelected(dragIndex)) {
        const QModelIndexList selected = m_selectionModel->selectedIndexes();
        for (const QModelIndex &candidate : selected) {
            if (candidate.column() == 0 && (m_model->flags(candidate) & Qt::ItemIsDragEnabled))
                indexes.append(candidate);
        }
    }
    if (indexes.isEmpty())
        indexes.append(dragIndex);

    QMimeData *mime = m_model->mimeData(indexes);
    if (!mime)
        return Qt::IgnoreAction;

    // §36/§38: the widget of the dragged item must survive the drag, otherwise a
    // scroll-induced recycle would destroy the drag source mid-gesture.
    m_dragSourceWidget = widgetForIndex(dragIndex);
    if (m_dragSourceWidget)
        pinWidget(m_dragSourceWidget);
    // The sources are remembered so the drag can refuse to drop onto itself.
    m_dragSourceIndexes.clear();
    for (const QModelIndex &source : indexes)
        m_dragSourceIndexes.append(QPersistentModelIndex(source));

    QDrag drag(this);
    drag.setMimeData(mime);
    if (m_dragSourceWidget) {
        const QPixmap pixmap = m_dragSourceWidget->grab();
        drag.setPixmap(pixmap);
        drag.setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    }
    const Qt::DropAction action = drag.exec(dragDropActions(), m_defaultDropAction);

    finishDrag();
    return action;
}

void VirtualItemView::finishDrag()
{
    if (m_dragSourceWidget) {
        unpinWidget(m_dragSourceWidget);
        m_dragSourceWidget = nullptr;
    }
    stopDragAutoscroll();
    hideDropIndicator();
    m_dragSourceIndexes.clear();
    m_dragHoverValid = false;
}

bool VirtualItemView::isDropOnItself(const DropTarget &target, Qt::DropAction action) const
{
    // QAbstractItemView only guards internal *moves*: copying an item next to
    // itself is a legitimate gesture.
    if (!target.isValid() || action != Qt::MoveAction || m_dragSourceIndexes.isEmpty())
        return false;
    for (const QPersistentModelIndex &persistent : m_dragSourceIndexes) {
        if (!persistent.isValid())
            continue;
        const QModelIndex source(persistent);
        // Into the dragged item, or into one of its descendants.
        for (QModelIndex ancestor = target.parent; ancestor.isValid(); ancestor = ancestor.parent()) {
            if (ancestor == source)
                return true;
        }
        // Between the dragged item and itself: the item would not move.
        if (target.parent == source.parent()
            && (target.row == source.row() || target.row == source.row() + 1))
            return true;
    }
    return false;
}

void VirtualItemView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragEnabled || !(event->buttons() & Qt::LeftButton) || !m_pressedIndex.isValid()) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = mousePosition(event);
    if ((pos - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        event->accept();
        return;
    }

    // The pressed index (not the position) carries the gesture: it survives
    // model mutations between press and move.
    const QModelIndex index(m_pressedIndex);
    m_pressedIndex = QPersistentModelIndex();
    event->accept();
    if (canStartDrag(index))
        startDrag(index);
}

void VirtualItemView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_dragEnabled || !m_model || !event->mimeData()) {
        event->ignore();
        return;
    }
    const Qt::DropActions supported = m_model->supportedDropActions();
    if (!(supported & event->proposedAction()) && !(supported & event->possibleActions())) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void VirtualItemView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_model || !event->mimeData()) {
        event->ignore();
        return;
    }
    const QPoint pos = dropPosition(event);
    m_dragHoverPos = pos;
    m_dragHoverValid = true;
    const DropTarget target = resolveDropTarget(pos);
    const Qt::DropAction action = event->dropAction() != Qt::IgnoreAction
        ? event->dropAction()
        : event->proposedAction();
    const bool canDrop = target.isValid() && !isDropOnItself(target, action)
        && m_model->canDropMimeData(event->mimeData(), action, target.row, target.column,
                                    target.parent);
    if (!canDrop) {
        hideDropIndicator();
        stopDragAutoscroll();
        event->ignore();
        return;
    }
    showDropIndicator(target);
    updateDragAutoscroll(pos);
    event->accept();
}

void VirtualItemView::dragLeaveEvent(QDragLeaveEvent *event)
{
    hideDropIndicator();
    stopDragAutoscroll();
    m_dragHoverValid = false;
    event->accept();
}

void VirtualItemView::dropEvent(QDropEvent *event)
{
    stopDragAutoscroll();
    hideDropIndicator();
    m_dragHoverValid = false;
    if (!m_model || !event->mimeData()) {
        event->ignore();
        return;
    }
    const DropTarget target = resolveDropTarget(dropPosition(event));
    const Qt::DropAction action = event->dropAction() != Qt::IgnoreAction
        ? event->dropAction()
        : event->proposedAction();
    if (!target.isValid() || isDropOnItself(target, action)
        || !m_model->canDropMimeData(event->mimeData(), action, target.row, target.column,
                                     target.parent)) {
        event->ignore();
        return;
    }
    // The model owns the semantics: it inserts, moves or rejects (§38).
    if (m_model->dropMimeData(event->mimeData(), action, target.row, target.column, target.parent)) {
        event->acceptProposedAction();
        emit itemDropped(target.parent, target.row, target.column, action);
        markDirty();
    } else {
        event->ignore();
    }
}

VirtualItemView::DropTarget VirtualItemView::resolveDropTarget(const QPoint &viewportPos) const
{
    DropTarget target;
    if (!m_layout || m_layout->itemCount() <= 0)
        return target;
    const qint64 offset = m_scrollOffset + qMax(0, viewportPos.y());
    if (offset < 0 || offset >= m_layout->contentExtent())
        return target;
    const qsizetype row = m_layout->indexAtOffset(offset);
    if (row < 0 || row >= m_layout->itemCount())
        return target;
    const qint64 rowOffset = m_layout->offsetOf(row);
    const bool after = offset - rowOffset > m_layout->itemSize(row) / 2;
    const QModelIndex index = viewIndex(row);
    if (!index.isValid())
        return target;
    target.parent = index.parent();
    target.row = index.row() + (after ? 1 : 0);
    target.column = -1;
    return target;
}

QRect VirtualItemView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || !m_layout || m_layout->itemCount() <= 0)
        return QRect();
    // The line sits at the boundary the insertion would use: below the item that
    // precedes the insertion point, or above the row that follows it.
    const QModelIndex before = target.row > 0
        ? m_model->index(target.row - 1, 0, target.parent)
        : QModelIndex();
    qint64 offset = -1;
    if (before.isValid()) {
        const qsizetype row = viewItemForIndex(before);
        if (row >= 0)
            offset = m_layout->offsetOf(row) + m_layout->itemSize(row);
    }
    if (offset < 0) {
        const QModelIndex at = m_model->index(target.row, 0, target.parent);
        const qsizetype row = at.isValid() ? viewItemForIndex(at) : -1;
        if (row < 0)
            return QRect();
        offset = m_layout->offsetOf(row);
    }
    const int y = int(offset - m_scrollOffset);
    return QRect(0, y - 1, viewport()->width(), 2);
}

VirtualItemView::DropTarget VirtualItemView::dropTargetAt(const QPoint &viewportPos) const
{
    return resolveDropTarget(viewportPos);
}

QRect VirtualItemView::dropIndicatorRect(const DropTarget &target) const
{
    return resolveDropIndicatorRect(target);
}

VirtualItemView::DropIndicatorStyle
VirtualItemView::dropIndicatorStyle(const DropTarget &target) const
{
    return target.ontoItem ? DropIndicatorStyle::Frame : DropIndicatorStyle::Line;
}

void VirtualItemView::showDropIndicator(const DropTarget &target)
{
    if (!m_dropIndicatorShown)
        return;
    const QRect rect = resolveDropIndicatorRect(target);
    if (rect.isEmpty()) {
        hideDropIndicator();
        return;
    }
    const bool frame = dropIndicatorStyle(target) == DropIndicatorStyle::Frame;
    if (!m_dropIndicator) {
        m_dropIndicator = new DropIndicatorWidget(viewport());
        viewport()->update();
    }
    static_cast<DropIndicatorWidget *>(m_dropIndicator)->setFrame(frame);
    m_dropIndicator->setGeometry(rect);
    m_dropIndicator->show();
    m_dropIndicator->raise();
}

void VirtualItemView::hideDropIndicator()
{
    if (m_dropIndicator)
        m_dropIndicator->hide();
}

void VirtualItemView::updateDragAutoscroll(const QPoint &viewportPos)
{
    const int margin = qMax(8, viewport()->height() / 12);
    int delta = 0;
    if (viewportPos.y() < margin)
        delta = -qMax(1, margin - viewportPos.y());
    else if (viewportPos.y() > viewport()->height() - margin)
        delta = qMax(1, viewportPos.y() - (viewport()->height() - margin));
    if (delta == 0) {
        stopDragAutoscroll();
        return;
    }
    m_dragAutoscrollDelta = delta;
    if (!m_dragAutoscrollTimer) {
        m_dragAutoscrollTimer = new QTimer(this);
        m_dragAutoscrollTimer->setInterval(40);
        connect(m_dragAutoscrollTimer, &QTimer::timeout, this, [this]() {
            const qint64 before = m_scrollOffset;
            scrollByPixels(qBound(-40, m_dragAutoscrollDelta, 40));
            if (m_scrollOffset == before || !m_dragHoverValid)
                return;
            // The rows below the cursor moved with the content: re-resolve the
            // target so the indicator stays glued to the insertion point (and
            // gets lifted above the widgets materialized by the scroll).
            const DropTarget target = resolveDropTarget(m_dragHoverPos);
            if (target.isValid())
                showDropIndicator(target);
            else
                hideDropIndicator();
        });
    }
    if (!m_dragAutoscrollTimer->isActive())
        m_dragAutoscrollTimer->start();
}

void VirtualItemView::stopDragAutoscroll()
{
    if (m_dragAutoscrollTimer)
        m_dragAutoscrollTimer->stop();
    m_dragAutoscrollDelta = 0;
}

void VirtualItemView::wheelEvent(QWheelEvent *event)
{
    // High resolution input (touchpads, smooth wheels) delivers pixel deltas and
    // must scroll pixel by pixel. Classic wheels scroll a fixed pixel step.
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    qint64 delta = 0;
    if (!pixel.isNull()) {
        delta = qint64(pixel.y());
    } else if (!angle.isNull()) {
        delta = qint64(angle.y()) * wheelStepPixels() / 120;
    }
    if (delta == 0) {
        event->ignore();
        return;
    }

    // Wheel up (positive delta) moves the content down.
    scrollByPixels(-delta);
    event->accept();
}

void VirtualItemView::keyPressEvent(QKeyEvent *event)
{
    if (handleItemKeyPress(event)) {
        event->accept();
        return;
    }

    const qsizetype count = viewItemCount();
    const QModelIndex current = currentIndex();
    const qsizetype currentRow = current.isValid() ? viewItemForIndex(current) : -1;
    const qsizetype page = qMax<qsizetype>(1, visibleItemCount());

    switch (event->key()) {
    case Qt::Key_Up:
        moveCurrentTo(currentRow < 0 ? count - 1 : currentRow - 1, event->modifiers());
        event->accept();
        return;
    case Qt::Key_Down:
        moveCurrentTo(currentRow < 0 ? 0 : currentRow + 1, event->modifiers());
        event->accept();
        return;
    case Qt::Key_PageUp:
        moveCurrentTo(currentRow < 0 ? 0 : currentRow - page, event->modifiers());
        event->accept();
        return;
    case Qt::Key_PageDown:
        moveCurrentTo(currentRow < 0 ? 0 : currentRow + page, event->modifiers());
        event->accept();
        return;
    case Qt::Key_Home:
        moveCurrentTo(0, event->modifiers());
        event->accept();
        return;
    case Qt::Key_End:
        moveCurrentTo(count - 1, event->modifiers());
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (current.isValid())
            emit activated(current);
        event->accept();
        return;
    case Qt::Key_Space:
        if (m_selectionModel && current.isValid())
            m_selectionModel->setCurrentIndex(current, QItemSelectionModel::Toggle | QItemSelectionModel::Current);
        event->accept();
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void VirtualItemView::moveCurrentToItem(qsizetype item, Qt::KeyboardModifiers modifiers)
{
    moveCurrentTo(item, modifiers);
}

void VirtualItemView::moveCurrentTo(qsizetype row, Qt::KeyboardModifiers modifiers)
{
    const qsizetype count = viewItemCount();
    if (count <= 0 || !m_layout)
        return;

    row = qBound<qsizetype>(qsizetype(0), row, count - 1);
    const QModelIndex index = indexForNavigation(row, currentIndex());
    if (!index.isValid())
        return;

    if (m_selectionModel) {
        switch (m_selectionMode) {
        case SelectionMode::NoSelection:
            m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
            break;
        case SelectionMode::SingleSelection:
            m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | rowFlags());
            m_selectionAnchor = QPersistentModelIndex(index);
            break;
        case SelectionMode::MultiSelection:
        case SelectionMode::ExtendedSelection:
            if (modifiers & Qt::ShiftModifier) {
                extendSelectionTo(index);
            } else if (m_selectionMode == SelectionMode::ExtendedSelection
                       && (modifiers & Qt::ControlModifier)) {
                // Move the current index and leave the selection untouched.
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current | rowFlags());
            } else {
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | rowFlags());
                pinCurrentIndex(index);
                m_selectionAnchor = QPersistentModelIndex(index);
            }
            break;
        }
    }
    scrollTo(index, EnsureVisible);
}

void VirtualItemView::updateSelectionForClick(const QModelIndex &index, Qt::KeyboardModifiers modifiers)
{
    if (!m_selectionModel || !index.isValid())
        return;

    switch (m_selectionMode) {
    case SelectionMode::NoSelection:
        // The current index still moves; nothing is ever selected.
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
        return;
    case SelectionMode::SingleSelection:
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | rowFlags());
        pinCurrentIndex(index);
        m_selectionAnchor = QPersistentModelIndex(index);
        return;
    case SelectionMode::MultiSelection:
    case SelectionMode::ExtendedSelection:
        break;
    }

    if (modifiers & Qt::ShiftModifier) {
        extendSelectionTo(index);
        return;
    }
    // MultiSelection toggles on every click; ExtendedSelection only with Ctrl.
    if (m_selectionMode == SelectionMode::MultiSelection || (modifiers & Qt::ControlModifier)) {
        toggleClickedIndex(index);
        return;
    }
    m_selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | rowFlags());
    pinCurrentIndex(index);
    m_selectionAnchor = QPersistentModelIndex(index);
}

void VirtualItemView::toggleClickedIndex(const QModelIndex &index)
{
    if (!m_selectionModel || !index.isValid())
        return;
    // Toggle and keep the rest of the selection, like QAbstractItemView.
    const bool wasSelected = m_selectionModel->isSelected(index);
    m_selectionModel->select(index, wasSelected ? QItemSelectionModel::Deselect
                                                : QItemSelectionModel::Select);
    m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current | rowFlags());
    pinCurrentIndex(index);
    m_selectionAnchor = QPersistentModelIndex(index);
}

void VirtualItemView::extendSelectionTo(const QModelIndex &index)
{
    if (!m_selectionModel || !index.isValid())
        return;
    // QItemSelectionModel has no "extend" flag, so the range is built from the
    // anchor explicitly.
    if (!m_selectionAnchor.isValid())
        m_selectionAnchor = QPersistentModelIndex(m_selectionModel->currentIndex());
    const QModelIndex base = m_selectionAnchor.isValid() ? QModelIndex(m_selectionAnchor) : index;
    if (base.isValid() && base.parent() == index.parent())
        m_selectionModel->select(QItemSelection(base, index),
                                 QItemSelectionModel::ClearAndSelect | rowFlags());
    else
        m_selectionModel->select(index, QItemSelectionModel::ClearAndSelect | rowFlags());
    m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current | rowFlags());
    pinCurrentIndex(index);
}

// ---------------------------------------------------------------------------
// Model signals
// ---------------------------------------------------------------------------

void VirtualItemView::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                                    const QVector<int> &roles)
{
    Q_UNUSED(roles);
    if (!topLeft.isValid() || !bottomRight.isValid())
        return;
    if (topLeft.parent() != bottomRight.parent() || !isLayoutParent(topLeft.parent()))
        return;

    // Identities are unchanged: rebind only the affected materialized items.
    rebindItemsInRange(topLeft, bottomRight);
    // Item sizes may have changed, so keep the visual position stable.
    setPendingAnchor(captureAnchor());
    markDirty();
}

void VirtualItemView::onRowsAboutToBeInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (!isLayoutParent(parent))
        return;
    setPendingAnchor(captureAnchor());
}

void VirtualItemView::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    if (!isLayoutParent(parent) || !m_layout)
        return;
    const qsizetype count = qsizetype(last) - qsizetype(first) + 1;
    if (count <= 0)
        return;
    m_layout->insertItems(first, count, estimateItemSize(first));
    markDirty();
}

void VirtualItemView::onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
{
    if (!isLayoutParent(parent))
        return;
    setPendingAnchor(captureAnchor());
    // The persistent indexes of the removed rows become invalid right after
    // the removal, so their widgets must be recycled now.
    recycleItemsInModelRange(parent, first, last);
}

void VirtualItemView::onRowsRemoved(const QModelIndex &parent, int first, int last)
{
    if (!isLayoutParent(parent) || !m_layout)
        return;
    const qsizetype count = qsizetype(last) - qsizetype(first) + 1;
    if (count > 0)
        m_layout->removeItems(first, count);
    markDirty();
}

void VirtualItemView::onRowsAboutToBeMoved(const QModelIndex &sourceParent, int start, int end,
                                              const QModelIndex &destParent, int row)
{
    Q_UNUSED(sourceParent);
    Q_UNUSED(start);
    Q_UNUSED(end);
    Q_UNUSED(destParent);
    Q_UNUSED(row);
    // Rows keep their identity: the QPersistentModelIndex of every materialized
    // item follows the move, so only geometry has to be refreshed.
}

void VirtualItemView::onRowsMoved(const QModelIndex &sourceParent, int start, int end,
                                     const QModelIndex &destParent, int row)
{
    if (!isLayoutParent(sourceParent) || !isLayoutParent(destParent)) {
        // Cross-parent moves change the visible mapping entirely.
        recycleAllItems();
        resetLayoutForNewModel();
        markDirty();
        return;
    }
    if (!m_layout)
        return;
    const qsizetype count = qsizetype(end) - qsizetype(start) + 1;
    if (count > 0)
        m_layout->moveItems(start, count, row);
    markDirty();
}

void VirtualItemView::onLayoutAboutToBeChanged(const QList<QPersistentModelIndex> &parents,
                                                  QAbstractItemModel::LayoutChangeHint hint)
{
    Q_UNUSED(parents);
    Q_UNUSED(hint);
    setPendingAnchor(captureAnchor());
    // The mapping between rows and items may have been rewritten completely:
    // rebuild the materialized set from scratch.
    recycleAllItems();
}

void VirtualItemView::onLayoutChanged(const QList<QPersistentModelIndex> &parents,
                                         QAbstractItemModel::LayoutChangeHint hint)
{
    Q_UNUSED(parents);
    Q_UNUSED(hint);
    // Sizes are keyed by row, so they cannot be trusted after a reorder.
    resetLayoutForNewModel();
    markDirty();
}

void VirtualItemView::onModelAboutToBeReset()
{
    recycleAllItems();
    m_explicitPinned.clear();
    cancelPendingAnchor();
}

void VirtualItemView::onModelReset()
{
    m_scrollOffset = 0;
    m_scrollMapper.resetAnchor();
    resetLayoutForNewModel();
    markDirty();
}

} // namespace viv
