#include "borderdemo.h"

#include <virtualitemviews/virtualtableview.h>
#include <QGraphicsEffect>
#include <QPainter>
#include <QPixmap>

namespace table_borders {

class RoundedAlphaEffect : public QGraphicsEffect
{
public:
    explicit RoundedAlphaEffect(QObject *parent) : QGraphicsEffect(parent) {}

    void setRadius(qreal radius)
    {
        if (m_radius == radius)
            return;
        m_radius = radius;
        m_mask = QPixmap();
        update();
    }

protected:
    void draw(QPainter *painter) override
    {
        if (m_radius <= 0) {
            drawSource(painter);
            return;
        }
        QPoint offset;
        const QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
        if (source.isNull())
            return;
        const qreal dpr = source.devicePixelRatioF();
        const QRectF bounds = sourceBoundingRect(Qt::LogicalCoordinates).translated(-QPointF(offset));
        if (m_mask.size() != source.size() || m_mask.devicePixelRatioF() != dpr
            || m_maskBounds != bounds) {
            m_mask = QPixmap(source.size());
            m_mask.setDevicePixelRatio(dpr);
            m_mask.fill(Qt::transparent);
            m_maskBounds = bounds;
            QPainter maskPainter(&m_mask);
            maskPainter.setRenderHint(QPainter::Antialiasing);
            maskPainter.fillPath(roundedOutline(bounds, m_radius), Qt::white);
        }
        QPixmap result(source.size());
        result.setDevicePixelRatio(dpr);
        result.fill(Qt::transparent);
        {
            QPainter buffer(&result);
            buffer.drawPixmap(QPointF(), source);
            buffer.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            // Draw the entire mask, including transparent pixels, to clear all four outside corners.
            buffer.drawPixmap(QPointF(), m_mask);
        }
        painter->drawPixmap(offset, result);
    }

private:
    qreal m_radius = 0;
    QPixmap m_mask;
    QRectF m_maskBounds;
};

class AlphaFrame : public BorderDemo
{
public:
    explicit AlphaFrame(QWidget *parent) : BorderDemo(parent)
    {
        attachView(new viv::VirtualTableView(this));
        m_effect = new RoundedAlphaEffect(view());
        // Qt includes the view's headers, viewport, materialized widgets and scrollbars in this effect.
        view()->setGraphicsEffect(m_effect);
    }

protected:
    void updateTechnique() override
    {
        if (m_effect)
            m_effect->setRadius(contentRadius());
    }
    void paintEvent(QPaintEvent *) override
    {
        // The composed table owns its alpha edge; a second base fill would make it opaque.
    }

private:
    RoundedAlphaEffect *m_effect = nullptr;
};

BorderDemo *createAlphaFrame(QWidget *parent)
{
    return new AlphaFrame(parent);
}

} // namespace table_borders
