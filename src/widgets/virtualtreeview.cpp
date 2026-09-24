#include <virtualitemviews/virtualtreeview.h>

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace viv {

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
    setSelectionBehavior(SelectionBehavior::SelectRows);
}

VirtualTreeView::~VirtualTreeView()
{
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    delete m_visibility;
}

void VirtualTreeView::setModel(QAbstractItemModel *model)
{
    QAbstractItemModel *previous = VirtualItemView::model();
    if (previous == model)
        return;
    if (previous)
        disconnect(previous, nullptr, this, nullptr);

    m_visibility->setModel(model);
    m_rootIndex = QModelIndex();
    m_visibility->setRootIndex(m_rootIndex);

    VirtualItemView::setModel(model);
    connectModelSignals(model);
    resetLayoutForNewModel();
    relayout();
}

void VirtualTreeView::connectModelSignals(QAbstractItemModel *model)
{
    if (!model)
        return;

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
        m_rootIndex = QModelIndex();
        m_visibility->handleModelReset();
        refreshVisibility(false);
    });
}

void VirtualTreeView::onStructureChanged()
{
    // Visible rows, expansion state and item identity all live in the index.
    m_visibility->handleModelChanged();
    refreshVisibility(false);
}

void VirtualTreeView::refreshVisibility(bool keepAnchor)
{
    if (m_updatingVisibility)
        return;
    m_updatingVisibility = true;
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

qsizetype VirtualTreeView::visibleRowCount() const
{
    return m_visibility->visibleRowCount();
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

void VirtualTreeView::setRootIndex(const QModelIndex &index)
{
    if (index == m_rootIndex)
        return;
    m_rootIndex = index;
    m_visibility->setRootIndex(index);
    refreshVisibility(false);
}

bool VirtualTreeView::hasChildren(const QModelIndex &index) const
{
    QAbstractItemModel *m = model();
    return m && index.isValid() && m->rowCount(index) > 0;
}

void VirtualTreeView::expand(const QModelIndex &index)
{
    if (!index.isValid() || m_visibility->isExpanded(index))
        return;
    // The anchor must be captured *before* the visible row mapping changes,
    // otherwise the view can jump by the number of inserted rows.
    setPendingAnchor(captureAnchor());
    m_visibility->expand(index);
    refreshVisibility(false);
    emit expanded(index);
}

void VirtualTreeView::collapse(const QModelIndex &index)
{
    if (!index.isValid() || !m_visibility->isExpanded(index))
        return;
    setPendingAnchor(captureAnchor());
    m_visibility->collapse(index);
    refreshVisibility(false);
    emit collapsed(index);
}

void VirtualTreeView::expandRecursively(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const bool wasExpanded = m_visibility->isExpanded(index);
    setPendingAnchor(captureAnchor());
    m_visibility->expandRecursively(index);
    refreshVisibility(false);
    if (!wasExpanded)
        emit expanded(index);
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
    return m_visibility->isExpanded(index);
}

void VirtualTreeView::collapseAll()
{
    setPendingAnchor(captureAnchor());
    m_visibility->collapseAll();
    refreshVisibility(false);
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

void VirtualTreeView::setBranchIndicatorsVisible(bool visible)
{
    if (m_branchIndicatorsVisible == visible)
        return;
    m_branchIndicatorsVisible = visible;
    // Turning them off must erase what is painted; turning them on must repaint
    // the strip (the rows themselves never carry the indicators).
    invalidateBranchIndicators();
}

void VirtualTreeView::setBranchIndicatorRenderer(BranchIndicatorRenderer *renderer, bool takeOwnership)
{
    if (m_branchRenderer == renderer)
        return;
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
    state.isExpanded = m_visibility->isExpanded(cell);
    state.hasSiblings = cell.row() + 1 < model()->rowCount(cell.parent());
    return state;
}

QRect VirtualTreeView::geometryForViewRow(qsizetype row) const
{
    const QRect rowRect = VirtualItemView::geometryForViewRow(row);
    if (!rowRect.isValid())
        return rowRect;
    // One indentation step per level plus one for the branch indicator.
    const int left = m_indentation * (itemDepth(viewIndex(row)) + 1);
    return QRect(rowRect.x() + left, rowRect.y(), qMax(0, rowRect.width() - left), rowRect.height());
}

int VirtualTreeView::branchIndicatorX(qsizetype row) const
{
    const int depth = itemDepth(viewIndex(row));
    return depth * m_indentation + kBranchIndicatorMargin;
}

QRect VirtualTreeView::branchCellRect(qsizetype row, int cellDepth) const
{
    const QRect rowRect = VirtualItemView::geometryForViewRow(row);
    if (!rowRect.isValid() || m_indentation <= 0 || cellDepth < 0)
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
    if (!m_branchIndicatorsVisible)
        return false;
    const QModelIndex index = viewIndex(row);
    return index.isValid() && hasChildren(index);
}

void VirtualTreeView::afterMaterialize()
{
    VirtualItemView::afterMaterialize();
    invalidateBranchIndicators();
}

void VirtualTreeView::invalidateBranchIndicators()
{
    if (!m_branchIndicatorsVisible) {
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
            width = qMax(width, branchIndicatorX(row) + kBranchIndicatorSize + 1);
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
    VirtualItemView::paintEvent(event);
    if (!m_branchIndicatorsVisible)
        return;

    QPainter painter(viewport());
    paintBranchIndicators(&painter);
}

void VirtualTreeView::paintBranchIndicators(QPainter *painter)
{
    const QRect viewportRect = viewport()->rect();
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;

        const QRect rowRect = VirtualItemView::geometryForViewRow(row);
        if (rowRect.height() <= 0 || !rowRect.intersects(viewportRect))
            continue;

        const QModelIndex index = item.index;
        const int depth = itemDepth(index);
        if (!m_branchRenderer) {
            if (hasBranchIndicator(row))
                BranchIndicatorRenderer::paintBuiltinBranch(
                    painter, branchState(index, depth), branchCellRect(row, depth), palette());
            continue;
        }

        // A custom renderer owns the whole decoration, so it also receives the
        // cells of the ancestors of this row (the connector lines).
        for (int cellDepth = 0; cellDepth <= depth; ++cellDepth) {
            const QRect cellRect = branchCellRect(row, cellDepth);
            if (cellRect.isEmpty() || !cellRect.intersects(viewportRect))
                continue;
            painter->save();
            m_branchRenderer->paintBranch(painter, branchState(index, cellDepth), index, cellRect);
            painter->restore();
        }
    }
}

void VirtualTreeView::toggleIfBranchClicked(const QPoint &viewportPos, bool *handled)
{
    if (handled)
        *handled = false;

    const QModelIndex index = indexAt(viewportPos);
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
            && isIndicatorPosition(index, viewportPos);
        m_indicatorPressToggled = false;
        if (!pressToggledThisBranch && index.isValid() && hasChildren(index))
            toggleExpanded(index);
    }
    VirtualItemView::mouseDoubleClickEvent(event);
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
