#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QVector>
#include <QWidget>

namespace viv {

// Snap both exclusive edges together; logical region clips otherwise round differently.
inline void fillPixelAlignedRegion(QPainter *painter, const QRegion &region, const QColor &color)
{
    const QTransform transform = painter->deviceTransform();
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
        return;
    QRegion pixels;
    for (const QRect &rect : region) {
        const QRectF device = transform.mapRect(QRectF(rect));
        const int left = qRound(device.left());
        const int top = qRound(device.top());
        pixels += QRect(left, top, qMax(0, qRound(device.right()) - left),
                        qMax(0, qRound(device.bottom()) - top));
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (const QRect &rect : pixels) {
        QRectF device(rect);
        device.adjust(0.001, 0.001, -0.001, -0.001);
        painter->fillRect(inverse.mapRect(device), color);
    }
    painter->restore();
}

// Keep antialiased corners inside the same exclusive device edges as flat backgrounds.
inline void fillPixelAlignedRoundedRegion(QPainter *painter, const QRect &rect,
                                         const QRegion &clip, int radius, const QColor &color)
{
    if (radius <= 0) {
        fillPixelAlignedRegion(painter, clip.intersected(QRegion(rect)), color);
        return;
    }
    const QTransform transform = painter->deviceTransform();
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
        return;
    const auto aligned = [&transform](const QRect &logical) {
        const QRectF device = transform.mapRect(QRectF(logical));
        const int left = qRound(device.left());
        const int top = qRound(device.top());
        return QRectF(left, top, qMax(0, qRound(device.right()) - left),
                      qMax(0, qRound(device.bottom()) - top));
    };
    QPainterPath deviceClip;
    for (const QRect &part : clip)
        deviceClip.addRect(aligned(part));
    deviceClip.setFillRule(Qt::WindingFill);
    const QRectF scaledRadius = transform.mapRect(QRectF(0, 0, radius, radius));
    QPainterPath shape;
    shape.addRoundedRect(aligned(rect), scaledRadius.width(), scaledRadius.height());
    painter->save();
    painter->setClipPath(inverse.map(deviceClip), Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillPath(inverse.map(shape), color);
    painter->restore();
}

// Union device-pixel bands before painting so thin/overlapping gaps stay uniform.
inline void fillPixelLines(QPainter *painter, const QVector<QRect> &lines,
                           int pixelWidth, const QColor &color, Qt::Orientation orientation,
                           bool trailingEdge = false)
{
    const QTransform transform = painter->deviceTransform();
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
        return;
    QRegion bands;
    for (const QRect &line : lines) {
        if (line.isEmpty())
            continue;
        const QRectF device = transform.mapRect(QRectF(line));
        const int thickness = qMax(1, pixelWidth);
        bands += QRect(orientation == Qt::Vertical && trailingEdge
                           ? qRound(device.right()) - thickness : qRound(device.left()),
                       orientation == Qt::Horizontal && trailingEdge
                           ? qRound(device.bottom()) - thickness : qRound(device.top()),
                       orientation == Qt::Vertical ? qMax(1, pixelWidth)
                           : qMax(0, qRound(device.right()) - qRound(device.left())),
                       orientation == Qt::Horizontal ? qMax(1, pixelWidth)
                           : qMax(0, qRound(device.bottom()) - qRound(device.top())));
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (const QRect &band : bands)
        painter->fillRect(inverse.mapRect(QRectF(band)), color);
    painter->restore();
}

inline void fillHorizontalPixelLines(QPainter *painter, const QVector<QRect> &lines,
                                     int pixelWidth, const QColor &color)
{
    fillPixelLines(painter, lines, pixelWidth, color, Qt::Horizontal);
}

inline void fillVerticalPixelLines(QPainter *painter, const QVector<QRect> &lines,
                                   int pixelWidth, const QColor &color)
{
    fillPixelLines(painter, lines, pixelWidth, color, Qt::Vertical);
}

class PixelAlignedHorizontalLine : public QWidget
{
public:
    explicit PixelAlignedHorizontalLine(QWidget *parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        // The line ends at the row edge, even when its logical band spans two pixels.
        fillPixelLines(&painter, {rect()}, height(), palette().color(QPalette::Window),
                       Qt::Horizontal, true);
    }
};

} // namespace viv
