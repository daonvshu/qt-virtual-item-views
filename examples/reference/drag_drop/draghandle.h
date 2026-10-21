#pragma once

#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <functional>

namespace reference {

// Only gesture detection. The adapter supplies the current index and preview.
class DragHandle : public QPushButton
{
public:
    explicit DragHandle(QWidget *parent) : QPushButton(QStringLiteral("Drag"), parent) {}
    std::function<void(const QPoint &)> beginDrag;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = event->button() == Qt::LeftButton;
        m_globalPress = mapToGlobal(event->pos());
        QPushButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressed && (event->buttons() & Qt::LeftButton) && beginDrag
            && (mapToGlobal(event->pos()) - m_globalPress).manhattanLength()
                >= QApplication::startDragDistance()) {
            m_pressed = false;
            setDown(false);
            event->accept();
            // QDrag::exec() may destroy the widget. Use local copies and return directly.
            const auto start = beginDrag;
            const QPoint press = m_globalPress;
            start(press);
            return;
        }
        QPushButton::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_pressed = false;
        QPushButton::mouseReleaseEvent(event);
    }

private:
    bool m_pressed = false;
    QPoint m_globalPress;
};

} // namespace reference
