#pragma once

#include <QPainter>
#include <QRegion>
#include <QWidget>

namespace viv {

// Paint the uncut target rectangle, letting the viewport clip real border edges.
class TreeDragPlaceholderOutline : public QWidget
{
public:
    explicit TreeDragPlaceholderOutline(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivTreeDragPlaceholderOutline"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

    void setFrame(const QRect &rect, const QColor &color, int logicalWidth)
    {
        m_frame = rect;
        m_color = color;
        m_width = qMax(0, logicalWidth);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QTransform transform = painter.deviceTransform();
        bool invertible = false;
        const QTransform inverse = transform.inverted(&invertible);
        if (!invertible || m_frame.isEmpty() || m_width <= 0)
            return;
        const auto deviceRect = [&transform](const QRectF &rect) {
            const QRectF device = transform.mapRect(rect);
            return QRect(qRound(device.left()), qRound(device.top()),
                         qMax(0, qRound(device.right()) - qRound(device.left())),
                         qMax(0, qRound(device.bottom()) - qRound(device.top())));
        };
        const QRect outer = deviceRect(QRectF(m_frame));
        QRegion border(outer);
        // Indicator widths are logical pixels; align both edges after DPI scaling.
        const QRectF inner = QRectF(m_frame).adjusted(m_width, m_width, -m_width, -m_width);
        if (!inner.isEmpty())
            border -= deviceRect(inner);
        // Union all four physical-pixel edges before painting translucent colors.
        for (const QRect &part : border)
            painter.fillRect(inverse.mapRect(QRectF(part)), m_color);
    }

private:
    QRect m_frame;
    QColor m_color;
    int m_width = 2;
};

} // namespace viv
