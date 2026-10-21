#pragma once

#include <QColor>
#include <QModelIndex>
#include <QPoint>
#include <QRect>
#include <QVariant>
#include <QtGlobal>

class QWidget;

namespace viv {

/// A point that can be omitted, including on C++14 toolchains.
class OptionalPoint
{
public:
    /// Starts without a point; value_or() returns its fallback.
    OptionalPoint() = default;
    /// Stores any coordinate, including the origin and negative coordinates.
    OptionalPoint(const QPoint &point) : m_point(point), m_hasValue(true) {}

    /// Replaces the stored point and marks it present.
    OptionalPoint &operator=(const QPoint &point)
    {
        m_point = point;
        m_hasValue = true;
        return *this;
    }
    /// Whether a point was explicitly supplied.
    bool has_value() const { return m_hasValue; }
    /// Returns the stored point, or the fallback when omitted.
    QPoint value_or(const QPoint &fallback) const { return m_hasValue ? m_point : fallback; }
    /// Clears the supplied point so the fallback is used again.
    void reset() { m_hasValue = false; }

private:
    QPoint m_point;
    bool m_hasValue = false;
};

/// Optional screenshot source and pointer position for a manually started drag.
struct DragOptions
{
    /// Uses dragSourceWidget() when null; ownership stays with the caller.
    QWidget *previewWidget = nullptr;
    /// Widget-local logical pixels; empty captures the whole widget.
    /// Nonempty rectangles are clipped to the widget's bounds.
    QRect previewRect;
    /// Anchors the preview at the mouse position by default; false centres it.
    bool usePressPosition = true;
    /// Explicit global press position, including (0, 0). When omitted, the view
    /// uses its matching recorded press or the cursor position at drag entry.
    OptionalPoint globalPressPosition;
};

/// Data and metadata supplied to adapter binding callbacks.
///
/// `index` remains the identity of the materialized item. `rowData` is read from
/// `itemDataRole()` using the row's canonical column (column 0 for tables), so
/// adapters do not have to unpack a business structure from every cell index.
struct ItemBindingContext
{
    QModelIndex index;
    QVariant rowData;
    /// Logical model column for tables (including row widgets); -1 for lists/trees.
    int logicalColumn = -1;
    /// Node depth relative to the displayed root; flat items have depth 0.
    int depth = -1;
};

/// Default model role used by VirtualItemView for one complete row/node payload.
constexpr int kDefaultItemDataRole = Qt::UserRole + 0x200;

/// Identifier used to classify the widgets produced by a WidgetAdapter.
///
/// The kernel only compares widget types for equality: it never interprets
/// them. An adapter is free to return the same value for every index (a single
/// pool) or one value per business row layout (several pools).
using WidgetType = int;

/// Widget type used when an adapter does not classify its widgets.
constexpr WidgetType kDefaultWidgetType = 0;

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
    /// Solid by default; every line style uses width as its physical-pixel stroke width.
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
