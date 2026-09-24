#pragma once

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
