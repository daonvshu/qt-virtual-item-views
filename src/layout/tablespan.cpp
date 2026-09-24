#include <virtualitemviews/tablespan.h>

namespace viv {

QModelIndex TableSpanProvider::anchorOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return index;
    const TableSpan limit = maximumSpan();
    if (!limit.isMerged())
        return index;
    // Bounded walk: the anchor of a merge lies above/left of every covered cell,
    // and no provider can register a span larger than maximumSpan().
    const int firstRow = qMax(0, index.row() - qMax(1, limit.rowSpan) + 1);
    const int firstColumn = qMax(0, index.column() - qMax(1, limit.columnSpan) + 1);
    for (int row = index.row(); row >= firstRow; --row) {
        for (int column = index.column(); column >= firstColumn; --column) {
            const QModelIndex candidate = index.sibling(row, column);
            if (!candidate.isValid())
                continue;
            const TableSpan span = spanAt(candidate);
            // Only a real merge can cover another cell: a 1x1 would "cover"
            // itself and every lookup would answer with the cell it started at.
            if (!span.isMerged() || span.rowSpan < 1 || span.columnSpan < 1)
                continue;
            if (row + span.rowSpan - 1 >= index.row()
                && column + span.columnSpan - 1 >= index.column()) {
                return candidate;
            }
        }
    }
    return index;
}

void TableSpanMap::setSpan(const QModelIndex &anchor, int rowSpan, int columnSpan)
{
    if (!anchor.isValid())
        return;
    const TableSpan span{qMax(1, rowSpan), qMax(1, columnSpan)};
    const QPersistentModelIndex key(anchor);
    if (!span.isMerged()) {
        removeSpan(anchor);
        return;
    }
    m_spans.insert(key, span);
    m_maximum.rowSpan = qMax(m_maximum.rowSpan, span.rowSpan);
    m_maximum.columnSpan = qMax(m_maximum.columnSpan, span.columnSpan);
}

void TableSpanMap::removeSpan(const QModelIndex &anchor)
{
    if (!anchor.isValid())
        return;
    m_spans.remove(QPersistentModelIndex(anchor));
}

void TableSpanMap::clearSpans()
{
    m_spans.clear();
    m_maximum = TableSpan();
}

TableSpan TableSpanMap::spanAt(const QModelIndex &index) const
{
    if (!index.isValid() || m_spans.isEmpty())
        return TableSpan();
    return m_spans.value(QPersistentModelIndex(index), TableSpan());
}

} // namespace viv
