#pragma once

#include <QColor>
#include <QHash>
#include <QRect>
#include <QVector>
#include <QtGlobal>

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

#include <functional>

namespace viv {

class HeaderGeometry;

/// How a pane scrolls (architecture document §43 "advanced panes").
enum class PaneScroll
{
    /// Never moves horizontally (a frozen pane).
    Frozen,
    /// Follows the horizontal offset of its scroll group.
    Scrollable,
};

/// One pane of an explicit pane list.
///
/// A pane never owns a column width, order or visibility: it only says *which*
/// columns it shows and how they scroll, exactly like the frozen sets of §31.
struct TablePaneSpec
{
    /// Columns of the pane; the pane shows them in visual order.
    QVector<int> logicalColumns;
    PaneScroll scroll = PaneScroll::Scrollable;
    /// Scrollable panes of one group share one horizontal offset. Frozen panes
    /// ignore it.
    int scrollGroup = 0;

    bool isFrozen() const { return scroll == PaneScroll::Frozen; }

    friend bool operator==(const TablePaneSpec &lhs, const TablePaneSpec &rhs)
    {
        return lhs.logicalColumns == rhs.logicalColumns && lhs.scroll == rhs.scroll
            && lhs.scrollGroup == rhs.scrollGroup;
    }
    friend bool operator!=(const TablePaneSpec &lhs, const TablePaneSpec &rhs) { return !(lhs == rhs); }
};

/// One horizontal pane of the table (architecture document §31).
///
/// Every pane is derived from the same committed HeaderGeometry: a pane never
/// holds an authoritative column width, order or visibility of its own.
struct TablePane
{
    enum class Type
    {
        FrozenLeft,
        Scrollable,
        FrozenRight,
    };

    Type type = Type::Scrollable;
    /// Horizontal scroll group of a scrollable pane; frozen panes use -1.
    int scrollGroup = -1;
    /// Where the pane lives inside the viewport (x/width matter; the vertical
    /// part is the full viewport height).
    QRect viewportRect;
    /// Logical columns of the pane in visual order (hidden columns excluded).
    QVector<int> logicalColumns;

    bool isEmpty() const { return logicalColumns.isEmpty(); }
    bool isFrozen() const { return type != Type::Scrollable; }
    int width() const { return viewportRect.width(); }

    friend bool operator==(const TablePane &lhs, const TablePane &rhs)
    {
        return lhs.type == rhs.type && lhs.viewportRect == rhs.viewportRect
            && lhs.logicalColumns == rhs.logicalColumns && lhs.scrollGroup == rhs.scrollGroup;
    }
    friend bool operator!=(const TablePane &lhs, const TablePane &rhs) { return !(lhs == rhs); }
};

/// Layout engine of the horizontal panes (§31).
///
/// Input: the HeaderGeometry (single source of truth) plus the frozen column
/// sets. Output: the three pane rects and every column's viewport x. Frozen
/// panes do not scroll; only the scrollable pane consumes the horizontal
/// offset, so scrolling can never hide a frozen column.
///
/// The layout is cached: update() has to be called after a geometry change, a
/// resize or a change of the frozen sets, exactly like the other derived
/// caches of the table.
class VIRTUALITEMVIEWS_EXPORT TablePaneLayout
{
public:
    void setGeometry(const HeaderGeometry *geometry) { m_geometry = geometry; }
    const HeaderGeometry *geometry() const { return m_geometry; }

    /// Logical columns frozen at the left/right edge (order is irrelevant, the
    /// pane lists them in visual order). A column in both sets stays on the left.
    void setFrozenColumns(const QVector<int> &logicalColumns);
    void setFrozenRightColumns(const QVector<int> &logicalColumns);
    const QVector<int> &frozenColumns() const { return m_frozenLeft; }
    const QVector<int> &frozenRightColumns() const { return m_frozenRight; }
    bool hasFrozenColumns() const { return !m_frozenLeft.isEmpty() || !m_frozenRight.isEmpty(); }

    /// Explicit pane list (§43 "advanced panes"): panes in visual order, each
    /// with its own columns and scroll group. An empty list falls back to the
    /// frozen column sets (the default three panes), so setFrozenColumns() stays
    /// the shorthand for the common case.
    ///
    /// Every pane of the list is kept - also one that currently has no visible
    /// column - so panes() and paneSpecs() stay index compatible.
    ///
    /// The list is normalized: a column belongs to the first pane that names it
    /// (later panes lose it, negative indexes are dropped), a negative scroll
    /// group becomes 0, and a group whose panes are not neighbours is kept as
    /// written but warned about - each problem at most once per call, and only
    /// when the list really changes.
    void setPaneSpecs(const QVector<TablePaneSpec> &specs);
    const QVector<TablePaneSpec> &paneSpecs() const { return m_specs; }
    bool usesExplicitPanes() const { return !m_specs.isEmpty(); }

    /// Model structure changes: the frozen sets and every explicit pane spec name
    /// *columns*, so their logical indices have to follow the items when the model
    /// inserts, removes or moves columns. Call these before the next update().
    void insertLogicalColumns(int first, int count);
    void removeLogicalColumns(int first, int count);
    /// \a destination is in pre-move coordinates, like the model signal.
    void moveLogicalColumns(int start, int count, int destination);

    /// Recomputes the cached pane rects and column positions. The panes span the
    /// full viewport height. Returns true when the layout changed.
    ///
    /// This is the *structural* pass: it walks every column, so it is called when
    /// the section set, the order, the visibility, the frozen sets, the pane specs
    /// or the viewport size changed. A pure scroll only moves the offsets, which is
    /// what refreshScrollWindows() handles (binary search per pane).
    bool update(int viewportWidth, int viewportHeight);
    /// Scroll fast path: recomputes each pane's visible window (and the union of
    /// the scrolling panes) from the cached pane-local prefix sums. Cost is
    /// O(panes x log(columns in pane)), independent of the total column count.
    /// Returns true when the window moved.
    bool refreshScrollWindows();
    /// Columns the last update()/refreshScrollWindows() looked at (diagnostics:
    /// proves the scroll path is window-bounded and the structure path is O(N)).
    qsizetype columnVisitsInLastUpdate() const { return m_columnVisits; }
    /// Size of the last update().
    int viewportWidth() const { return m_viewportWidth; }

    const QVector<TablePane> &panes() const { return m_panes; }
    /// Pane of a type; the rect is empty and the column list empty when the pane
    /// does not exist (no frozen columns of that side).
    TablePane pane(TablePane::Type type) const;
    QRect paneRect(TablePane::Type type) const { return pane(type).viewportRect; }

    /// Viewport x of a column, or -1 when it is hidden or unknown.
    int columnViewportX(int logicalIndex) const;
    /// Pane a column is painted in (scrollable when it is not frozen).
    TablePane::Type paneOfColumn(int logicalIndex) const;
    /// Index of the pane that shows \a logicalIndex (-1 when hidden/unknown).
    int paneIndexOfColumn(int logicalIndex) const;
    /// Pane at \a paneIndex (invalid when out of range).
    TablePane paneAt(int paneIndex) const;
    bool isFrozenColumn(int logicalIndex) const { return paneOfColumn(logicalIndex) != TablePane::Type::Scrollable; }

    // -- scroll groups (§43 "advanced panes") --------------------------------
    /// Scroll group of a column; -1 for a frozen column or a hidden one.
    int scrollGroupOfColumn(int logicalIndex) const;
    /// Index of the primary scrolling pane: the first pane of the list that
    /// scrolls, which is the pane the horizontal scroll bar and
    /// HeaderGeometry::viewportOffset() drive (-1 when nothing scrolls).
    int primaryPaneIndex() const;
    /// Scroll group of the primary pane (-1 when nothing scrolls). The primary
    /// group is the one that follows the header geometry: its offset is
    /// HeaderGeometry::viewportOffset(), so the scroll bar and every geometry
    /// query keep working for it (§43). With the default panes that is group 0.
    int primaryScrollGroup() const { return m_primaryScrollGroup; }
    /// Scroll groups of the current layout, ascending (empty when nothing
    /// scrolls).
    QVector<int> scrollGroups() const;
    /// Extent (sum of the visible column widths) of one group.
    qint64 groupExtent(int scrollGroup) const { return m_groupExtents.value(scrollGroup, 0); }
    /// Width the group currently has.
    int groupWidth(int scrollGroup) const { return m_groupWidths.value(scrollGroup, 0); }
    /// Offset of one group. The primary group (see primaryScrollGroup()) is driven
    /// by the header geometry's viewport offset, so a table with the default three
    /// panes behaves exactly as before.
    qint64 groupOffset(int scrollGroup) const;
    /// Sets the offset of \a scrollGroup, clamped to [0, maximumGroupOffset()].
    /// The primary group is ignored: the application scrolls it through
    /// setHorizontalOffset() (the header and the scroll bar drive it).
    void setGroupOffset(int scrollGroup, qint64 offset);
    qint64 maximumGroupOffset(int scrollGroup) const
    {
        return qMax<qint64>(0, groupExtent(scrollGroup) - groupWidth(scrollGroup));
    }

    /// Width of the frozen left/right pane (0 when it does not exist).
    int frozenLeftWidth() const { return paneRect(TablePane::Type::FrozenLeft).width(); }
    int frozenRightWidth() const { return paneRect(TablePane::Type::FrozenRight).width(); }
    /// Extent of the primary scroll group. With the default panes it is the extent
    /// of the scrolling pane; an explicit pane list may put several scrolling panes
    /// into one group, and then the group is what scrolls.
    qint64 scrollableExtent() const { return groupExtent(primaryScrollGroup()); }
    /// Width available to the scrollable pane.
    int scrollableWidth() const { return paneRect(TablePane::Type::Scrollable).width(); }
    /// Maximum horizontal offset of the primary scroll group.
    qint64 maximumOffset() const { return maximumGroupOffset(primaryScrollGroup()); }

    /// Visual index range of the scrollable columns that intersect the
    /// scrolling panes, all groups together (the range may contain hidden or
    /// frozen indices, callers skip them - same contract as
    /// HeaderGeometry::visibleVisualRange()).
    VisibleRange visibleScrollableRange() const { return m_visibleScrollable; }
    /// Logical columns to lay out, in visual order: the frozen columns plus the
    /// scrollable columns of the visible window widened by \a overscan sections.
    QVector<int> columnsForLayout(int overscan) const;

private:
    /// Window refresh without resetting the visit counter (update() counts the
    /// structural pass and this part together).
    bool refreshScrollWindowsImpl();
    qint64 extentOf(const QVector<int> &logicalColumns) const;
    QVector<int> visualOrderOf(const QVector<int> &logicalColumns) const;
    /// Applies \a remap (which may yield -1 for a dropped column) to the frozen
    /// sets and to every explicit pane spec, dropping what is gone.
    void remapLogicalColumns(const std::function<int(int)> &remap);

    const HeaderGeometry *m_geometry = nullptr;
    QVector<int> m_frozenLeft;
    QVector<int> m_frozenRight;
    QVector<TablePane> m_panes;
    /// Pane-local content x of every column of a pane, in the pane's own order
    /// (one prefix entry per column plus a trailing total), indexed by pane. This
    /// is what makes columnViewportX() O(log) instead of a per-column cache that
    /// has to be rewritten on every scroll.
    QVector<QVector<qint64>> m_panePrefixX;
    /// Slot of a logical column inside its pane's column list (-1 = no pane).
    QVector<int> m_paneSlotByLogical;
    /// Visible window of a pane as slots into its column list (-1 = nothing
    /// visible); the visual-index range is derived from it for the public API.
    QVector<QPair<int, int>> m_paneSlotWindows;
    /// Columns examined by the last update()/refreshScrollWindows().
    mutable qsizetype m_columnVisits = 0;
    /// Pane type per logical column.
    QVector<int> m_paneByLogical;
    /// Pane index per logical column (-1 = hidden/unknown).
    QVector<int> m_paneIndexByLogical;
    /// Scroll group per logical column (-1 = frozen/hidden).
    QVector<int> m_groupByLogical;
    QVector<TablePaneSpec> m_specs;
    QHash<int, qint64> m_groupOffsets;
    QHash<int, qint64> m_groupExtents;
    QHash<int, int> m_groupWidths;
    /// Visible window (visual indexes) of every pane, keyed by pane index.
    QHash<int, VisibleRange> m_paneWindows;
    VisibleRange m_visibleScrollable;
    /// Scroll group of the primary pane (see primaryScrollGroup()).
    int m_primaryScrollGroup = -1;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
};

} // namespace viv
