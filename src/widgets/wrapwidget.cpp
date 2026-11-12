#include <virtualitemviews/wrapwidget.h>
#include <virtualitemviews/wraplayout.h>
#include "../core/transparenthost_p.h"
#include <QEvent>
#include <QPointer>
#include <QScrollBar>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>
#include <QWheelEvent>

namespace viv {

WrapWidget::WrapWidget(QWidget *parent) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("vivWrapWidget"));
    viewport()->setObjectName(QStringLiteral("vivWrapViewport"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_content = new QWidget;
    m_content->setObjectName(QStringLiteral("vivWrapContent"));
    m_layout = new WrapLayout(m_content);
    // The host manages its scrollable size; minimum child widths cannot force
    // the scroll area itself to grow beyond the parent layout's allocation.
    m_layout->setSizeConstraint(QLayout::SetNoConstraint);
    setWidget(m_content);
    // QScrollArea::setWidget enables auto-fill; apply transparency afterwards.
    detail::prepareTransparentHost(m_content);
    connect(m_layout, &WrapLayout::layoutChanged, this, &WrapWidget::scheduleUpdate);
    scheduleUpdate();
}
WrapWidget::~WrapWidget()
{
    m_destroying = true;
    disconnect(m_layout, nullptr, this, nullptr);
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
    const QPointer<WrapWidget> self(this);
    m_autoHeight = enabled;
    QSizePolicy policy = sizePolicy();
    if (enabled) {
        m_previousPolicy = policy.verticalPolicy();
        policy.setVerticalPolicy(QSizePolicy::Fixed);
    } else {
        policy.setVerticalPolicy(m_previousPolicy);
        if (m_fixedHeightApplied) {
            m_fixedHeightApplied = false;
            // Release the computed minimum before restoring the saved maximum.
            setMinimumHeight(0);
            if (!self) return;
            setMaximumHeight(m_previousMaximumHeight);
            if (!self) return;
            setMinimumHeight(m_previousMinimumHeight);
            if (!self) return;
        }
    }
    setSizePolicy(policy);
    m_lastHint = QSize();
    updateGeometry();
    scheduleUpdate();
}
void WrapWidget::setAutoHeightMaximumHeight(int height)
{
    height = qBound(0, height, QWIDGETSIZE_MAX);
    if (m_autoHeightMaximumHeight == height) return;
    m_autoHeightMaximumHeight = height;
    if (m_autoHeight) {
        updateGeometry();
        scheduleUpdate();
    }
}
int WrapWidget::autoHeightDecorationExtent() const
{
    const QMargins margins = viewportMargins();
    int height = 2 * frameWidth() + margins.top() + margins.bottom();
    if (horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOn) {
        const QScrollBar *bar = horizontalScrollBar();
        QStyleOption option;
        option.initFrom(this);
        if (!bar->style()->styleHint(QStyle::SH_ScrollBar_Transient, &option, bar)) {
            height += bar->sizeHint().height();
            if (style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents, &option, this))
                height += qMax(0, style()->pixelMetric(QStyle::PM_ScrollView_ScrollBarSpacing, &option, this));
        }
    }
    return height;
}
QSize WrapWidget::sizeHint() const
{
    if (!m_autoHeight || !m_layout) return QScrollArea::sizeHint();
    const int width = qMax(1, maximumViewportSize().width());
    // Do not read QWidget's height constraints: setFixedHeight stores the last
    // computed result there, not the independent business-configured limit.
    const qint64 contentHeight = m_layout->heightForWidth(width);
    const int limit = m_autoHeightMaximumHeight > 0 ? m_autoHeightMaximumHeight : QWIDGETSIZE_MAX;
    return QSize(320, int(qBound(qint64(0), contentHeight + autoHeightDecorationExtent(), qint64(limit))));
}
QSize WrapWidget::minimumSizeHint() const
{
    if (!m_autoHeight) return QScrollArea::minimumSizeHint();
    return QSize(2 * frameWidth(), sizeHint().height());
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
    const QPointer<WrapWidget> self(this);
    const bool result = QScrollArea::event(event);
    if (!self || m_destroying) return result;
    if (event->type() == QEvent::Show || event->type() == QEvent::ParentChange) {
        // Hidden geometry updates may not invalidate the parent's QWidgetItem.
        m_lastHint = QSize();
    }
    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::FontChange || event->type() == QEvent::Show
        || event->type() == QEvent::LayoutRequest || event->type() == QEvent::ParentChange
        || event->type() == QEvent::ContentsRectChange)
        scheduleUpdate();
    return result;
}
void WrapWidget::scheduleUpdate()
{
    if (m_destroying || m_updatePending || !m_layout) return;
    m_updatePending = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_updatePending = false;
        updateContent();
    }, Qt::QueuedConnection);
}
void WrapWidget::updateContent()
{
    if (m_destroying || m_updating) return;
    const QPointer<WrapWidget> self(this);
    m_updating = true;
    if (m_autoHeight) {
        const int targetHeight = sizeHint().height();
        if (!m_fixedHeightApplied) {
            m_previousMinimumHeight = minimumHeight();
            m_previousMaximumHeight = maximumHeight();
            m_fixedHeightApplied = true;
        }
        if (minimumHeight() != targetHeight || maximumHeight() != targetHeight) {
            setFixedHeight(targetHeight);
            if (!self) return;
        }
    }
    // Decide scrollbar need using the final host height and scrollbar-free
    // width. A visible AsNeeded scrollbar must not prevent later shrinking.
    const QSize full = maximumViewportSize();
    const bool needed = m_layout->heightForWidth(full.width()) > full.height();
    int width = full.width();
    if (needed && verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded)
        width -= verticalScrollBar()->sizeHint().width();
    width = qMax(1, width);
    m_content->resize(width, qMax(0, m_layout->heightForWidth(width)));
    if (!self) return;
    m_layout->setGeometry(m_content->rect());
    if (!self) return;
    const QSize hint = sizeHint();
    if (hint != m_lastHint) {
        m_lastHint = hint;
        updateGeometry();
    }
    m_updating = false;
}

} // namespace viv
