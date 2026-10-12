#include <virtualitemviews/branchindicator.h>

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPolygon>

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
    const QRect target(cellRect.x() + (cellRect.width() - size.width()) / 2,
                       cellRect.y() + (cellRect.height() - size.height()) / 2,
                       size.width(), size.height());
    icon.paint(painter, target);
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
    if (!painter || cellRect.isEmpty())
        return;
    // Only the row's own cell carries the expand/collapse indicator.
    if (!state.adjoinsItem || !state.hasChildren)
        return;

    size = qMin(size, qMin(cellRect.width() - kBranchIndicatorMargin, cellRect.height()));
    if (size <= 0)
        return;

    const int x = cellRect.x() + kBranchIndicatorMargin;
    const int centerY = cellRect.center().y();
    QPolygon arrow;
    if (state.isExpanded) {
        arrow << QPoint(x, centerY - size / 2)
              << QPoint(x + size, centerY - size / 2)
              << QPoint(x + size / 2, centerY + size / 2);
    } else {
        arrow << QPoint(x, centerY - size / 2)
              << QPoint(x + size, centerY)
              << QPoint(x, centerY + size / 2);
    }

    painter->save();
    painter->setClipRect(cellRect, Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color.isValid() ? color : palette.color(QPalette::WindowText));
    painter->drawPolygon(arrow);
    painter->restore();
}

void BranchIndicatorRenderer::paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                                 const QRect &cellRect)
{
    paintBuiltinBranch(painter, state, cellRect, QApplication::palette());
}

} // namespace viv
