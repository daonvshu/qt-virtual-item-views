#include <virtualitemviews/wrapwidget.h>
#include <virtualitemviews/wraplayout.h>
#include <QEvent>
#include <QScrollBar>
#include <QResizeEvent>
#include <QWheelEvent>

namespace viv {

WrapWidget::WrapWidget(QWidget *parent) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("vivWrapWidget"));
    viewport()->setObjectName(QStringLiteral("vivWrapViewport"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_content = new QWidget;
    m_content->setObjectName(QStringLiteral("vivWrapContent"));
    m_layout = new WrapLayout(m_content);
    // The host manages its scrollable size; minimum child widths cannot force
    // the scroll area itself to grow beyond the parent layout's allocation.
    m_layout->setSizeConstraint(QLayout::SetNoConstraint);
    setWidget(m_content);
    connect(m_layout, &WrapLayout::layoutChanged, this, &WrapWidget::scheduleUpdate);
    scheduleUpdate();
}
void WrapWidget::addWidget(QWidget *widget) { insertWidget(m_layout->count(), widget); }
void WrapWidget::insertWidget(int index, QWidget *widget)
{
    if (!widget || widget == this || widget->isAncestorOf(this)) return;
    m_layout->insertWidget(index, widget);
    widget->show();
}
QWidget *WrapWidget::takeWidget(int index)
{
    QLayoutItem *item = m_layout->takeAt(index);
    if (!item) return nullptr;
    QWidget *widget = item->widget();
    delete item;
    if (widget) {
        widget->hide();
        widget->setParent(nullptr);
    }
    return widget;
}
void WrapWidget::clear()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}
void WrapWidget::setAutoHeightEnabled(bool enabled)
{
    if (m_autoHeight == enabled) return;
    m_autoHeight = enabled;
    QSizePolicy policy = sizePolicy();
    if (enabled) {
        m_previousPolicy = policy.verticalPolicy();
        policy.setVerticalPolicy(QSizePolicy::Maximum);
    } else {
        policy.setVerticalPolicy(m_previousPolicy);
    }
    setSizePolicy(policy);
    updateGeometry();
    scheduleUpdate();
}
QSize WrapWidget::sizeHint() const
{
    if (!m_autoHeight || !m_layout) return QScrollArea::sizeHint();
    const int width = qMax(1, maximumViewportSize().width());
    return QSize(320, qBound(minimumHeight(), m_layout->heightForWidth(width)
        + 2 * frameWidth(), maximumHeight()));
}
QSize WrapWidget::minimumSizeHint() const
{
    return m_autoHeight ? QSize(2 * frameWidth(), 2 * frameWidth()) : QScrollArea::minimumSizeHint();
}
void WrapWidget::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    scheduleUpdate();
}
void WrapWidget::wheelEvent(QWheelEvent *event)
{
    // Let fitting real-widget collections scroll their enclosing scroll area.
    if (verticalScrollBar()->maximum() == verticalScrollBar()->minimum()
        && horizontalScrollBar()->maximum() == horizontalScrollBar()->minimum()) {
        event->ignore();
        return;
    }
    QScrollArea::wheelEvent(event);
}
bool WrapWidget::event(QEvent *event)
{
    const bool result = QScrollArea::event(event);
    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest)
        scheduleUpdate();
    return result;
}
void WrapWidget::scheduleUpdate()
{
    if (m_updatePending || !m_layout) return;
    m_updatePending = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_updatePending = false;
        updateContent();
    }, Qt::QueuedConnection);
}
void WrapWidget::updateContent()
{
    if (m_updating) return;
    m_updating = true;
    // Compute from the scrollbar-free width first. Narrowing a wrap layout can
    // only increase its height, so this decision cannot oscillate at a threshold.
    const QSize full = maximumViewportSize();
    const bool needed = m_layout->heightForWidth(full.width()) > full.height();
    int width = full.width();
    if (needed && verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded)
        width -= verticalScrollBar()->sizeHint().width();
    width = qMax(1, width);
    m_content->resize(width, qMax(1, m_layout->heightForWidth(width)));
    m_layout->setGeometry(m_content->rect());
    const QSize hint = sizeHint();
    if (hint != m_lastHint) {
        m_lastHint = hint;
        updateGeometry();
    }
    m_updating = false;
}

} // namespace viv
