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
    if (!span.isMerged()) {
        removeSpan(anchor);
        return;
    }
    // Overlapping spans are illegal (docs/spans.md §1): the later one is ignored
    // instead of leaving two anchors fighting over the same cells.
    if (overlapsExisting(anchor, span)) {
        if (!m_overlapWarningShown) {
            m_overlapWarningShown = true;
            qWarning("TableSpanMap::setSpan(): the span anchored at (%d, %d) overlaps an existing "
                     "one and was ignored", anchor.row(), anchor.column());
        }
        return;
    }

    m_spans.insert(QPersistentModelIndex(anchor), span);
    m_maximum.rowSpan = qMax(m_maximum.rowSpan, span.rowSpan);
    m_maximum.columnSpan = qMax(m_maximum.columnSpan, span.columnSpan);
}

void TableSpanMap::removeSpan(const QModelIndex &anchor)
{
    if (!anchor.isValid())
        return;
    m_spans.remove(QPersistentModelIndex(anchor));
    // maximumSpan() is the bound anchorOf() walks: dropping a large span has to
    // shrink it, otherwise the reverse lookup keeps its old range forever.
    recomputeMaximum();
}

void TableSpanMap::clearSpans()
{
    m_spans.clear();
    m_maximum = TableSpan();
    m_overlapWarningShown = false;
}

bool TableSpanMap::overlapsExisting(const QModelIndex &anchor, const TableSpan &span) const
{
    const QPersistentModelIndex self(anchor);
    for (auto it = m_spans.constBegin(); it != m_spans.constEnd(); ++it) {
        if (it.key() == self || !it.key().isValid())
            continue;                       // replacing one's own span is allowed
        const QModelIndex other = it.key();
        const TableSpan otherSpan = it.value();
        // Two model rectangles are disjoint when one of them ends before the
        // other begins on either axis.
        const bool disjoint = anchor.row() + span.rowSpan - 1 < other.row()
            || other.row() + otherSpan.rowSpan - 1 < anchor.row()
            || anchor.column() + span.columnSpan - 1 < other.column()
            || other.column() + otherSpan.columnSpan - 1 < anchor.column();
        if (!disjoint)
            return true;
    }
    return false;
}

void TableSpanMap::recomputeMaximum()
{
    TableSpan maximum;
    for (auto it = m_spans.constBegin(); it != m_spans.constEnd(); ++it) {
        maximum.rowSpan = qMax(maximum.rowSpan, it.value().rowSpan);
        maximum.columnSpan = qMax(maximum.columnSpan, it.value().columnSpan);
    }
    m_maximum = maximum;
}

TableSpan TableSpanMap::spanAt(const QModelIndex &index) const
{
    if (!index.isValid() || m_spans.isEmpty())
        return TableSpan();
    return m_spans.value(QPersistentModelIndex(index), TableSpan());
}

} // namespace viv
