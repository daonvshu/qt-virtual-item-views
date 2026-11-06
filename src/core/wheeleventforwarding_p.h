#pragma once

#include <QCoreApplication>
#include <QPointer>
#include <QWheelEvent>
#include <QWidget>

namespace viv {

// Sibling overlays bypass the scroll area's viewport when wheel events bubble up.
// Forward only unhandled input, keeping viewport filters and scroll-group routing active.
inline void forwardWheelToViewport(QWidget *source, QWidget *viewport, QWheelEvent *event)
{
    const QPointer<QWidget> guardedSource(source);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
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
    // Synthetic wheel events do not bubble. Let the original event continue
    // from the source when the viewport leaves the forwarded input unhandled.
    // Keep it accepted if delivery destroys the source: its native propagation
    // chain must not resume through a deleted widget.
    event->accept();
    QCoreApplication::sendEvent(viewport, &forwarded);
    if (guardedSource)
        event->setAccepted(forwarded.isAccepted());
}

} // namespace viv
