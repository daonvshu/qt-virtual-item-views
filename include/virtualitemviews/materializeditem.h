#pragma once

#include <virtualitemviews/types.h>

#include <QPersistentModelIndex>
#include <QRect>

class QWidget;

namespace viv {

/// One item that currently owns a real QWidget.
///
/// Invariants (see docs/architecture.md):
///  - materialized widgets == visible + overscan + pinned items,
///  - a widget is never stored here without a valid persistent index,
///  - one QModelIndex maps to at most one MaterializedItem at any time.
///
/// \note The identity of the item is MaterializedItem::index (a
///       QPersistentModelIndex). The row of the item is deliberately *not*
///       stored: rows change on insert/remove/move/sort/filter.
struct MaterializedItem
{
    QPersistentModelIndex index;
    QWidget *widget = nullptr;
    WidgetType type = kDefaultWidgetType;
    /// Item geometry in viewport coordinates; may lie outside the viewport for
    /// pinned items that left the overscan window.
    QRect geometry;
    /// Pinned items survive leaving the overscan window (focus, IME, popup or
    /// an explicit requestPin() from business code).
    bool pinned = false;

    bool isValid() const { return widget != nullptr && index.isValid(); }
};

} // namespace viv

