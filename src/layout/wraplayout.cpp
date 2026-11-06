#include <virtualitemviews/wraplayout.h>
#include "wrapgeometry_p.h"
#include <QWidget>

namespace viv {

WrapLayout::WrapLayout(QWidget *parent) : QLayout(parent)
{
    setContentsMargins(0, 0, 0, 0);
}
WrapLayout::~WrapLayout()
{
    // QLayoutItem ownership and QWidget ownership are separate.
    qDeleteAll(m_items);
}
void WrapLayout::addItem(QLayoutItem *item)
{
    if (!item) return;
    m_items.append(item);
    invalidate();
}
void WrapLayout::insertWidget(int index, QWidget *widget)
{
    if (!widget) return;
    if (parentWidget() && (widget == parentWidget() || widget->isAncestorOf(parentWidget()))) {
        qWarning("WrapLayout: cannot insert the host or its ancestor");
        return;
    }
    addChildWidget(widget);
    m_items.insert(qBound(0, index, int(m_items.size())), new QWidgetItem(widget));
    invalidate();
}
int WrapLayout::count() const { return int(m_items.size()); }
QLayoutItem *WrapLayout::itemAt(int index) const
{
    return index >= 0 && index < count() ? m_items[index] : nullptr;
}
QLayoutItem *WrapLayout::takeAt(int index)
{
    if (index < 0 || index >= count()) return nullptr;
    QLayoutItem *item = m_items.takeAt(index);
    invalidate();
    return item;
}
Qt::Orientations WrapLayout::expandingDirections() const { return Qt::Horizontal; }
QSize WrapLayout::minimumSize() const
{
    const QMargins margins = contentsMargins();
    int width = 0;
    bool occupied = false;
    for (QLayoutItem *item : m_items) {
        if (item->isEmpty()) continue;
        width = qMax(width, item->minimumSize().width());
        occupied = true;
    }
    return QSize(width + margins.left() + margins.right(),
                 (occupied ? m_height : 0) + margins.top() + margins.bottom());
}
QSize WrapLayout::sizeHint() const
{
    const int width = parentWidget() && parentWidget()->width() > 0
        ? parentWidget()->width() : qMax(320, minimumSize().width());
    return QSize(width, heightForWidth(width));
}
int WrapLayout::heightForWidth(int width) const { return arrange(QRect(0, 0, width, 0), false); }
void WrapLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}
int WrapLayout::arrange(const QRect &rect, bool apply) const
{
    detail::WrapGeometry geometry;
    geometry.width = qMax(0, rect.width());
    geometry.settings.height = m_height;
    geometry.settings.itemSpacing = m_itemSpacing;
    geometry.settings.rowSpacing = m_rowSpacing;
    geometry.settings.margins = contentsMargins();
    geometry.settings.alignment = m_alignment;
    if (parentWidget()) geometry.settings.direction = parentWidget()->layoutDirection();
    QVector<int> widths;
    QVector<QLayoutItem *> visible;
    for (QLayoutItem *item : m_items) {
        if (item->isEmpty()) continue;
        widths.append(qMax(1, qBound(item->minimumSize().width(),
            item->sizeHint().width(), qMax(item->minimumSize().width(), item->maximumSize().width()))));
        visible.append(item);
    }
    geometry.rebuild(widths);
    if (apply) {
        for (int i = 0; i < visible.size(); ++i)
            visible[i]->setGeometry(geometry.rect(i).translated(rect.topLeft()));
    }
    return int(qMin<qint64>(QWIDGETSIZE_MAX, geometry.extent()));
}
void WrapLayout::invalidate()
{
    QLayout::invalidate();
    emit layoutChanged();
}
void WrapLayout::setFixedItemHeight(int height)
{
    height = qMax(1, height);
    if (height == m_height) return;
    m_height = height;
    invalidate();
}
void WrapLayout::setItemSpacing(int pixels)
{
    pixels = qMax(0, pixels);
    if (pixels == m_itemSpacing) return;
    m_itemSpacing = pixels;
    invalidate();
}
void WrapLayout::setRowSpacing(int pixels)
{
    pixels = qMax(0, pixels);
    if (pixels == m_rowSpacing) return;
    m_rowSpacing = pixels;
    invalidate();
}
void WrapLayout::setLineAlignment(Qt::Alignment alignment)
{
    if (alignment == m_alignment) return;
    m_alignment = alignment;
    invalidate();
}

} // namespace viv
