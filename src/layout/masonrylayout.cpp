#include "masonrylayout_p.h"

#include <QtMath>
#include <algorithm>
#include <limits>
#include <utility>

namespace viv {
namespace {
constexpr qint64 kGeometryClamp = qint64(1) << 24;
constexpr int kMaximumColumns = 4096;

qint64 safeAdd(qint64 lhs, qint64 rhs)
{
    const qint64 limit = std::numeric_limits<qint64>::max() / 2;
    if (rhs > 0 && lhs > limit - rhs)
        return limit;
    return lhs + rhs;
}
}

void MasonryLayout::updateColumns()
{
    const qint64 available = qMax<qint64>(0, qint64(m_width)
        - m_margins.left() - m_margins.right());
    if (available <= 0) {
        m_columns = 0;
        return;
    }
    const qint64 fitting = qMax<qint64>(1,
        (available + m_columnSpacing)
            / (qint64(m_minimumWidth) + m_columnSpacing));
    if (m_requestedColumns > 0) {
        m_columns = int(qMin<qint64>(qMin<qint64>(m_requestedColumns, fitting),
                                     kMaximumColumns));
        return;
    }
    const qint64 denominator = qint64(m_maximumWidth) + m_columnSpacing;
    const qint64 desired = qMax<qint64>(1,
        (available + m_columnSpacing + denominator - 1) / denominator);
    m_columns = int(qMin<qint64>(qMin(desired, fitting), kMaximumColumns));
}

void MasonryLayout::setCrossExtent(int extent)
{
    const int width = qMax(0, extent);
    if (m_width == width)
        return;
    m_width = width;
    rebuild();
}

void MasonryLayout::setWidthRange(int minimum, int maximum)
{
    m_minimumWidth = qMax(1, minimum);
    m_maximumWidth = qMax(m_minimumWidth, maximum);
    rebuild();
}

void MasonryLayout::setColumnCount(int count)
{
    m_requestedColumns = qMax(0, count);
    rebuild();
}

void MasonryLayout::setSpacing(int rows, int columns)
{
    m_rowSpacing = qMax(0, rows);
    m_columnSpacing = qMax(0, columns);
    rebuild();
}

void MasonryLayout::setMargins(const QMargins &margins)
{
    m_margins = QMargins(qMax(0, margins.left()), qMax(0, margins.top()),
                         qMax(0, margins.right()), qMax(0, margins.bottom()));
    rebuild();
}

void MasonryLayout::setHeightMode(HeightMode mode)
{
    if (m_heightMode == mode)
        return;
    m_heightMode = mode;
    rebuild();
}

void MasonryLayout::setFixedHeight(int height)
{
    m_fixedHeight = qMax(1, height);
    rebuild();
}

void MasonryLayout::setAspectRatio(qreal ratio)
{
    if (!qIsFinite(ratio) || ratio <= 0)
        return;
    m_aspectRatio = ratio;
    rebuild();
}

void MasonryLayout::setEstimatedSize(qsizetype item, const QSize &size)
{
    if (item < 0 || item >= m_count)
        return;
    m_estimatedSizes[item] = size;
    rebuildFrom(item);
}

void MasonryLayout::setEstimatedSizes(const QVector<QSize> &sizes)
{
    m_estimatedSizes = sizes;
    m_estimatedSizes.resize(int(m_count));
    for (const QSize &size : std::as_const(m_estimatedSizes)) {
        if (size.height() > 0) {
            m_estimateHeight = size.height();
            break;
        }
    }
    rebuild();
}

QSize MasonryLayout::estimatedSize(qsizetype item) const
{
    return item >= 0 && item < m_estimatedSizes.size()
        ? m_estimatedSizes.at(int(item)) : QSize();
}

int MasonryLayout::columnWidth(int column) const
{
    if (column < 0 || column >= m_columns || m_columns <= 0)
        return 0;
    const qint64 available = qMax<qint64>(0, qint64(m_width)
        - qint64(m_margins.left()) - qint64(m_margins.right())
        - qint64(m_columns - 1) * qint64(m_columnSpacing));
    const qint64 width = available / m_columns
        + (column < available % m_columns ? 1 : 0);
    return int(qBound<qint64>(qint64(0), width,
                              qint64(std::numeric_limits<int>::max())));
}

int MasonryLayout::columnX(int column) const
{
    if (column < 0 || column >= m_columns || m_columns <= 0)
        return -1;
    const qint64 available = qMax<qint64>(0, qint64(m_width)
        - qint64(m_margins.left()) - qint64(m_margins.right())
        - qint64(m_columns - 1) * qint64(m_columnSpacing));
    const qint64 position = qint64(m_margins.left())
        + qint64(column) * (available / m_columns + qint64(m_columnSpacing))
        + qMin<qint64>(column, available % m_columns);
    return int(qBound<qint64>(qint64(std::numeric_limits<int>::min()), position,
                              qint64(std::numeric_limits<int>::max())));
}

int MasonryLayout::heightFor(qsizetype item, int width) const
{
    if (item >= 0 && item < m_explicitHeights.size() && m_explicitHeights.at(int(item)) > 0)
        return m_explicitHeights.at(int(item));
    if (m_heightMode == HeightMode::Fixed)
        return m_fixedHeight;
    if (m_heightMode == HeightMode::AspectRatio)
        return qMax(1, qRound(qreal(width) / m_aspectRatio));
    const QSize estimate = estimatedSize(item);
    if (estimate.width() > 0 && estimate.height() > 0)
        return qMax(1, qRound(qreal(width) * estimate.height() / estimate.width()));
    return qMax(1, m_estimateHeight);
}

int MasonryLayout::clampColumn(int column) const
{
    return qBound(0, column, qMax(0, m_columns - 1));
}

void MasonryLayout::rebuild()
{
    updateColumns();
    m_geometry.resize(int(m_count));
    m_contentExtent = 0;
    if (m_count <= 0 || m_columns <= 0) {
        m_columnItems.clear();
        return;
    }

    m_columnItems.clear();
    m_columnItems.resize(m_columns);

    rebuildFrom(0);
}

void MasonryLayout::rebuildFrom(qsizetype firstItem)
{
    if (m_count <= 0 || m_columns <= 0)
        return;

    const qsizetype first = qBound<qsizetype>(0, firstItem, m_count);
    QVector<qint64> bottoms(m_columns, m_margins.top());
    QVector<bool> occupied(m_columns, false);
    m_contentExtent = 0;

    for (int column = 0; column < m_columns; ++column) {
        auto &items = m_columnItems[column];
        while (!items.isEmpty() && items.constLast() >= first)
            items.removeLast();
        if (!items.isEmpty()) {
            const QRect &last = m_geometry.at(int(items.constLast())).rect;
            bottoms[column] = safeAdd(last.top(), last.height());
            occupied[column] = true;
            m_contentExtent = qMax(m_contentExtent, bottoms.at(column));
        }
    }

    for (qsizetype item = first; item < m_count; ++item) {
        int column = 0;
        for (int candidate = 1; candidate < m_columns; ++candidate) {
            if (bottoms.at(candidate) < bottoms.at(column))
                column = candidate;
        }
        const int width = columnWidth(column);
        const qint64 top = bottoms.at(column) + (occupied.at(column) ? m_rowSpacing : 0);
        const int height = heightFor(item, width);
        const qint64 bottom = safeAdd(top, height);
        m_geometry[int(item)] = {QRect(columnX(column), int(qBound(-kGeometryClamp, top,
                                                                  kGeometryClamp)), width, height), column};
        m_columnItems[column].append(item);
        bottoms[column] = bottom;
        occupied[column] = true;
        m_contentExtent = qMax(m_contentExtent, bottom);
    }
    m_contentExtent = safeAdd(m_contentExtent, m_margins.bottom());
}

int MasonryLayout::itemSize(qsizetype item) const
{
    return item >= 0 && item < m_geometry.size() ? m_geometry.at(int(item)).rect.height() : 0;
}

qint64 MasonryLayout::offsetOf(qsizetype item) const
{
    return item >= 0 && item < m_geometry.size() ? m_geometry.at(int(item)).rect.top() : m_contentExtent;
}

qint64 MasonryLayout::contentExtent() const
{
    return m_contentExtent;
}

QRect MasonryLayout::logicalItemRect(qsizetype item) const
{
    return item >= 0 && item < m_geometry.size() ? m_geometry.at(int(item)).rect : QRect();
}

int MasonryLayout::itemColumn(qsizetype item) const
{
    return item >= 0 && item < m_geometry.size() ? m_geometry.at(int(item)).column : -1;
}

qsizetype MasonryLayout::nearestVertical(qsizetype item, bool down) const
{
    if (item < 0 || item >= m_geometry.size())
        return -1;
    const QRect source = logicalItemRect(item);
    const int sourceCenter = source.center().y();
    qsizetype best = -1;
    qint64 bestVertical = std::numeric_limits<qint64>::max();
    qint64 bestHorizontal = std::numeric_limits<qint64>::max();
    for (const auto &column : m_columnItems) {
        int low = 0;
        int high = column.size();
        while (low < high) {
            const int middle = low + (high - low) / 2;
            const int center = logicalItemRect(column.at(middle)).center().y();
            if ((down && center <= sourceCenter) || (!down && center < sourceCenter))
                low = middle + 1;
            else
                high = middle;
        }
        if (!down)
            --low;
        if (low < 0 || low >= column.size())
            continue;
        const qsizetype candidate = column.at(low);
        if (candidate == item)
            continue;
        const QRect target = logicalItemRect(candidate);
        const qint64 vertical = qAbs(qint64(target.center().y()) - sourceCenter);
        const qint64 horizontal = qAbs(qint64(target.center().x()) - source.center().x());
        if (vertical < bestVertical
            || (vertical == bestVertical && horizontal < bestHorizontal)
            || (vertical == bestVertical && horizontal == bestHorizontal && candidate < best)) {
            best = candidate;
            bestVertical = vertical;
            bestHorizontal = horizontal;
        }
    }
    return best;
}

qsizetype MasonryLayout::nearestHorizontal(qsizetype item, bool right) const
{
    if (item < 0 || item >= m_geometry.size())
        return -1;
    const Geometry &source = m_geometry.at(int(item));
    const int direction = right ? 1 : -1;
    int column = source.column + direction;
    while (column >= 0 && column < m_columns) {
        const auto &items = m_columnItems.at(column);
        if (!items.isEmpty()) {
            const int sourceCenter = source.rect.center().y();
            int low = 0;
            int high = items.size();
            while (low < high) {
                const int middle = low + (high - low) / 2;
                if (logicalItemRect(items.at(middle)).center().y() < sourceCenter)
                    low = middle + 1;
                else
                    high = middle;
            }
            qsizetype best = -1;
            qint64 bestDistance = std::numeric_limits<qint64>::max();
            for (int candidate = qMax(0, low - 1); candidate < qMin(items.size(), low + 2); ++candidate) {
                const qsizetype target = items.at(candidate);
                const qint64 distance = qAbs(qint64(logicalItemRect(target).center().y()) - sourceCenter);
                if (distance < bestDistance || (distance == bestDistance && target < best)) {
                    best = target;
                    bestDistance = distance;
                }
            }
            if (best >= 0)
                return best;
        }
        column += direction;
    }
    return -1;
}

qsizetype MasonryLayout::nearestByVerticalOffset(qsizetype item, qint64 delta) const
{
    if (item < 0 || item >= m_geometry.size())
        return -1;
    const qint64 target = qint64(logicalItemRect(item).center().y()) + delta;
    qsizetype best = -1;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (const auto &column : m_columnItems) {
        const auto position = std::lower_bound(column.cbegin(), column.cend(), target,
            [this](qsizetype candidate, qint64 value) {
                return qint64(logicalItemRect(candidate).center().y()) < value;
            });
        const int insertion = int(position - column.cbegin());
        for (int offset = -1; offset <= 1; ++offset) {
            const int index = insertion + offset;
            if (index < 0 || index >= column.size())
                continue;
            const qsizetype candidate = column.at(index);
            if (candidate == item)
                continue;
            const qint64 distance = qAbs(qint64(logicalItemRect(candidate).center().y()) - target);
            if (distance < bestDistance
                || (distance == bestDistance && candidate < best)) {
                best = candidate;
                bestDistance = distance;
            }
        }
    }
    return best;
}

qsizetype MasonryLayout::nextItemInColumn(qsizetype item) const
{
    const int column = itemColumn(item);
    if (column < 0 || column >= m_columnItems.size())
        return -1;
    const auto &items = m_columnItems.at(column);
    const auto position = std::find(items.cbegin(), items.cend(), item);
    if (position == items.cend() || position + 1 == items.cend())
        return -1;
    return *(position + 1);
}

QRect MasonryLayout::itemRect(qsizetype item, qint64 scrollOffset) const
{
    QRect rect = logicalItemRect(item);
    if (rect.isEmpty())
        return rect;
    rect.moveTop(int(qBound(-kGeometryClamp, qint64(rect.top()) - scrollOffset, kGeometryClamp)));
    return rect;
}

QVector<qsizetype> MasonryLayout::columnItemsAt(int column, qint64 top, qint64 bottom) const
{
    QVector<qsizetype> result;
    if (column < 0 || column >= m_columnItems.size() || bottom < top)
        return result;
    const auto &items = m_columnItems.at(column);
    auto first = std::lower_bound(items.cbegin(), items.cend(), top,
        [this](qsizetype item, qint64 value) { return logicalItemRect(item).bottom() < value; });
    for (auto it = first; it != items.cend(); ++it) {
        const QRect rect = logicalItemRect(*it);
        if (rect.top() > bottom)
            break;
        if (rect.bottom() >= top)
            result.append(*it);
    }
    return result;
}

QVector<qsizetype> MasonryLayout::itemsInVerticalRange(qint64 top, qint64 bottom) const
{
    QVector<qsizetype> result;
    for (int column = 0; column < m_columns; ++column)
        result += columnItemsAt(column, top, bottom);
    std::sort(result.begin(), result.end());
    return result;
}

QVector<qsizetype> MasonryLayout::itemsAtOrAfterOffset(qint64 offset) const
{
    QVector<qsizetype> result;
    for (const auto &items : m_columnItems) {
        int low = 0;
        int high = items.size();
        while (low < high) {
            const int middle = low + (high - low) / 2;
            if (logicalItemRect(items.at(middle)).bottom() < offset)
                low = middle + 1;
            else
                high = middle;
        }
        if (low < items.size())
            result.append(items.at(low));
    }
    std::sort(result.begin(), result.end());
    return result;
}

qsizetype MasonryLayout::indexAtOffset(qint64 offset) const
{
    const QVector<qsizetype> items = itemsInVerticalRange(offset, offset);
    if (items.isEmpty())
        return m_count;
    return items.first();
}

qsizetype MasonryLayout::lastIndexAtOffset(qint64 offset) const
{
    const QVector<qsizetype> items = itemsInVerticalRange(offset, offset);
    if (items.isEmpty())
        return m_count;
    return items.last();
}

qsizetype MasonryLayout::itemAtPoint(const QPoint &point, qint64 scrollOffset) const
{
    const qint64 logicalY = qint64(point.y()) + scrollOffset;
    const qint64 pointX = point.x();
    const qint64 right = qint64(m_width) - qint64(m_margins.right());
    if (pointX < m_margins.left() || pointX >= right
        || logicalY < m_margins.top() || logicalY >= m_contentExtent - m_margins.bottom())
        return -1;
    int column = -1;
    for (int candidate = 0; candidate < m_columns; ++candidate) {
        const qint64 left = columnX(candidate);
        const qint64 width = columnWidth(candidate);
        if (pointX >= left && pointX < left + width) {
            column = candidate;
            break;
        }
    }
    if (column < 0)
        return -1;
    const QVector<qsizetype> items = columnItemsAt(column, logicalY, logicalY);
    for (qsizetype item : items) {
        const QRect rect = logicalItemRect(item);
        if (pointX >= rect.left() && pointX <= rect.right()
            && logicalY >= rect.top() && logicalY <= rect.bottom())
            return item;
    }
    return -1;
}

void MasonryLayout::resetItems(qsizetype count, int estimate)
{
    m_count = qMax<qsizetype>(0, count);
    if (estimate > 0)
        m_estimateHeight = estimate;
    m_estimatedSizes.fill(QSize(), int(m_count));
    m_explicitHeights.fill(0, int(m_count));
    rebuild();
}

void MasonryLayout::insertItems(qsizetype index, qsizetype count, int estimate)
{
    if (count <= 0 || index < 0 || index > m_count)
        return;
    const int inserted = int(qMin<qsizetype>(count, std::numeric_limits<int>::max()));
    m_estimatedSizes.insert(int(index), inserted, QSize());
    m_explicitHeights.insert(int(index), inserted, 0);
    m_count += inserted;
    if (estimate > 0)
        m_estimateHeight = estimate;
    rebuild();
}

void MasonryLayout::removeItems(qsizetype index, qsizetype count)
{
    if (count <= 0 || index < 0 || index >= m_count)
        return;
    const qsizetype removed = qMin(count, m_count - index);
    m_estimatedSizes.remove(int(index), int(removed));
    m_explicitHeights.remove(int(index), int(removed));
    m_count -= removed;
    rebuild();
}

void MasonryLayout::moveItems(qsizetype from, qsizetype count, qsizetype to)
{
    if (count <= 0 || from < 0 || from >= m_count)
        return;
    const qsizetype moved = qMin(count, m_count - from);
    qsizetype destination = qBound<qsizetype>(0, to > from ? to - moved : to, m_count - moved);
    if (destination == from)
        return;
    QVector<QSize> estimates;
    QVector<int> heights;
    for (qsizetype i = 0; i < moved; ++i) {
        estimates.append(m_estimatedSizes.at(int(from + i)));
        heights.append(m_explicitHeights.at(int(from + i)));
    }
    m_estimatedSizes.remove(int(from), int(moved));
    m_explicitHeights.remove(int(from), int(moved));
    for (qsizetype i = 0; i < moved; ++i) {
        m_estimatedSizes.insert(int(destination + i), estimates.at(int(i)));
        m_explicitHeights.insert(int(destination + i), heights.at(int(i)));
    }
    rebuild();
}

void MasonryLayout::setItemSize(qsizetype item, int size)
{
    if (item < 0 || item >= m_count)
        return;
    m_explicitHeights[int(item)] = qMax(1, size);
    rebuildFrom(item);
}

} // namespace viv
