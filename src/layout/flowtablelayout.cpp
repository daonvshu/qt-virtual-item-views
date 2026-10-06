#include "flowtablelayout_p.h"

#include <QtMath>
#include <limits>

namespace viv {
namespace {
// Bound scroll extents and pinned widget coordinates without integer overflow.
qint64 saturatedProduct(qint64 count, qint64 size)
{
    const qint64 limit = std::numeric_limits<qint64>::max() / 2;
    return size > 0 && count > limit / size ? limit : count * size;
}
}

void FlowTableLayout::updateColumns()
{
    const qint64 available = qMax<qint64>(0, qint64(m_width) - m_margins.left() - m_margins.right());
    const qint64 denominator = qint64(m_maximum) + m_columnSpacing;
    const qint64 desired = qMax<qint64>(1, (available + m_columnSpacing + denominator - 1) / denominator);
    const qint64 fitting = qMax<qint64>(1, (available + m_columnSpacing) / (qint64(m_columnSpacing) + 1));
    const qint64 columns = qMin(desired, fitting);
    m_columns = int(columns);
    m_cellSpace = int(qMax<qint64>(0, available - (columns - 1) * m_columnSpacing));
}

void FlowTableLayout::setCrossExtent(int extent)
{
    m_width = qMax(0, extent);
    updateColumns();
}

void FlowTableLayout::setWidthRange(int minimum, int maximum)
{
    m_minimum = qMax(1, minimum);
    m_maximum = qMax(m_minimum, maximum);
    updateColumns();
}

void FlowTableLayout::setFixedHeight(int height) { m_fixedHeight = qMax(1, height); }
void FlowTableLayout::setProportional(bool enabled) { m_proportional = enabled; }

void FlowTableLayout::setAspectRatio(qreal ratio)
{
    if (qIsFinite(ratio) && ratio > 0)
        m_ratio = ratio;
}

void FlowTableLayout::setSpacing(int rows, int columns)
{
    m_rowSpacing = qMax(0, rows);
    m_columnSpacing = qMax(0, columns);
    updateColumns();
}

void FlowTableLayout::setMargins(const QMargins &margins)
{
    m_margins = QMargins(qMax(0, margins.left()), qMax(0, margins.top()),
                         qMax(0, margins.right()), qMax(0, margins.bottom()));
    updateColumns();
}

int FlowTableLayout::columnWidth(int column) const
{
    return column >= 0 && column < m_columns
        ? m_cellSpace / m_columns + (column < m_cellSpace % m_columns ? 1 : 0) : 0;
}

int FlowTableLayout::columnX(int column) const
{
    return m_margins.left() + int(qint64(column) * (m_cellSpace / m_columns + qint64(m_columnSpacing))
                                + qMin(column, m_cellSpace % m_columns));
}

int FlowTableLayout::heightForWidth(int width) const
{
    if (!m_proportional)
        return m_fixedHeight;
    const qreal height = qMin(qreal(std::numeric_limits<int>::max() / 4), width / m_ratio);
    return qMax(1, qRound(height));
}

int FlowTableLayout::rowHeight() const { return heightForWidth(columnWidth(0)); }
qint64 FlowTableLayout::stride() const { return qint64(rowHeight()) + m_rowSpacing; }
qsizetype FlowTableLayout::rowCount() const { return m_count / m_columns + (m_count % m_columns != 0); }

int FlowTableLayout::itemSize(qsizetype item) const
{
    return item >= 0 && item < m_count ? heightForWidth(columnWidth(int(item % m_columns))) : 0;
}

qint64 FlowTableLayout::offsetOf(qsizetype item) const
{
    if (item >= m_count)
        return contentExtent();
    return m_margins.top() + saturatedProduct(qMax<qsizetype>(0, item) / m_columns, stride());
}

qint64 FlowTableLayout::contentExtent() const
{
    if (m_count <= 0 || m_cellSpace <= 0)
        return 0;
    return saturatedProduct(rowCount(), stride()) - m_rowSpacing
        + qint64(m_margins.top()) + m_margins.bottom();
}

qsizetype FlowTableLayout::indexAtOffset(qint64 offset) const
{
    if (m_count <= 0 || m_cellSpace <= 0 || offset >= contentExtent())
        return m_count;
    const qint64 row = qMax<qint64>(0, offset - m_margins.top()) / stride();
    return qMin<qsizetype>(m_count, saturatedProduct(row, m_columns));
}

qsizetype FlowTableLayout::lastIndexAtOffset(qint64 offset) const
{
    const qsizetype first = indexAtOffset(offset);
    return first >= m_count ? m_count : first + qMin<qsizetype>(m_columns, m_count - first) - 1;
}

QRect FlowTableLayout::itemRect(qsizetype item, qint64 scrollOffset) const
{
    if (item < 0 || item >= m_count || m_cellSpace <= 0)
        return {};
    const int column = int(item % m_columns);
    const qint64 top = qBound<qint64>(-(qint64(1) << 24), offsetOf(item) - scrollOffset, qint64(1) << 24);
    return QRect(columnX(column), int(top), columnWidth(column), itemSize(item));
}

qsizetype FlowTableLayout::itemAtPoint(const QPoint &point, qint64 scrollOffset) const
{
    if (point.x() < m_margins.left() || point.x() >= m_width - m_margins.right())
        return -1;
    const qint64 y = scrollOffset + point.y();
    if (y < m_margins.top() || y >= contentExtent() - m_margins.bottom())
        return -1;
    const qsizetype first = indexAtOffset(y);
    int low = 0;
    int high = m_columns;
    while (low + 1 < high) {
        const int mid = low + (high - low) / 2;
        if (columnX(mid) <= point.x())
            low = mid;
        else
            high = mid;
    }
    if (first >= m_count || low >= m_count - first)
        return -1;
    const qsizetype item = first + low;
    return itemRect(item, scrollOffset).contains(point) ? item : -1;
}

void FlowTableLayout::resetItems(qsizetype count, int estimate)
{
    Q_UNUSED(estimate);
    m_count = qMax<qsizetype>(0, count);
}

void FlowTableLayout::insertItems(qsizetype index, qsizetype count, int estimate)
{
    Q_UNUSED(estimate);
    if (index >= 0 && index <= m_count && count > 0)
        m_count += qMin(count, std::numeric_limits<qsizetype>::max() - m_count);
}

void FlowTableLayout::removeItems(qsizetype index, qsizetype count)
{
    if (index >= 0 && index < m_count && count > 0)
        m_count -= qMin(count, m_count - index);
}

void FlowTableLayout::moveItems(qsizetype from, qsizetype count, qsizetype to)
{
    Q_UNUSED(from);
    Q_UNUSED(count);
    Q_UNUSED(to);
}

void FlowTableLayout::setItemSize(qsizetype item, int size)
{
    // Height is configured globally; model estimates cannot replace the two sizing modes.
    Q_UNUSED(item);
    Q_UNUSED(size);
}

} // namespace viv
