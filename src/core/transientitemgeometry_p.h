#pragma once

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QWidget>
#include <virtualitemviews/layoutpolicy.h>
#include <virtualitemviews/types.h>

namespace viv { namespace detail {

// Split shared grid layers at the revealed subtree band. Coordinates are local
// to the painter; the viewport origin accounts for header and spacing hosts.
template<typename Paint>
void paintWithTransientItemOpacity(QPainter *painter, const LayoutPolicy *layout,
                                  qint64 offset, const QRect &viewport, Paint paint)
{
    if (!layout || !layout->hasTransientGeometry() || viewport.isEmpty()) {
        paint();
        return;
    }
    QVector<VisibleRange> ranges{{layout->indexAtOffset(offset),
        qMin(layout->itemCount() - 1, layout->indexAtOffset(offset + viewport.height()))}};
    layout->filterVisibleRanges(ranges);
    QRegion faded;
    qreal opacity = 1.0;
    for (const VisibleRange &range : ranges) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            const qreal alpha = layout->itemOpacity(row);
            if (alpha >= 1.0)
                continue;
            const qint64 top = qMax<qint64>(0, layout->offsetOf(row) - offset);
            const qint64 bottom = qMin<qint64>(viewport.height(), layout->offsetOf(row + 1) - offset);
            if (bottom > top) {
                faded += QRect(viewport.x(), viewport.y() + int(top), viewport.width(), int(bottom - top));
                opacity = alpha;
            }
        }
    }
    if (faded.isEmpty()) {
        paint();
        return;
    }
    const QRegion clip = painter->hasClipping() ? painter->clipRegion()
        : QRegion(QRect(0, 0, painter->device()->width(), painter->device()->height()));
    painter->save();
    painter->setClipRegion(clip - faded, Qt::IntersectClip);
    paint();
    painter->restore();
    painter->save();
    painter->setClipRegion(faded, Qt::IntersectClip);
    painter->setOpacity(painter->opacity() * opacity);
    paint();
    painter->restore();
}

// A framework-owned parent clips and optionally fades a live item without
// replacing business graphics effects. The child keeps its natural height.
class TransientItemClipHost : public QWidget
{
public:
    TransientItemClipHost(QWidget *item, QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        item->installEventFilter(this);
        connect(item, &QObject::destroyed, this, &QObject::deleteLater);
    }

    void setItemOpacity(qreal opacity)
    {
        if (opacity >= 1.0) {
            if (graphicsEffect())
                setGraphicsEffect(nullptr);
            return;
        }
        auto *effect = static_cast<QGraphicsOpacityEffect *>(graphicsEffect());
        if (!effect) {
            effect = new QGraphicsOpacityEffect(this);
            setGraphicsEffect(effect);
        }
        effect->setOpacity(opacity);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::ParentChange && object->parent() != this) {
            hide();
            deleteLater();
        }
        if (object->parent() == this) {
            if (event->type() == QEvent::Hide)
                hide();
            else if (event->type() == QEvent::ShowToParent)
                show();
        }
        return false;
    }
};

inline void prepareTransientItemGeometry(QWidget *item, QWidget *&parent,
                                         QRect &geometry, int naturalHeight, bool transient,
                                         qreal opacity = 1.0)
{
    if (!transient || !geometry.isValid()
        || (geometry.height() >= naturalHeight && opacity >= 1.0))
        return;
    auto *host = dynamic_cast<TransientItemClipHost *>(item->parentWidget());
    if (!host || host->parentWidget() != parent)
        host = new TransientItemClipHost(item, parent);
    host->setGeometry(geometry);
    host->setItemOpacity(opacity);
    host->show();
    parent = host;
    geometry = QRect(0, 0, geometry.width(), naturalHeight);
}

} } // namespace viv::detail
