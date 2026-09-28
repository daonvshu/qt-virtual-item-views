#include <virtualitemviews/reorderabletablemodel.h>

#include <limits>

namespace viv {

ReorderableTableModel::ReorderableTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

ReorderableTableModel::ReorderableTableModel(int rows, int columns, QObject *parent)
    : QAbstractTableModel(parent)
    , m_columns(qMax(0, columns))
{
    const int count = qMax(0, rows);
    m_order.reserve(count);
    for (int row = 0; row < count; ++row)
        m_order.append(row);
    m_sourceOrder = m_order;
    m_nextSourceId = count;
}

ReorderableTableModel::~ReorderableTableModel() = default;

int ReorderableTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_order.size());
}

int ReorderableTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columns;
}

QVariant ReorderableTableModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(index);
    Q_UNUSED(role);
    return QVariant();   // the base holds no data (see the header)
}

QVariant ReorderableTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);
    return QVariant();   // labels come from the subclass (see the header)
}

void ReorderableTableModel::setColumnCount(int columns)
{
    const int clamped = qMax(0, columns);
    if (m_columns == clamped)
        return;
    if (clamped > m_columns) {
        beginInsertColumns(QModelIndex(), m_columns, clamped - 1);
        m_columns = clamped;
        endInsertColumns();
        return;
    }
    beginRemoveColumns(QModelIndex(), clamped, m_columns - 1);
    m_columns = clamped;
    endRemoveColumns();
}

void ReorderableTableModel::setRowCount(int rows)
{
    const int wanted = qMax(0, rows);
    const int current = int(m_order.size());
    if (wanted == current)
        return;
    if (wanted > current)
        insertRows(current, wanted - current);
    else
        removeRows(wanted, current - wanted);
}

bool ReorderableTableModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0 || row > m_order.size()
        || count > std::numeric_limits<int>::max() - int(m_order.size())
        || count > std::numeric_limits<int>::max() - m_nextSourceId)
        return false;
    const int sourceSlot = row == m_order.size() ? m_sourceOrder.size()
        : int(m_sourceOrder.indexOf(m_order.at(row)));
    beginInsertRows(parent, row, row + count - 1);
    for (int index = 0; index < count; ++index) {
        const int id = m_nextSourceId++;
        m_order.insert(row + index, id);
        m_sourceOrder.insert(sourceSlot + index, id);
    }
    endInsertRows();
    return true;
}

bool ReorderableTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0 || row >= m_order.size()
        || count > m_order.size() - row)
        return false;
    beginRemoveRows(parent, row, row + count - 1);
    for (int index = 0; index < count; ++index)
        m_sourceOrder.removeOne(m_order.at(row + index));
    m_order.remove(row, count);
    endRemoveRows();
    return true;
}

int ReorderableTableModel::sourceRow(int viewRow) const
{
    if (viewRow < 0 || viewRow >= m_order.size())
        return -1;
    return m_order.at(viewRow);
}

int ReorderableTableModel::viewRow(int sourceRow) const
{
    return int(m_order.indexOf(sourceRow));
}

void ReorderableTableModel::resetRowOrder()
{
    if (isIdentityOrder())
        return;
    beginResetModel();
    m_order = m_sourceOrder;
    endResetModel();
    emit rowOrderChanged();
}

bool ReorderableTableModel::isIdentityOrder() const
{
    return m_order == m_sourceOrder;
}

bool ReorderableTableModel::moveRows(const QModelIndex &sourceParent, int sourceRow,
                                     int count, const QModelIndex &destinationParent,
                                     int destinationChild)
{
    if (sourceParent.isValid() || destinationParent.isValid() || count <= 0 || sourceRow < 0
        || sourceRow >= m_order.size() || count > m_order.size() - sourceRow)
        return false;
    if (destinationChild < 0 || destinationChild > m_order.size())
        return false;
    // Qt's "a move onto itself is no move" rule (and the assertion beginMoveRows() would
    // otherwise hit).
    if (destinationChild >= sourceRow && destinationChild <= sourceRow + count)
        return false;
    if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent,
                       destinationChild))
        return false;

    QVector<int> moved;
    moved.reserve(count);
    for (int index = 0; index < count; ++index)
        moved.append(m_order.at(sourceRow + index));
    m_order.remove(sourceRow, count);
    // destinationChild names the row *before* which the run lands, so removing the run
    // first shifts a destination that sat behind it.
    const int target = destinationChild > sourceRow ? destinationChild - count : destinationChild;
    for (int index = 0; index < count; ++index)
        m_order.insert(target + index, moved.at(index));
    endMoveRows();
    emit rowOrderChanged();
    return true;
}

} // namespace viv
