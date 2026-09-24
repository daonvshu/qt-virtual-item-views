#include <virtualitemviews/treevisibilityindex.h>

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
    m_rowOfDirty = true;
    if (!m_model) {
        m_rowOf.clear();
        m_rowOfDirty = false;
        return;
    }

    // Iterative depth-first walk: only expanded nodes are descended into, so a
    // collapsed sub-tree costs a single rowCount() query.
    struct Cursor
    {
        QModelIndex parent;
        int next = 0;
    };

    QList<Cursor> cursors;
    cursors.append({m_rootIndex, 0});
    while (!cursors.isEmpty()) {
        if (cursors.last().next >= childCount(cursors.last().parent)) {
            cursors.removeLast();
            continue;
        }
        const QModelIndex child = m_model->index(cursors.last().next, 0, cursors.last().parent);
        ++m_modelQueryCount;
        ++cursors.last().next;
        m_visibleRows.append(child);
        if (isExpanded(child) && childCount(child) > 0)
            cursors.append({child, 0});
    }

    buildRowLookup();
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

void TreeVisibilityIndex::buildRowLookup() const
{
    m_rowOf.clear();
    m_rowOf.reserve(m_visibleRows.size());
    for (qsizetype row = 0; row < m_visibleRows.size(); ++row) {
        const QModelIndex index = m_visibleRows.at(row);
        if (index.isValid())
            m_rowOf.insert(index, row);
    }
    m_rowOfDirty = false;
}

qsizetype TreeVisibilityIndex::visibleRowForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !m_model || index.model() != m_model)
        return -1;
    if (m_rowOfDirty)
        buildRowLookup();
    // Visible rows are column 0 rows; a cell index maps to its row.
    const QModelIndex key = index.column() == 0 ? index : index.siblingAtColumn(0);
    return m_rowOf.value(key, -1);
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

    m_expanded.insert(QPersistentModelIndex(index));

    const qsizetype row = visibleRowForIndex(index);
    if (row < 0)
        return; // the expanded item is not visible: nothing to insert

    const QVector<QModelIndex> subtreeRows = visibleSubtreeRows(index);
    if (subtreeRows.isEmpty())
        return;

    QVector<QModelIndex> spliced;
    spliced.reserve(m_visibleRows.size() + subtreeRows.size());
    spliced.append(m_visibleRows.mid(0, int(row) + 1));
    spliced.append(subtreeRows);
    spliced.append(m_visibleRows.mid(int(row) + 1));
    m_visibleRows = spliced;
    m_rowOfDirty = true;
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

    qsizetype end = row + 1;
    while (end < m_visibleRows.size() && isDescendantOf(m_visibleRows.at(end), index))
        ++end;
    if (end > row + 1)
        m_visibleRows.remove(int(row) + 1, int(end - row - 1));
    m_rowOfDirty = true;
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

QVector<QModelIndex> TreeVisibilityIndex::visibleSubtreeRows(const QModelIndex &index)
{
    QVector<QModelIndex> rows;
    if (!m_model || !index.isValid())
        return rows;

    struct Cursor
    {
        QModelIndex parent;
        int next = 0;
    };

    QList<Cursor> cursors;
    cursors.append({index, 0});
    while (!cursors.isEmpty()) {
        if (cursors.last().next >= childCount(cursors.last().parent)) {
            cursors.removeLast();
            continue;
        }
        const QModelIndex child = m_model->index(cursors.last().next, 0, cursors.last().parent);
        ++m_modelQueryCount;
        ++cursors.last().next;
        rows.append(child);
        if (isExpanded(child) && childCount(child) > 0)
            cursors.append({child, 0});
    }
    return rows;
}

bool TreeVisibilityIndex::isDescendantOf(const QModelIndex &candidate, const QModelIndex &ancestor) const
{
    if (!candidate.isValid() || !ancestor.isValid())
        return false;
    for (QModelIndex parent = candidate.parent(); parent.isValid(); parent = parent.parent()) {
        if (parent == ancestor)
            return true;
    }
    return false;
}

} // namespace viv
