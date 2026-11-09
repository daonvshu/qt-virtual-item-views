#pragma once

#include <QBrush>
#include <QPalette>
#include <QWidget>

namespace viv {
namespace detail {

// QSS resolves a background into Base and Window, but may clear Base on a viewport.
// Check resolved roles so a text-only rule does not suppress the native panel.
inline bool hasStyleSheetBackground(const QWidget *widget)
{
    if (!widget || !widget->testAttribute(Qt::WA_StyleSheetTarget))
        return false;
    const QPalette palette = widget->palette();
    const QPalette::ColorGroup group = palette.currentColorGroup();
    return palette.isBrushSet(group, QPalette::Base)
        && palette.isBrushSet(group, QPalette::Window);
}

// Qt clears an embedded viewport's background role for translucent QSS fills.
// Window retains that fill; ordinary palettes still use Base, including NoBrush.
inline QBrush resolvedBackgroundBrush(const QWidget *widget)
{
    const QPalette palette = widget->palette();
    const QBrush base = palette.brush(QPalette::Base);
    return base.style() == Qt::NoBrush && hasStyleSheetBackground(widget)
        ? palette.brush(QPalette::Window) : base;
}

} // namespace detail
} // namespace viv
