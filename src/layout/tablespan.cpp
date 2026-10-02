#include <virtualitemviews/tablespan.h>

#include <QPair>
#include <QVector>

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>

namespace viv {

QModelIndex TableSpanProvider::anchorOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return index;
    const QPersistentModelIndex target(index);
    const TableSpan limit = maximumSpan();
    if (!target.isValid() || QModelIndex(target) != index)
        return QModelIndex();
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
            const QPersistentModelIndex guardedCandidate(candidate);
            const TableSpan span = spanAt(candidate);
            // A provider callback may reset, remove or move model items.
            if (!target.isValid() || QModelIndex(target) != index
                || !guardedCandidate.isValid() || QModelIndex(guardedCandidate) != candidate)
                return QModelIndex();
            // Only a real merge can cover another cell: a 1x1 would "cover"
            // itself and every lookup would answer with the cell it started at.
            if (!span.isMerged() || span.rowSpan < 1 || span.columnSpan < 1)
                continue;
            if (qint64(row) + span.rowSpan - 1 >= index.row()
                && qint64(column) + span.columnSpan - 1 >= index.column()) {
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
    // Overlapping spans are illegal (docs/history/spans.md §1): the later one is ignored
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

void TableSpanMap::modelStructureChanged()
{
    using Entry = QPair<QPersistentModelIndex, TableSpan>;
    QHash<QPersistentModelIndex, QVector<Entry>> byParent;
    for (auto it = m_spans.cbegin(); it != m_spans.cend(); ++it) {
        if (it.key().isValid())
            byParent[it.key().parent()].append(qMakePair(it.key(), it.value()));
    }
    m_spans.clear();
    for (auto group = byParent.begin(); group != byParent.end(); ++group) {
        auto &entries = group.value();
        std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
            if (left.first.model() != right.first.model())
                return std::less<const QAbstractItemModel *>()(left.first.model(),
                                                                right.first.model());
            return left.first.row() == right.first.row()
                ? left.first.column() < right.first.column()
                : left.first.row() < right.first.row();
        });
        std::map<int, qint64> activeColumns;
        std::multimap<qint64, int> expirations;
        const QAbstractItemModel *currentModel = nullptr;
        for (const Entry &entry : entries) {
            if (entry.first.model() != currentModel) {
                activeColumns.clear();
                expirations.clear();
                currentModel = entry.first.model();
            }
            const int row = entry.first.row();
            const int column = entry.first.column();
            while (!expirations.empty() && expirations.begin()->first < row) {
                activeColumns.erase(expirations.begin()->second);
                expirations.erase(expirations.begin());
            }
            const qint64 lastColumn = qint64(column) + entry.second.columnSpan - 1;
            const auto next = activeColumns.lower_bound(column);
            const bool overlap = (next != activeColumns.end() && next->first <= lastColumn)
                || (next != activeColumns.begin()
                    && std::prev(next)->second >= column);
            if (overlap) {
                if (!m_overlapWarningShown) {
                    m_overlapWarningShown = true;
                    qWarning("TableSpanMap::modelStructureChanged(): reordered spans overlap; "
                             "the later anchor was removed");
                }
                continue;
            }
            activeColumns.emplace(column, lastColumn);
            expirations.emplace(qint64(row) + entry.second.rowSpan - 1, column);
            m_spans.insert(entry.first, entry.second);
        }
    }
    recomputeMaximum();
}

bool TableSpanMap::overlapsExisting(const QModelIndex &anchor, const TableSpan &span) const
{
    const QPersistentModelIndex self(anchor);
    for (auto it = m_spans.constBegin(); it != m_spans.constEnd(); ++it) {
        if (it.key() == self || !it.key().isValid()
            || it.key().model() != anchor.model()
            || it.key().parent() != anchor.parent())
            continue;                       // replacing one's own span is allowed
        const QModelIndex other = it.key();
        const TableSpan otherSpan = it.value();
        // Two model rectangles are disjoint when one of them ends before the
        // other begins on either axis.
        const bool disjoint = qint64(anchor.row()) + span.rowSpan - 1 < other.row()
            || qint64(other.row()) + otherSpan.rowSpan - 1 < anchor.row()
            || qint64(anchor.column()) + span.columnSpan - 1 < other.column()
            || qint64(other.column()) + otherSpan.columnSpan - 1 < anchor.column();
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
