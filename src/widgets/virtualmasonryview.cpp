#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>

#include "../layout/masonrylayout_p.h"
#include "../core/pixelalignedlines_p.h"

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>
#include <limits>
#include <utility>

#include <algorithm>

namespace viv {

namespace {

class MasonryGridOverlay : public QWidget
{
public:
    explicit MasonryGridOverlay(QWidget *parent, std::function<void(QPainter *)> paint)
        : QWidget(parent), m_paint(std::move(paint))
    {
        setObjectName(QStringLiteral("vivMasonryGridOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        m_paint(&painter);
    }

private:
    std::function<void(QPainter *)> m_paint;
};

} // namespace

VirtualMasonryView::VirtualMasonryView(QWidget *parent) : VirtualItemView(parent)
{
    m_masonry = new MasonryLayout;
    m_heightAnimation = new QVariantAnimation(this);
    connect(m_heightAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant &value) {
            if (m_animatingItem < 0)
                return;
            const ScrollAnchor anchor = captureAnchor();
            applyItemHeight(m_animatingItem, value.toInt());
            setPendingAnchor(anchor);
            relayout();
            viewport()->update();
        });
    connect(m_heightAnimation, &QVariantAnimation::finished, this,
        [this]() { finishItemHeightAnimation(); });
    setAutoMeasureItemHeight(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridOverlay = new MasonryGridOverlay(viewport(),
        [this](QPainter *painter) { paintGridLines(painter); });
    setLayoutPolicy(m_masonry, true);
}

void VirtualMasonryView::setModel(QAbstractItemModel *newModel)
{
    if (model() == newModel)
        return;
    const quint64 request = ++m_modelRequest;
    const QPointer<VirtualMasonryView> self(this);
    const QPointer<QAbstractItemModel> requested(newModel);
    for (const auto &connection : m_modelConnections)
        disconnect(connection);
    m_modelConnections.clear();
    m_pendingItemSizes.clear();
    m_savedItemSizes.clear();
    m_sizeChangeAnchor = {};
    m_columnChange = {};
    finishItemHeightAnimation();
    m_root = QModelIndex();
    m_rootRemoving = false;
    ++m_revision;
    VirtualItemView::setModel(newModel);
    if (!self || request != m_modelRequest || !requested || model() != requested.data())
        return;
    const auto changed = [this, self, requested, request]() {
        if (!requested || model() != requested.data() || request != m_modelRequest)
            return;
        restoreItemSizes();
        ++m_revision;
        if (m_rootRemoving && !m_root.isValid()) {
            m_rootRemoving = false;
            m_pendingItemSizes.clear();
            rekeyPersistentState();
            cancelPendingAnchor();
            resetLayoutForNewModel();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
            refreshAdapterEstimates();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
        }
        rekeyPersistentState();
        markDirty();
    };
    const auto reordered = [this, self, requested, request, changed]() {
        const ScrollAnchor anchor = std::exchange(m_sizeChangeAnchor, {});
        changed();
        if (!self || !requested || model() != requested.data() || request != m_modelRequest)
            return;
        setPendingAnchor(anchor);
        refreshAdapterEstimates(anchor);
    };
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsInserted,
                                      this, changed));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsRemoved,
                                      this, changed));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeInserted,
        this, [this, self, requested, request](const QModelIndex &parent, int, int) {
            finishItemHeightAnimation();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
            m_sizeChangeAnchor = isLayoutParent(parent) ? captureAnchor() : ScrollAnchor();
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeRemoved,
        this, [this, self, requested, request](const QModelIndex &parent, int first, int last) {
            finishItemHeightAnimation();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
            m_sizeChangeAnchor = isLayoutParent(parent) ? captureAnchor() : ScrollAnchor();
            for (QModelIndex ancestor = m_root; ancestor.isValid(); ancestor = ancestor.parent()) {
                if (ancestor.parent() == parent && ancestor.row() >= first && ancestor.row() <= last) {
                    m_rootRemoving = true;
                    ++m_revision;
                    m_pendingItemSizes.clear();
                    m_savedItemSizes.clear();
                    m_sizeChangeAnchor = {};
                    m_columnChange = {};
                    cancelPendingAnchor();
                    recycleAllItems();
                    break;
                }
            }
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeMoved,
        this, [this, self, requested, request]() {
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
            finishItemHeightAnimation();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
            saveItemSizes();
            setPendingAnchor(captureAnchor());
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsMoved,
                                      this, reordered));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::layoutAboutToBeChanged,
        this, [this, self, requested, request]() {
            finishItemHeightAnimation();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
            saveItemSizes();
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::layoutChanged,
                                      this, reordered));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::modelAboutToBeReset,
        this, [this]() {
            finishItemHeightAnimation();
            m_rootRemoving = false;
            m_pendingItemSizes.clear();
            m_savedItemSizes.clear();
            m_sizeChangeAnchor = {};
            m_columnChange = {};
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::modelReset,
                                      this, changed));
    m_modelConnections.append(connect(newModel, &QObject::destroyed, this,
        [this, request]() {
            if (request != m_modelRequest)
                return;
            m_root = QModelIndex();
            m_rootRemoving = false;
            m_savedItemSizes.clear();
            m_sizeChangeAnchor = {};
            m_columnChange = {};
            ++m_revision;
            rekeyPersistentState();
            markDirty();
        }));
    const auto beginColumnChange = [this, self, requested, request](bool kernelRecycled) {
        finishItemHeightAnimation();
        if (!self || !requested || model() != requested.data() || request != m_modelRequest)
            return;
        const ScrollAnchor anchor = captureAnchor();
        m_columnChange = {m_root, viewItemForIndex(anchor.index), anchor.offsetInsideItem, true};
        m_pendingItemSizes.clear();
        m_savedItemSizes.clear();
        m_sizeChangeAnchor = {};
        ++m_revision;
        cancelPendingAnchor();
        // The kernel handles top-level insert/remove and top-level-to-top-level
        // moves. Root-local and cross-parent moves need Masonry's own release.
        if (!kernelRecycled)
            recycleItemsForColumnChange();
    };
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsAboutToBeInserted,
        this, [this, beginColumnChange](const QModelIndex &parent, int first, int) {
            if (isLayoutParent(parent) && first == 0)
                beginColumnChange(!parent.isValid());
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsAboutToBeRemoved,
        this, [this, self, requested, request, beginColumnChange](const QModelIndex &parent, int first, int last) {
            for (QModelIndex ancestor = m_root; ancestor.isValid(); ancestor = ancestor.parent()) {
                if (ancestor.parent() != parent || ancestor.column() < first || ancestor.column() > last)
                    continue;
                finishItemHeightAnimation();
                if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                    return;
                m_rootRemoving = true;
                m_pendingItemSizes.clear();
                m_savedItemSizes.clear();
                m_sizeChangeAnchor = {};
                m_columnChange = {};
                ++m_revision;
                cancelPendingAnchor();
                // Consume the kernel's row-pin snapshot while it still names the
                // old root; removed children must not be repinned at top level.
                restoreItemsAfterColumnChange();
                recycleAllItems();
                return;
            }
            if (isLayoutParent(parent) && first == 0)
                beginColumnChange(!parent.isValid());
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsAboutToBeMoved,
        this, [this, beginColumnChange](const QModelIndex &source, int first, int,
                                       const QModelIndex &destination, int column) {
            if ((isLayoutParent(source) && first == 0)
                || (isLayoutParent(destination) && column == 0))
                beginColumnChange(!source.isValid() && !destination.isValid());
        }));
    const auto columnsChanged = [this, self, requested, request, changed]() {
        if (!requested || model() != requested.data() || request != m_modelRequest)
            return;
        const ColumnChange columns = std::exchange(m_columnChange, {});
        restoreItemsAfterColumnChange();
        if (columns.pending && columns.root == m_root) {
            const quint64 revision = m_revision;
            resetLayoutForNewModel();
            if (!self || !requested || model() != requested.data() || request != m_modelRequest
                || revision != m_revision || columns.root != m_root)
                return;
            if (currentIndex().isValid() && viewItemForIndex(currentIndex()) < 0)
                setCurrentIndex(QModelIndex());
            if (!self || !requested || model() != requested.data() || request != m_modelRequest)
                return;
        }
        changed();
        if (!self || !requested || model() != requested.data() || request != m_modelRequest
            || !columns.pending || columns.root != m_root)
            return;
        ScrollAnchor anchor;
        anchor.index = QPersistentModelIndex(viewIndex(columns.anchorItem));
        anchor.offsetInsideItem = columns.offsetInsideItem;
        if (itemHeightMode() == ItemHeightMode::AdapterEstimate && adapter())
            refreshAdapterEstimates(anchor);
        else
            reflow(anchor);
    };
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsInserted,
                                      this, columnsChanged));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsRemoved,
                                      this, columnsChanged));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsMoved,
                                      this, columnsChanged));
    // Restore persistent sizing before estimate callbacks can publish new geometry.
    connectEstimateSignals(newModel);
    refreshAdapterEstimates();
}

void VirtualMasonryView::setAdapter(WidgetAdapter *newAdapter, bool takeOwnership)
{
    if (adapter() == newAdapter) {
        VirtualItemView::setAdapter(newAdapter, takeOwnership);
        return;
    }
    const QPointer<VirtualMasonryView> self(this);
    finishItemHeightAnimation();
    if (!self)
        return;
    m_pendingItemSizes.clear();
    m_savedItemSizes.clear();
    m_sizeChangeAnchor = {};
    m_columnChange = {};
    if (!installAdapter(newAdapter, takeOwnership, false) || !self)
        return;
    ++m_revision;
    refreshAdapterEstimates();
    if (self)
        markDirty();
}

void VirtualMasonryView::setRootIndex(const QModelIndex &index)
{
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualMasonryView: root belongs to another model");
        return;
    }
    if (index == m_root)
        return;
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_root = index;
    m_rootRemoving = false;
    m_pendingItemSizes.clear();
    m_savedItemSizes.clear();
    m_sizeChangeAnchor = {};
    m_columnChange = {};
    ++m_revision;
    recycleAllItems();
    resetLayoutForNewModel();
    setPendingAnchor(anchor);
    relayout();
    refreshAdapterEstimates();
}

qsizetype VirtualMasonryView::viewItemCount() const
{
    return model() && model()->columnCount(m_root) > 0 ? model()->rowCount(m_root) : 0;
}

QModelIndex VirtualMasonryView::viewIndex(qsizetype item, int column) const
{
    return model() && column == 0 && item >= 0 && item < viewItemCount()
        ? model()->index(int(item), 0, m_root) : QModelIndex();
}

qsizetype VirtualMasonryView::viewItemForIndex(const QModelIndex &index) const
{
    return index.isValid() && index.model() == model() && index.parent() == m_root
        && index.column() == 0 ? index.row() : -1;
}

bool VirtualMasonryView::isLayoutParent(const QModelIndex &parent) const
{
    return !m_rootRemoving && parent == m_root;
}

bool VirtualMasonryView::isPersistentRowStateValid(const QModelIndex &index) const
{
    return viewItemForIndex(index) >= 0;
}

qsizetype VirtualMasonryView::anchorItemAtOffset(qint64 offset) const
{
    const QVector<qsizetype> candidates = m_masonry->itemsAtOrAfterOffset(offset);
    if (candidates.isEmpty())
        return -1;
    qsizetype best = candidates.first();
    for (qsizetype item : candidates) {
        const qint64 candidate = m_masonry->offsetOf(item);
        const qint64 current = m_masonry->offsetOf(best);
        if (candidate < current || (candidate == current && item < best))
            best = item;
    }
    return best;
}

void VirtualMasonryView::reflow(const ScrollAnchor &anchor)
{
    ++m_revision;
    setPendingAnchor(anchor);
    if (relayoutActive())
        markDirty();
    else
        relayout();
    viewport()->update();
    emit masonryLayoutChanged();
}

void VirtualMasonryView::setCellWidthRange(int minimum, int maximum)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setWidthRange(minimum, maximum);
    reflow(anchor);
}

int VirtualMasonryView::minimumCellWidth() const { return m_masonry->minimumWidth(); }
int VirtualMasonryView::maximumCellWidth() const { return m_masonry->maximumWidth(); }

void VirtualMasonryView::setColumnCount(int count)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setColumnCount(count);
    reflow(anchor);
}

int VirtualMasonryView::columnCount() const { return m_masonry->columnCount(); }

void VirtualMasonryView::setColumnSpacing(int pixels)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setSpacing(rowSpacing(), pixels);
    reflow(anchor);
}

int VirtualMasonryView::columnSpacing() const { return m_masonry->columnSpacing(); }
int VirtualMasonryView::rowSpacing() const { return m_masonry->rowSpacing(); }

void VirtualMasonryView::setRowSpacing(int pixels)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setSpacing(pixels, columnSpacing());
    setPendingAnchor(anchor);
    ++m_revision;
    VirtualItemView::setRowSpacing(pixels);
    viewport()->update();
    emit masonryLayoutChanged();
}

void VirtualMasonryView::setContentMargins(const QMargins &margins)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setMargins(margins);
    reflow(anchor);
}

QMargins VirtualMasonryView::contentMargins() const { return m_masonry->margins(); }

void VirtualMasonryView::setItemHeightMode(ItemHeightMode mode)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setHeightMode(static_cast<MasonryLayout::HeightMode>(mode));
    reflow(anchor);
    if (mode == ItemHeightMode::AdapterEstimate)
        refreshAdapterEstimates();
}

VirtualMasonryView::ItemHeightMode VirtualMasonryView::itemHeightMode() const
{
    return static_cast<ItemHeightMode>(m_masonry->heightMode());
}

void VirtualMasonryView::setFixedItemHeight(int height)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setFixedHeight(height);
    reflow(anchor);
}

int VirtualMasonryView::fixedItemHeight() const { return m_masonry->fixedHeight(); }

void VirtualMasonryView::setItemAspectRatio(qreal ratio)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setAspectRatio(ratio);
    reflow(anchor);
}

qreal VirtualMasonryView::itemAspectRatio() const { return m_masonry->aspectRatio(); }

void VirtualMasonryView::setItemEstimatedSize(const QModelIndex &index, const QSize &size)
{
    const qsizetype item = viewItemForIndex(index);
    if (item < 0)
        return;
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setEstimatedSize(item, size);
    reflow(anchor);
}

void VirtualMasonryView::setItemEstimatedSizes(const QVector<QSize> &sizes)
{
    finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    m_masonry->setEstimatedSizes(sizes);
    reflow(anchor);
}

void VirtualMasonryView::setItemHeight(const QModelIndex &index, int height)
{
    const qsizetype item = viewItemForIndex(index);
    if (item < 0)
        return;
    const int target = qMax(1, height);
    if (m_heightAnimationEnabled && m_heightAnimationDuration > 0
        && item != m_animatingItem && m_masonry->itemSize(item) != target) {
        animateItemHeight(item, target);
        return;
    }
    if (item == m_animatingItem)
        finishItemHeightAnimation();
    const ScrollAnchor anchor = captureAnchor();
    applyItemHeight(item, target);
    reflow(anchor);
}

void VirtualMasonryView::invalidateItemSize(const QModelIndex &index)
{
    if (!adapter() || viewItemForIndex(index) < 0)
        return;
    m_pendingItemSizes.append(QPersistentModelIndex(index));
    if (m_itemSizesScheduled)
        return;
    m_itemSizesScheduled = true;
    QMetaObject::invokeMethod(this, [this]() { applyPendingItemSizes(); }, Qt::QueuedConnection);
}

void VirtualMasonryView::applyPendingItemSizes()
{
    m_itemSizesScheduled = false;
    auto pending = std::exchange(m_pendingItemSizes, {});
    if (pending.isEmpty() || !adapter() || !model())
        return;
    const QPointer<VirtualMasonryView> self(this);
    finishItemHeightAnimation();
    if (!self || !adapter() || !model())
        return;
    const QPointer<QAbstractItemModel> activeModel(model());
    WidgetAdapter *const activeAdapter = adapter();
    const QPersistentModelIndex root = m_root;
    const quint64 revision = m_revision;
    const ScrollAnchor anchor = captureAnchor();
    std::sort(pending.begin(), pending.end(), [](const QPersistentModelIndex &lhs,
                                               const QPersistentModelIndex &rhs) {
        return lhs.row() < rhs.row();
    });
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
    QVector<QPair<qsizetype, QSize>> estimates;
    estimates.reserve(pending.size());
    for (const QPersistentModelIndex &index : std::as_const(pending)) {
        const qsizetype item = viewItemForIndex(index);
        if (item < 0)
            continue;
        const QSize size = activeAdapter->estimatedSize(index);
        if (!self || !activeModel || model() != activeModel.data()
            || adapter() != activeAdapter || revision != m_revision) {
            if (self && activeModel && model() == activeModel.data()
                && adapter() == activeAdapter && m_root == root) {
                for (const QPersistentModelIndex &retry : std::as_const(pending))
                    invalidateItemSize(retry);
            }
            return;
        }
        estimates.append(qMakePair(item, size));
    }
    if (estimates.isEmpty())
        return;
    m_masonry->setItemEstimates(estimates);
    reflow(anchor);
}

void VirtualMasonryView::setItemHeightAnimationEnabled(bool enabled)
{
    if (m_heightAnimationEnabled == enabled)
        return;
    m_heightAnimationEnabled = enabled;
    if (!enabled)
        finishItemHeightAnimation();
}

void VirtualMasonryView::setItemHeightAnimationDuration(int milliseconds)
{
    const int duration = qMax(0, milliseconds);
    if (m_heightAnimationDuration == duration)
        return;
    m_heightAnimationDuration = duration;
    if (m_heightAnimation)
        m_heightAnimation->setDuration(duration);
    if (duration == 0)
        finishItemHeightAnimation();
}

void VirtualMasonryView::applyItemHeight(qsizetype item, int height)
{
    if (item >= 0 && item < viewItemCount()) {
        m_masonry->setItemSize(item, qMax(1, height));
        ++m_revision;
    }
}

void VirtualMasonryView::animateItemHeight(qsizetype item, int height)
{
    if (!m_heightAnimation || item < 0 || item >= viewItemCount())
        return;
    if (m_animatingItem >= 0)
        finishItemHeightAnimation();
    const int start = m_masonry->itemSize(item);
    if (start == height) {
        const ScrollAnchor anchor = captureAnchor();
        applyItemHeight(item, height);
        reflow(anchor);
        return;
    }
    m_animatingItem = item;
    m_animationTargetHeight = height;
    m_heightAnimation->stop();
    m_heightAnimation->setStartValue(start);
    m_heightAnimation->setEndValue(height);
    m_heightAnimation->setDuration(m_heightAnimationDuration);
    m_heightAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_heightAnimation->start();
}

void VirtualMasonryView::finishItemHeightAnimation()
{
    if (m_animatingItem < 0)
        return;
    const qsizetype item = m_animatingItem;
    const int target = m_animationTargetHeight;
    m_animatingItem = -1;
    if (m_heightAnimation && m_heightAnimation->state() != QAbstractAnimation::Stopped)
        m_heightAnimation->stop();
    if (item < 0 || item >= viewItemCount())
        return;
    const ScrollAnchor anchor = captureAnchor();
    applyItemHeight(item, target);
    reflow(anchor);
}

int VirtualMasonryView::itemColumn(const QModelIndex &index) const
{
    return m_masonry->itemColumn(viewItemForIndex(index));
}

QRect VirtualMasonryView::visualRect(const QModelIndex &index) const
{
    const qsizetype item = viewItemForIndex(index);
    return item < 0 ? QRect() : m_masonry->itemRect(item, verticalOffset());
}

QModelIndex VirtualMasonryView::indexAt(const QPoint &position) const
{
    const qsizetype item = m_masonry->itemAtPoint(position, verticalOffset());
    return item < 0 ? QModelIndex() : viewIndex(item);
}

void VirtualMasonryView::setVisualStateBackgroundVisible(bool visible)
{
    if (m_backgroundVisible == visible)
        return;
    m_backgroundVisible = visible;
    viewport()->update();
}

void VirtualMasonryView::setStateBackgroundCornerRadius(int pixels)
{
    const int radius = qMax(0, pixels);
    if (m_cornerRadius == radius)
        return;
    m_cornerRadius = radius;
    viewport()->update();
}

void VirtualMasonryView::setHoverBackgroundThroughRowSpacing(bool enabled)
{
    if (m_hoverThroughRows == enabled)
        return;
    m_hoverThroughRows = enabled;
    viewport()->update();
}

void VirtualMasonryView::setSelectedBackgroundThroughRowSpacing(bool enabled)
{
    if (m_selectedThroughRows == enabled)
        return;
    m_selectedThroughRows = enabled;
    viewport()->update();
}

void VirtualMasonryView::setHoverBackgroundThroughColumnSpacing(bool enabled)
{
    if (m_hoverThroughColumns == enabled)
        return;
    m_hoverThroughColumns = enabled;
    viewport()->update();
}

void VirtualMasonryView::setSelectedBackgroundThroughColumnSpacing(bool enabled)
{
    if (m_selectedThroughColumns == enabled)
        return;
    m_selectedThroughColumns = enabled;
    viewport()->update();
}

void VirtualMasonryView::setVerticalGridLinesVisible(bool visible)
{
    if (m_verticalLines == visible)
        return;
    m_verticalLines = visible;
    m_gridOverlay->update();
}

void VirtualMasonryView::setHorizontalGridLinesVisible(bool visible)
{
    if (m_horizontalLines == visible)
        return;
    m_horizontalLines = visible;
    m_gridOverlay->update();
}

void VirtualMasonryView::setVerticalGridLineWidth(int pixels)
{
    const int width = qMax(1, pixels);
    if (m_verticalLineWidth == width)
        return;
    m_verticalLineWidth = width;
    m_gridOverlay->update();
}

void VirtualMasonryView::setHorizontalGridLineWidth(int pixels)
{
    const int width = qMax(1, pixels);
    if (m_horizontalLineWidth == width)
        return;
    m_horizontalLineWidth = width;
    m_gridOverlay->update();
}

void VirtualMasonryView::setVerticalGridLineColor(const QColor &color)
{
    if (m_verticalLineColor == color)
        return;
    m_verticalLineColor = color;
    m_gridOverlay->update();
}

void VirtualMasonryView::setHorizontalGridLineColor(const QColor &color)
{
    if (m_horizontalLineColor == color)
        return;
    m_horizontalLineColor = color;
    m_gridOverlay->update();
}

void VirtualMasonryView::setVerticalSpacingLineThroughRowSpacing(bool enabled)
{
    if (m_verticalThroughRows == enabled)
        return;
    m_verticalThroughRows = enabled;
    m_gridOverlay->update();
}

void VirtualMasonryView::setHorizontalSpacingLineThroughColumnSpacing(bool enabled)
{
    if (m_horizontalThroughColumns == enabled)
        return;
    m_horizontalThroughColumns = enabled;
    m_gridOverlay->update();
}

QVector<qsizetype> VirtualMasonryView::materializationItems(const QVector<VisibleRange> &ranges) const
{
    QVector<qsizetype> items = m_masonry->itemsInVerticalRange(
        verticalOffset(), verticalOffset() + viewportMainExtent() - 1,
        overscanBefore(), overscanAfter());
    Q_UNUSED(ranges);
    for (const QPersistentModelIndex &index : explicitPinnedIndexes()) {
        const qsizetype item = viewItemForIndex(index);
        if (item >= 0)
            items.append(item);
    }
    const qsizetype current = viewItemForIndex(currentIndex());
    if (current >= 0 && isItemPinned(currentIndex()))
        items.append(current);
    const qsizetype dragSource = viewItemForIndex(dragSourceIndex());
    if (dragSource >= 0)
        items.append(dragSource);
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    return items;
}

void VirtualMasonryView::resizeEvent(QResizeEvent *event)
{
    const ScrollAnchor anchor = captureAnchor();
    VirtualItemView::resizeEvent(event);
    if (m_masonry->crossExtent() != viewport()->width()) {
        m_masonry->setCrossExtent(viewport()->width());
        reflow(anchor);
    }
}

void VirtualMasonryView::paintEvent(QPaintEvent *event)
{
    VirtualItemView::paintEvent(event);
    if (m_backgroundVisible) {
        QPainter painter(viewport());
        paintVisualStateBackgrounds(&painter, event->rect());
    }
}

void VirtualMasonryView::afterMaterialize()
{
    m_gridOverlay->setGeometry(viewport()->rect());
    m_gridOverlay->show();
    m_gridOverlay->raise();
    m_gridOverlay->update();
}

void VirtualMasonryView::paintGridLines(QPainter *painter) const
{
    QVector<QRect> horizontalTrailing;
    QVector<QRect> horizontalLeading;
    QVector<QRect> verticalTrailing;
    QVector<QRect> verticalLeading;
    const auto colorFor = [this](const QColor &color) {
        return color.isValid() ? color : palette().color(QPalette::Mid);
    };

    for (const MaterializedItem &materialized : materializedItems()) {
        const qsizetype item = viewItemForIndex(materialized.index);
        if (item < 0)
            continue;
        const QRect rect = m_masonry->itemRect(item, verticalOffset());
        if (rect.isEmpty())
            continue;
        const int column = m_masonry->itemColumn(item);
        if (column < 0)
            continue;
        const qsizetype next = m_masonry->nextItemInColumn(item);
        const QRect nextRect = next >= 0 ? m_masonry->itemRect(next, verticalOffset()) : QRect();
        const bool hasLeftColumn = column > 0;
        const bool hasRightColumn = column + 1 < m_masonry->columnCount();

        QRect verticalSegment = rect;
        if (m_verticalThroughRows && !nextRect.isEmpty() && nextRect.top() > rect.bottom())
            verticalSegment.setBottom(nextRect.top() - 1);
        if (m_verticalLines) {
            if (hasRightColumn)
                verticalTrailing.append(verticalSegment);
            if (hasLeftColumn && m_masonry->columnSpacing() > 0)
                verticalLeading.append(verticalSegment);
        }

        if (next < 0 || !m_horizontalLines)
            continue;
        const bool throughColumns = m_horizontalThroughColumns && hasRightColumn;
        const int segmentRight = throughColumns
            ? m_masonry->columnX(column + 1) - 1 : rect.right();
        QRect horizontalSegment(rect.x(), rect.y(),
                                qMax(0, segmentRight - rect.x() + 1), rect.height());
        if (horizontalSegment.width() <= 0)
            continue;
        horizontalTrailing.append(horizontalSegment);
        if (m_masonry->rowSpacing() > 0) {
            const int nextColumn = m_masonry->itemColumn(next);
            const bool nextHasRightColumn = nextColumn + 1 < m_masonry->columnCount();
            const int nextRight = m_horizontalThroughColumns && nextHasRightColumn
                ? m_masonry->columnX(nextColumn + 1) - 1 : nextRect.right();
            horizontalLeading.append(QRect(nextRect.x(), nextRect.y(),
                                           qMax(0, nextRight - nextRect.x() + 1),
                                           nextRect.height()));
        }
    }

    if (m_horizontalLines) {
        fillPixelLines(painter, horizontalTrailing, m_horizontalLineWidth,
                       colorFor(m_horizontalLineColor), Qt::Horizontal, true);
        fillPixelLines(painter, horizontalLeading, m_horizontalLineWidth,
                       colorFor(m_horizontalLineColor), Qt::Horizontal);
    }
    if (m_verticalLines) {
        fillPixelLines(painter, verticalTrailing, m_verticalLineWidth,
                       colorFor(m_verticalLineColor), Qt::Vertical, true);
        fillPixelLines(painter, verticalLeading, m_verticalLineWidth,
                       colorFor(m_verticalLineColor), Qt::Vertical);
    }
}

void VirtualMasonryView::paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty) const
{
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype ordinal = viewItemForIndex(item.index);
        if (ordinal < 0)
            continue;
        const QRect rect = m_masonry->itemRect(ordinal, verticalOffset());
        const qsizetype next = m_masonry->nextItemInColumn(ordinal);
        const QRect nextRect = next >= 0 ? m_masonry->itemRect(next, verticalOffset()) : QRect();
        const VisualState state = visualState(item.index);
        const auto paint = [&](bool throughRows, bool throughColumns, qreal opacity,
                               const QColor &color) {
            if (opacity <= 0)
                return;
            QRect extended = rect;
            if (throughColumns && m_masonry->itemColumn(ordinal) + 1 < m_masonry->columnCount())
                extended.setRight(extended.right() + m_masonry->columnSpacing());
            if (throughRows && !nextRect.isEmpty())
                extended.setBottom(nextRect.top() - 1);
            const QRegion clip(extended.intersected(viewport()->rect()).intersected(dirty));
            if (clip.isEmpty())
                return;
            painter->save();
            painter->setOpacity(opacity);
            fillPixelAlignedRoundedRegion(painter, extended, clip, m_cornerRadius, color);
            painter->restore();
        };
        paint(m_hoverThroughRows, m_hoverThroughColumns, state.hoverProgress,
              hoverBackgroundColor());
        paint(m_selectedThroughRows, m_selectedThroughColumns, state.selectedProgress,
              selectedBackgroundColor());
    }
}

bool VirtualMasonryView::handleItemKeyPress(QKeyEvent *event)
{
    const qsizetype count = viewItemCount();
    if (count <= 0)
        return false;
    const QModelIndex current = currentIndex();
    const qsizetype item = viewItemForIndex(current);
    if (item < 0) {
        if (event->key() == Qt::Key_Home)
            moveCurrentToItem(0, event->modifiers());
        else if (event->key() == Qt::Key_End)
            moveCurrentToItem(count - 1, event->modifiers());
        else if (event->key() == Qt::Key_PageUp)
            moveCurrentToItem(0, event->modifiers());
        else if (event->key() == Qt::Key_PageDown)
            moveCurrentToItem(count - 1, event->modifiers());
        else
            return false;
        return true;
    }
    qsizetype target = -1;
    switch (event->key()) {
    case Qt::Key_Left:
        target = m_masonry->nearestHorizontal(item, false);
        break;
    case Qt::Key_Right:
        target = m_masonry->nearestHorizontal(item, true);
        break;
    case Qt::Key_Up:
        target = m_masonry->nearestVertical(item, false);
        break;
    case Qt::Key_Down:
        target = m_masonry->nearestVertical(item, true);
        break;
    case Qt::Key_PageUp:
        target = m_masonry->nearestByVerticalOffset(item,
                                                    -qMax<qint64>(1, viewportMainExtent()));
        break;
    case Qt::Key_PageDown:
        target = m_masonry->nearestByVerticalOffset(item,
                                                    qMax<qint64>(1, viewportMainExtent()));
        break;
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = count - 1;
        break;
    default:
        return false;
    }
    if (target < 0 || target >= count)
        return true;
    moveCurrentToItem(target, event->modifiers());
    return true;
}

VirtualItemView::DropTarget VirtualMasonryView::resolveDropTarget(const QPoint &position) const
{
    DropTarget target;
    if (!model() || !viewport()->rect().contains(position))
        return target;
    const qsizetype count = viewItemCount();
    if (count <= 0) {
        target.parent = rootIndex();
        target.row = 0;
        target.trailing = true;
        return target;
    }

    const qsizetype hit = m_masonry->itemAtPoint(position, verticalOffset());
    if (hit >= 0) {
        const QRect rect = m_masonry->itemRect(hit, verticalOffset());
        target.parent = rootIndex();
        target.row = int(hit + (position.y() > rect.center().y() ? 1 : 0));
        target.trailing = target.row == count;
        return target;
    }

    const qint64 logicalY = qint64(position.y()) + verticalOffset();
    int column = -1;
    for (int candidate = 0; candidate < m_masonry->columnCount(); ++candidate) {
        const qint64 left = m_masonry->columnX(candidate);
        const qint64 width = m_masonry->columnWidth(candidate);
        if (qint64(position.x()) >= left && qint64(position.x()) < left + width) {
            column = candidate;
            break;
        }
    }
    const qsizetype before = m_masonry->itemBeforeOffset(logicalY, column);
    const qsizetype after = m_masonry->itemAfterOffset(logicalY, column);
    const qsizetype insertion = after >= 0 ? after : before >= 0 ? before + 1 : count;
    target.parent = rootIndex();
    target.row = int(qBound<qsizetype>(0, insertion, count));
    target.trailing = target.row == count;
    return target;
}

QRect VirtualMasonryView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || target.parent != rootIndex() || viewItemCount() <= 0)
        return target.isValid() && target.row == 0
            ? QRect(contentMargins().left(), contentMargins().top(),
                   qMax(1, viewport()->width() - contentMargins().left()
                            - contentMargins().right()), 2).intersected(viewport()->rect())
            : QRect();
    const bool trailing = target.row >= viewItemCount();
    const qsizetype item = trailing ? viewItemCount() - 1 : target.row;
    const QRect itemRect = m_masonry->itemRect(item, verticalOffset());
    if (itemRect.isEmpty())
        return {};
    const int y = trailing ? itemRect.bottom() : itemRect.top() - 1;
    return QRect(itemRect.x(), y, itemRect.width(), 2).intersected(viewport()->rect());
}

void VirtualMasonryView::saveItemSizes()
{
    m_sizeChangeAnchor = captureAnchor();
    m_savedItemSizes.clear();
    for (qsizetype item = 0; item < m_masonry->itemCount(); ++item) {
        const QSize estimate = m_masonry->estimatedSize(item);
        const int height = m_masonry->explicitItemHeight(item);
        if (estimate != QSize() || height > 0)
            m_savedItemSizes.append({QPersistentModelIndex(viewIndex(item)), estimate, height});
    }
}

void VirtualMasonryView::restoreItemSizes()
{
    const auto saved = std::exchange(m_savedItemSizes, {});
    if (saved.isEmpty())
        return;
    QVector<QSize> estimates;
    estimates.resize(int(viewItemCount()));
    QVector<int> heights(estimates.size(), 0);
    for (const SavedItemSize &size : saved) {
        const qsizetype item = viewItemForIndex(size.index);
        if (item >= 0 && item < estimates.size()) {
            estimates[int(item)] = size.estimate;
            heights[int(item)] = size.height;
        }
    }
    m_masonry->restoreItemSizes(estimates, heights);
}

void VirtualMasonryView::refreshAdapterEstimates(const ScrollAnchor &preservedAnchor)
{
    if (itemHeightMode() != ItemHeightMode::AdapterEstimate || !adapter() || !model())
        return;
    const QPointer<VirtualMasonryView> self(this);
    finishItemHeightAnimation();
    if (!self || itemHeightMode() != ItemHeightMode::AdapterEstimate || !adapter() || !model())
        return;
    const QPointer<QAbstractItemModel> activeModel(model());
    WidgetAdapter *const activeAdapter = adapter();
    const quint64 revision = m_revision;
    const qsizetype count = viewItemCount();
    const ScrollAnchor anchor = preservedAnchor.isValid() ? preservedAnchor : captureAnchor();
    m_pendingItemSizes.clear();
    QVector<QSize> estimates;
    estimates.reserve(int(count));
    for (qsizetype row = 0; row < count; ++row) {
        const QSize size = activeAdapter->estimatedSize(viewIndex(row));
        if (!self || !activeModel || model() != activeModel.data()
            || adapter() != activeAdapter || revision != m_revision)
            return;
        estimates.append(size);
    }
    m_masonry->setEstimatedSizes(estimates);
    reflow(anchor);
}

void VirtualMasonryView::refreshAdapterEstimateRange(const QModelIndex &topLeft,
                                                     const QModelIndex &bottomRight)
{
    if (itemHeightMode() != ItemHeightMode::AdapterEstimate || !adapter() || !model()
        || topLeft.parent() != m_root || bottomRight.parent() != m_root)
        return;
    const int first = qMax(0, topLeft.row());
    const int last = qMin(int(viewItemCount()) - 1, bottomRight.row());
    if (first > last)
        return;
    for (int row = first; row <= last; ++row)
        invalidateItemSize(model()->index(row, 0, m_root));
}

void VirtualMasonryView::connectEstimateSignals(QAbstractItemModel *newModel)
{
    for (const auto &connection : m_estimateConnections)
        disconnect(connection);
    m_estimateConnections.clear();
    if (!newModel)
        return;
    const auto refresh = [this](const QModelIndex &parent) {
        if (isLayoutParent(parent))
            refreshAdapterEstimates(std::exchange(m_sizeChangeAnchor, {}));
    };
    m_estimateConnections.append(connect(newModel, &QAbstractItemModel::rowsInserted,
        this, [refresh](const QModelIndex &parent, int, int) { refresh(parent); }));
    m_estimateConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeInserted,
        this, [this](const QModelIndex &, int, int) { finishItemHeightAnimation(); }));
    m_estimateConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeRemoved,
        this, [this](const QModelIndex &, int, int) { finishItemHeightAnimation(); }));
    m_estimateConnections.append(connect(newModel, &QAbstractItemModel::rowsRemoved,
        this, [refresh](const QModelIndex &parent, int, int) { refresh(parent); }));
    m_estimateConnections.append(connect(newModel, &QAbstractItemModel::modelReset,
        this, [this]() { refreshAdapterEstimates(); }));
    m_estimateConnections.append(connect(newModel, &QAbstractItemModel::dataChanged,
        this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                    const QVector<int> &) {
            refreshAdapterEstimateRange(topLeft, bottomRight);
        }));
}

} // namespace viv
