#include "borderdemo.h"

#include <virtualitemviews/virtualtableview.h>
#include <QPainter>
#include <QResizeEvent>

namespace table_borders {

QColor stageColor()
{
    return QColor(QStringLiteral("#e8edf4"));
}

QPainterPath roundedOutline(const QRectF &rect, qreal radius)
{
    QPainterPath path;
    if (rect.isEmpty())
        return path;
    radius = qBound(qreal(0), radius, qMin(rect.width(), rect.height()) / 2);
    path.addRoundedRect(rect, radius, radius);
    return path;
}

class BorderOverlay : public QWidget
{
public:
    explicit BorderOverlay(BorderDemo *demo) : QWidget(demo), m_demo(demo)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPainterPath outer = m_demo->outerOutline();
        if (m_demo->coversCorners()) {
            QPainterPath rectangle;
            rectangle.addRect(QRectF(rect()));
            // Deliberately uses a solid color: patterned parent backgrounds expose this limitation.
            painter.fillPath(rectangle.subtracted(outer), stageColor());
        }
        if (m_demo->options().width > 0)
            painter.fillPath(outer.subtracted(m_demo->contentOutline(this)), m_demo->options().color);
    }

private:
    BorderDemo *m_demo;
};

BorderDemo::BorderDemo(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(260, 220);
    m_border = new BorderOverlay(this);
}

void BorderDemo::attachView(viv::VirtualTableView *view)
{
    m_view = view;
    m_view->setParent(this);
    // The example owns the outer frame; the table retains its normal input and virtualization.
    m_view->setFrameShape(QFrame::NoFrame);
    m_border->raise();
}

void BorderDemo::setOptions(const BorderOptions &options)
{
    m_options = options;
    m_view->setHorizontalHeaderVisible(options.headers);
    m_view->setVerticalHeaderVisible(options.headers);
    m_view->setVisualStateCornerRadius(options.stateRadius);
    m_view->setRowSpacing(options.gaps ? 10 : 0);
    m_view->setColumnSpacing(options.gaps ? 10 : 0);
    m_view->setFrozenColumns(options.frozen ? QVector<int>{0} : QVector<int>{});
    const int last = m_view->columnCount() - 1;
    m_view->setFrozenRightColumns(options.frozen && last > 0 ? QVector<int>{last} : QVector<int>{});
    m_view->setFrozenRows(options.frozen ? 1 : 0);
    m_view->setFrozenBottomRows(options.frozen ? 1 : 0);
    const auto policy = options.scrollBars ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded;
    m_view->setHorizontalScrollBarPolicy(policy);
    m_view->setVerticalScrollBarPolicy(policy);
    layoutContent();
    update();
}

QPainterPath BorderDemo::outerOutline() const
{
    return roundedOutline(QRectF(rect()), m_options.radius);
}

qreal BorderDemo::contentRadius() const
{
    return qMax(0, m_options.radius - m_options.width);
}

QPainterPath BorderDemo::contentOutline(const QWidget *target) const
{
    const int width = m_options.width;
    QPainterPath path = roundedOutline(QRectF(rect()).adjusted(width, width, -width, -width),
                                       contentRadius());
    // Every target belongs to this demo; map through an ancestor, never between siblings.
    const QPoint origin = target->mapTo(this, QPoint());
    return path.translated(-origin.x(), -origin.y());
}

void BorderDemo::layoutContent()
{
    if (!m_view)
        return;
    const int inset = m_options.width;
    m_view->setGeometry(inset, inset, qMax(0, width() - 2 * inset), qMax(0, height() - 2 * inset));
    m_border->setGeometry(rect());
    updateTechnique();
    m_border->raise();
    m_border->update();
}

void BorderDemo::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillPath(outerOutline(), Qt::white);
}

void BorderDemo::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutContent();
}

} // namespace table_borders
