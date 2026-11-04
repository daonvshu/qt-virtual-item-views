#pragma once

#include <virtualitemviews/virtualitemview.h>
#include "stylesheetbackground_p.h"

#include <QPainter>

namespace viv {
namespace detail {

// The view and viewport paint their own layers; Qt must not add an automatic QSS fill.
inline void prepareViewBackground(QWidget *widget)
{
    widget->setAutoFillBackground(false);
    widget->setAttribute(Qt::WA_StyledBackground, false);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
}

// No viewport rule means the overall view base remains visible through the content.
inline QBrush viewportBackgroundBrush(const VirtualItemView *view)
{
    return view->backgroundVisible() && hasStyleSheetBackground(view->viewport())
        ? view->viewport()->palette().brush(QPalette::Base) : QBrush(Qt::NoBrush);
}

// Only opaque combined layers can safely erase an already painted item/state below a host.
inline bool bodyBackgroundIsOpaque(const VirtualItemView *view)
{
    return view->backgroundBrush().isOpaque() || viewportBackgroundBrush(view).isOpaque();
}

// Reconstruct both layers in a descendant's coordinate space, using full layer bounds
// so gradients never restart at a dirty rectangle, row, column or frozen-pane edge.
inline void paintBodyBackground(const VirtualItemView *view, QPainter *painter,
                                const QWidget *coordinateWidget, const QRect &dirtyRect)
{
    painter->save();
    painter->setClipRect(dirtyRect, Qt::IntersectClip);
    painter->translate(-coordinateWidget->mapTo(view, QPoint()));
    painter->fillRect(view->rect(), view->backgroundBrush());
    painter->translate(view->viewport()->mapTo(view, QPoint()));
    painter->fillRect(view->viewport()->rect(), viewportBackgroundBrush(view));
    painter->restore();
}

// The parent view already painted the first layer. Paint the viewport layer only once.
inline void paintViewportBackground(const VirtualItemView *view, QPainter *painter,
                                    const QRect &dirtyRect)
{
    painter->save();
    painter->setClipRect(dirtyRect, Qt::IntersectClip);
    painter->fillRect(view->viewport()->rect(), viewportBackgroundBrush(view));
    painter->restore();
}

} // namespace detail
} // namespace viv
