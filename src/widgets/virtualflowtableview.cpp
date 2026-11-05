#include <virtualitemviews/virtualflowtableview.h>
#include "../core/performanceprobe_p.h"
#include "../layout/flowtablelayout_p.h"
#include "../core/pixelalignedlines_p.h"

#include <QAbstractItemModel>
#include <QResizeEvent>
#include <QScrollBar>
#include <QKeyEvent>
#include <QPointer>
#include <QPaintEvent>
#include <QtMath>
#include <functional>

namespace viv {

namespace {

class FlowGridOverlay : public QWidget
{
public:
    explicit FlowGridOverlay(QWidget *parent, std::function<void(QPainter *)> paint)
        : QWidget(parent), m_paint(std::move(paint))
    {
        setObjectName(QStringLiteral("vivFlowGridOverlay"));
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

VirtualFlowTableView::VirtualFlowTableView(QWidget *parent) : VirtualItemView(parent)
{
    m_flow = new FlowTableLayout;
    m_flow->setOuterSpacingEnabled(gridLinePlacement() == GridLinePlacement::SpacingCenter);
    setAutoMeasureItemHeight(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridOverlay = new FlowGridOverlay(viewport(), [this](QPainter *painter) { paintGridLines(painter); });
    setLayoutPolicy(m_flow, true);
}

qsizetype VirtualFlowTableView::viewItemCount() const
{
    return model() ? model()->rowCount(m_root) : 0;
}

QModelIndex VirtualFlowTableView::viewIndex(qsizetype item, int column) const
{
    return model() && column == 0 && item >= 0 && item < viewItemCount()
        ? model()->index(int(item), 0, m_root) : QModelIndex();
}

qsizetype VirtualFlowTableView::viewItemForIndex(const QModelIndex &index) const
{
    return index.isValid() && index.model() == model() && index.parent() == m_root
        && index.column() == 0 ? index.row() : -1;
}

bool VirtualFlowTableView::isLayoutParent(const QModelIndex &parent) const { return parent == m_root; }

void VirtualFlowTableView::setModel(QAbstractItemModel *newModel)
{
    if (model() == newModel)
        return;
    const quint64 request = ++m_modelRequest;
    const QPointer<VirtualFlowTableView> self(this);
    const QPointer<QAbstractItemModel> requested(newModel);
    for (const auto &connection : m_modelConnections)
        disconnect(connection);
    m_modelConnections.clear();
    m_removalAnchor = {};
    m_root = QModelIndex();
    ++m_revision;
    VirtualItemView::setModel(newModel);
    // setModel can reenter through adapter/selection notifications.
    if (!self || request != m_modelRequest || !requested || model() != requested)
        return;
    const auto changed = [this, requested, request]() {
        if (!requested || model() != requested || request != m_modelRequest)
            return;
        ++m_revision;
        rekeyPersistentState();
        markDirty();
    };
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsInserted, this, changed));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsRemoved, this, [this, changed]() {
        changed();
        setPendingAnchor(m_removalAnchor);
        m_removalAnchor = {};
    }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeRemoved, this,
        [this](const QModelIndex &parent, int first, int last) {
            m_removalAnchor = {};
            if (!isLayoutParent(parent))
                return;
            ScrollAnchor anchor = captureAnchor();
            if (!anchor.isValid() || anchor.index.row() < first || anchor.index.row() > last)
                return;
            const qsizetype next = last + qint64(1) < viewItemCount() ? last + qint64(1) : first - qint64(1);
            anchor.index = viewIndex(next);
            m_removalAnchor = anchor;
        }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsAboutToBeMoved, this,
        [this]() { setPendingAnchor(captureAnchor()); }));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::rowsMoved, this, changed));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::layoutChanged, this, changed));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::modelReset, this, changed));
    m_modelConnections.append(connect(newModel, &QObject::destroyed, this, [this, request]() {
        if (request != m_modelRequest)
            return;
        m_root = QModelIndex();
        m_removalAnchor = {};
        ++m_revision;
        rekeyPersistentState();
        markDirty();
    }));
    const auto columnsChanging = [this](const QModelIndex &parent, int first, int) {
        if (parent.isValid() && isLayoutParent(parent) && first == 0)
            recycleItemsForColumnChange();
    };
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsAboutToBeInserted, this, columnsChanging));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsAboutToBeRemoved, this, columnsChanging));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsAboutToBeMoved, this,
        [this](const QModelIndex &source, int first, int, const QModelIndex &destination, int column) {
            if ((source.isValid() && isLayoutParent(source) && first == 0)
                || (destination.isValid() && isLayoutParent(destination) && column == 0))
                recycleItemsForColumnChange();
        }));
    const auto columnsChanged = [this, changed, requested, request]() {
        if (!requested || model() != requested || request != m_modelRequest)
            return;
        restoreItemsAfterColumnChange();
        changed();
    };
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsInserted, this, columnsChanged));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsRemoved, this, columnsChanged));
    m_modelConnections.append(connect(newModel, &QAbstractItemModel::columnsMoved, this, columnsChanged));
}

bool VirtualFlowTableView::isPersistentRowStateValid(const QModelIndex &index) const
{
    return viewItemForIndex(index) >= 0;
}

qsizetype VirtualFlowTableView::anchorItemAtOffset(qint64 offset) const
{
    // No item covers the leading margin. Anchoring its first item would scroll
    // the margin away when spacing, column count or viewport dimensions change.
    if (offset == 0 || offset < qint64(contentMargins().top()) + m_flow->outerSpacing().top())
        return -1;
    return m_flow->indexAtOffset(offset);
}

void VirtualFlowTableView::setRootIndex(const QModelIndex &index)
{
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualFlowTableView: root belongs to another model");
        return;
    }
    if (index == m_root)
        return;
    m_root = index;
    ++m_revision;
    cancelPendingAnchor();
    recycleAllItems();
    resetLayoutForNewModel();
    relayout();
}

void VirtualFlowTableView::reflow(const ScrollAnchor &anchor)
{
    detail::PerformanceScope performance(this, "flow.reflow");
    ++m_revision;
    setPendingAnchor(anchor);
    if (relayoutActive())
        markDirty();
    else
        relayout();
    viewport()->update();
}

void VirtualFlowTableView::setCellWidthRange(int minimum, int maximum)
{
    minimum = qMax(1, minimum);
    maximum = qMax(minimum, maximum);
    if (minimumCellWidth() == minimum && maximumCellWidth() == maximum)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setWidthRange(minimum, maximum);
    reflow(anchor);
}
int VirtualFlowTableView::minimumCellWidth() const { return m_flow->minimumWidth(); }
int VirtualFlowTableView::maximumCellWidth() const { return m_flow->maximumWidth(); }

void VirtualFlowTableView::setCellHeightMode(CellHeightMode mode)
{
    const bool proportional = mode == CellHeightMode::AspectRatio;
    if (m_flow->isProportional() == proportional)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setProportional(proportional);
    reflow(anchor);
}
VirtualFlowTableView::CellHeightMode VirtualFlowTableView::cellHeightMode() const
{
    return m_flow->isProportional() ? CellHeightMode::AspectRatio : CellHeightMode::Fixed;
}
void VirtualFlowTableView::setFixedCellHeight(int height)
{
    height = qMax(1, height);
    if (fixedCellHeight() == height)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setFixedHeight(height);
    reflow(anchor);
}
int VirtualFlowTableView::fixedCellHeight() const { return m_flow->fixedHeight(); }

void VirtualFlowTableView::setCellAspectRatio(qreal ratio)
{
    if (!qIsFinite(ratio) || ratio <= 0 || cellAspectRatio() == ratio)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setAspectRatio(ratio);
    reflow(anchor);
}
qreal VirtualFlowTableView::cellAspectRatio() const { return m_flow->aspectRatio(); }

void VirtualFlowTableView::setColumnSpacing(int pixels)
{
    pixels = qMax(0, pixels);
    if (columnSpacing() == pixels)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setSpacing(rowSpacing(), pixels);
    reflow(anchor);
}
int VirtualFlowTableView::columnSpacing() const { return m_flow->columnSpacing(); }

void VirtualFlowTableView::setRowSpacing(int pixels)
{
    pixels = qMax(0, pixels);
    if (rowSpacing() == pixels)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setSpacing(pixels, columnSpacing());
    setPendingAnchor(anchor);
    ++m_revision;
    VirtualItemView::setRowSpacing(pixels);
    viewport()->update();
}

void VirtualFlowTableView::setContentMargins(const QMargins &margins)
{
    const QMargins bounded(qMax(0, margins.left()), qMax(0, margins.top()),
                           qMax(0, margins.right()), qMax(0, margins.bottom()));
    if (contentMargins() == bounded)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setMargins(bounded);
    reflow(anchor);
}
QMargins VirtualFlowTableView::contentMargins() const { return m_flow->margins(); }

void VirtualFlowTableView::setGridLinePlacement(GridLinePlacement placement)
{
    if (gridLinePlacement() == placement)
        return;
    const ScrollAnchor anchor = captureAnchor();
    m_flow->setOuterSpacingEnabled(placement == GridLinePlacement::SpacingCenter);
    ++m_revision;
    setPendingAnchor(anchor);
    VirtualItemView::setGridLinePlacement(placement);
}

void VirtualFlowTableView::setSpacingBackgroundColor(const QColor &color)
{
    if (spacingBackgroundColor() == color)
        return;
    const QPointer<VirtualFlowTableView> self(this);
    VirtualItemView::setSpacingBackgroundColor(color);
    if (self)
        viewport()->update();
}

void VirtualFlowTableView::setVisualStateBackgroundVisible(bool visible)
{
    m_backgroundVisible = visible;
    viewport()->update();
}

void VirtualFlowTableView::setStateBackgroundCornerRadius(int pixels)
{
    m_cornerRadius = qMax(0, pixels);
    viewport()->update();
}

void VirtualFlowTableView::setVerticalGridLinesVisible(bool visible)
{
    m_verticalLines = visible;
    m_gridOverlay->update();
}

void VirtualFlowTableView::setHorizontalGridLinesVisible(bool visible)
{
    m_horizontalLines = visible;
    m_gridOverlay->update();
}

void VirtualFlowTableView::setVerticalGridLineWidth(int pixels)
{
    m_verticalLineWidth = qMax(1, pixels);
    m_gridOverlay->update();
}

void VirtualFlowTableView::setHorizontalGridLineWidth(int pixels)
{
    m_horizontalLineWidth = qMax(1, pixels);
    m_gridOverlay->update();
}

void VirtualFlowTableView::setVerticalGridLineColor(const QColor &color)
{
    m_verticalLineColor = color;
    m_gridOverlay->update();
}

void VirtualFlowTableView::setHorizontalGridLineColor(const QColor &color)
{
    m_horizontalLineColor = color;
    m_gridOverlay->update();
}

void VirtualFlowTableView::setVerticalSpacingLineThroughRowSpacing(bool enabled)
{
    if (gridLinePlacement() == GridLinePlacement::SpacingCenter)
        return;
    m_verticalThroughRows = enabled;
    m_gridOverlay->update();
}

void VirtualFlowTableView::setHorizontalSpacingLineThroughColumnSpacing(bool enabled)
{
    if (gridLinePlacement() == GridLinePlacement::SpacingCenter)
        return;
    m_horizontalThroughColumns = enabled;
    m_gridOverlay->update();
}

void VirtualFlowTableView::refreshVisualStates()
{
    VirtualItemView::refreshVisualStates();
    viewport()->update();
}

void VirtualFlowTableView::refreshVisualState(const QModelIndex &index)
{
    VirtualItemView::refreshVisualState(index);
    viewport()->update();
}

void VirtualFlowTableView::paintEvent(QPaintEvent *event)
{
    detail::PerformanceScope performance(this, "paint.flow", detail::PerformanceDomain::Paint);
    VirtualItemView::paintEvent(event);
    QPainter painter(viewport());
    paintRowSpacingBackground(&painter, event->rect());
    if (m_backgroundVisible)
        paintVisualStateBackgrounds(&painter, event->rect());
}

void VirtualFlowTableView::paintRowSpacingBackground(QPainter *painter, const QRect &dirtyRect) const
{
    if (!spacingBackgroundColor().isValid() || (rowSpacing() == 0 && columnSpacing() == 0))
        return;
    detail::PerformanceScope performance(this, "paint.flow-spacing", detail::PerformanceDomain::Paint);
    const VisibleRange range = gridPaintRange();
    if (!range.isValid())
        return;
    const QRegion clip(dirtyRect.intersected(viewport()->rect()));
    const QMargins outer = m_flow->outerSpacing();
    const bool centered = gridLinePlacement() == GridLinePlacement::SpacingCenter;
    QRegion gaps;
    for (qsizetype ordinal = range.first; ordinal <= range.last; ++ordinal) {
        const QRect rect = m_flow->itemRect(ordinal, verticalOffset());
        const bool columnGap = ordinal % columnCount() + 1 < columnCount();
        const bool rowGap = ordinal / columnCount() + 1 < displayRowCount();
        const bool nextColumn = columnGap && ordinal + 1 < viewItemCount();
        const int left = ordinal % columnCount() == 0 ? outer.left() : 0;
        const int right = centered ? (nextColumn ? columnSpacing() : outer.right())
                                   : (columnGap ? columnSpacing() : 0);
        const int top = ordinal < columnCount() ? outer.top() : 0;
        const int bottom = rowGap ? rowSpacing() : outer.bottom();
        const QRect body(rect.x(), rect.y(), rect.width(), m_flow->rowHeight());
        const QRect slot = body.adjusted(-left, -top, right, bottom);
        gaps += QRegion(slot).subtracted(QRegion(body)).intersected(clip);
    }
    fillPixelAlignedRegion(painter, gaps, spacingBackgroundColor());
}

void VirtualFlowTableView::paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty) const
{
    detail::PerformanceScope performance(this, "paint.flow-state", detail::PerformanceDomain::Paint);
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype ordinal = viewItemForIndex(item.index);
        if (ordinal < 0)
            continue;
        const QRect rect = m_flow->itemRect(ordinal, verticalOffset());
        const QRegion clip(rect.intersected(viewport()->rect()).intersected(dirty));
        if (clip.isEmpty())
            continue;
        const VisualState state = visualState(item.index);
        painter->save();
        painter->setOpacity(state.hoverProgress);
        fillPixelAlignedRoundedRegion(painter, rect, clip, m_cornerRadius, hoverBackgroundColor());
        painter->setOpacity(state.selectedProgress);
        fillPixelAlignedRoundedRegion(painter, rect, clip, m_cornerRadius, selectedBackgroundColor());
        painter->restore();
    }
}

VisibleRange VirtualFlowTableView::gridPaintRange() const
{
    VisibleRange range = coreVisibleRange();
    if (range.isValid()) {
        // The previous row owns the far edge of the gap at the viewport top.
        // Include its geometry even when its widgets have already been recycled.
        range.first = qMax<qsizetype>(0, (range.first / columnCount() - 1) * columnCount());
    }
    return range;
}

void VirtualFlowTableView::paintGridLines(QPainter *painter) const
{
    detail::PerformanceScope performance(this, "paint.flow-grid", detail::PerformanceDomain::Paint);
    const VisibleRange range = gridPaintRange();
    if (!range.isValid())
        return;
    QVector<QRect> horizontal;
    QVector<QRect> vertical;
    QVector<QRect> horizontalLeading;
    QVector<QRect> verticalLeading;
    QVector<QRect> horizontalCenters;
    QVector<QRect> verticalCenters;
    const bool centered = gridLinePlacement() == GridLinePlacement::SpacingCenter;
    const QMargins outer = m_flow->outerSpacing();
    // A bottom content margin separates the terminal line from the outer frame.
    const bool showTrailingLine = contentMargins().bottom() > 0 || trailingRowGridLineVisible();
    for (qsizetype ordinal = range.first; ordinal <= range.last; ++ordinal) {
        const QRect rect = m_flow->itemRect(ordinal, verticalOffset());
        const bool hasTrailingColumnBoundary = ordinal % columnCount() + 1 < columnCount();
        const bool hasNextColumn = hasTrailingColumnBoundary && ordinal + 1 < viewItemCount();
        const bool hasNextRow = ordinal / columnCount() + 1 < displayRowCount();
        const bool hasItemBelow = viewItemCount() - ordinal > columnCount();
        const int top = ordinal < columnCount() ? outer.top() : 0;
        // A partial next row only continues boundaries beside its real cards.
        // Otherwise stop at the row-gap center instead of extending into empty cells.
        const int rowGapTail = centered && !hasItemBelow ? rowSpacing() / 2 : rowSpacing();
        const int bottom = hasNextRow ? (verticalSpacingLineThroughRowSpacing() ? rowGapTail : 0)
                                      : outer.bottom();
        const QRect verticalSegment(rect.x(), rect.y() - top, rect.width(),
                                    m_flow->rowHeight() + top + bottom);
        const int left = ordinal % columnCount() == 0 ? outer.left() : 0;
        const int right = centered ? (hasNextColumn ? columnSpacing() : outer.right())
            : (hasTrailingColumnBoundary && horizontalSpacingLineThroughColumnSpacing() ? columnSpacing() : 0);
        const QRect horizontalSegment(rect.x() - left, rect.y(),
                                      rect.width() + left + right, m_flow->rowHeight());
        if (m_verticalLines) {
            // Close a partial final row even when the next display cell is empty.
            if (hasTrailingColumnBoundary) {
                // Keep the full virtual gap as the center reference even after the
                // final real card, so its line aligns with the same column above.
                if (centered && columnSpacing() > 0)
                    verticalCenters.append(QRect(rect.right() + 1, verticalSegment.y(), columnSpacing(),
                                                 verticalSegment.height()));
                else
                    vertical.append(verticalSegment);
                if (!centered && columnSpacing() > 0) {
                    // Both gap edges belong to the preceding real card, even when
                    // the next display cell in a partial row has no model item.
                    verticalLeading.append(QRect(rect.right() + 1 + columnSpacing(), rect.y(),
                                                 rect.width(), verticalSegment.height()));
                }
            }
        }
        if (m_horizontalLines) {
            if (hasNextRow) {
                // Proportional cards can differ in height within the same display row.
                // Its gap still starts after the tallest card, shared by all columns.
                if (centered && rowSpacing() > 0)
                    horizontalCenters.append(QRect(horizontalSegment.x(), rect.y() + m_flow->rowHeight(),
                                                   horizontalSegment.width(), rowSpacing()));
                else
                    horizontal.append(horizontalSegment);
                if (!centered && rowSpacing() > 0) {
                    horizontalLeading.append(QRect(rect.x(), rect.y() + m_flow->rowHeight() + rowSpacing(),
                                                   horizontalSegment.width(), m_flow->rowHeight()));
                }
            } else if (showTrailingLine) {
                // The terminal line closes the outer half-gap in center mode.
                QRect boundary = horizontalSegment;
                boundary.setHeight(boundary.height() + outer.bottom());
                horizontal.append(boundary);
            }
        }
    }
    fillPixelLines(painter, horizontal, m_horizontalLineWidth,
        m_horizontalLineColor.isValid() ? m_horizontalLineColor : palette().color(QPalette::Mid),
        Qt::Horizontal, true);
    fillPixelLines(painter, vertical, m_verticalLineWidth,
        m_verticalLineColor.isValid() ? m_verticalLineColor : palette().color(QPalette::Mid),
        Qt::Vertical, true);
    // Each nonzero gap owns both edges; zero gaps share one line.
    fillPixelLines(painter, horizontalLeading, m_horizontalLineWidth,
        m_horizontalLineColor.isValid() ? m_horizontalLineColor : palette().color(QPalette::Mid),
        Qt::Horizontal);
    fillPixelLines(painter, verticalLeading, m_verticalLineWidth,
        m_verticalLineColor.isValid() ? m_verticalLineColor : palette().color(QPalette::Mid),
        Qt::Vertical);
    fillPixelLines(painter, horizontalCenters, m_horizontalLineWidth,
        m_horizontalLineColor.isValid() ? m_horizontalLineColor : palette().color(QPalette::Mid),
        Qt::Horizontal, false, false, true);
    fillPixelLines(painter, verticalCenters, m_verticalLineWidth,
        m_verticalLineColor.isValid() ? m_verticalLineColor : palette().color(QPalette::Mid),
        Qt::Vertical, false, false, true);
}
int VirtualFlowTableView::columnCount() const { return m_flow->columnCount(); }
qsizetype VirtualFlowTableView::displayRowCount() const { return m_flow->rowCount(); }
int VirtualFlowTableView::columnWidth(int column) const { return m_flow->columnWidth(column); }
int VirtualFlowTableView::columnViewportX(int column) const
{
    return column >= 0 && column < columnCount() ? m_flow->columnX(column) : -1;
}
qsizetype VirtualFlowTableView::displayRowForIndex(const QModelIndex &index) const
{
    const qsizetype item = viewItemForIndex(index);
    return item < 0 ? -1 : item / columnCount();
}
int VirtualFlowTableView::displayColumnForIndex(const QModelIndex &index) const
{
    const qsizetype item = viewItemForIndex(index);
    return item < 0 ? -1 : int(item % columnCount());
}

void VirtualFlowTableView::resizeEvent(QResizeEvent *event)
{
    detail::PerformanceScope performance(this, "flow.resize");
    const ScrollAnchor anchor = captureAnchor();
    ++m_revision;
    setPendingAnchor(anchor);
    VirtualItemView::resizeEvent(event);
    if (relayoutActive())
        markDirty();
}

QSize VirtualFlowTableView::scrollBarFreeViewportSize() const
{
    const int barWidth = verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn
        ? verticalScrollBar()->sizeHint().width() : 0;
    return maximumViewportSize() + QSize(barWidth, 0);
}

qint64 VirtualFlowTableView::autoHeightContentExtent() const
{
    // A visible scrollbar must not reduce the preferred proportional height and
    // prevent the parent from granting enough space to remove that scrollbar.
    FlowTableLayout candidate = *m_flow;
    candidate.setCrossExtent(scrollBarFreeViewportSize().width());
    candidate.resetItems(viewItemCount(), 0);
    return candidate.contentExtent();
}

void VirtualFlowTableView::stabilizeScrollBar()
{
    if (m_stabilizing)
        return;
    // Solve using the scrollbar-free width. A narrower proportional grid may
    // fit again; retaining the bar in that case avoids a two-state resize loop.
    const QSize full = scrollBarFreeViewportSize();
    FlowTableLayout candidate = *m_flow;
    candidate.setCrossExtent(full.width());
    const bool needed = candidate.contentExtent() > full.height();
    const Qt::ScrollBarPolicy policy = needed ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff;
    if (verticalScrollBarPolicy() == policy)
        return;
    m_stabilizing = true;
    setVerticalScrollBarPolicy(policy);
    m_stabilizing = false;
    markDirty();
}

void VirtualFlowTableView::afterMaterialize()
{
    // Deliberately bypass sizeHint measurement: the flow owns height policy.
    if (autoHeightEnabled()) {
        // The base queues ancestor-layout activation before this callback.
        // Compare against the allocated height, not the previous content height.
        if (!m_autoHeightScrollBarPending) {
            m_autoHeightScrollBarPending = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_autoHeightScrollBarPending = false;
                stabilizeScrollBar();
            }, Qt::QueuedConnection);
        }
    } else {
        stabilizeScrollBar();
    }
    m_gridOverlay->setGeometry(viewport()->rect());
    m_gridOverlay->show();
    m_gridOverlay->raise();
    m_gridOverlay->update();
    const QSize size(viewport()->width(), m_flow->rowHeight());
    const qsizetype count = m_flow->itemCount();
    if (m_revision != m_reportedRevision || size != m_reportedSize || count != m_reportedCount || columnCount() != m_reportedColumns
        || m_flow->rowHeight() != m_reportedHeight) {
        m_reportedSize = size;
        m_reportedRevision = m_revision;
        m_reportedCount = count;
        m_reportedColumns = columnCount();
        m_reportedHeight = m_flow->rowHeight();
        emit flowLayoutChanged();
    }
}

bool VirtualFlowTableView::handleItemKeyPress(QKeyEvent *event)
{
    const qsizetype count = viewItemCount();
    const qsizetype current = viewItemForIndex(currentIndex());
    const qsizetype columns = columnCount();
    qsizetype target = current;
    const qint64 pageRows = qMax<qint64>(1, viewport()->height()
        / (qint64(m_flow->rowHeight()) + rowSpacing()));
    const auto vertical = [&](qint64 delta) {
        if (current < 0)
            return delta < 0 ? count - 1 : qsizetype(0);
        const qint64 row = qBound<qint64>(0, current / columns + delta, m_flow->rowCount() - 1);
        return qMin<qsizetype>(count - 1, row * columns + current % columns);
    };
    switch (event->key()) {
    case Qt::Key_Left: target = current < 0 ? count - 1 : current - 1; break;
    case Qt::Key_Right: target = current < 0 ? 0 : current + 1; break;
    case Qt::Key_Up: target = vertical(-1); break;
    case Qt::Key_Down: target = vertical(1); break;
    case Qt::Key_PageUp: target = vertical(-pageRows); break;
    case Qt::Key_PageDown: target = vertical(pageRows); break;
    default: return false;
    }
    if (count > 0)
        moveCurrentToItem(target, event->modifiers());
    return true;
}

QPixmap VirtualFlowTableView::grabDragPreview(QWidget *sourceWidget, const QRect &capture)
{
    if (!sourceWidget || (sourceWidget != viewport() && !viewport()->isAncestorOf(sourceWidget)))
        return VirtualItemView::grabDragPreview(sourceWidget, capture);

    const QRect viewportCapture = capture.translated(sourceWidget->mapTo(viewport(), QPoint()));
    QRegion visible(viewportCapture.intersected(viewport()->rect()));
    for (QWidget *widget = sourceWidget; widget; widget = widget->parentWidget()) {
        const QPoint origin = widget->mapTo(viewport(), QPoint());
        visible &= QRect(origin, widget->size());
        const QRegion mask = widget->mask();
        if (!mask.isEmpty())
            visible &= mask.translated(origin);
        if (widget == viewport())
            break;
    }
    if (visible.isEmpty())
        return QPixmap();

    // Capture the viewport background and grid overlay together with the card.
    // Keep the source coordinate origin and DPR so the press hotspot stays fixed.
    QPixmap pixmap = viewport()->grab(viewportCapture);
    if (pixmap.isNull() || visible == QRegion(viewportCapture))
        return pixmap;
    QPixmap clipped(pixmap.size());
    clipped.setDevicePixelRatio(pixmap.devicePixelRatio());
    clipped.fill(Qt::transparent);
    {
        QPainter painter(&clipped);
        painter.setClipRegion(visible.translated(-viewportCapture.topLeft()));
        painter.drawPixmap(QPoint(), pixmap);
    }
    return clipped;
}

VirtualItemView::DropTarget VirtualFlowTableView::resolveDropTarget(const QPoint &position) const
{
    DropTarget target;
    if (!model() || !viewport()->rect().contains(position)
        || position.x() < contentMargins().left()
        || position.x() >= viewport()->width() - contentMargins().right()
        || position.y() + verticalOffset() < contentMargins().top())
        return target;
    const qsizetype count = viewItemCount();
    const QModelIndex hit = indexAt(position);
    qsizetype insertion = count;
    if (hit.isValid()) {
        const QRect rect = visualRect(hit);
        insertion = hit.row() + (position.x() >= rect.x() + rect.width() / 2 ? 1 : 0);
    } else if (count > 0) {
        const qint64 y = position.y() + verticalOffset();
        const qsizetype first = m_flow->indexAtOffset(y);
        if (first < count) {
            const QRect row = m_flow->itemRect(first, verticalOffset());
            if (position.y() >= row.y() + m_flow->rowHeight()) {
                insertion = qMin<qsizetype>(count, first + columnCount());
            } else {
                // A column gap inserts before the next item; absent last-row cells append.
                int low = 0;
                int high = columnCount();
                while (low < high) {
                    const int middle = low + (high - low) / 2;
                    if (position.x() >= m_flow->columnX(middle))
                        low = middle + 1;
                    else
                        high = middle;
                }
                insertion = qMin<qsizetype>(count, first + low);
            }
        }
    }
    target.parent = rootIndex();
    target.row = int(insertion);
    target.trailing = insertion == count;
    return target;
}

QRect VirtualFlowTableView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || target.parent != rootIndex() || target.row > viewItemCount())
        return {};
    if (viewItemCount() == 0)
        return QRect(contentMargins().left(), contentMargins().top() + m_flow->outerSpacing().top(),
                     2, m_flow->rowHeight())
            .intersected(viewport()->rect());
    const bool trailing = target.row == viewItemCount();
    const qsizetype item = trailing ? target.row - 1 : target.row;
    const QRect rect = visualRect(viewIndex(item));
    int boundary = trailing ? rect.right() + 1 : rect.left();
    if (gridLinePlacement() == GridLinePlacement::SpacingCenter && columnSpacing() > 0) {
        // Internal boundaries use gap centers; outer boundaries close the half-gaps.
        if (trailing)
            boundary += m_flow->outerSpacing().right();
        else if (!trailing && item % columnCount() > 0)
            boundary -= columnSpacing() / 2;
        else
            boundary -= m_flow->outerSpacing().left();
    }
    return QRect(boundary - 1, rect.top(), 2, m_flow->rowHeight())
        .intersected(viewport()->rect());
}

} // namespace viv
