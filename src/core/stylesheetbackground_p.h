#pragma once

#include <QPalette>
#include <QWidget>

namespace viv {
namespace detail {

// QSS resolves a background brush into Base and Window on the matched widget.
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

} // namespace detail
} // namespace viv
