#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

namespace viv {

// All capture geometry stays in logical widget coordinates, including at high DPI.
inline QRect dragPreviewRect(const QRect &widgetRect, const QRect &requestedRect)
{
    return requestedRect.isEmpty() ? widgetRect : requestedRect.intersected(widgetRect);
}

// Translate the original press into the actual clipped capture, preserving outside offsets.
inline QPoint dragPreviewHotSpot(const QRect &capture, const QPoint &localPressPosition)
{
    return localPressPosition - capture.topLeft();
}

// QDrag consumes logical pixels even though QPixmap::size() is in device pixels.
inline QPoint dragPreviewCenter(const QSize &pixelSize, qreal devicePixelRatio)
{
    return QRect(QPoint(), pixelSize / devicePixelRatio).center();
}

} // namespace viv
