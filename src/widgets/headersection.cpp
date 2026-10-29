#include <virtualitemviews/headersection.h>
#include "../core/performanceprobe_p.h"

#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtableview.h>

#include <QEvent>
#include <QPainter>

namespace viv {

HeaderSection::HeaderSection(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // VirtualHeaderView filters the section's events to handle resize and drag gestures.
    setFocusPolicy(Qt::NoFocus);
}

void HeaderSection::setLogicalIndex(int logicalIndex)
{
    if (m_logicalIndex == logicalIndex)
        return;
    m_logicalIndex = logicalIndex;
    update();
}

const VirtualHeaderView *HeaderSection::headerView() const
{
    return qobject_cast<const VirtualHeaderView *>(parentWidget());
}

const VirtualTableView *HeaderSection::tableView() const
{
    const auto *header = headerView();
    return qobject_cast<const VirtualTableView *>(header ? header->parentWidget() : parentWidget());
}

Qt::Orientation HeaderSection::orientation() const
{
    const auto *header = headerView();
    return header ? header->orientation() : Qt::Horizontal;
}

VirtualItemView::VisualState HeaderSection::visualState() const
{
    const auto *header = headerView();
    const auto *table = tableView();
    return header && table && m_logicalIndex >= 0
        ? table->headerVisualState(header->orientation(), m_logicalIndex)
        : VirtualItemView::VisualState();
}

void HeaderSection::paintEvent(QPaintEvent *)
{
    detail::PerformanceScope performance(this, "paint.header-section", detail::PerformanceDomain::Paint);
    QPainter painter(this);
    painter.setClipRect(rect());
    painter.save();
    detail::performanceCall(this, "paint.header-background", [&] { return paintBackground(&painter); });
    painter.restore();
    painter.save();
    detail::performanceCall(this, "paint.header-content", [&] { return paintContent(&painter); });
    painter.restore();
    painter.save();
    detail::performanceCall(this, "paint.header-foreground", [&] { return paintForeground(&painter); });
    painter.restore();
}

void HeaderSection::paintBackground(QPainter *painter)
{
    Q_UNUSED(painter);
}

void HeaderSection::paintContent(QPainter *painter)
{
    Q_UNUSED(painter);
}

void HeaderSection::paintForeground(QPainter *painter)
{
    Q_UNUSED(painter);
}

void HeaderSection::changeEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::FontChange:
    case QEvent::EnabledChange:
        update();
        break;
    default:
        break;
    }
    QWidget::changeEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void HeaderSection::enterEvent(QEnterEvent *event)
#else
void HeaderSection::enterEvent(QEvent *event)
#endif
{
    update();
    QWidget::enterEvent(event);
}

void HeaderSection::leaveEvent(QEvent *event)
{
    update();
    QWidget::leaveEvent(event);
}

QWidget *HeaderSectionAdapter::createSection(WidgetType type, QWidget *parent)
{
    Q_UNUSED(type);
    return new HeaderSection(parent);
}

void HeaderSectionAdapter::bindSection(QWidget *widget, int logicalIndex)
{
    static_cast<HeaderSection *>(widget)->setLogicalIndex(logicalIndex);
}

void HeaderSectionAdapter::unbindSection(QWidget *widget, int logicalIndex)
{
    Q_UNUSED(logicalIndex);
    static_cast<HeaderSection *>(widget)->setLogicalIndex(-1);
}

} // namespace viv
