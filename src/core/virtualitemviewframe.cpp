#include <virtualitemviews/virtualitemview.h>
#include "performanceprobe_p.h"

#include <QEvent>
#include <QGraphicsEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QPixmap>
#include <QScrollBar>

namespace viv {
namespace {

QPainterPath roundedFramePath(const QRectF &rect, qreal radius)
{
    QPainterPath path;
    if (!rect.isEmpty()) {
        radius = qBound(qreal(0), radius, qMin(rect.width(), rect.height()) / 2);
        path.addRoundedRect(rect, radius, radius);
    }
    return path;
}

QRectF pixelAlignedRect(const QRectF &logical, const QTransform &transform)
{
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
        return logical;
    const QRectF device = transform.mapRect(logical);
    const int left = qRound(device.left());
    const int top = qRound(device.top());
    const int right = qRound(device.right());
    const int bottom = qRound(device.bottom());
    return inverse.mapRect(QRectF(left, top, qMax(0, right - left), qMax(0, bottom - top)));
}

class FrameAlphaEffect : public QGraphicsEffect
{
public:
    explicit FrameAlphaEffect(VirtualItemView *view) : QGraphicsEffect(view), m_view(view) {}

    void invalidateMask()
    {
        m_mask = QPixmap();
        update();
    }

protected:
    void draw(QPainter *painter) override
    {
        detail::PerformanceScope performance(m_view, "paint.frame-composite", detail::PerformanceDomain::Paint);
        QPoint offset;
        const QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
        if (source.isNull())
            return;
        const qreal dpr = source.devicePixelRatioF();
        const QRectF logicalBounds = QRectF(m_view->rect()).translated(-QPointF(offset));
        QPixmap aligned(source.size());
        aligned.setDevicePixelRatio(dpr);
        QPainter transformPainter(&aligned);
        const QTransform transform = transformPainter.deviceTransform();
        transformPainter.end();
        // Align the outer contour and the inner contour on the same physical grid,
        // while keeping all paths in the pixmap's logical coordinate system.
        const QRectF outer = pixelAlignedRect(logicalBounds, transform);
        const qreal radius = qMin(qreal(m_view->frameCornerRadius()),
                                 qMin(outer.width(), outer.height()) / 2);
        const QRectF outerDevice = transform.mapRect(outer);
        const int borderPixels = qMax(0, qRound(m_view->frameBorderWidth() * dpr));
        const qreal borderLogical = dpr > 0 ? qreal(borderPixels) / dpr : 0;
        const QRectF innerDevice = outerDevice.adjusted(borderPixels, borderPixels,
                                                        -borderPixels, -borderPixels);
        const QRectF inner = transform.inverted().mapRect(innerDevice);
        const QPainterPath innerPath = roundedFramePath(inner,
                                                        qMax(qreal(0), radius - borderLogical));
        if (m_mask.size() != source.size() || m_mask.devicePixelRatioF() != dpr
            || m_maskBounds != outer) {
            m_mask = QPixmap(source.size());
            m_mask.setDevicePixelRatio(dpr);
            m_mask.fill(Qt::transparent);
            m_maskBounds = outer;
            QPainter maskPainter(&m_mask);
            maskPainter.setRenderHint(QPainter::Antialiasing);
            maskPainter.fillPath(innerPath, Qt::white);
        }
        QPixmap result(source.size());
        result.setDevicePixelRatio(dpr);
        result.fill(Qt::transparent);
        {
            QPainter buffer(&result);
            buffer.drawPixmap(QPointF(), source);
            buffer.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            // Transparent mask pixels must clear the source outside the inner contour too.
            buffer.drawPixmap(QPointF(), m_mask);
            if (borderPixels > 0) {
                // The border and content have complementary coverage at the inner edge.
                buffer.setCompositionMode(QPainter::CompositionMode_Plus);
                buffer.setRenderHint(QPainter::Antialiasing);
                const QColor color = m_view->frameBorderColor();
                buffer.fillPath(roundedFramePath(outer, radius).subtracted(innerPath),
                                color.isValid() ? color : m_view->palette().color(QPalette::Mid));
            }
        }
        painter->drawPixmap(offset, result);
    }

private:
    VirtualItemView *m_view;
    QPixmap m_mask;
    QRectF m_maskBounds;
};

} // namespace

bool VirtualItemView::event(QEvent *event)
{
    if (event->type() == QEvent::Paint && !m_frameEffect.isNull()) {
        // Qt5 can paint the scrollbars after the scroll-area's effect source has
        // been composed.  Refresh their local masks immediately before the
        // source is rendered so the rounded outer corners are clipped there too.
        updateFrameChildMasks();
        // The QFrame shape reserves layout space only. Painting its native box
        // and scroll-area corner would leave rectangular edges inside the Alpha frame.
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        return true;
    }
    const bool refreshChildMasks = event->type() == QEvent::LayoutRequest
        || event->type() == QEvent::StyleChange;
    const bool handled = QAbstractScrollArea::event(event);
    if (refreshChildMasks && !m_frameEffect.isNull())
        updateFrameChildMasks();
    return handled;
}

void VirtualItemView::setFrameCornerRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_frameCornerRadius == radius)
        return;
    m_frameCornerRadius = radius;
    updateFrameAppearance();
}

void VirtualItemView::setFrameBorderWidth(int width)
{
    // QFrame stores line widths as shorts.
    width = qBound(0, width, 32767);
    if (m_frameBorderWidth == width)
        return;
    m_frameBorderWidth = width;
    updateFrameAppearance();
}

void VirtualItemView::setFrameBorderColor(const QColor &color)
{
    if (m_frameBorderColor == color)
        return;
    m_frameBorderColor = color;
    if (m_frameEffect)
        m_frameEffect->update();
}

void VirtualItemView::updateFrameAppearance()
{
    if (m_frameCornerRadius == 0 && m_frameBorderWidth == 0) {
        updateFrameChildMasks();
        if (m_frameEffect) {
            setGraphicsEffect(nullptr);
            setFrameStyle(m_savedFrameStyle);
            setLineWidth(m_savedLineWidth);
            setMidLineWidth(m_savedMidLineWidth);
        }
        return;
    }
    if (!m_frameEffect) {
        m_savedFrameStyle = frameStyle();
        m_savedLineWidth = lineWidth();
        m_savedMidLineWidth = midLineWidth();
        m_frameEffect = new FrameAlphaEffect(this);
        setGraphicsEffect(m_frameEffect);
    }
    // QAbstractScrollArea lays out the viewport, headers and scrollbar containers
    // inside this frame inset. The effect replaces the native rectangular border.
    setFrameStyle(QFrame::Box | QFrame::Plain);
    setMidLineWidth(0);
    setLineWidth(m_frameBorderWidth);
    static_cast<FrameAlphaEffect *>(m_frameEffect.data())->invalidateMask();
    updateFrameChildMasks();
}

void VirtualItemView::updateFrameChildMasks()
{
    QWidget *children[] = {horizontalScrollBar(), verticalScrollBar(), cornerWidget()};
    const bool clipToFrame = m_frameCornerRadius > 0;
    // Keep scrollbar hover painting inside the border.  The native hover
    // primitive can extend to the child's edge by one device pixel, so the
    // child mask needs one extra device pixel beyond the configured border.
    const qreal dpr = qMax(qreal(1), devicePixelRatioF());
    const qreal inset = m_frameBorderWidth > 0
        ? qreal(m_frameBorderWidth) + qreal(1) / dpr
        : 0;
    const QRectF contentRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    const QPainterPath contentPath = roundedFramePath(
        contentRect, qMax(qreal(0), qreal(m_frameCornerRadius) - inset));
    const QPolygon contentPolygon = contentPath.toFillPolygon().toPolygon();

    for (QWidget *child : children) {
        if (!child)
            continue;
        if (!clipToFrame) {
            child->clearMask();
            continue;
        }

        QPolygon localPolygon = contentPolygon;
        localPolygon.translate(-child->mapTo(this, QPoint()));
        QRegion mask(child->rect());
        mask &= QRegion(localPolygon);
        const QRegion fullRegion(child->rect());
        if (mask == fullRegion)
            child->clearMask();
        else
            child->setMask(mask);
    }
}

} // namespace viv
