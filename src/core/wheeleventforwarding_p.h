#pragma once

#include <QCoreApplication>
#include <QWheelEvent>
#include <QWidget>

namespace viv {

// Sibling overlays bypass the scroll area's viewport when wheel events bubble up.
// Forward only unhandled input, keeping viewport filters and scroll-group routing active.
inline void forwardWheelToViewport(QWidget *viewport, QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF global = event->globalPosition();
#else
    const QPointF global = event->globalPosF();
#endif
    const QPointF local = global - QPointF(viewport->mapToGlobal(QPoint()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QWheelEvent forwarded(local, global, event->pixelDelta(), event->angleDelta(),
                          event->buttons(), event->modifiers(), event->phase(),
                          event->inverted(), event->source(), event->pointingDevice());
#else
    QWheelEvent forwarded(local, global, event->pixelDelta(), event->angleDelta(),
                          event->buttons(), event->modifiers(), event->phase(),
                          event->inverted(), event->source());
#endif
    forwarded.setTimestamp(event->timestamp());
    // sendEvent also propagates ignored input to ancestors. Do not propagate it twice.
    // Accept before delivery: a viewport filter may destroy the originating overlay.
    event->accept();
    QCoreApplication::sendEvent(viewport, &forwarded);
}

} // namespace viv
