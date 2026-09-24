#pragma once

#include <QColor>
#include <QRect>
#include <QVector>
#include <QtGlobal>

#include <virtualitemviews/types.h>

namespace viv {

class HeaderGeometry;

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
            && lhs.logicalColumns == rhs.logicalColumns;
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
    bool isFrozenColumn(int logicalIndex) const { return paneOfColumn(logicalIndex) != TablePane::Type::Scrollable; }

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
    qint64 m_scrollableExtent = 0;
    VisibleRange m_visibleScrollable;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
};

} // namespace viv
