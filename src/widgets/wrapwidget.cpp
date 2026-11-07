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
WrapWidget::~WrapWidget()
{
    m_destroying = true;
    updateAutoHeightAncestorTracking();
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
    m_autoHeight = enabled;
    QSizePolicy policy = sizePolicy();
    if (enabled) {
        m_previousPolicy = policy.verticalPolicy();
        policy.setVerticalPolicy(QSizePolicy::Maximum);
    } else {
        policy.setVerticalPolicy(m_previousPolicy);
    }
    setSizePolicy(policy);
    updateAutoHeightAncestorTracking();
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
    if (!m_autoHeight) return QScrollArea::minimumSizeHint();
    QSize hint(2 * frameWidth(), qMin(maximumHeight(), 2 * frameWidth()));
    // A resizable outer scroll area allocates space from layout minima. Keep
    // all wrapped rows there, bounded by this host's own maximum height.
    if (autoHeightPreservesContentMinimum())
        hint.setHeight(sizeHint().height());
    return hint;
}
bool WrapWidget::autoHeightPreservesContentMinimum() const
{
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(ancestor)) {
            auto *outer = qobject_cast<QScrollArea *>(area);
            return outer && outer->widgetResizable() && outer->widget()
                && (outer->widget() == this || outer->widget()->isAncestorOf(this));
        }
        if (ancestor->isWindow() || ancestor->maximumHeight() < QWIDGETSIZE_MAX)
            break;
    }
    return false;
}
void WrapWidget::updateAutoHeightAncestorTracking()
{
    QVector<QPointer<QWidget>> ancestors;
    if (m_autoHeight && !m_destroying) {
        // Observe beyond temporary height limits so removing a collapse limit
        // also refreshes the parent's cached layout hints.
        for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            ancestors.append(ancestor);
            if (ancestor->isWindow() || qobject_cast<QAbstractScrollArea *>(ancestor))
                break;
        }
    }
    if (ancestors != m_autoHeightAncestors) {
        for (const QPointer<QWidget> &ancestor : m_autoHeightAncestors) {
            if (ancestor) ancestor->removeEventFilter(this);
        }
        m_autoHeightAncestors = ancestors;
        for (const QPointer<QWidget> &ancestor : m_autoHeightAncestors) {
            if (ancestor) ancestor->installEventFilter(this);
        }
        m_lastHint = QSize();
        m_lastMinimumHint = QSize();
    }
    const bool preservesMinimum = m_autoHeight && !m_destroying
        && autoHeightPreservesContentMinimum();
    if (preservesMinimum != m_preservesContentMinimum) {
        m_preservesContentMinimum = preservesMinimum;
        m_lastHint = QSize();
        m_lastMinimumHint = QSize();
    }
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
        m_lastMinimumHint = QSize();
        updateAutoHeightAncestorTracking();
    }
    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest
        || event->type() == QEvent::ParentChange || event->type() == QEvent::ContentsRectChange)
        scheduleUpdate();
    return result;
}
bool WrapWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (m_destroying) return false;
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget && m_autoHeight && m_autoHeightAncestors.contains(widget)) {
        switch (event->type()) {
        case QEvent::LayoutRequest:
        case QEvent::Resize:
        case QEvent::ContentsRectChange:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ParentChange:
            scheduleUpdate();
            break;
        default:
            break;
        }
        return false;
    }
    return QScrollArea::eventFilter(watched, event);
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
    m_updating = true;
    updateAutoHeightAncestorTracking();
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
    const QSize minimumHint = minimumSizeHint();
    if (hint != m_lastHint || minimumHint != m_lastMinimumHint) {
        m_lastHint = hint;
        m_lastMinimumHint = minimumHint;
        updateGeometry();
    }
    m_updating = false;
}

} // namespace viv
