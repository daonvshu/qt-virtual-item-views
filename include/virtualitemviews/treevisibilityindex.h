#pragma once

#include <virtualitemviews/global.h>

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QSet>
#include <QVector>
#include <QtGlobal>

namespace viv {

/// Maps a QAbstractItemModel tree onto the flat list of *visible* rows.
///
/// This is the Tree counterpart of SizeIndex: the virtualization kernel works
/// on visible rows, and the tree only differs by how a visible row maps back to
/// a QModelIndex (plus depth and branch state).
///
/// Performance contract:
///  - expand()/collapse() splice the flat visible list and traverse the model
///    only for the expanded subtree; the whole tree is never re-flattened,
///  - indexAtVisibleRow() is O(1),
///  - visibleRowForIndex()/isVisible() are O(depth x log(siblings)): the row of an
///    item is the sum, over its ancestors, of "1 + the visible sub-tree sizes of
///    the preceding siblings". Those sizes live in one Fenwick tree per expanded
///    parent (see BranchBlock), so a wide parent costs 4 bytes per child and
///    nothing is rebuilt when the expansion state changes,
///  - no per-row lookup table at all: a table keyed by the model index *value*
///    would have to be rebuilt on every expand/collapse (that is the O(visible
///    rows) cost the benchmark used to show), and keying by
///    QPersistentModelIndex would register a million persistent indexes with the
///    model instead,
///  - a structural change rebuilds the visible list (O(visible rows)), an
///    expand()/collapse() only walks its own sub-tree and pokes the ancestors on
///    the path - the model is never walked for a collapsed sub-tree,
///  - model mutations (insert/remove/move/layoutChanged) rebuild the visible
///    list while keeping the expansion state; modelReset clears it too.
class VIRTUALITEMVIEWS_EXPORT TreeVisibilityIndex
{
public:
    explicit TreeVisibilityIndex(QAbstractItemModel *model = nullptr);

    void setModel(QAbstractItemModel *model);
    QAbstractItemModel *model() const { return m_model; }

    /// Root of the flattened sub-tree (invalid = the model's invisible root).
    QModelIndex rootIndex() const { return m_rootIndex; }
    void setRootIndex(const QModelIndex &index);

    /// Rebuilds the visible row list from the model, keeping the expansion state.
    void rebuild();
    /// Hook for rowsInserted/rowsRemoved/rowsMoved/layoutChanged.
    void handleModelChanged();
    /// Hook for modelReset: resets the expansion state as well.
    void handleModelReset();

    qsizetype visibleRowCount() const { return m_visibleRows.size(); }
    /// Visible row of an item, or -1 when it is not visible.
    qsizetype visibleRowForIndex(const QModelIndex &index) const;
    QModelIndex indexAtVisibleRow(qsizetype row) const;
    bool isVisible(const QModelIndex &index) const { return visibleRowForIndex(index) >= 0; }
    /// Number of ancestors of \a index (top level items have depth 0).
    int depth(const QModelIndex &index) const;

    void expand(const QModelIndex &index);
    void collapse(const QModelIndex &index);
    /// Expands \a index and every branch below it (QTreeView's `*`). The
    /// sub-tree is walked once and the visible rows are rebuilt once.
    void expandRecursively(const QModelIndex &index);
    void setExpanded(const QModelIndex &index, bool expanded);
    void toggleExpanded(const QModelIndex &index);
    bool isExpanded(const QModelIndex &index) const;
    /// Collapses every item and rebuilds (top level items stay visible).
    void collapseAll();
    qsizetype expandedCount() const { return m_expanded.size(); }

    /// Number of model queries (rowCount/index/parent) since the last
    /// resetModelQueryCount(); used by tests to prove that expand/collapse does
    /// not walk the whole tree.
    quint64 modelQueryCount() const { return m_modelQueryCount; }
    void resetModelQueryCount() { m_modelQueryCount = 0; }

private:
    /// One parent's children: a Fenwick tree over the visible sub-tree size of each child.
    ///
    /// The visible row of a child is the prefix sum of the children before it, so a
    /// sub-tree that grows or shrinks only updates its own entry and adds the delta to the
    /// ancestors on the path (O(log children) each). A parent that is not expanded keeps no
    /// block: nothing inside it is visible and no offset can shift.
    struct BranchBlock
    {
        /// Fenwick tree, 1-based; index i covers children [i - lowbit(i), i).
        QVector<qsizetype> tree;
        qsizetype childCount = 0;

        void reset(qsizetype children)
        {
            childCount = qMax<qsizetype>(0, children);
            // QVector::assign() only exists in Qt 6; a filled copy reads the same in both.
            tree = QVector<qsizetype>(int(childCount) + 1, qsizetype(0));
        }
        void add(qsizetype childIndex, qsizetype delta)
        {
            if (childIndex < 0 || childIndex >= childCount)
                return;
            for (qsizetype i = childIndex + 1; i <= childCount; i += i & -i)
                tree[int(i)] += delta;
        }
        /// Sum of the visible sub-tree sizes of the children before \a childIndex.
        qsizetype prefix(qsizetype childIndex) const
        {
            qsizetype sum = 0;
            for (qsizetype i = qMin(childIndex, childCount); i > 0; i -= i & -i)
                sum += tree.at(int(i));
            return sum;
        }
        qsizetype total() const { return prefix(childCount); }
    };

    int childCount(const QModelIndex &parent);
    /// Visible rows of \a index's sub-tree (that node excluded). When \a block is given it
    /// is filled with each child's visible sub-tree size.
    QVector<QModelIndex> visibleSubtreeRows(const QModelIndex &index, BranchBlock *block = nullptr);
    /// Adds \a delta to the entry of every ancestor of \a index (and to the index itself),
    /// stopping at the first ancestor that is not expanded - above and below it nothing
    /// shifts, because nothing there is visible.
    void addToAncestors(const QModelIndex &index, qsizetype delta);
    /// Visible row of an item, computed from the branch blocks (see the contract above).
    qsizetype visibleRowForKey(const QModelIndex &index) const;

    QAbstractItemModel *m_model = nullptr;
    QModelIndex m_rootIndex;
    QSet<QPersistentModelIndex> m_expanded;
    QVector<QModelIndex> m_visibleRows;
    /// Children of every expanded parent (and of the root), keyed by the parent index
    /// value; the invalid index is the model's invisible root.
    QHash<QModelIndex, BranchBlock> m_branches;
    quint64 m_modelQueryCount = 0;
};

} // namespace viv
