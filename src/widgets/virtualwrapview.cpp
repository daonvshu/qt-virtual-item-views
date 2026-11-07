#include <virtualitemviews/virtualwrapview.h>
#include <virtualitemviews/widgetadapter.h>
#include "../layout/wrapitemlayout_p.h"
#include "../core/performanceprobe_p.h"
#include "../core/pixelalignedlines_p.h"
#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>

namespace viv {

VirtualWrapView::VirtualWrapView(QWidget *parent) : VirtualItemView(parent)
{
    m_wrap = new WrapItemLayout;
    auto settings = m_wrap->settings();
    settings.direction = layoutDirection();
    m_wrap->setSettings(settings);
    setAutoMeasureItemHeight(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLayoutPolicy(m_wrap, true);
    VirtualItemView::setRowSpacing(settings.rowSpacing);
}
qsizetype VirtualWrapView::viewItemCount() const
{
    return model() && model()->columnCount(m_root) > 0 ? model()->rowCount(m_root) : 0;
}
QModelIndex VirtualWrapView::viewIndex(qsizetype item, int column) const
{
    return model() && column == 0 && item >= 0 && item < viewItemCount()
        ? model()->index(int(item), 0, m_root) : QModelIndex();
}
qsizetype VirtualWrapView::viewItemForIndex(const QModelIndex &index) const
{
    return index.isValid() && index.model() == model() && index.parent() == m_root
        && index.column() == 0 ? index.row() : -1;
}
bool VirtualWrapView::isLayoutParent(const QModelIndex &parent) const { return parent == m_root; }
bool VirtualWrapView::isPersistentRowStateValid(const QModelIndex &index) const
{
    return viewItemForIndex(index) >= 0;
}
int VirtualWrapView::estimateItemSize(qsizetype) const { return fixedItemHeight(); }
void VirtualWrapView::applyRowSpacingOverrides()
{
    rekeyWidths();
    refreshWidths(0, int(viewItemCount()) - 1);
}
void VirtualWrapView::rekeyWidths()
{
    // A persistent index changes its hash after a model changes row coordinates.
    QHash<QPersistentModelIndex, int> valid;
    for (auto it = m_widthOverrides.cbegin(); it != m_widthOverrides.cend(); ++it)
        if (isPersistentRowStateValid(it.key())) valid.insert(it.key(), it.value());
    m_widthOverrides.swap(valid);
}
void VirtualWrapView::setModel(QAbstractItemModel *newModel)
{
    if (model() == newModel) return;
    m_root = QModelIndex();
    m_hadRoot = false;
    m_widthOverrides.clear();
    ++m_revision;
    const QPointer<VirtualWrapView> self(this);
    const QPointer<QAbstractItemModel> requested(newModel);
    const quint64 expectedSerial = modelChangeSerial() + 1;
    VirtualItemView::setModel(newModel);
    if (!self || !requested || model() != requested || modelChangeSerial() != expectedSerial) return;
    const quint64 serial = modelChangeSerial();
    const auto current = [this, requested, serial]() {
        return requested && model() == requested && serial == modelChangeSerial();
    };
    const auto changed = [this, current]() {
        if (!current()) return;
        ++m_revision;
        rekeyWidths();
        rekeyPersistentState();
        markDirty();
    };
    connect(newModel, &QAbstractItemModel::rowsInserted, this,
        [this, changed, current](const QModelIndex &parent, int first, int last) {
            if (!current() || !isLayoutParent(parent)) return;
            changed();
            refreshWidths(first, last);
        });
    connect(newModel, &QAbstractItemModel::rowsRemoved, this, [this, changed, current]() {
        if (!current()) return;
        changed();
        if (m_hadRoot && !m_root.isValid()) {
            m_hadRoot = false;
            cancelPendingAnchor();
            resetLayoutForNewModel();
        }
    });
    connect(newModel, &QAbstractItemModel::rowsAboutToBeMoved, this,
        [this, current]() { if (current()) setPendingAnchor(captureAnchor()); });
    connect(newModel, &QAbstractItemModel::rowsMoved, this,
        [this, changed, current](const QModelIndex &source, int, int, const QModelIndex &destination, int) {
            if (!current()) return;
            changed();
            if (source != destination) refreshWidths(0, int(viewItemCount()) - 1);
        });
    connect(newModel, &QAbstractItemModel::layoutChanged, this, changed);
    connect(newModel, &QAbstractItemModel::modelReset, this, [this, changed, current]() {
        if (!current()) return;
        m_hadRoot = false;
        changed();
    });
    connect(newModel, &QAbstractItemModel::dataChanged, this,
        [this, current](const QModelIndex &first, const QModelIndex &last) {
            if (!current() || !isLayoutParent(first.parent()) || first.column() > 0) return;
            ++m_revision;
            refreshWidths(first.row(), last.row());
        });
    const auto columnsChanging = [this, current](const QModelIndex &parent, int first, int) {
        if (current() && parent.isValid() && isLayoutParent(parent) && first == 0)
            recycleItemsForColumnChange();
    };
    connect(newModel, &QAbstractItemModel::columnsAboutToBeInserted, this, columnsChanging);
    connect(newModel, &QAbstractItemModel::columnsAboutToBeRemoved, this, columnsChanging);
    connect(newModel, &QAbstractItemModel::columnsAboutToBeMoved, this,
        [this, current](const QModelIndex &source, int first, int, const QModelIndex &destination, int column) {
            if (current() && ((source.isValid() && isLayoutParent(source) && first == 0)
                || (destination.isValid() && isLayoutParent(destination) && column == 0)))
                recycleItemsForColumnChange();
        });
    const auto columnsChanged = [this, changed, current]() {
        if (!current()) return;
        changed();
        restoreItemsAfterColumnChange();
        resetLayoutForNewModel();
    };
    connect(newModel, &QAbstractItemModel::columnsInserted, this, columnsChanged);
    connect(newModel, &QAbstractItemModel::columnsRemoved, this, columnsChanged);
    connect(newModel, &QAbstractItemModel::columnsMoved, this, columnsChanged);
    connect(newModel, &QObject::destroyed, this, [this, serial]() {
        if (serial != modelChangeSerial()) return;
        m_root = QModelIndex();
        m_hadRoot = false;
        m_widthOverrides.clear();
        ++m_revision;
        markDirty();
    });
}
void VirtualWrapView::setAdapter(WidgetAdapter *newAdapter, bool takeOwnership)
{
    const QPointer<VirtualWrapView> self(this);
    if (!installAdapter(newAdapter, takeOwnership, false) || !self) return;
    invalidateItemWidths();
}
void VirtualWrapView::setRootIndex(const QModelIndex &index)
{
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualWrapView: root belongs to another model");
        return;
    }
    if (index == m_root) return;
    const QPointer<VirtualWrapView> self(this);
    m_root = index;
    m_hadRoot = index.isValid();
    m_widthOverrides.clear();
    ++m_revision;
    cancelPendingAnchor();
    recycleAllItems();
    if (!self) return;
    resetLayoutForNewModel();
    if (self) markDirty();
}
void VirtualWrapView::refreshWidths(int first, int last)
{
    if (m_refreshingWidths) {
        if (!m_widthRefreshPending) {
            m_widthRefreshPending = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_widthRefreshPending = false;
                invalidateItemWidths();
            }, Qt::QueuedConnection);
        }
        return;
    }
    first = qMax(0, first);
    last = qMin(last, int(viewItemCount()) - 1);
    if (first > last) return;
    detail::PerformanceScope performance(this, "wrap.estimate-widths");
    const QPointer<VirtualWrapView> self(this);
    const QPointer<QAbstractItemModel> activeModel(model());
    WidgetAdapter *const activeAdapter = adapter();
    const quint64 revision = m_revision;
    const quint64 serial = modelChangeSerial();
    const qsizetype count = viewItemCount();
    const auto valid = [&]() {
        return self && activeModel && model() == activeModel && adapter() == activeAdapter
            && revision == m_revision && serial == modelChangeSerial() && viewItemCount() == count;
    };
    m_refreshingWidths = true;
    QVector<int> widths;
    widths.reserve(last - first + 1);
    for (int row = first; row <= last; ++row) {
        const QPersistentModelIndex index(viewIndex(row));
        if (!valid()) break;
        int width = m_widthOverrides.value(index, 0);
        if (width <= 0) {
            width = index.data(Qt::SizeHintRole).toSize().width();
            if (!valid()) break;
        }
        if (width <= 0 && activeAdapter) {
            const WidgetType type = activeAdapter->widgetType(index);
            if (!valid()) break;
            width = detail::performanceCall(this, "callback.wrap.estimated-size", [&]() {
                return activeAdapter->estimatedSize(type, index).width();
            });
            if (!valid()) break;
        }
        widths.append(width > 0 ? width : 100);
    }
    if (!self) return;
    m_refreshingWidths = false;
    if (!valid()) {
        if (!m_widthRefreshPending) {
            m_widthRefreshPending = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_widthRefreshPending = false;
                invalidateItemWidths();
            }, Qt::QueuedConnection);
        }
        return;
    }
    m_wrap->setWidths(first, widths);
    performance.count("wrap.widths", widths.size());
    markDirty();
}
void VirtualWrapView::invalidateItemWidths()
{
    const ScrollAnchor anchor = captureAnchor();
    ++m_revision;
    const QPointer<VirtualWrapView> self(this);
    refreshWidths(0, int(viewItemCount()) - 1);
    if (self) reflow(anchor);
}
void VirtualWrapView::reflow(const ScrollAnchor &anchor)
{
    ++m_revision;
    setPendingAnchor(anchor);
    markDirty();
    viewport()->update();
}
void VirtualWrapView::setItemWidth(const QModelIndex &index, int width)
{
    setItemWidths({qMakePair(index, width)});
}
void VirtualWrapView::setItemWidths(const QVector<QPair<QModelIndex, int>> &widths)
{
    const ScrollAnchor anchor = captureAnchor();
    bool changed = false;
    for (const auto &entry : widths) {
        const qsizetype row = viewItemForIndex(entry.first);
        const int width = qMax(1, entry.second);
        if (row < 0 || m_widthOverrides.value(entry.first, 0) == width) continue;
        m_widthOverrides.insert(entry.first, width);
        m_wrap->setWidths(int(row), {width});
        changed = true;
    }
    if (changed) reflow(anchor);
}
void VirtualWrapView::clearItemWidth(const QModelIndex &index)
{
    if (viewItemForIndex(index) < 0 || !m_widthOverrides.remove(index)) return;
    const ScrollAnchor anchor = captureAnchor();
    const QPointer<VirtualWrapView> self(this);
    refreshWidths(index.row(), index.row());
    if (self) reflow(anchor);
}
void VirtualWrapView::clearItemWidths()
{
    if (m_widthOverrides.isEmpty()) return;
    m_widthOverrides.clear();
    invalidateItemWidths();
}
void VirtualWrapView::setFixedItemHeight(int height)
{
    height = qMax(1, height);
    if (height == fixedItemHeight()) return;
    const ScrollAnchor anchor = captureAnchor();
    auto settings = m_wrap->settings();
    settings.height = height;
    m_wrap->setSettings(settings);
    reflow(anchor);
}
int VirtualWrapView::fixedItemHeight() const { return m_wrap->settings().height; }
void VirtualWrapView::setItemWidthRange(int minimum, int maximum)
{
    minimum = qMax(1, minimum);
    maximum = qMax(minimum, maximum);
    if (minimum == minimumItemWidth() && maximum == maximumItemWidth()) return;
    const ScrollAnchor anchor = captureAnchor();
    auto settings = m_wrap->settings();
    settings.minimumWidth = minimum;
    settings.maximumWidth = maximum;
    m_wrap->setSettings(settings);
    reflow(anchor);
}
int VirtualWrapView::minimumItemWidth() const { return m_wrap->settings().minimumWidth; }
int VirtualWrapView::maximumItemWidth() const { return m_wrap->settings().maximumWidth; }
void VirtualWrapView::setItemSpacing(int pixels)
{
    pixels = qMax(0, pixels);
    if (pixels == itemSpacing()) return;
    const ScrollAnchor anchor = captureAnchor();
    auto settings = m_wrap->settings();
    settings.itemSpacing = pixels;
    m_wrap->setSettings(settings);
    reflow(anchor);
}
int VirtualWrapView::itemSpacing() const { return m_wrap->settings().itemSpacing; }
void VirtualWrapView::setRowSpacing(int pixels)
{
    pixels = qMax(0, pixels);
    if (pixels == m_wrap->settings().rowSpacing) return;
    const ScrollAnchor anchor = captureAnchor();
    auto settings = m_wrap->settings();
    settings.rowSpacing = pixels;
    m_wrap->setSettings(settings);
    setPendingAnchor(anchor);
    ++m_revision;
    VirtualItemView::setRowSpacing(pixels);
}
void VirtualWrapView::setContentMargins(const QMargins &margins)
{
    const QMargins normalized(qMax(0, margins.left()), qMax(0, margins.top()),
                              qMax(0, margins.right()), qMax(0, margins.bottom()));
    if (normalized == contentMargins()) return;
    const ScrollAnchor anchor = captureAnchor();
    auto settings = m_wrap->settings();
    settings.margins = normalized;
    m_wrap->setSettings(settings);
    reflow(anchor);
}
QMargins VirtualWrapView::contentMargins() const { return m_wrap->settings().margins; }
void VirtualWrapView::setLineAlignment(Qt::Alignment alignment)
{
    if (alignment == lineAlignment()) return;
    const ScrollAnchor anchor = captureAnchor();
    auto settings = m_wrap->settings();
    settings.alignment = alignment;
    m_wrap->setSettings(settings);
    reflow(anchor);
}
Qt::Alignment VirtualWrapView::lineAlignment() const { return m_wrap->settings().alignment; }
qsizetype VirtualWrapView::displayRowCount() const { return m_wrap->geometry().rows.size(); }
qsizetype VirtualWrapView::displayRowForIndex(const QModelIndex &index) const
{
    const qsizetype item = viewItemForIndex(index);
    return item >= 0 && item < m_wrap->itemCount() ? m_wrap->geometry().cells[int(item)].row : -1;
}
qsizetype VirtualWrapView::anchorItemAtOffset(qint64 offset) const
{
    return offset <= contentMargins().top() ? -1 : m_wrap->indexAtOffset(offset);
}
qint64 VirtualWrapView::autoHeightContentExtent() const
{
    return m_wrap->extentForWidth(maximumViewportSize().width());
}
void VirtualWrapView::afterMaterialize()
{
    // Heights come from the layout; never measure all child widgets.
    if (autoHeightEnabled()) {
        // Let Qt settle the viewport after the host height changes before
        // deciding whether a scrollbar still has to reserve horizontal space.
        if (!m_settlePending) {
            m_settlePending = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_settlePending = false;
                settleViewportWidth();
            }, Qt::QueuedConnection);
        }
    } else {
        settleViewportWidth();
    }
    if (m_reportedRevision != m_revision || m_reportedWidth != m_wrap->crossExtent()) {
        m_reportedRevision = m_revision;
        m_reportedWidth = m_wrap->crossExtent();
        emit wrapLayoutChanged();
    }
}
void VirtualWrapView::settleViewportWidth()
{
    if (verticalScrollBarPolicy() != Qt::ScrollBarAsNeeded) return;
    const QSize full = maximumViewportSize();
    if (full.width() <= m_wrap->crossExtent()
        || m_wrap->extentForWidth(full.width()) > full.height()) return;
    // A scrollbar can keep an otherwise fitting line wrapped, keeping itself
    // alive. Commit the fitting geometry once; the kernel publishes a zero
    // scroll range and Qt returns the width to the viewport. Forced policies
    // remain under the caller's control.
    const ScrollAnchor anchor = captureAnchor();
    m_wrap->setCrossExtent(full.width());
    reflow(anchor);
}
void VirtualWrapView::resizeEvent(QResizeEvent *event)
{
    const ScrollAnchor anchor = captureAnchor();
    setPendingAnchor(anchor);
    VirtualItemView::resizeEvent(event);
}
void VirtualWrapView::changeEvent(QEvent *event)
{
    VirtualItemView::changeEvent(event);
    if (event->type() == QEvent::LayoutDirectionChange && m_wrap) {
        const ScrollAnchor anchor = captureAnchor();
        auto settings = m_wrap->settings();
        settings.direction = layoutDirection();
        m_wrap->setSettings(settings);
        reflow(anchor);
    }
}
void VirtualWrapView::setVisualStateBackgroundVisible(bool visible)
{
    m_statesVisible = visible;
    viewport()->update();
}
void VirtualWrapView::setStateBackgroundCornerRadius(int pixels)
{
    m_radius = qMax(0, pixels);
    viewport()->update();
}
void VirtualWrapView::refreshVisualStates()
{
    const QPointer<VirtualWrapView> self(this);
    VirtualItemView::refreshVisualStates();
    if (self) viewport()->update();
}
void VirtualWrapView::refreshVisualState(const QModelIndex &index)
{
    const QPointer<VirtualWrapView> self(this);
    VirtualItemView::refreshVisualState(index);
    if (self) viewport()->update();
}
void VirtualWrapView::paintEvent(QPaintEvent *event)
{
    detail::PerformanceScope performance(this, "paint.wrap-state", detail::PerformanceDomain::Paint);
    VirtualItemView::paintEvent(event);
    if (!m_statesVisible) return;
    QPainter painter(viewport());
    for (const MaterializedItem &item : materializedItems()) {
        const QRect rect = visualRect(item.index);
        const QRegion clip(rect.intersected(viewport()->rect()).intersected(event->rect()));
        if (clip.isEmpty()) continue;
        const VisualState state = visualState(item.index);
        painter.setOpacity(state.hoverProgress);
        fillPixelAlignedRoundedRegion(&painter, rect, clip, m_radius, hoverBackgroundColor());
        painter.setOpacity(state.selectedProgress);
        fillPixelAlignedRoundedRegion(&painter, rect, clip, m_radius, selectedBackgroundColor());
    }
}
bool VirtualWrapView::handleItemKeyPress(QKeyEvent *event)
{
    const auto &g = m_wrap->geometry();
    const qsizetype currentItem = viewItemForIndex(currentIndex());
    const int current = currentItem >= 0 && currentItem < g.cells.size() ? int(currentItem) : -1;
    int target = current;
    const bool rtl = layoutDirection() == Qt::RightToLeft;
    const auto vertical = [&](int delta) {
        if (current < 0) return delta < 0 ? int(viewItemCount()) - 1 : 0;
        const int row = qBound(0, g.cells[current].row + delta, int(g.rows.size()) - 1);
        const auto &line = g.rows[row];
        const int center = g.cells[current].x + g.cells[current].width / 2;
        int closest = line.first;
        qint64 distance = std::numeric_limits<qint64>::max();
        for (int i = line.first; i < line.first + line.count; ++i) {
            const qint64 d = qAbs(qint64(g.cells[i].x) + g.cells[i].width / 2 - center);
            if (d < distance) { distance = d; closest = i; }
        }
        return closest;
    };
    const int page = int(qMax<qint64>(1, viewport()->height() / g.stride()));
    switch (event->key()) {
    case Qt::Key_Left: target = current < 0 ? 0 : current + (rtl ? 1 : -1); break;
    case Qt::Key_Right: target = current < 0 ? 0 : current + (rtl ? -1 : 1); break;
    case Qt::Key_Up: target = vertical(-1); break;
    case Qt::Key_Down: target = vertical(1); break;
    case Qt::Key_PageUp: target = vertical(-page); break;
    case Qt::Key_PageDown: target = vertical(page); break;
    default: return false;
    }
    if (viewItemCount() > 0) moveCurrentToItem(target, event->modifiers());
    return true;
}
QPixmap VirtualWrapView::grabDragPreview(QWidget *widget, const QRect &capture)
{
    if (!widget || (widget != viewport() && !viewport()->isAncestorOf(widget)))
        return VirtualItemView::grabDragPreview(widget, capture);
    const QRect area = capture.translated(widget->mapTo(viewport(), QPoint()));
    QRegion visible(area.intersected(viewport()->rect()));
    for (QWidget *ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
        const QPoint origin = ancestor->mapTo(viewport(), QPoint());
        visible &= QRect(origin, ancestor->size());
        if (!ancestor->mask().isEmpty()) visible &= ancestor->mask().translated(origin);
        if (ancestor == viewport()) break;
    }
    if (visible.isEmpty()) return {};
    const QPixmap pixmap = viewport()->grab(area);
    if (pixmap.isNull() || visible == QRegion(area)) return pixmap;
    // Preserve the capture origin and DPR so native-drag hotspot math stays valid.
    QPixmap clipped(pixmap.size());
    clipped.setDevicePixelRatio(pixmap.devicePixelRatio());
    clipped.fill(Qt::transparent);
    {
        QPainter painter(&clipped);
        painter.setClipRegion(visible.translated(-area.topLeft()));
        painter.drawPixmap(QPoint(), pixmap);
    }
    return clipped;
}
VirtualItemView::DropTarget VirtualWrapView::resolveDropTarget(const QPoint &position) const
{
    DropTarget target;
    if (!model() || !viewport()->rect().contains(position)
        || position.x() < contentMargins().left()
        || position.x() >= viewport()->width() - contentMargins().right()
        || qint64(position.y()) + verticalOffset() < contentMargins().top()) return target;
    const auto &g = m_wrap->geometry();
    const int row = g.rowAt(qint64(position.y()) + verticalOffset());
    int insertion = int(viewItemCount());
    if (row < g.rows.size()) {
        const auto &line = g.rows[row];
        insertion = line.first + line.count;
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        for (int i = line.first; i < line.first + line.count; ++i) {
            const int center = g.cells[i].x + g.cells[i].width / 2;
            if (rtl ? position.x() > center : position.x() < center) { insertion = i; break; }
        }
    }
    target.parent = m_root;
    target.row = insertion;
    target.trailing = insertion == viewItemCount();
    return target;
}
QRect VirtualWrapView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || target.parent != m_root || target.row > viewItemCount()) return {};
    const bool rtl = layoutDirection() == Qt::RightToLeft;
    if (!viewItemCount())
        return QRect(rtl ? viewport()->width() - contentMargins().right() - 2 : contentMargins().left(),
                     contentMargins().top(), 2, fixedItemHeight()).intersected(viewport()->rect());
    const bool trailing = target.row == viewItemCount();
    const QRect rect = visualRect(viewIndex(trailing ? target.row - 1 : target.row));
    const int x = trailing != rtl ? rect.right() + 1 : rect.left();
    return QRect(x - 1, rect.top(), 2, rect.height()).intersected(viewport()->rect());
}

} // namespace viv
