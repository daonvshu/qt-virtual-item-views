#pragma once

#include <QHash>
#include <QModelIndex>
#include <QPersistentModelIndex>

namespace viv {

/// Span of one cell, in model coordinates (architecture document §43 "spans",
/// see docs/spans.md).
///
/// A span is *projection information only*: it never carries a column width or a
/// row height of its own. The merged rectangle is always derived from the
/// committed HeaderGeometry and the row layout, so dragging a column, moving a
/// column, hiding a column or changing a row height can never make a span and
/// the rest of the table disagree.
struct TableSpan
{
    int rowSpan = 1;
    int columnSpan = 1;

    bool isMerged() const { return rowSpan > 1 || columnSpan > 1; }

    friend bool operator==(const TableSpan &lhs, const TableSpan &rhs)
    {
        return lhs.rowSpan == rhs.rowSpan && lhs.columnSpan == rhs.columnSpan;
    }
    friend bool operator!=(const TableSpan &lhs, const TableSpan &rhs) { return !(lhs == rhs); }
};

/// Source of the spans of a table.
///
/// Contract: `spanAt()` reports the span of the cell *only when that cell is the
/// anchor* of the merge; every cell covered by it reports 1x1. That way a merge
/// has exactly one owner and the model needs no span API at all.
class TableSpanProvider
{
public:
    virtual ~TableSpanProvider() = default;

    /// Span whose anchor is \a index (1x1 when the cell is not an anchor).
    virtual TableSpan spanAt(const QModelIndex &index) const = 0;

    /// Largest span this provider can register; used to bound the reverse lookup
    /// of anchorOf(). The default (1x1) means "nothing is ever merged".
    virtual TableSpan maximumSpan() const { return TableSpan(); }

    /// Anchor of the merged area containing \a index (\a index itself when the
    /// cell is not merged). The default walks up/left through spanAt() bounded by
    /// maximumSpan(); providers with many or large spans should override it.
    virtual QModelIndex anchorOf(const QModelIndex &index) const;
};

/// Map based provider: the application registers the anchors it wants merged.
///
/// The anchors are QPersistentModelIndex, so a span follows the *item* it was
/// registered on through inserts, removals, moves and sorting - exactly like the
/// pinned items of the view (§36). The span extent itself stays "N model rows /
/// M model columns from the anchor", which is what the merged geometry means.
class TableSpanMap : public TableSpanProvider
{
public:
    /// Merges the cells starting at \a anchor. Values below 1 are treated as 1.
    void setSpan(const QModelIndex &anchor, int rowSpan = 1, int columnSpan = 1);
    /// Drops the span registered on \a anchor (no-op when there is none).
    void removeSpan(const QModelIndex &anchor);
    void clearSpans();
    bool isEmpty() const { return m_spans.isEmpty(); }
    int count() const { return int(m_spans.size()); }

    TableSpan spanAt(const QModelIndex &index) const override;
    TableSpan maximumSpan() const override { return m_maximum; }

private:
    QHash<QPersistentModelIndex, TableSpan> m_spans;
    TableSpan m_maximum;
};

} // namespace viv
