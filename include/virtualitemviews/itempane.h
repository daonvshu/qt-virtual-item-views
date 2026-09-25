#pragma once

#include <QRect>
#include <QtGlobal>

#include <virtualitemviews/types.h>

namespace viv {

/// One horizontal band ("pane") of a view: the row direction of §31, specified in
/// docs/row-freezing.md.
///
/// The frozen panes are pinned to the top/bottom edge and never scroll; the
/// scrolling pane between them follows the vertical offset. A view without frozen
/// rows has exactly one pane covering the viewport, so nothing changes.
///
/// Like TablePane, an ItemPane never owns a row height: it only says which rows it
/// shows and where it sits. The row geometry keeps coming from ListLayout/SizeIndex.
struct ItemPane
{
    enum class Type
    {
        /// Pinned to the top edge: the first frozen rows.
        FrozenTop,
        /// Follows the vertical offset.
        Scrollable,
        /// Pinned to the bottom edge: the last frozen rows.
        FrozenBottom,
    };

    Type type = Type::Scrollable;
    /// Where the pane lives inside the viewport (y and height matter; the width is
    /// the viewport width).
    QRect viewportRect;
    /// View rows the pane covers, inclusive (-1 when the pane does not exist).
    qsizetype firstRow = -1;
    qsizetype lastRow = -1;

    bool isEmpty() const
    {
        return firstRow < 0 || lastRow < firstRow || viewportRect.height() <= 0;
    }
    qsizetype rowCount() const { return isEmpty() ? 0 : lastRow - firstRow + 1; }
    bool containsRow(qsizetype row) const
    {
        return !isEmpty() && row >= firstRow && row <= lastRow;
    }
    bool isFrozen() const { return type != Type::Scrollable; }

    friend bool operator==(const ItemPane &lhs, const ItemPane &rhs)
    {
        return lhs.type == rhs.type && lhs.viewportRect == rhs.viewportRect
            && lhs.firstRow == rhs.firstRow && lhs.lastRow == rhs.lastRow;
    }
    friend bool operator!=(const ItemPane &lhs, const ItemPane &rhs) { return !(lhs == rhs); }
};

} // namespace viv
