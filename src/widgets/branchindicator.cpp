#include <virtualitemviews/branchindicator.h>

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygon>
#include <QTransform>
#include <cmath>

namespace viv {

BranchIndicatorRenderer::~BranchIndicatorRenderer() = default;

QIcon BranchIndicatorRenderer::branchIcon(const BranchIndicatorState &state, const QModelIndex &index) const
{
    Q_UNUSED(state);
    Q_UNUSED(index);
    return QIcon();
}

void BranchIndicatorRenderer::paintBranch(QPainter *painter, const BranchIndicatorState &state,
                                          const QModelIndex &index, const QRect &cellRect) const
{
    if (!painter)
        return;
    const QIcon icon = branchIcon(state, index);
    if (icon.isNull())
        return;

    const QSize size = icon.actualSize(cellRect.size());
    const QPointF origin = QRectF(cellRect).center()
        - QPointF(size.width() / 2.0, size.height() / 2.0);
    painter->save();
    painter->translate(origin);
    icon.paint(painter, QRect(QPoint(), size));
    painter->restore();
}

QRect BranchIndicatorRenderer::branchIndicatorTargetRect(const QRect &cellRect, int size,
                                                          Qt::Alignment alignment,
                                                          const QMargins &padding)
{
    return branchIndicatorTargetRectF(QRectF(cellRect), qreal(size), alignment,
                                      QMarginsF(padding)).toRect();
}

QRectF BranchIndicatorRenderer::branchIndicatorTargetRectF(const QRectF &cellRect, qreal size,
                                                           Qt::Alignment alignment,
                                                           const QMarginsF &padding)
{
    if (cellRect.isEmpty())
        return QRectF();
    if (!(alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter)))
        alignment |= Qt::AlignLeft;
    if (!(alignment & (Qt::AlignTop | Qt::AlignBottom | Qt::AlignVCenter)))
        alignment |= Qt::AlignVCenter;

    const QMarginsF clamped(qMax(qreal(0), padding.left()), qMax(qreal(0), padding.top()),
                            qMax(qreal(0), padding.right()), qMax(qreal(0), padding.bottom()));
    const QRectF available = cellRect.marginsRemoved(clamped);
    if (available.isEmpty())
        return QRectF();

    const qreal side = qMin(qMax(qreal(0), size), qMin(available.width(), available.height()));
    if (side <= 0)
        return QRectF();

    qreal x = available.x();
    if (alignment & Qt::AlignRight)
        x = available.right() - side;
    else if (alignment & Qt::AlignHCenter)
        x = available.x() + (available.width() - side) / 2.0;

    qreal y = available.y();
    if (alignment & Qt::AlignBottom)
        y = available.bottom() - side;
    else if (alignment & Qt::AlignVCenter)
        y = available.y() + (available.height() - side) / 2.0;

    return QRectF(x, y, side, side);
}

void BranchIndicatorRenderer::paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                                 const QRect &cellRect, const QPalette &palette)
{
    paintBuiltinBranch(painter, state, cellRect, palette, kBranchIndicatorSize, QColor());
}

void BranchIndicatorRenderer::paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                                 const QRect &cellRect, const QPalette &palette,
                                                 int size, const QColor &color)
{
    paintBuiltinBranch(painter, state, cellRect, palette, size, color,
                       Qt::AlignCenter, QMargins());
}

void BranchIndicatorRenderer::paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                                 const QRect &cellRect, const QPalette &palette,
                                                 int size, const QColor &color,
                                                 Qt::Alignment alignment, const QMargins &padding)
{
    paintBuiltinBranch(painter, state, cellRect, palette, size, color, alignment, padding, 0.0);
}

void BranchIndicatorRenderer::paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                                 const QRect &cellRect, const QPalette &palette,
                                                 int size, const QColor &color,
                                                 Qt::Alignment alignment, const QMargins &padding,
                                                 qreal rotationDegrees)
{
    if (!painter || cellRect.isEmpty())
        return;
    // Only the row's own cell carries the expand/collapse indicator.
    if (!state.adjoinsItem || !state.hasChildren)
        return;

    const QRectF target = branchIndicatorTargetRectF(QRectF(cellRect), qreal(size), alignment,
                                                    QMarginsF(padding));
    if (target.isEmpty())
        return;

    const qreal side = target.width();
    const qreal height = side * std::sqrt(qreal(3)) / 2.0;
    const QPointF center = target.center();
    // Place the triangle's centroid at the fixed icon-frame center.
    QPolygonF arrow;
    if (state.isExpanded) {
        arrow << center + QPointF(-side / 2.0, -height / 3.0)
              << center + QPointF(side / 2.0, -height / 3.0)
              << center + QPointF(0.0, 2.0 * height / 3.0);
    } else {
        arrow << center + QPointF(-height / 3.0, -side / 2.0)
              << center + QPointF(2.0 * height / 3.0, 0.0)
              << center + QPointF(-height / 3.0, side / 2.0);
    }

    // Scale the corner rounding with the configured icon size.
    const qreal cornerFraction = 0.36;
    QPainterPath path;
    for (int i = 0; i < 3; ++i) {
        const QPointF &vertex = arrow[i];
        const QPointF entry = vertex + (arrow[(i + 2) % 3] - vertex) * cornerFraction;
        const QPointF exit = vertex + (arrow[(i + 1) % 3] - vertex) * cornerFraction;
        if (i == 0)
            path.moveTo(entry);
        else
            path.lineTo(entry);
        path.quadTo(vertex, exit);
    }
    path.closeSubpath();

    if (!qFuzzyIsNull(rotationDegrees)) {
        QTransform rotation;
        rotation.translate(center.x(), center.y());
        rotation.rotate(rotationDegrees);
        rotation.translate(-center.x(), -center.y());
        path = rotation.map(path);
    }

    painter->save();
    painter->setClipRect(QRectF(cellRect), Qt::IntersectClip);
    // Keep logical subpixel coordinates; QPainter maps the vector path to device pixels.
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color.isValid() ? color : palette.color(QPalette::WindowText));
    painter->drawPath(path);
    painter->restore();
}

void BranchIndicatorRenderer::paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                                 const QRect &cellRect)
{
    paintBuiltinBranch(painter, state, cellRect, QApplication::palette());
}

} // namespace viv
