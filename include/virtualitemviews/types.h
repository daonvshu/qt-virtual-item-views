#pragma once

#include <QColor>
#include <QtGlobal>

namespace viv {

/// Identifier used to classify the widgets produced by a WidgetAdapter.
///
/// The kernel only compares widget types for equality: it never interprets
/// them. An adapter is free to return the same value for every index (a single
/// pool) or one value per business row layout (several pools).
using WidgetType = int;

/// Widget type used when an adapter does not classify its widgets.
inline constexpr WidgetType kDefaultWidgetType = 0;

/// Look of the line that separates two panes (§31). Shared by the column panes
/// (VirtualTableView) and the row panes (VirtualItemView), so both boundaries look
/// the same.
struct PaneSeparatorStyle
{
    /// Pixels the line occupies inside the preceding pane (1 = the default hair
    /// line, 0 hides the boundary line, 3 = a thick divider).
    int width = 1;
    /// Explicit colour. An invalid colour (the default) means "use the colour the
    /// current style paints separators with", so the boundary line matches the
    /// lines between items instead of a guessed palette role.
    QColor color;
    /// Solid by default; dashed/dotted lines are centred on the band.
    Qt::PenStyle lineStyle = Qt::SolidLine;

    bool isVisible() const { return width > 0; }
    /// Colour to paint with: the explicit one, or \a styleSeparatorColor.
    QColor effectiveColor(const QColor &styleSeparatorColor) const
    {
        return color.isValid() ? color : styleSeparatorColor;
    }

    friend bool operator==(const PaneSeparatorStyle &lhs, const PaneSeparatorStyle &rhs)
    {
        return lhs.width == rhs.width && lhs.color == rhs.color && lhs.lineStyle == rhs.lineStyle;
    }
    friend bool operator!=(const PaneSeparatorStyle &lhs, const PaneSeparatorStyle &rhs)
    {
        return !(lhs == rhs);
    }
};

/// Inclusive range of item indices (visible rows, visible columns, ...).
struct VisibleRange
{
    qsizetype first = -1;
    qsizetype last = -1;

    bool isValid() const { return first >= 0 && last >= first; }
    bool contains(qsizetype item) const { return isValid() && item >= first && item <= last; }
    qsizetype count() const { return isValid() ? last - first + 1 : 0; }

    /// Range widened by \a before/\a after items, clamped to [0, upperBound).
    static VisibleRange expanded(qsizetype coreFirst, qsizetype coreLast, qsizetype before,
                                 qsizetype after, qsizetype upperBound);
};

inline VisibleRange VisibleRange::expanded(qsizetype coreFirst, qsizetype coreLast, qsizetype before,
                                           qsizetype after, qsizetype upperBound)
{
    VisibleRange range;
    if (upperBound <= 0 || coreFirst < 0 || coreLast < coreFirst)
        return range;
    range.first = qMax<qsizetype>(0, coreFirst - before);
    range.last = qMin<qsizetype>(upperBound - 1, coreLast + after);
    return range;
}

} // namespace viv
