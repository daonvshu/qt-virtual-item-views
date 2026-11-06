#include "wrapitemlayout_p.h"

namespace viv {

void WrapItemLayout::invalidate(int first)
{
    m_dirty = m_dirty < 0 ? first : qMin(m_dirty, first);
    m_hintWidth = -1;
}

const detail::WrapGeometry &WrapItemLayout::geometry() const
{
    if (m_dirty >= 0) {
        m_geometry.rebuild(m_widths, m_dirty);
        m_dirty = -1;
    }
    return m_geometry;
}

void WrapItemLayout::setSettings(const detail::WrapSettings &settings)
{
    m_geometry.settings = settings;
    invalidate();
}

void WrapItemLayout::setCrossExtent(int extent)
{
    extent = qMax(0, extent);
    if (extent == m_geometry.width) return;
    m_geometry.width = extent;
    invalidate();
}

void WrapItemLayout::resetItems(qsizetype count, int)
{
    m_widths.fill(100, int(qBound<qsizetype>(qsizetype(0), count, qsizetype(std::numeric_limits<int>::max()))));
    invalidate();
}

void WrapItemLayout::insertItems(qsizetype index, qsizetype count, int)
{
    if (index < 0 || index > m_widths.size() || count <= 0) return;
    m_widths.insert(int(index), int(count), 100);
    invalidate(int(index));
}

void WrapItemLayout::removeItems(qsizetype index, qsizetype count)
{
    if (index < 0 || index >= m_widths.size() || count <= 0) return;
    m_widths.remove(int(index), int(qMin(count, m_widths.size() - index)));
    invalidate(int(index));
}

void WrapItemLayout::moveItems(qsizetype from, qsizetype count, qsizetype to)
{
    if (from < 0 || count <= 0 || from + count > m_widths.size()
        || to < 0 || to > m_widths.size() || (to >= from && to <= from + count)) return;
    auto begin = m_widths.begin();
    if (to < from)
        std::rotate(begin + to, begin + from, begin + from + count);
    else
        std::rotate(begin + from, begin + from + count, begin + to);
    invalidate(int(qMin(from, to)));
}

void WrapItemLayout::setWidths(int first, const QVector<int> &widths)
{
    for (int i = 0; i < widths.size() && first + i < m_widths.size(); ++i) {
        if (first + i < 0 || m_widths[first + i] == widths[i]) continue;
        m_widths[first + i] = qMax(1, widths[i]);
        invalidate(first + i);
    }
}

int WrapItemLayout::itemSize(qsizetype item) const
{
    return item >= 0 && item < itemCount() ? settings().height : 0;
}
qint64 WrapItemLayout::offsetOf(qsizetype item) const
{
    const auto &g = geometry();
    return item >= 0 && item < itemCount() ? g.offset(g.cells[int(item)].row) : g.extent();
}
qsizetype WrapItemLayout::indexAtOffset(qint64 offset) const
{
    const auto &g = geometry();
    const int row = g.rowAt(offset);
    return row < g.rows.size() ? g.rows[row].first : itemCount();
}
qsizetype WrapItemLayout::lastIndexAtOffset(qint64 offset) const
{
    const auto &g = geometry();
    const int row = g.rowAt(offset);
    return row < g.rows.size() ? g.rows[row].first + g.rows[row].count - 1 : itemCount();
}
QRect WrapItemLayout::itemRect(qsizetype item, qint64 offset) const
{
    return item >= 0 && item < itemCount() ? geometry().rect(int(item), offset) : QRect();
}
qsizetype WrapItemLayout::itemAtPoint(const QPoint &point, qint64 offset) const
{
    return geometry().hit(point, offset);
}
qint64 WrapItemLayout::extentForWidth(int width) const
{
    if (width == crossExtent()) return contentExtent();
    if (width != m_hintWidth) {
        detail::WrapGeometry candidate;
        candidate.width = width;
        candidate.settings = settings();
        candidate.rebuild(m_widths);
        m_hintWidth = width;
        m_hintExtent = candidate.extent();
    }
    return m_hintExtent;
}

} // namespace viv
