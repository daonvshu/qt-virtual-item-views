#pragma once

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
///  - visibleRowForIndex()/isVisible() are O(1) after a lazily rebuilt row
///    lookup table, depth() is O(depth),
///  - the lookup table is keyed by the model index *value*, not by a
///    QPersistentModelIndex: one persistent index per visible row would register
///    a million persistent indexes with the model (hundreds of bytes per row,
///    and a quadratic cost when the table is cleared),
///  - a structural change and an expand()/collapse() rebuild that table, which
///    is O(visible rows) - the model itself is never walked for a collapsed
///    sub-tree,
///  - model mutations (insert/remove/move/layoutChanged) rebuild the visible
///    list while keeping the expansion state; modelReset clears it too.
class TreeVisibilityIndex
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
    int childCount(const QModelIndex &parent);
    QVector<QModelIndex> visibleSubtreeRows(const QModelIndex &index);
    void buildRowLookup() const;
    bool isDescendantOf(const QModelIndex &candidate, const QModelIndex &ancestor) const;

    QAbstractItemModel *m_model = nullptr;
    QModelIndex m_rootIndex;
    QSet<QPersistentModelIndex> m_expanded;
    QVector<QModelIndex> m_visibleRows;
    /// Visible row of an item, keyed by the model index value (column 0). Rows
    /// are only looked up after a structural change was processed, so the row
    /// component of a key is always current.
    mutable QHash<QModelIndex, qsizetype> m_rowOf;
    mutable bool m_rowOfDirty = true;
    quint64 m_modelQueryCount = 0;
};

} // namespace viv
