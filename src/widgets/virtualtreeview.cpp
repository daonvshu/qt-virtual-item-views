#include <virtualitemviews/virtualtreeview.h>
#include "../core/performanceprobe_p.h"
#include "treeexpansiontransition_p.h"
#include "treedragtransition_p.h"
#include "../core/pixelalignedlines_p.h"

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSet>
#include <QtMath>
#include <limits>

namespace viv {

constexpr int VirtualTreeView::NodeRowSpacingRole;
constexpr int VirtualTreeView::NodeRowSpacingBelowRole;
constexpr int VirtualTreeView::NodeRowSpacingAboveRole;

namespace {
inline QPoint eventPosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/// Ancestor of \a index \a levels above it (0 = the index itself).
QModelIndex ancestorAt(const QModelIndex &index, int levels)
{
    QModelIndex current = index;
    for (int level = 0; level < levels && current.isValid(); ++level)
        current = current.parent();
    return current;
}
} // namespace

VirtualTreeView::VirtualTreeView(QWidget *parent)
    : VirtualItemView(parent)
{
    m_visibility = new TreeVisibilityIndex(nullptr);
    m_rowLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_rowLayout, true);
    m_expansionTransition = new TreeExpansionTransition(this, m_rowLayout,
        [this]() {
            const QPointer<VirtualTreeView> guard(this);
            relayout();
            if (guard)
                viewport()->update();
        });
    m_dragTransition = new TreeDragTransition(this);
}

VirtualTreeView::~VirtualTreeView()
{
    delete m_dragTransition;
    m_dragTransition = nullptr;
    m_expansionTransition->cancel();
    m_expansionTransition->setEnabled(false);
    qDeleteAll(m_rowGridLines);
    qDeleteAll(m_rowGridLinePool);
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    delete m_expansionTransition;
    delete m_visibility;
}

void VirtualTreeView::setModel(QAbstractItemModel *model)
{
    detail::PerformanceScope performance(this, "refresh.tree-model");
    const QPointer<QAbstractItemModel> previous(VirtualItemView::model());
    if (previous == model)
        return;
    const QPointer<VirtualTreeView> guard(this);
    const QPointer<QAbstractItemModel> requestedModel(model);
    clearDragFeedback();
    if (!guard || (model && !requestedModel))
        return;
    m_expansionTransition->cancel();
    if (previous)
        disconnect(previous.data(), nullptr, this, nullptr);

    m_siblingBranchCells.clear();

    detail::performanceCall(this, "model.tree-set-model", [&] { m_visibility->setModel(model); });
    m_rootIndex = QModelIndex();
    detail::performanceCall(this, "model.tree-set-root", [&] { m_visibility->setRootIndex(m_rootIndex); });

    const quint64 previousChangeSerial = modelChangeSerial();
    VirtualItemView::setModel(model);
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    m_expansionTransition->watchModel(this->model());
    connectModelSignals(this->model());
    resetLayoutForNewModel();
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    relayout();
}

void VirtualTreeView::connectModelSignals(QAbstractItemModel *model)
{
    if (!model)
        return;

    const auto clearBranchCells = [this]() {
        m_siblingBranchCells.clear();
        clearDragFeedback();
    };
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, clearBranchCells);
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, clearBranchCells);
    connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this, clearBranchCells);
    connect(model, &QAbstractItemModel::columnsAboutToBeInserted, this, clearBranchCells);
    connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this, clearBranchCells);
    connect(model, &QAbstractItemModel::columnsAboutToBeMoved, this, clearBranchCells);
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, clearBranchCells);
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, clearBranchCells);
    connect(model, &QObject::destroyed, this, [this]() { clearDragFeedback(); });

    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                if (!roles.isEmpty() && !roles.contains(NodeRowSpacingBelowRole)
                    && !roles.contains(NodeRowSpacingAboveRole))
                    return;
                if (auto *layout = m_rowLayout) {
                    layout->setItemSpacing(rowSpacing());
                    applyRowSpacingOverrides();
                }
                relayout();
            });
    // The anchor has to be captured before the model changes, and removed rows
    // must lose their widgets before their persistent indexes become invalid.
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex &, int, int) { setPendingAnchor(captureAnchor()); });
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &parent, int first, int last) {
                setPendingAnchor(captureAnchor());
                recycleItemsInModelRange(parent, first, last);
            });

    connect(model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { onStructureChanged(); });
    connect(model, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { onStructureChanged(); });
    connect(model, &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) { onStructureChanged(); });
    connect(model, &QAbstractItemModel::layoutChanged, this,
            [this](const QList<QPersistentModelIndex> &, QAbstractItemModel::LayoutChangeHint) {
                onStructureChanged();
            });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        detail::PerformanceScope performance(this, "refresh.tree-reset");
        m_rootIndex = QModelIndex();
        detail::performanceCall(this, "model.tree-reset", [&] { m_visibility->handleModelReset(); });
        refreshVisibility(false);
    });
    // The visibility index stores QModelIndex values (see TreeVisibilityIndex), and a column
    // change renames - or, for a removed column 0, invalidates - the cells they name, so the
    // mapping is re-derived. It is *not* onStructureChanged(): the row structure did not
    // change, so the measured heights and the anchor stay valid (P2 of the fourth review).
    // The base kernel releases the row widgets before the change, which is what keeps the
    // unbind/rebind pair honest.
    connect(model, &QAbstractItemModel::columnsInserted, this,
            [this]() { onColumnStructureChanged(); });
    connect(model, &QAbstractItemModel::columnsRemoved, this,
            [this]() { onColumnStructureChanged(); });
    connect(model, &QAbstractItemModel::columnsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                onColumnStructureChanged();
            });
}

void VirtualTreeView::onStructureChanged()
{
    detail::PerformanceScope performance(this, "refresh.tree-structure");
    // Visible rows, expansion state and item identity all live in the index.
    detail::performanceCall(this, "model.tree-rebuild", [&] { m_visibility->handleModelChanged(); });
    refreshVisibility(false);
}

void VirtualTreeView::onColumnStructureChanged()
{
    detail::PerformanceScope performance(this, "refresh.tree-columns");
    m_siblingBranchCells.clear();
    // Same reason as onStructureChanged(), minus the layout reset: the visible rows are
    // (row, 0) indexes, so a column change re-derives them - but the rows themselves did not
    // move, and their measured heights must survive. The expansion state names cells, not rows,
    // so a column change that renames column 0 can collapse it (see docs/history/model-signals.md:
    // column mutation is not part of the List / Tree contract); what this path guarantees is
    // that the mapping is re-derived instead of handing out stale cells.
    detail::performanceCall(this, "model.tree-rebuild", [&] { m_visibility->handleModelChanged(); });
    relayout();
}

void VirtualTreeView::refreshVisibility(bool keepAnchor)
{
    detail::PerformanceScope performance(this, "tree.visibility");
    if (m_updatingVisibility)
        return;
    m_updatingVisibility = true;
    m_siblingBranchCells.clear();
    if (keepAnchor)
        setPendingAnchor(captureAnchor());
    // The visible row mapping changed: row sizes are keyed by view row, so they
    // are reset to the estimate (measurement re-measures the visible rows) while
    // ScrollAnchor keeps the anchored item in place.
    resetLayoutForNewModel();
    relayout();
    m_updatingVisibility = false;
}

// ---------------------------------------------------------------------------
// Identity mapping
// ---------------------------------------------------------------------------

qsizetype VirtualTreeView::viewItemCount() const
{
    return m_visibility->visibleRowCount();
}

QModelIndex VirtualTreeView::viewIndex(qsizetype item, int column) const
{
    const QModelIndex index = m_visibility->indexAtVisibleRow(item);
    if (!index.isValid() || column == index.column())
        return index;
    return index.siblingAtColumn(column);
}

qsizetype VirtualTreeView::viewItemForIndex(const QModelIndex &index) const
{
    return m_visibility->visibleRowForIndex(index);
}

bool VirtualTreeView::isLayoutParent(const QModelIndex &parent) const
{
    // The kernel works on visible rows, never on model rows: model mutations are
    // routed through TreeVisibilityIndex.
    Q_UNUSED(parent);
    return false;
}

int VirtualTreeView::itemDepth(const QModelIndex &index) const
{
    const int depth = m_visibility->depth(index);
    return depth < 0 ? 0 : depth;
}

int VirtualTreeView::effectiveRowSpacing(qsizetype row) const
{
    const QVariant value = viewIndex(row).data(NodeRowSpacingBelowRole);
    bool valid = false;
    const int spacing = value.toInt(&valid);
    const int below = valid && spacing >= 0 ? spacing : rowSpacing();
    const QVariant aboveValue = row + 1 < viewItemCount()
        ? viewIndex(row + 1).data(NodeRowSpacingAboveRole) : QVariant();
    const int above = aboveValue.toInt(&valid);
    return int(qMin<qint64>(qint64(below) + (valid ? qMax(0, above) : 0),
                           std::numeric_limits<int>::max()));
}

void VirtualTreeView::setRowGridLinesVisible(bool visible)
{
    if (m_rowGridLinesVisible == visible)
        return;
    m_rowGridLinesVisible = visible;
    relayout();
}

void VirtualTreeView::setRowGridLineWidth(int pixels)
{
    const int width = qMax(1, pixels);
    if (m_rowGridLineWidth == width)
        return;
    m_rowGridLineWidth = width;
    relayout();
}

void VirtualTreeView::setRowGridLineColor(const QColor &color)
{
    if (m_rowGridLineColor == color)
        return;
    m_rowGridLineColor = color;
    relayout();
}

void VirtualTreeView::setRowGridLineExtent(RowGridLineExtent extent)
{
    if (extent != RowGridLineExtent::NodeOnly && extent != RowGridLineExtent::NodeAndIcon
        && extent != RowGridLineExtent::FullWidth)
        return;
    if (m_rowGridLineExtent == extent)
        return;
    m_rowGridLineExtent = extent;
    relayout();
}

void VirtualTreeView::setVisualStateBackgroundVisible(bool visible)
{
    if (m_visualStateBackgroundVisible == visible)
        return;
    m_visualStateBackgroundVisible = visible;
    relayout();
    viewport()->update();
}

void VirtualTreeView::setVisualStateCornerRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_visualStateCornerRadius == radius)
        return;
    m_visualStateCornerRadius = radius;
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeView::setVisualStateBackgroundMargins(const QMargins &margins)
{
    const QMargins clamped(qMax(0, margins.left()), qMax(0, margins.top()),
                           qMax(0, margins.right()), qMax(0, margins.bottom()));
    if (m_visualStateBackgroundMargins == clamped)
        return;
    m_visualStateBackgroundMargins = clamped;
    refreshVisualStateBackgroundGeometry();
}

void VirtualTreeView::setVisualStateBackgroundHeight(int height)
{
    height = qMax(0, height);
    if (m_visualStateBackgroundHeight == height)
        return;
    m_visualStateBackgroundHeight = height;
    refreshVisualStateBackgroundGeometry();
}

void VirtualTreeView::setVisualStateBackgroundExtent(VisualStateBackgroundExtent extent)
{
    if (extent != VisualStateBackgroundExtent::NodeOnly
        && extent != VisualStateBackgroundExtent::NodeAndIcon
        && extent != VisualStateBackgroundExtent::FullWidth
        && extent != VisualStateBackgroundExtent::WidgetOnly
        && extent != VisualStateBackgroundExtent::WidgetAndIcon)
        return;
    if (m_visualStateBackgroundExtent == extent)
        return;
    m_visualStateBackgroundExtent = extent;
    refreshVisualStateBackgroundGeometry();
}

void VirtualTreeView::refreshVisualStateBackgroundGeometry()
{
    const QPointer<VirtualTreeView> guard(this);
    refreshHoveredIndex();
    if (guard && m_visualStateBackgroundVisible)
        viewport()->update();
}

bool VirtualTreeView::hasOwnBranchCell(const QModelIndex &index) const
{
    if (!m_branchAreaVisible)
        return false;
    if (m_indentLeafItems || !index.isValid() || !model())
        return true;
    const QPersistentModelIndex parent(index.parent());
    const auto cached = m_siblingBranchCells.constFind(parent);
    if (cached != m_siblingBranchCells.cend())
        return cached.value();
    detail::PerformanceScope performance(this, "model.tree-sibling-branches");
    bool hasBranch = false;
    const int count = model()->rowCount(parent);
    int row = 0;
    for (; row < count; ++row) {
        if (hasChildren(model()->index(row, 0, parent))) {
            hasBranch = true;
            break;
        }
    }
    if (performance.isActive())
        performance.count("tree.siblings-scanned", qint64(row) + (hasBranch ? 1 : 0));
    m_siblingBranchCells.insert(parent, hasBranch);
    return hasBranch;
}

int VirtualTreeView::rowGridLineInset(const QModelIndex &index) const
{
    const int depth = itemDepth(index);
    const qint64 cells = m_rowGridLineExtent == RowGridLineExtent::NodeOnly
        ? qint64(depth) + int(hasOwnBranchCell(index))
        : m_rowGridLineExtent == RowGridLineExtent::NodeAndIcon ? qint64(depth) : 0;
    return int(qMin<qint64>(qMax(0, viewport()->width()), qMax<qint64>(0, cells) * m_indentation));
}

QColor VirtualTreeView::itemPaneSeparatorColor() const
{
    return m_rowGridLineColor.isValid() ? m_rowGridLineColor
                                        : VirtualItemView::itemPaneSeparatorColor();
}

void VirtualTreeView::configureRowSpacingWidget(QWidget *widget) const
{
    widget->setProperty("vivShowSpacingLines", m_rowGridLinesVisible);
    widget->setProperty("vivSpacingLineWidth", m_rowGridLineWidth);
    const QModelIndex index = widget->property("vivSpacingIndex").value<QModelIndex>();
    widget->setProperty("vivSpacingLineLeftInset", rowGridLineInset(index));
    widget->setProperty("vivFillSpacingBackground", true);
}

void VirtualTreeView::applyRowSpacingOverrides()
{
    detail::PerformanceScope performance(this, "model.tree-spacing");
    if (!m_rowLayout)
        return;
    if (performance.isActive())
        performance.count("tree.spacing-rows", m_rowLayout->itemCount());
    for (qsizetype row = 0; row < m_rowLayout->itemCount(); ++row) {
        const int spacing = effectiveRowSpacing(row);
        if (spacing != rowSpacing())
            m_rowLayout->setSpacingAfter(row, spacing);
    }
}

qsizetype VirtualTreeView::visibleRowCount() const
{
    return m_visibility->visibleRowCount();
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

void VirtualTreeView::setRootIndex(const QModelIndex &index)
{
    detail::PerformanceScope performance(this, "refresh.tree-root");
    const QPointer<VirtualTreeView> guard(this);
    clearDragFeedback();
    if (!guard)
        return;
    m_expansionTransition->cancel();
    if (index == m_rootIndex)
        return;
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualTreeView::setRootIndex(): the index belongs to another model "
                 "(or the view has no model); the root is unchanged");
        return;
    }
    m_rootIndex = index;
    detail::performanceCall(this, "model.tree-set-root", [&] { m_visibility->setRootIndex(index); });
    refreshVisibility(false);
}

bool VirtualTreeView::hasChildren(const QModelIndex &index) const
{
    QAbstractItemModel *m = model();
    return m && index.isValid() && m->rowCount(index) > 0;
}

void VirtualTreeView::setExpansionAnimationEnabled(bool enabled)
{
    m_expansionTransition->setEnabled(enabled);
}

bool VirtualTreeView::expansionAnimationEnabled() const
{
    return m_expansionTransition->isEnabled();
}

void VirtualTreeView::setExpansionAnimationDuration(int milliseconds)
{
    m_expansionTransition->setDuration(milliseconds);
}

int VirtualTreeView::expansionAnimationDuration() const
{
    return m_expansionTransition->duration();
}

void VirtualTreeView::setExpansionFadeEnabled(bool enabled)
{
    m_expansionTransition->setFadeEnabled(enabled);
}

bool VirtualTreeView::expansionFadeEnabled() const
{
    return m_expansionTransition->isFadeEnabled();
}

void VirtualTreeView::setDragFeedbackMode(DragFeedbackMode mode)
{
    if (m_dragTransition->mode == mode)
        return;
    m_dragTransition->mode = mode;
    const QPointer<VirtualTreeView> guard(this);
    clearDragFeedback();
    if (guard)
        refreshDragFeedback();
}

VirtualTreeView::DragFeedbackMode VirtualTreeView::dragFeedbackMode() const
{
    return m_dragTransition->mode;
}

void VirtualTreeView::setDragReorderAnimationDuration(int milliseconds)
{
    milliseconds = qMax(0, milliseconds);
    if (m_dragTransition->duration == milliseconds)
        return;
    m_dragTransition->duration = milliseconds;
    m_dragTransition->finishTween();
}

int VirtualTreeView::dragReorderAnimationDuration() const
{
    return m_dragTransition->duration;
}

void VirtualTreeView::setDragReorderAnimationEasingCurve(const QEasingCurve &curve)
{
    if (m_dragTransition->easing == curve)
        return;
    m_dragTransition->easing = curve;
    m_dragTransition->finishTween();
}

QEasingCurve VirtualTreeView::dragReorderAnimationEasingCurve() const
{
    return m_dragTransition->easing;
}

bool VirtualTreeView::prepareDrag(const QModelIndexList &indexes)
{
    detail::PerformanceScope performance(this, "drag.tree-prepare");
    if (dragFeedbackMode() != DragFeedbackMode::RowMoveAnimation || indexes.isEmpty())
        return true;
    const QPersistentModelIndex node(indexes.first().siblingAtColumn(0));
    for (const QModelIndex &index : indexes) {
        if (!index.isValid() || index.model() != model()
            || index.siblingAtColumn(0) != node)
            return true;
    }
    const QPointer<VirtualTreeView> guard(this);
    const QPointer<QAbstractItemModel> sourceModel(model());
    const quint64 serial = modelChangeSerial();
    const QPersistentModelIndex root(rootIndex());
    const auto current = [=]() {
        return guard && sourceModel && guard->model() == sourceModel.data()
            && guard->modelChangeSerial() == serial && guard->rootIndex() == root
            && guard->isDragEnabled() && node.isValid()
            && guard->viewItemForIndex(node) >= 0;
    };
    if (!current())
        return false;
    clearDragFeedback();
    if (!current())
        return false;
    // Finish any earlier transition, then change visibility directly so the
    // source's descendants never take part in a collapse animation during drag.
    m_expansionTransition->stop();
    if (!current())
        return false;
    if (m_visibility->isExpanded(node)) {
        setPendingAnchor(captureAnchor());
        if (!current())
            return false;
        detail::performanceCall(this, "model.tree-collapse", [&] { m_visibility->collapse(node); });
        refreshVisibility(false);
        if (!current())
            return false;
        if (!isExpanded(node)) {
            detail::performanceCall(this, "callback.signal.collapsed", [&] { emit collapsed(node); });
        }
    }
    // A business callback can replace the model, remove or reopen the source.
    return current() && !isExpanded(node);
}

bool VirtualTreeView::updateDragFeedback(const DropTarget &target, const QPoint &position,
                                        Qt::DropAction action,
                                        const QList<QPersistentModelIndex> &sources)
{
    return m_dragTransition && m_dragTransition->update(target, position, action, sources);
}

void VirtualTreeView::clearDragFeedback()
{
    if (m_dragTransition)
        m_dragTransition->clear();
}

QRect VirtualTreeView::visualRowRect(qsizetype row) const
{
    return m_dragTransition ? m_dragTransition->rowRect(row)
                            : VirtualItemView::geometryForViewRow(row);
}

void VirtualTreeView::augmentMaterializationRanges(QVector<VisibleRange> &ranges) const
{
    if (m_dragTransition)
        m_dragTransition->augmentRanges(ranges);
}

int VirtualTreeView::rowSpacingVisualOffset(qsizetype row) const
{
    return visualRowRect(row).y() - VirtualItemView::geometryForViewRow(row).y();
}

void VirtualTreeView::expand(const QModelIndex &index)
{
    detail::PerformanceScope performance(this, "tree.expand");
    const QPointer<VirtualTreeView> operationGuard(this);
    const QPersistentModelIndex requested(index);
    clearDragFeedback();
    if (!operationGuard)
        return;
    if (!m_expansionTransition->affects(requested))
        m_expansionTransition->stop();
    if (!operationGuard)
        return;
    if (m_expansionTransition->resumeExpansion(requested)) {
        if (operationGuard && requested.isValid()) {
            detail::performanceCall(this, "callback.signal.expanded", [&] { emit expanded(requested); });
        }
        return;
    }
    if (!requested.isValid() || m_visibility->isExpanded(requested))
        return;
    const QPersistentModelIndex node(requested);
    // The anchor must be captured *before* the visible row mapping changes,
    // otherwise the view can jump by the number of inserted rows.
    const QPointer<VirtualTreeView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    const qsizetype animationFirst = viewItemForIndex(node) + 1;
    const qsizetype animationCountBefore = viewItemCount();
    setPendingAnchor(captureAnchor());
    detail::performanceCall(this, "model.tree-expand", [&] { m_visibility->expand(node); });
    refreshVisibility(false);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot) {
        m_expansionTransition->expand(node, animationFirst, viewItemCount() - animationCountBefore);
        if (!animationGuard)
            return;
    }
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot
        && node.isValid() && isExpanded(node)) {
        detail::performanceCall(this, "callback.signal.expanded", [&] { emit expanded(node); });
    }
}

void VirtualTreeView::collapse(const QModelIndex &index)
{
    detail::PerformanceScope performance(this, "tree.collapse");
    const QPointer<VirtualTreeView> operationGuard(this);
    const QPersistentModelIndex requested(index);
    clearDragFeedback();
    if (!operationGuard)
        return;
    if (!m_expansionTransition->affects(requested))
        m_expansionTransition->stop();
    if (!operationGuard)
        return;
    if (m_expansionTransition->isCollapsing(requested))
        return;
    if (!requested.isValid() || !m_visibility->isExpanded(requested))
        return;
    const QPersistentModelIndex node(requested);
    const QPointer<VirtualTreeView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    if (m_expansionTransition->collapse(m_visibility, node,
            [animationGuard, node, animationSerial, animationRoot]() {
                if (animationGuard && node.isValid()
                    && animationGuard->modelChangeSerial() == animationSerial
                    && animationGuard->rootIndex() == animationRoot)
                    animationGuard->collapse(node);
            }))
        return;
    setPendingAnchor(captureAnchor());
    detail::performanceCall(this, "model.tree-collapse", [&] { m_visibility->collapse(node); });
    refreshVisibility(false);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot
        && node.isValid() && !isExpanded(node)) {
        detail::performanceCall(this, "callback.signal.collapsed", [&] { emit collapsed(node); });
    }
}

void VirtualTreeView::expandRecursively(const QModelIndex &index)
{
    detail::PerformanceScope performance(this, "tree.expand-recursively");
    const QPointer<VirtualTreeView> operationGuard(this);
    const QPersistentModelIndex requested(index);
    clearDragFeedback();
    if (!operationGuard)
        return;
    m_expansionTransition->stop();
    if (!operationGuard)
        return;
    if (!requested.isValid())
        return;
    const QPersistentModelIndex node(requested);
    const bool wasExpanded = m_visibility->isExpanded(requested);
    const QPointer<VirtualTreeView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    const qsizetype animationFirst = viewItemForIndex(node) + 1;
    const qsizetype animationCountBefore = viewItemCount();
    setPendingAnchor(captureAnchor());
    detail::performanceCall(this, "model.tree-expand-recursively", [&] { m_visibility->expandRecursively(node); });
    refreshVisibility(false);
    if (!animationGuard)
        return;
    if (!wasExpanded && modelChangeSerial() == animationSerial && m_rootIndex == animationRoot)
        m_expansionTransition->expand(node, animationFirst, viewItemCount() - animationCountBefore);
    if (!animationGuard)
        return;
    if (!wasExpanded && modelChangeSerial() == animationSerial && m_rootIndex == animationRoot
        && node.isValid() && isExpanded(node)) {
        detail::performanceCall(this, "callback.signal.expanded", [&] { emit expanded(node); });
    }
}

void VirtualTreeView::setExpanded(const QModelIndex &index, bool expanded)
{
    expanded ? expand(index) : collapse(index);
}

void VirtualTreeView::toggleExpanded(const QModelIndex &index)
{
    setExpanded(index, !isExpanded(index));
}

bool VirtualTreeView::isExpanded(const QModelIndex &index) const
{
    return !m_expansionTransition->isCollapsing(index)
        && m_visibility->isExpanded(index);
}

void VirtualTreeView::collapseAll()
{
    detail::PerformanceScope performance(this, "tree.collapse-all");
    const QPointer<VirtualTreeView> operationGuard(this);
    clearDragFeedback();
    if (!operationGuard)
        return;
    m_expansionTransition->stop();
    if (!operationGuard)
        return;
    const QPointer<VirtualTreeView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    setPendingAnchor(captureAnchor());
    detail::performanceCall(this, "model.tree-collapse-all", [&] { m_visibility->collapseAll(); });
    refreshVisibility(false);
    if (!animationGuard)
        return;
}

// ---------------------------------------------------------------------------
// Geometry / branch UI
// ---------------------------------------------------------------------------

void VirtualTreeView::setIndentation(int pixels)
{
    const int clamped = qMax(0, pixels);
    if (m_indentation == clamped)
        return;
    m_indentation = clamped;
    relayout();
}

void VirtualTreeView::setIndentLeafItems(bool enabled)
{
    if (m_indentLeafItems == enabled)
        return;
    m_expansionTransition->stop();
    m_indentLeafItems = enabled;
    m_siblingBranchCells.clear();
    relayout();
    viewport()->update();
}

void VirtualTreeView::setBranchAreaVisible(bool visible)
{
    if (m_branchAreaVisible == visible)
        return;
    const QPointer<VirtualTreeView> guard(this);
    m_expansionTransition->stop();
    if (!guard)
        return;
    m_branchAreaVisible = visible;
    relayout();
    if (guard)
        viewport()->update();
}

void VirtualTreeView::setBranchIndicatorsVisible(bool visible)
{
    if (m_branchIndicatorsVisible == visible)
        return;
    m_branchIndicatorsVisible = visible;
    // Turning them off must erase what is painted; turning them on must repaint
    // the strip (the rows themselves never carry the indicators).
    invalidateBranchIndicators();
    if (m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeAndIcon
        || m_visualStateBackgroundExtent == VisualStateBackgroundExtent::WidgetAndIcon)
        refreshVisualStateBackgroundGeometry();
}

void VirtualTreeView::setBranchIndicatorRenderer(BranchIndicatorRenderer *renderer, bool takeOwnership)
{
    if (m_branchRenderer == renderer) {
        m_ownBranchRenderer = m_ownBranchRenderer || (renderer && takeOwnership);
        return;
    }
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    m_branchRenderer = renderer;
    m_ownBranchRenderer = renderer && takeOwnership;
    // The strip that was painted with the old renderer may be wider (a renderer
    // can draw the ancestor cells too), so erase the previous one as well.
    invalidateBranchIndicators();
}

BranchIndicatorState VirtualTreeView::branchState(const QModelIndex &index, int cellDepth) const
{
    BranchIndicatorState state;
    if (!index.isValid() || !model())
        return state;

    const int itemDepth = qMax(0, this->itemDepth(index));
    state.itemDepth = itemDepth;
    // A negative cellDepth asks for the row's own cell.
    state.cellDepth = cellDepth < 0 ? itemDepth : qBound(0, cellDepth, itemDepth);

    // The cell at level L belongs to the ancestor at level L, so walk up as many
    // levels as separate the row from that ancestor.
    const QModelIndex cell = ancestorAt(index, itemDepth - state.cellDepth);
    if (!cell.isValid())
        return state;

    state.adjoinsItem = state.cellDepth == itemDepth;
    state.hasChildren = model()->rowCount(cell) > 0;
    state.isExpanded = isExpanded(cell);
    state.hasSiblings = cell.row() + 1 < model()->rowCount(cell.parent());
    return state;
}

QModelIndex VirtualTreeView::indexAt(const QPoint &viewportPos) const
{
    const QModelIndex index = VirtualItemView::indexAt(viewportPos);
    if (!index.isValid()
        || (m_visualStateBackgroundExtent == VisualStateBackgroundExtent::FullWidth
            && m_visualStateBackgroundMargins.isNull() && m_visualStateBackgroundHeight == 0))
        return index;
    const qsizetype row = viewItemForIndex(index);
    const QRect rowGeometry = VirtualItemView::geometryForViewRow(row);
    const QRect hitRect = visualStateBackgroundRect(index, rowGeometry)
        .intersected(rowGeometry).intersected(itemPaneRect(itemPaneForRow(row)))
        .intersected(viewport()->rect());
    return hitRect.contains(viewportPos) ? index : QModelIndex();
}

QRect VirtualTreeView::geometryForViewRow(qsizetype row) const
{
    const QRect rowRect = VirtualItemView::geometryForViewRow(row);
    if (!rowRect.isValid())
        return rowRect;
    const QModelIndex index = viewIndex(row);
    // All-leaf sibling groups can share the parent's content inset.
    const int left = m_indentation * (itemDepth(index) + int(hasOwnBranchCell(index)));
    return QRect(rowRect.x() + left, rowRect.y(), qMax(0, rowRect.width() - left), rowRect.height());
}

void VirtualTreeView::setExpandedBranchIcon(const QIcon &icon)
{
    m_expandedBranchIcon = icon;
    invalidateBranchIndicators();
}

void VirtualTreeView::setCollapsedBranchIcon(const QIcon &icon)
{
    m_collapsedBranchIcon = icon;
    invalidateBranchIndicators();
}

void VirtualTreeView::setBranchIconSize(int pixels)
{
    pixels = qMax(1, pixels);
    if (m_branchIconSize == pixels)
        return;
    m_branchIconSize = pixels;
    invalidateBranchIndicators();
}

void VirtualTreeView::setBranchIconColor(const QColor &color)
{
    if (m_branchIconColor == color)
        return;
    m_branchIconColor = color;
    invalidateBranchIndicators();
}

void VirtualTreeView::setBranchIndicatorAlignment(Qt::Alignment alignment)
{
    if (!(alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter)))
        alignment |= Qt::AlignLeft;
    if (!(alignment & (Qt::AlignTop | Qt::AlignBottom | Qt::AlignVCenter)))
        alignment |= Qt::AlignVCenter;
    if (m_branchIndicatorAlignment == alignment)
        return;
    m_branchIndicatorAlignment = alignment;
    invalidateBranchIndicators();
}

void VirtualTreeView::setBranchIndicatorPadding(const QMargins &padding)
{
    const QMargins clamped(qMax(0, padding.left()), qMax(0, padding.top()),
                           qMax(0, padding.right()), qMax(0, padding.bottom()));
    if (m_branchIndicatorPadding == clamped)
        return;
    m_branchIndicatorPadding = clamped;
    invalidateBranchIndicators();
}

int VirtualTreeView::branchIndicatorX(qsizetype row) const
{
    const int depth = itemDepth(viewIndex(row));
    return depth * m_indentation + kBranchIndicatorMargin;
}

QRect VirtualTreeView::branchCellRect(qsizetype row, int cellDepth) const
{
    const QRect rowRect = visualRowRect(row);
    if (!m_branchAreaVisible || !rowRect.isValid() || m_indentation <= 0 || cellDepth < 0)
        return QRect();
    const QModelIndex index = viewIndex(row);
    if (cellDepth == itemDepth(index) && !hasOwnBranchCell(index))
        return QRect();
    return QRect(rowRect.x() + cellDepth * m_indentation, rowRect.y(), m_indentation,
                 rowRect.height());
}

bool VirtualTreeView::isIndicatorPosition(const QModelIndex &index, const QPoint &viewportPos) const
{
    if (!m_branchIndicatorsVisible || !index.isValid())
        return false;
    const qsizetype row = viewItemForIndex(index);
    if (row < 0 || !hasBranchIndicator(row))
        return false;
    const int left = itemDepth(index) * m_indentation;
    return viewportPos.x() >= left && viewportPos.x() < left + m_indentation;
}

bool VirtualTreeView::hasBranchIndicator(qsizetype row) const
{
    if (!m_branchAreaVisible || !m_branchIndicatorsVisible)
        return false;
    const QModelIndex index = viewIndex(row);
    return index.isValid() && hasChildren(index);
}

void VirtualTreeView::afterMaterialize()
{
    detail::PerformanceScope performance(this, "tree.after-materialize");
    const QPointer<VirtualTreeView> guard(this);
    VirtualItemView::afterMaterialize();
    if (!guard)
        return;
    if (m_dragTransition && m_dragTransition->isActive()) {
        m_dragTransition->applyFrame();
        return;
    }
    invalidateBranchIndicators();
    syncRowGridLines();
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeView::refreshVisualStates()
{
    detail::PerformanceScope performance(this, "state.tree-all");
    VirtualItemView::refreshVisualStates();
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeView::refreshVisualState(const QModelIndex &index)
{
    detail::PerformanceScope performance(this, "state.tree-item");
    VirtualItemView::refreshVisualState(index);
    if (m_visualStateBackgroundVisible && index.isValid()) {
        viewport()->update(visualRect(index));
    }
}

void VirtualTreeView::syncRowGridLines()
{
    detail::PerformanceScope performance(this, "grid.tree-lines");
    QHash<qsizetype, QRect> desired;
    QSet<qsizetype> leadingEdges;
    const QRect viewportRect = viewport()->geometry();
    if (m_rowGridLinesVisible && m_rowLayout && viewportRect.width() > 0) {
        for (const MaterializedItem &item : materializedItems()) {
            const qsizetype row = viewItemForIndex(item.index);
            if (row < 0)
                continue;
            // Keep the trailing edge too: a tree without a bottom line looks
            // cut off when the last row is a branch or a leaf.
            if (m_rowLayout->spacingAfter(row) > 0)
                continue;
            const QRect rowRect = visualRowRect(row);
            if (!rowRect.intersects(viewport()->rect()))
                continue;
            const int left = rowGridLineInset(viewIndex(row));
            const QRect lineRect(viewportRect.x() + left,
                                 viewportRect.y() + rowRect.bottom() - m_rowGridLineWidth + 1,
                                 viewportRect.width() - left, m_rowGridLineWidth);
            const QRect paneRect = itemPaneRect(itemPaneForRow(row))
                .translated(viewportRect.topLeft());
            const QRect clipped = lineRect.intersected(paneRect);
            if (!clipped.isEmpty() && !isRowGridLineCoveredByPaneSeparator(clipped))
                desired.insert(row, clipped);
        }
        if (m_dragTransition && m_dragTransition->isActive()) {
            QRegion covered;
            for (const QRect &rect : desired)
                covered += rect;
            for (const QRect &rect : rowSpacingWidgetRectsInView())
                covered += rect;
            const QRect clip = viewport()->rect().translated(viewportRect.topLeft());
            qsizetype key = -1;
            for (const auto &boundary : m_dragTransition->placeholderBoundaries(m_rowGridLineWidth)) {
                const QRegion missing = QRegion(boundary.geometry.translated(viewportRect.topLeft())
                    .intersected(clip)).subtracted(covered);
                // Synthetic keys share the normal separator pool and disappear
                // automatically when feedback ends or grid lines are disabled.
                for (const QRect &rect : missing) {
                    desired.insert(key, rect);
                    if (!boundary.trailingEdge)
                        leadingEdges.insert(key);
                    --key;
                }
                covered += missing;
            }
        }
    }
    for (auto it = m_rowGridLines.begin(); it != m_rowGridLines.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->hide();
        performance.count("grid.lines-recycled");
        m_rowGridLinePool.append(it.value());
        it = m_rowGridLines.erase(it);
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        QWidget *line = m_rowGridLines.value(it.key(), nullptr);
        if (!line) {
            line = m_rowGridLinePool.isEmpty() ? new PixelAlignedHorizontalLine(this) : m_rowGridLinePool.takeLast();
            line->setObjectName(QStringLiteral("vivTreeRowGridLine"));
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setAutoFillBackground(false);
            m_rowGridLines.insert(it.key(), line);
        }
        QPalette colors = line->palette();
        QColor color = itemPaneSeparatorColor();
        color.setAlphaF(color.alphaF() * (it.key() >= 0 ? m_rowLayout->itemOpacity(it.key()) : 1.0));
        colors.setColor(QPalette::Window, color);
        line->setPalette(colors);
        static_cast<PixelAlignedHorizontalLine *>(line)->setTrailingEdge(!leadingEdges.contains(it.key()));
        line->setGeometry(it.value());
        line->clearMask();
        line->show();
        line->raise();
    }
    performance.count("grid.line-hosts", m_rowGridLines.size());
}

void VirtualTreeView::invalidateBranchIndicators()
{
    detail::PerformanceScope performance(this, "grid.tree-branch-invalidation");
    if (!m_branchAreaVisible || !m_branchIndicatorsVisible) {
        // Erase the indicators that are already painted before giving up the
        // strip.
        if (m_indicatorStripWidth > 0) {
            viewport()->update(QRect(0, 0, m_indicatorStripWidth, viewport()->height()));
            m_indicatorStripWidth = 0;
        }
        return;
    }

    int width = 0;
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        // The built-in indicator sits in the row's own cell; a renderer may also
        // decorate the ancestor cells, which reach (depth + 1) * indentation.
        const int depth = itemDepth(item.index);
        width = qMax(width, (depth + 1) * m_indentation);
        if (!m_branchRenderer)
            width = qMax(width, branchIndicatorX(row) + qMin(m_branchIconSize,
                qMax(0, m_indentation - kBranchIndicatorMargin)) + 1);
    }
    // The strip invalidated by the previous pass is part of the region too: an
    // indicator that left the viewport must be erased where it was painted.
    const int strip = qMax(width, m_indicatorStripWidth);
    m_indicatorStripWidth = width;
    if (strip > 0)
        viewport()->update(QRect(0, 0, strip, viewport()->height()));
}

void VirtualTreeView::paintEvent(QPaintEvent *event)
{
    detail::PerformanceScope performance(this, "paint.tree", detail::PerformanceDomain::Paint);
    VirtualItemView::paintEvent(event);
    QPainter painter(viewport());
    if (m_visualStateBackgroundVisible)
        paintVisualStateBackgrounds(&painter, event->rect());
    if (m_branchAreaVisible && m_branchIndicatorsVisible)
        paintBranchIndicators(&painter);
}

QRect VirtualTreeView::visualStateBackgroundRect(const QModelIndex &index,
                                               const QRect &rowGeometry) const
{
    QRect background;
    if (m_visualStateBackgroundExtent == VisualStateBackgroundExtent::WidgetOnly
        || m_visualStateBackgroundExtent == VisualStateBackgroundExtent::WidgetAndIcon) {
        QWidget *widget = widgetForIndex(index);
        if (!widget || !widget->isVisible())
            return {};
        // Pane and transition hosts can sit between the widget and viewport.
        // Keep the full rectangle for corners; callers clip the revealed part.
        background = QRect(viewport()->mapFromGlobal(widget->mapToGlobal(QPoint())), widget->size());
        if (m_visualStateBackgroundExtent == VisualStateBackgroundExtent::WidgetAndIcon) {
            // A reserved but empty cell must not extend a leaf's background.
            const qsizetype row = viewItemForIndex(index);
            if (hasBranchIndicator(row))
                background = background.united(branchCellRect(row, itemDepth(index)));
        }
    } else {
        const int depth = itemDepth(index);
        const bool nodeOnly = m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeOnly
            || (m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeAndIcon
                && !hasBranchIndicator(viewItemForIndex(index)));
        const qint64 cells = nodeOnly
            ? qint64(depth) + int(hasOwnBranchCell(index))
            : m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeAndIcon
                ? qint64(depth) : 0;
        const int left = int(qMin<qint64>(viewport()->width(),
                                         qMax<qint64>(0, cells) * m_indentation));
        background = QRect(left, rowGeometry.y(), viewport()->width() - left, rowGeometry.height());
        // Insets and centering follow the natural row, not its currently revealed slice.
        if (m_rowLayout->hasTransientGeometry()
            && (!m_visualStateBackgroundMargins.isNull() || m_visualStateBackgroundHeight > 0))
            background.setHeight(m_rowLayout->sizeIndex()->sizeOf(viewItemForIndex(index)));
    }
    const QMargins &margins = m_visualStateBackgroundMargins;
    if (qint64(margins.left()) + margins.right() >= background.width()
        || qint64(margins.top()) + margins.bottom() >= background.height())
        return {};
    background.adjust(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    if (m_visualStateBackgroundHeight > 0) {
        const int height = qMin(m_visualStateBackgroundHeight, background.height());
        background = QRect(background.x(), background.y() + (background.height() - height) / 2,
                           background.width(), height);
    }
    return background;
}

void VirtualTreeView::paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty)
{
    detail::PerformanceScope performance(this, "paint.tree-state", detail::PerformanceDomain::Paint);
    const QRect viewportRect = viewport()->rect();
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        const QRect rowGeometry = visualRowRect(row);
        if (!item.index.isValid() || !rowGeometry.intersects(dirty))
            continue;
        const VisualState state = visualState(QModelIndex(item.index));
        if (state.hoverProgress <= 0.0 && state.selectedProgress <= 0.0)
            continue;
        const QRect background = visualStateBackgroundRect(item.index, rowGeometry);
        const QRect clip = rowGeometry.intersected(itemPaneRect(itemPaneForRow(row)))
            .intersected(viewportRect).intersected(dirty);
        if (background.isEmpty() || clip.isEmpty())
            continue;
        performance.count("paint.state-rows");
        painter->save();
        painter->setOpacity(state.hoverProgress * m_rowLayout->itemOpacity(row));
        fillPixelAlignedRoundedRegion(painter, background, QRegion(clip),
            m_visualStateCornerRadius, hoverBackgroundColor());
        painter->setOpacity(state.selectedProgress * m_rowLayout->itemOpacity(row));
        fillPixelAlignedRoundedRegion(painter, background, QRegion(clip),
            m_visualStateCornerRadius, selectedBackgroundColor());
        painter->restore();
    }
}

void VirtualTreeView::paintBranchIndicators(QPainter *painter)
{
    detail::PerformanceScope performance(this, "paint.tree-branches", detail::PerformanceDomain::Paint);
    const QRect viewportRect = viewport()->rect();
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;

        const QRect rowRect = visualRowRect(row);
        const QRect clip = rowRect.intersected(itemPaneRect(itemPaneForRow(row)))
            .intersected(viewportRect);
        if (clip.isEmpty())
            continue;

        const QModelIndex index = item.index;
        const int depth = itemDepth(index);
        const auto animatedCell = [this, row](int cellDepth) {
            QRect cell = branchCellRect(row, cellDepth);
            if (m_rowLayout->hasTransientGeometry())
                cell.setHeight(m_rowLayout->sizeIndex()->sizeOf(row));
            return cell;
        };
        if (!m_branchRenderer) {
            if (hasBranchIndicator(row)) {
                performance.count("paint.branch-cells");
                painter->save();
                painter->setOpacity(painter->opacity() * m_rowLayout->itemOpacity(row));
                painter->setClipRect(clip, Qt::IntersectClip);
                paintDefaultBranchIndicator(
                    painter, branchState(index, depth), index, animatedCell(depth));
                painter->restore();
            }
            continue;
        }

        // A custom renderer owns the whole decoration, so it also receives the
        // cells of the ancestors of this row (the connector lines).
        for (int cellDepth = 0; cellDepth <= depth; ++cellDepth) {
            const QRect cellRect = animatedCell(cellDepth);
            if (cellRect.isEmpty() || !cellRect.intersects(clip))
                continue;
            performance.count("paint.branch-cells");
            painter->save();
            painter->setOpacity(painter->opacity() * m_rowLayout->itemOpacity(row));
            painter->setClipRect(clip, Qt::IntersectClip);
            detail::performanceCall(this, "callback.branch.paint", [&] {
                return m_branchRenderer->paintBranch(painter, branchState(index, cellDepth), index, cellRect);
            });
            painter->restore();
        }
    }
}

void VirtualTreeView::paintDefaultBranchIndicator(QPainter *painter,
                                                 const BranchIndicatorState &state,
                                                 const QModelIndex &index, const QRect &cellRect)
{
    detail::PerformanceScope performance(this, "paint.tree-branch-icon", detail::PerformanceDomain::Paint);
    if (!state.adjoinsItem || !state.hasChildren)
        return;
    const qreal progress = m_expansionTransition->expansionProgress(index, state.isExpanded);
    const bool hasExpandedIcon = !m_expandedBranchIcon.isNull();
    const bool hasCollapsedIcon = !m_collapsedBranchIcon.isNull();
    if (!hasExpandedIcon && !hasCollapsedIcon) {
        // The built-in decoration is one expanded triangle reused for both states.
        BranchIndicatorState expandedState = state;
        expandedState.isExpanded = true;
        BranchIndicatorRenderer::paintBuiltinBranch(painter, expandedState, cellRect, palette(),
                                                    m_branchIconSize, m_branchIconColor,
                                                    m_branchIndicatorAlignment,
                                                    m_branchIndicatorPadding,
                                                    (progress - 1.0) * 90.0);
        return;
    }
    const bool separateIcons = hasExpandedIcon && hasCollapsedIcon;
    const QIcon &icon = (separateIcons ? state.isExpanded : hasExpandedIcon)
        ? m_expandedBranchIcon : m_collapsedBranchIcon;
    const qreal rotation = separateIcons ? 0.0
        : (hasExpandedIcon ? progress - 1.0 : progress) * 90.0;
    const QRectF target = BranchIndicatorRenderer::branchIndicatorTargetRectF(
        QRectF(cellRect), qreal(m_branchIconSize), m_branchIndicatorAlignment,
        QMarginsF(m_branchIndicatorPadding));
    if (target.isEmpty())
        return;
    painter->save();
    painter->setClipRect(QRectF(cellRect), Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->translate(target.center());
    painter->rotate(rotation);
    painter->translate(-target.center());
    if (!m_branchIconColor.isValid()) {
        painter->translate(target.topLeft());
        icon.paint(painter, QRect(QPoint(), target.size().toSize()));
        painter->restore();
        return;
    }
    const qreal dpr = painter->device()->devicePixelRatioF();
    const qreal size = target.width();
    QPixmap pixmap = icon.pixmap(QSize(qMax(1, qCeil(size * dpr)),
                                     qMax(1, qCeil(size * dpr))));
    if (pixmap.isNull()) {
        painter->restore();
        return;
    }
    pixmap.setDevicePixelRatio(dpr);
    {
        QPainter tint(&pixmap);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(pixmap.rect(), m_branchIconColor);
    }
    QSizeF logicalSize = pixmap.size();
    logicalSize.scale(target.size(), Qt::KeepAspectRatio);
    const QRectF imageRect(target.center()
        - QPointF(logicalSize.width() / 2.0, logicalSize.height() / 2.0), logicalSize);
    painter->drawPixmap(imageRect, pixmap, QRectF(pixmap.rect()));
    painter->restore();
}

void VirtualTreeView::toggleIfBranchClicked(const QPoint &viewportPos, bool *handled)
{
    if (handled)
        *handled = false;

    const QModelIndex index = VirtualItemView::indexAt(viewportPos);
    if (!isIndicatorPosition(index, viewportPos))
        return;

    toggleExpanded(index);
    if (handled)
        *handled = true;
}

void VirtualTreeView::mousePressEvent(QMouseEvent *event)
{
    // A new gesture: the previous indicator toggle (if any) no longer applies.
    m_indicatorPressToggled = false;
    if (event->button() == Qt::LeftButton) {
        bool handled = false;
        toggleIfBranchClicked(eventPosition(event), &handled);
        if (handled) {
            m_indicatorPressToggled = true;
            event->accept();
            return;
        }
    }
    VirtualItemView::mousePressEvent(event);
}

void VirtualTreeView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_indicatorPressToggled && event->button() == Qt::LeftButton) {
        // The press toggled a branch: this gesture is not a click on the row
        // (no clicked()/selection), and the flag stays set for the double click
        // event that follows a double click on the indicator.
        event->accept();
        return;
    }
    VirtualItemView::mouseReleaseEvent(event);
}

void VirtualTreeView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint viewportPos = eventPosition(event);
        const QModelIndex index = indexAt(viewportPos);
        // A double click on the indicator toggles once: the press of this very
        // gesture already did it.
        const bool pressToggledThisBranch = m_indicatorPressToggled
            && isIndicatorPosition(VirtualItemView::indexAt(viewportPos), viewportPos);
        m_indicatorPressToggled = false;
        if (!pressToggledThisBranch && index.isValid() && hasChildren(index))
            toggleExpanded(index);
    }
    VirtualItemView::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
// Drag & drop (§38): drop between siblings or into an item
// ---------------------------------------------------------------------------

bool VirtualTreeView::acceptsDropInto(const QModelIndex &index) const
{
    QAbstractItemModel *targetModel = model();
    return targetModel && index.isValid()
        && (targetModel->flags(index) & Qt::ItemIsDropEnabled);
}

VirtualItemView::DropTarget VirtualTreeView::resolveDropTarget(const QPoint &viewportPos) const
{
    DropTarget target;
    QAbstractItemModel *treeModel = model();
    if (!treeModel)
        return target;

    // The rows of the tree are the children of the root: the empty area below
    // them (and an empty tree) extends that level instead of being "outside the
    // content", which is what the flat kernel would report.
    const qsizetype rows = viewItemCount();
    const QRect lastRect = rows > 0 ? geometryForViewRow(rows - 1) : QRect();
    if (rows <= 0 || viewportPos.y() > lastRect.bottom()) {
        target.parent = m_rootIndex.isValid() ? QModelIndex(m_rootIndex) : QModelIndex();
        target.row = treeModel->rowCount(target.parent);
        target.trailing = true;
        return target;
    }

    // Drag targeting uses natural rows; animated widgets must not retarget themselves.
    const QModelIndex index = m_dragTransition && m_dragTransition->isActive()
        ? VirtualItemView::indexAt(viewportPos) : indexAt(viewportPos);
    if (!index.isValid())
        return target;
    const qsizetype row = viewItemForIndex(index);
    if (row < 0)
        return target;

    // Three bands per row, like QTreeView: the top/bottom quarter inserts a
    // sibling, the middle drops into the item (only if the model takes it).
    const QRect rowRect = geometryForViewRow(row);
    const int y = qBound(rowRect.top(), viewportPos.y(), rowRect.bottom());
    const int band = qMax(3, rowRect.height() / 4);
    const bool above = y < rowRect.top() + band;
    const bool below = y > rowRect.bottom() - band;
    if (!above && !below && acceptsDropInto(index)) {
        target.parent = index;
        target.row = treeModel->rowCount(index);
        target.ontoItem = true;
        return target;
    }
    const bool insertAbove = above || (!below && y < rowRect.center().y());
    target.parent = index.parent();
    target.row = index.row() + (insertAbove ? 0 : 1);
    return target;
}

QRect VirtualTreeView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || !model())
        return QRect();
    if (target.ontoItem) {
        const qsizetype row = viewItemForIndex(target.parent);
        return row >= 0 ? geometryForViewRow(row) : QRect();
    }
    return insertionLineRect(target);
}

qsizetype VirtualTreeView::visibleSubtreeEnd(QModelIndex node) const
{
    QAbstractItemModel *treeModel = model();
    if (!treeModel)
        return 0;
    while (node.isValid() && node != rootIndex()) {
        const QModelIndex next = treeModel->index(node.row() + 1, 0, node.parent());
        if (next.isValid()) {
            const qsizetype row = viewItemForIndex(next);
            if (row >= 0)
                return row;
        }
        node = node.parent();
    }
    return viewItemCount();
}

QRect VirtualTreeView::insertionLineRect(const DropTarget &target) const
{
    QAbstractItemModel *treeModel = model();
    if (!treeModel)
        return QRect();
    const QModelIndex parent = target.parent;
    const int row = target.row;
    const qsizetype rows = viewItemCount();
    // Match the destination sibling's content inset, even when the boundary
    // is anchored to a deeper descendant or the pointer is below the tree.
    const QModelIndex sibling = treeModel->index(qMax(0, row - 1), 0, parent);
    const int depth = parent == rootIndex() ? 0 : itemDepth(parent) + 1;
    const qint64 cells = qint64(depth) + int(hasOwnBranchCell(sibling));
    const int left = int(qBound(qint64(0), cells * m_indentation, qint64(viewport()->width())));
    const auto lineAt = [this, left](int y) {
        return QRect(left, y, viewport()->width() - left, 2);
    };
    // The empty area below the tree: the line marks the end of the content, which
    // is the only row boundary that area can borrow.
    if (target.trailing) {
        if (rows <= 0)
            return lineAt(0);
        const QRect last = geometryForViewRow(rows - 1);
        return lineAt(last.bottom());
    }
    // A row boundary of the visible tree: below the row that precedes the
    // insertion point (the hovered row itself when the drop is "after" it), or
    // above the row that follows it.
    if (row > 0) {
        const QModelIndex precedingNode = treeModel->index(row - 1, 0, parent);
        const qsizetype before = viewItemForIndex(precedingNode);
        if (before >= 0) {
            // Inserting beside an expanded parent belongs below its entire
            // subtree, at the same height as appending beside its last child.
            const qsizetype last = qMax(before, visibleSubtreeEnd(precedingNode) - 1);
            const QRect rect = geometryForViewRow(last);
            return lineAt(rect.bottom());
        }
    }
    const qsizetype after = viewItemForIndex(treeModel->index(row, 0, parent));
    if (after >= 0) {
        const QRect rect = geometryForViewRow(after);
        return lineAt(rect.top() - 1);
    }
    // Inside a collapsed branch the insertion is drawn directly below it.
    const qsizetype parentRow = viewItemForIndex(parent);
    if (parentRow >= 0) {
        const QRect rect = geometryForViewRow(parentRow);
        return lineAt(rect.bottom());
    }
    if (rows > 0) {
        const QRect rect = geometryForViewRow(0);
        return lineAt(rect.top());
    }
    return lineAt(0);
}

// ---------------------------------------------------------------------------
// Keyboard navigation
// ---------------------------------------------------------------------------

bool VirtualTreeView::handleItemKeyPress(QKeyEvent *event)
{
    const QModelIndex current = currentIndex();
    if (!current.isValid())
        return false;

    switch (event->key()) {
    case Qt::Key_Right:
        if (hasChildren(current) && !isExpanded(current)) {
            // Collapsed branch: expand it.
            expand(current);
        } else if (hasChildren(current)) {
            // Expanded branch: move to the first child.
            const QModelIndex child = model()->index(0, 0, current);
            const qsizetype row = viewItemForIndex(child);
            if (row >= 0)
                moveCurrentToItem(row);
        }
        return true;
    case Qt::Key_Left:
        if (hasChildren(current) && isExpanded(current)) {
            // Expanded branch: collapse it.
            collapse(current);
        } else {
            // Otherwise move to the parent.
            const QModelIndex parent = current.parent();
            const qsizetype row = viewItemForIndex(parent);
            if (row >= 0)
                moveCurrentToItem(row);
        }
        return true;
    case Qt::Key_Asterisk: // expand the item and its descendants (QTreeView)
        if (hasChildren(current))
            expandRecursively(current);
        return true;
    default:
        break;
    }
    return false;
}

} // namespace viv
