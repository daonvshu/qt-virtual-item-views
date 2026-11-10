#include <virtualitemviews/virtualitemview.h>
#include "performanceprobe_p.h"
#include "viewbackground_p.h"

#include <QEvent>
#include <QChildEvent>
#include <QCoreApplication>
#include <QGraphicsEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QPixmap>
#include <QScrollBar>
#include <QResizeEvent>

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

QPainterPath visibleFrameBorder(const QRectF &outer, const QPainterPath &border, Qt::Edges edges)
{
    const Qt::Edges allEdges = Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge;
    if (edges == allEdges)
        return border;
    if (!edges || outer.isEmpty())
        return QPainterPath();

    // Split adjacent edges at 45 degrees so each owns half of the rounded corner.
    const qreal depth = qMin(outer.width(), outer.height()) / 2;
    const QPointF tl = outer.topLeft();
    const QPointF tr = outer.topRight();
    const QPointF bl = outer.bottomLeft();
    const QPointF br = outer.bottomRight();
    QPainterPath coverage;
    coverage.setFillRule(Qt::WindingFill);
    const auto addEdge = [&coverage, edges](Qt::Edge edge, const QPolygonF &polygon) {
        if (edges.testFlag(edge)) {
            coverage.addPolygon(polygon);
            coverage.closeSubpath();
        }
    };
    addEdge(Qt::TopEdge, QPolygonF() << tl << tr
        << tr + QPointF(-depth, depth) << tl + QPointF(depth, depth));
    addEdge(Qt::RightEdge, QPolygonF() << tr << br
        << br + QPointF(-depth, -depth) << tr + QPointF(-depth, depth));
    addEdge(Qt::BottomEdge, QPolygonF() << br << bl
        << bl + QPointF(depth, -depth) << br + QPointF(-depth, -depth));
    addEdge(Qt::LeftEdge, QPolygonF() << bl << tl
        << tl + QPointF(depth, depth) << bl + QPointF(depth, -depth));
    return border.intersected(coverage);
}

QPainterPath frameContentPath(const QRectF &outer, qreal radius, qreal width, Qt::Edges edges)
{
    radius = qBound(qreal(0), radius, qMax(qreal(0), qMin(outer.width(), outer.height()) / 2));
    if (radius == 0) {
        return roundedFramePath(outer.adjusted(edges.testFlag(Qt::LeftEdge) ? width : 0,
                                               edges.testFlag(Qt::TopEdge) ? width : 0,
                                               edges.testFlag(Qt::RightEdge) ? -width : 0,
                                               edges.testFlag(Qt::BottomEdge) ? -width : 0), 0);
    }
    const QPainterPath outerPath = roundedFramePath(outer, radius);
    if (width <= 0 || !edges)
        return outerPath;
    const QPainterPath innerPath = roundedFramePath(outer.adjusted(width, width, -width, -width),
                                                    qMax(qreal(0), radius - width));
    if (edges == (Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge))
        return innerPath;
    // Restore content under hidden border segments while keeping the outer rounded contour.
    return outerPath.subtracted(visibleFrameBorder(outer, outerPath.subtracted(innerPath), edges));
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
        const int borderPixels = qMax(0, qRound(m_view->frameBorderWidth() * dpr));
        const qreal borderLogical = dpr > 0 ? qreal(borderPixels) / dpr : 0;
        const QPainterPath innerPath = frameContentPath(outer, radius, borderLogical,
                                                        m_view->frameBorderEdges());
        const QPainterPath outerPath = roundedFramePath(outer, radius);
        QPainterPath borderPath;
        if (borderPixels > 0 && m_view->frameBorderEdges()) {
            borderPath = outerPath;
            const Qt::Edges allEdges = Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge;
            if (m_view->frameBorderEdges() == allEdges) {
                // Preserve curves instead of flattening them through boolean subtraction.
                borderPath.setFillRule(Qt::OddEvenFill);
                borderPath.addPath(innerPath);
            } else {
                borderPath = borderPath.subtracted(innerPath);
            }
        }
        if (m_mask.size() != source.size() || m_mask.devicePixelRatioF() != dpr
            || m_maskBounds != outer) {
            m_mask = QPixmap(source.size());
            m_mask.setDevicePixelRatio(dpr);
            m_mask.fill(Qt::transparent);
            m_maskBounds = outer;
            QPainter maskPainter(&m_mask);
            maskPainter.setRenderHint(QPainter::Antialiasing);
            // Like Qt's stylesheet background, end content at the border centerline.
            // Keep smooth coverage beneath the inner half of the border so normal
            // SourceOver composition does not leave transparent gaps at the join.
            maskPainter.fillPath(frameContentPath(outer, radius, borderLogical / 2,
                                                   m_view->frameBorderEdges()), Qt::white);
        }
        QPixmap result(source.size());
        result.setDevicePixelRatio(dpr);
        result.fill(Qt::transparent);
        {
            QPainter buffer(&result);
            buffer.drawPixmap(QPointF(), source);
            buffer.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            // Clear source pixels outside the antialiased content contour.
            buffer.drawPixmap(QPointF(), m_mask);
            if (borderPixels > 0 && m_view->frameBorderEdges()) {
                // Blend the border over the clipped content without adding their colors.
                buffer.setCompositionMode(QPainter::CompositionMode_SourceOver);
                buffer.setRenderHint(QPainter::Antialiasing);
                const QColor color = m_view->frameBorderColor();
                buffer.fillPath(borderPath,
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
    const QPointer<VirtualItemView> self(this);
    if (m_frameEffect && event->type() == QEvent::Resize) {
        const auto *resize = static_cast<QResizeEvent *>(event);
        if (resize->size() == resize->oldSize()
            && (m_updatingFrameContentsMargins || updateFrameContentsMargins())) {
            // setFrameRect() emits a same-size resize before ContentsRectChange.
            // Restore the inset before the scroll-area layout reads contentsRect(),
            // without recursively restarting that layout from setContentsMargins().
            return true;
        }
        if (!self)
            return true;
    }
    if (event->type() == QEvent::ContentsRectChange) {
        if (m_updatingFrameContentsMargins)
            return true;
        // Hidden widgets defer their resize event but send this event immediately.
        updateFrameContentsMargins();
        if (!self)
            return true;
    }
    if (event->type() == QEvent::Paint) {
        detail::PerformanceScope performance(this, "paint.view-background", detail::PerformanceDomain::Paint);
        // Qt5 can paint the scrollbars after the scroll-area's effect source has
        // been composed.  Refresh their local masks immediately before the
        // source is rendered so the rounded outer corners are clipped there too.
        if (m_frameEffect)
            updateFrameChildMasks();
        QPainter painter(this);
        painter.save();
        painter.setClipRect(contentsRect());
        // Paint once behind the viewport, transparent headers and scroll-area corner.
        painter.fillRect(rect(), backgroundBrush());
        painter.restore();
        if (!m_frameEffect) {
            // Keep native frame styling outside the content. Its QSS panel must not
            // paint a second background under a translucent view or viewport.
            painter.setClipRegion(QRegion(rect()) - QRegion(contentsRect()));
            drawFrame(&painter);
        }
        return true;
    }
    const bool refreshChildMasks = event->type() == QEvent::LayoutRequest
        || event->type() == QEvent::StyleChange;
    const bool refreshFrameMetrics = m_frameEffect && !m_updatingFrameContentsMargins
        && (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange
            || event->type() == QEvent::ParentChange);
    if (refreshFrameMetrics) {
        restoreFrameContentsMargins();
        if (!self)
            return true;
    }
    // QFrame preserves frameRect() when changing style metrics. Let it see native
    // margins throughout that operation, then apply our per-edge correction once.
    if (refreshFrameMetrics)
        m_updatingFrameContentsMargins = true;
    const bool handled = QAbstractScrollArea::event(event);
    if (!self)
        return handled;
    if (refreshFrameMetrics)
        m_updatingFrameContentsMargins = false;
    if (m_frameEffect)
        updateFrameContentsMargins();
    if (!self)
        return handled;
    if (event->type() == QEvent::ParentChange && m_autoHeightEnabled) {
        m_autoHeightLastHints = QSize();
        scheduleAutoHeightGeometryUpdate();
    }
    if ((event->type() == QEvent::Polish || event->type() == QEvent::StyleChange
         || (event->type() == QEvent::ChildPolished
             && static_cast<QChildEvent *>(event)->child() == viewport()))
        && testAttribute(Qt::WA_StyleSheet) && viewport()->styleSheet().isEmpty()) {
        // Qt skips independent QSS rules for an embedded viewport with an empty
        // local stylesheet. A declaration-free sheet enables ancestor selectors
        // without overriding them or replacing an application's viewport rules.
        viewport()->setStyleSheet(QStringLiteral("/* Enable independent viewport QSS matching. */"));
        if (!self)
            return handled;
    }
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange
        || event->type() == QEvent::PaletteChange) {
        detail::prepareViewBackground(this);
        detail::prepareViewBackground(viewport());
        updateBackgroundAppearance();
    } else if (event->type() == QEvent::ChildPolished
               && static_cast<QChildEvent *>(event)->child() == viewport()) {
        // Initial viewport polish can re-enable Qt's automatic background fill.
        detail::prepareViewBackground(viewport());
    }
    if (refreshChildMasks && !m_frameEffect.isNull())
        updateFrameChildMasks();
    if (m_frameEffect && (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange
        || event->type() == QEvent::ParentChange || event->type() == QEvent::Show)) {
        // A style can reset frame metrics after the scroll area's own layout pass.
        // Queue one pass for that lifecycle change, never for the correction itself.
        updateFrameContentsMargins();
        if (!self)
            return handled;
        QCoreApplication::postEvent(this, new QEvent(QEvent::LayoutRequest));
    }
    if (refreshChildMasks || event->type() == QEvent::ContentsRectChange)
        updateAutoHeightGeometry();
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
    // A transparent frame must not suppress an adjacent terminal grid line.
    markDirty();
}

void VirtualItemView::setFrameBorderEdges(Qt::Edges edges)
{
    edges &= Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge;
    if (m_frameBorderEdges == edges)
        return;
    m_frameBorderEdges = edges;
    updateFrameAppearance();
}

void VirtualItemView::setFrameBorderVisible(Qt::Edge edge, bool visible)
{
    Qt::Edges edges = m_frameBorderEdges;
    edges.setFlag(edge, visible);
    setFrameBorderEdges(edges);
}

bool VirtualItemView::frameBorderVisible(Qt::Edge edge) const
{
    const Qt::Edges allEdges = Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge;
    return allEdges.testFlag(edge) && m_frameBorderEdges.testFlag(edge);
}

void VirtualItemView::updateFrameAppearance()
{
    // Reconcile grid edges even when the frame change leaves viewport size unchanged.
    markDirty();
    const QPointer<VirtualItemView> self(this);
    restoreFrameContentsMargins();
    if (!self)
        return;
    if (m_frameCornerRadius == 0 && m_frameBorderWidth == 0) {
        updateFrameChildMasks();
        if (m_frameEffect) {
            setGraphicsEffect(nullptr);
            setFrameStyle(m_savedFrameStyle);
            setLineWidth(m_savedLineWidth);
            setMidLineWidth(m_savedMidLineWidth);
            // Remove any inset supplied for a style whose native frame was zero.
            setFrameRect(rect());
            QCoreApplication::postEvent(this, new QEvent(QEvent::LayoutRequest));
        }
        updateAutoHeightGeometry();
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
    m_updatingFrameContentsMargins = true;
    setFrameStyle(QFrame::Box | QFrame::Plain);
    setMidLineWidth(0);
    setLineWidth(m_frameBorderWidth);
    // Reset the previous correction when reducing the width, including to zero.
    setFrameRect(rect());
    m_updatingFrameContentsMargins = false;
    updateFrameContentsMargins();
    QCoreApplication::postEvent(this, new QEvent(QEvent::LayoutRequest));
    static_cast<FrameAlphaEffect *>(m_frameEffect.data())->invalidateMask();
    updateFrameChildMasks();
    updateAutoHeightGeometry();
}

bool VirtualItemView::updateFrameContentsMargins()
{
    if (!m_frameEffect || m_updatingFrameContentsMargins || m_destroying)
        return false;
    const QMargins current = contentsMargins();
    if (m_hasFrameContentsMargins && current == m_appliedFrameContentsMargins)
        return false;
    // Native per-side frame insets may differ under QSS. Keep any remaining
    // frameRect offsets (such as scrollbar space) and replace only the border.
    const QRect content = contentsRect();
    const QRect frame = frameRect();
    const QMargins required(
        current.left() - (content.left() - frame.left())
            + (m_frameBorderEdges.testFlag(Qt::LeftEdge) ? m_frameBorderWidth : 0),
        current.top() - (content.top() - frame.top())
            + (m_frameBorderEdges.testFlag(Qt::TopEdge) ? m_frameBorderWidth : 0),
        current.right() - (frame.right() - content.right())
            + (m_frameBorderEdges.testFlag(Qt::RightEdge) ? m_frameBorderWidth : 0),
        current.bottom() - (frame.bottom() - content.bottom())
            + (m_frameBorderEdges.testFlag(Qt::BottomEdge) ? m_frameBorderWidth : 0));
    m_nativeFrameContentsMargins = current;
    m_appliedFrameContentsMargins = required;
    m_hasFrameContentsMargins = true;
    if (required == current)
        return false;
    const QPointer<VirtualItemView> self(this);
    m_updatingFrameContentsMargins = true;
    setContentsMargins(required);
    if (self)
        m_updatingFrameContentsMargins = false;
    return true;
}

void VirtualItemView::restoreFrameContentsMargins()
{
    if (!m_hasFrameContentsMargins || m_updatingFrameContentsMargins)
        return;
    m_hasFrameContentsMargins = false;
    if (contentsMargins() != m_appliedFrameContentsMargins)
        return;
    const QPointer<VirtualItemView> self(this);
    m_updatingFrameContentsMargins = true;
    setContentsMargins(m_nativeFrameContentsMargins);
    if (self)
        m_updatingFrameContentsMargins = false;
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
    const QPainterPath contentPath = frameContentPath(QRectF(rect()), m_frameCornerRadius,
                                                     inset, m_frameBorderEdges);
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
