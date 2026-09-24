#pragma once

#include <QColor>
#include <QHash>
#include <QRect>
#include <QVector>
#include <QtGlobal>

#include <virtualitemviews/types.h>

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

/// Look of the line that separates two panes (§31). The same style is used by
/// the header edge line and by the body line, so the boundary stays continuous.
struct PaneSeparatorStyle
{
    /// Pixels the line occupies inside the frozen pane (1 = the default hair
    /// line, 0 hides the boundary line, 3 = a thick divider).
    int width = 1;
    /// Explicit colour. An invalid colour (the default) means "use the colour
    /// the current style paints section separators with", so the boundary line
    /// matches the lines between the other columns instead of a guessed palette
    /// role.
    QColor color;
    /// Solid by default; dashed/dotted lines are centred on the band.
    Qt::PenStyle lineStyle = Qt::SolidLine;

    bool isVisible() const { return width > 0; }
    /// Colour to paint with: the explicit one, or \a styleSeparatorColor.
    QColor effectiveColor(const QColor &styleSeparatorColor) const
    {
        return color.isValid() ? color : styleSeparatorColor;
    }
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
class TablePaneLayout
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
    void setPaneSpecs(const QVector<TablePaneSpec> &specs);
    const QVector<TablePaneSpec> &paneSpecs() const { return m_specs; }
    bool usesExplicitPanes() const { return !m_specs.isEmpty(); }

    /// Recomputes the cached pane rects and column positions. The panes span the
    /// full viewport height. Returns true when the layout changed.
    bool update(int viewportWidth, int viewportHeight);
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
    /// Scroll groups of the current layout, ascending (empty when nothing
    /// scrolls).
    QVector<int> scrollGroups() const;
    /// Extent (sum of the visible column widths) of one group.
    qint64 groupExtent(int scrollGroup) const { return m_groupExtents.value(scrollGroup, 0); }
    /// Width the group currently has.
    int groupWidth(int scrollGroup) const { return m_groupWidths.value(scrollGroup, 0); }
    /// Offset of one group. Group 0 is the primary group: the header geometry's
    /// viewport offset drives it, so a table with the default three panes behaves
    /// exactly as before.
    qint64 groupOffset(int scrollGroup) const;
    /// Sets the offset of \a scrollGroup, clamped to [0, maximumGroupOffset()].
    /// Group 0 is ignored: the application scrolls it through
    /// setHorizontalOffset() (the header and the scroll bar drive it).
    void setGroupOffset(int scrollGroup, qint64 offset);
    qint64 maximumGroupOffset(int scrollGroup) const
    {
        return qMax<qint64>(0, groupExtent(scrollGroup) - groupWidth(scrollGroup));
    }

    /// Width of the frozen left/right pane (0 when it does not exist).
    int frozenLeftWidth() const { return paneRect(TablePane::Type::FrozenLeft).width(); }
    int frozenRightWidth() const { return paneRect(TablePane::Type::FrozenRight).width(); }
    /// Extent of the columns of the scrollable pane only.
    qint64 scrollableExtent() const { return m_scrollableExtent; }
    /// Width available to the scrollable pane.
    int scrollableWidth() const { return paneRect(TablePane::Type::Scrollable).width(); }
    /// Maximum horizontal offset of the scrollable pane.
    qint64 maximumOffset() const
    {
        return qMax<qint64>(0, m_scrollableExtent - scrollableWidth());
    }

    /// Visual index range of the scrollable columns that intersect the
    /// scrollable pane (the range may contain hidden or frozen indices, callers
    /// skip them - same contract as HeaderGeometry::visibleVisualRange()).
    VisibleRange visibleScrollableRange() const { return m_visibleScrollable; }
    /// Logical columns to lay out, in visual order: the frozen columns plus the
    /// scrollable columns of the visible window widened by \a overscan sections.
    QVector<int> columnsForLayout(int overscan) const;

private:
    int extentOf(const QVector<int> &logicalColumns) const;
    QVector<int> visualOrderOf(const QVector<int> &logicalColumns) const;

    const HeaderGeometry *m_geometry = nullptr;
    QVector<int> m_frozenLeft;
    QVector<int> m_frozenRight;
    QVector<TablePane> m_panes;
    /// Viewport x per logical column (-1 = hidden/unknown).
    QVector<int> m_viewportXByLogical;
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
    qint64 m_scrollableExtent = 0;
    VisibleRange m_visibleScrollable;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
};

} // namespace viv
