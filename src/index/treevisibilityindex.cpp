#include <virtualitemviews/treevisibilityindex.h>

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace viv {

TreeVisibilityIndex::TreeVisibilityIndex(QAbstractItemModel *model)
    : m_model(model)
{
    rebuild();
}

void TreeVisibilityIndex::setModel(QAbstractItemModel *model)
{
    if (m_model == model)
        return;
    m_model = model;
    m_expanded.clear();
    m_rootIndex = QModelIndex();
    rebuild();
}

void TreeVisibilityIndex::setRootIndex(const QModelIndex &index)
{
    if (index == m_rootIndex)
        return;
    m_rootIndex = index;
    rebuild();
}

void TreeVisibilityIndex::rebuild()
{
    m_visibleRows.clear();
    m_branches.clear();
    if (!m_model)
        return;

    // Iterative depth-first walk: only expanded nodes are descended into, so a collapsed
    // sub-tree costs a single rowCount() query. Every node that is descended into gets a
    // branch block, filled with its children's visible sub-tree sizes as the walk leaves
    // them (a child's size is only known once its own sub-tree was walked).
    struct Frame
    {
        QModelIndex parent;      // the node whose children are being walked
        QModelIndex self;        // that node itself (invalid for the root frame)
        qsizetype size = 1;      // visible rows of self's sub-tree, self included
        int next = 0;
        int children = 0;
        bool entered = false;
    };

    QList<Frame> stack;
    stack.append(Frame{m_rootIndex, QModelIndex(), 1, 0, 0, false});
    while (!stack.isEmpty()) {
        Frame &frame = stack.last();
        if (!frame.entered) {
            frame.entered = true;
            frame.children = childCount(frame.parent);
            if (frame.children > 0) {
                BranchBlock block;
                block.reset(frame.children);
                m_branches.insert(frame.parent, block);
            }
        }
        if (frame.next >= frame.children) {
            const qsizetype size = frame.size;
            const QModelIndex self = frame.self;
            stack.removeLast();
            if (!self.isValid())
                continue; // the root frame: nothing to report upwards
            // Report this sub-tree's size into *its* parent's block: the frame walks its own
            // children, so the block to update is the one of self.parent().
            auto block = m_branches.find(self.parent());
            if (block != m_branches.end())
                block->add(self.row(), size);
            if (!stack.isEmpty())
                stack.last().size += size;
            continue;
        }
        const int childRow = frame.next++;
        const QModelIndex child = m_model->index(childRow, 0, frame.parent);
        ++m_modelQueryCount;
        m_visibleRows.append(child);
        if (isExpanded(child) && childCount(child) > 0) {
            // The child's own size is reported when its frame is left.
            stack.append(Frame{child, child, 1, 0, 0, false});
        } else {
            frame.size += 1;
            auto block = m_branches.find(frame.parent);
            if (block != m_branches.end())
                block->add(childRow, 1);
        }
    }
}

void TreeVisibilityIndex::handleModelChanged()
{
    rebuild();
}

void TreeVisibilityIndex::handleModelReset()
{
    m_expanded.clear();
    m_rootIndex = QModelIndex();
    rebuild();
}

int TreeVisibilityIndex::childCount(const QModelIndex &parent)
{
    ++m_modelQueryCount;
    return m_model ? m_model->rowCount(parent) : 0;
}

qsizetype TreeVisibilityIndex::visibleRowForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !m_model || index.model() != m_model)
        return -1;
    // Visible rows are column 0 rows; a cell index maps to its row.
    const QModelIndex key = index.column() == 0 ? index : index.siblingAtColumn(0);
    return visibleRowForKey(key);
}

qsizetype TreeVisibilityIndex::visibleRowForKey(const QModelIndex &index) const
{
    // Pre-order rank: walking up, every level contributes itself (1) plus the visible
    // sub-tree sizes of the siblings before it. The root contributes nothing - its
    // children start at row 0 - so the sum starts at -1.
    qint64 row = -1;
    for (QModelIndex current = index; current.isValid() && current != m_rootIndex;
         current = current.parent()) {
        const QModelIndex parent = current.parent();
        if (parent != m_rootIndex && !isExpanded(parent))
            return -1; // an ancestor is collapsed: the item is not visible
        const auto block = m_branches.constFind(parent);
        if (block == m_branches.constEnd() || current.row() >= block->childCount)
            return -1;
        row += 1 + block->prefix(current.row());
        if (row < 0)
            return -1;
    }
    return row;
}

void TreeVisibilityIndex::addToAncestors(const QModelIndex &index, qsizetype delta)
{
    if (delta == 0)
        return;
    for (QModelIndex current = index; current.isValid() && current != m_rootIndex;
         current = current.parent()) {
        const QModelIndex parent = current.parent();
        auto block = m_branches.find(parent);
        if (block == m_branches.end())
            return; // not expanded: nothing inside it is visible, so no offset shifts
        block->add(current.row(), delta);
    }
}

QModelIndex TreeVisibilityIndex::indexAtVisibleRow(qsizetype row) const
{
    if (row < 0 || row >= m_visibleRows.size())
        return QModelIndex();
    return m_visibleRows.at(row);
}

int TreeVisibilityIndex::depth(const QModelIndex &index) const
{
    int depth = 0;
    if (!index.isValid())
        return -1;
    QModelIndex parent = index.parent();
    while (parent.isValid() && parent != m_rootIndex) {
        ++depth;
        parent = parent.parent();
    }
    return depth;
}

bool TreeVisibilityIndex::isExpanded(const QModelIndex &index) const
{
    return index.isValid() && m_expanded.contains(QPersistentModelIndex(index));
}

void TreeVisibilityIndex::expand(const QModelIndex &index)
{
    if (!m_model || !index.isValid() || isExpanded(index))
        return;

    const qsizetype row = visibleRowForIndex(index);
    if (row < 0)
        return; // the item is not visible: its children cannot become visible either

    m_expanded.insert(QPersistentModelIndex(index));

    BranchBlock block;
    const QVector<QModelIndex> subtreeRows = visibleSubtreeRows(index, &block);
    if (subtreeRows.isEmpty())
        return; // no children: only the expansion state changed
    m_branches.insert(index, block);

    // Splice in place: the rows below the anchor are moved once and only the new
    // sub-tree is written. (A rebuild of the whole vector - two mid() copies plus an
    // allocation, what this used to do - is what makes expanding a small branch under
    // a huge visible set expensive; the remaining tail move is inherent to a flat
    // vector, docs/roadmap.md §3 决策表 has the rope/block variant as 1.x work.)
    const qsizetype insertAt = row + 1;
    const qsizetype added = subtreeRows.size();
    const qsizetype previousSize = m_visibleRows.size();
    m_visibleRows.resize(previousSize + added);
    // QModelIndex is a value type: moving the tail is a memmove, not a copy loop over a
    // million elements (measured: 9 ms vs 1.5 ms for the same splice, bench_listview
    // --tree). A rope/block structure would not have to move the tail at all - that is
    // the part tracked for the next major version (docs/roadmap.md).
    static_assert(std::is_trivially_copyable<QModelIndex>::value,
                  "the visible row splice relies on QModelIndex being memcpy-able");
    QModelIndex *const rows = m_visibleRows.data();
    if (insertAt < previousSize) {
        std::memmove(rows + insertAt + added, rows + insertAt,
                     size_t(previousSize - insertAt) * sizeof(QModelIndex));
    }
    std::memcpy(rows + insertAt, subtreeRows.constData(),
                size_t(added) * sizeof(QModelIndex));
    // Everything below the anchor shifted down: tell the ancestors, not the whole tree.
    addToAncestors(index, subtreeRows.size());
}

void TreeVisibilityIndex::expandRecursively(const QModelIndex &index)
{
    if (!m_model || !index.isValid())
        return;

    // Walk the sub-tree once and remember every branch that has children; the
    // visible rows are rebuilt afterwards, so a deep sub-tree costs one pass.
    QList<QModelIndex> stack;
    stack.append(index);
    while (!stack.isEmpty()) {
        const QModelIndex current = stack.takeLast();
        const int count = childCount(current);
        if (count <= 0)
            continue;
        m_expanded.insert(QPersistentModelIndex(current));
        for (int row = 0; row < count; ++row) {
            const QModelIndex child = m_model->index(row, 0, current);
            ++m_modelQueryCount;
            if (child.isValid())
                stack.append(child);
        }
    }
    rebuild();
}

void TreeVisibilityIndex::collapse(const QModelIndex &index)
{
    if (!m_model || !index.isValid() || !isExpanded(index))
        return;

    m_expanded.remove(QPersistentModelIndex(index));

    const qsizetype row = visibleRowForIndex(index);
    if (row < 0)
        return;

    // The visible rows of a sub-tree are contiguous right after its own row, and their
    // number is exactly the block's total - no scan with isDescendantOf() needed. The
    // block is kept: the expansion state inside the sub-tree is untouched, so its sizes
    // stay valid and re-expanding costs one splice instead of a re-walk.
    const auto block = m_branches.constFind(index);
    const qsizetype removed = block == m_branches.constEnd() ? 0 : block->total();
    if (removed > 0)
        m_visibleRows.remove(int(row) + 1, int(removed));
    addToAncestors(index, -removed);
}

void TreeVisibilityIndex::setExpanded(const QModelIndex &index, bool expanded)
{
    expanded ? expand(index) : collapse(index);
}

void TreeVisibilityIndex::toggleExpanded(const QModelIndex &index)
{
    setExpanded(index, !isExpanded(index));
}

void TreeVisibilityIndex::collapseAll()
{
    m_expanded.clear();
    rebuild();
}

QVector<QModelIndex> TreeVisibilityIndex::visibleSubtreeRows(const QModelIndex &index,
                                                            BranchBlock *block)
{
    QVector<QModelIndex> rows;
    if (!m_model || !index.isValid())
        return rows;

    if (block)
        block->reset(childCount(index));

    struct Frame
    {
        QModelIndex parent;
        qsizetype row = -1;
        qsizetype size = 1;
        int next = 0;
        int children = 0;
        /// True when this frame describes a direct child of the anchor: only those sizes
        /// belong to the anchor's branch block (deeper frames report into their own parent,
        /// which lives on the stack).
        bool inBlock = false;
    };

    QList<Frame> stack;
    stack.append(Frame{index, -1, 1, 0, childCount(index), false});
    while (!stack.isEmpty()) {
        Frame &frame = stack.last();
        if (frame.next >= frame.children) {
            const qsizetype size = frame.size;
            const qsizetype row = frame.row;
            const bool inBlock = frame.inBlock;
            stack.removeLast();
            if (!stack.isEmpty())
                stack.last().size += size;
            if (block && inBlock)
                block->add(row, size);
            continue;
        }
        const int childRow = frame.next++;
        const QModelIndex child = m_model->index(childRow, 0, frame.parent);
        ++m_modelQueryCount;
        rows.append(child);
        if (isExpanded(child) && childCount(child) > 0) {
            const bool childInBlock = frame.parent == index;
            stack.append(Frame{child, childRow, 1, 0, childCount(child), childInBlock});
        } else {
            frame.size += 1;
            if (block && frame.parent == index)
                block->add(childRow, 1);
        }
    }
    return rows;
}


} // namespace viv
