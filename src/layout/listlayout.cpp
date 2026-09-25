#include <virtualitemviews/listlayout.h>

#include <algorithm>

namespace viv {

namespace {
/// Widget geometry is 32-bit; items far outside the viewport are clamped into a
/// safe range so that pinned widgets stay off-screen without overflowing.
constexpr qint64 kGeometryClamp = qint64(1) << 24;
}

void LayoutPolicy::setSizeIndex(SizeIndex *index, bool takeOwnership)
{
    // A policy without an index model cannot use what it was handed, but it can
    // still honour the ownership part of the contract.
    if (takeOwnership)
        delete index;
}

ListLayout::ListLayout(Qt::Orientation orientation)
    : m_sizeIndex(new FixedSizeIndex())
    , m_orientation(orientation)
{
}

ListLayout::ListLayout(SizeIndex *sizeIndex, Qt::Orientation orientation)
    : m_sizeIndex(sizeIndex ? sizeIndex : new FixedSizeIndex())
    , m_orientation(orientation)
{
}

ListLayout::~ListLayout()
{
    if (m_ownsSizeIndex)
        delete m_sizeIndex;
}

void ListLayout::setOrientation(Qt::Orientation orientation)
{
    m_orientation = orientation;
}

void ListLayout::setSizeIndex(SizeIndex *sizeIndex, bool takeOwnership)
{
    if (sizeIndex == m_sizeIndex) {
        m_ownsSizeIndex = m_ownsSizeIndex || takeOwnership;
        return;
    }
    if (m_ownsSizeIndex)
        delete m_sizeIndex;
    m_sizeIndex = sizeIndex ? sizeIndex : new FixedSizeIndex();
    m_ownsSizeIndex = sizeIndex ? takeOwnership : true;
}

void ListLayout::setEstimate(int estimate)
{
    m_estimate = qMax(0, estimate);
}

void ListLayout::setViewportMargins(const QMargins &margins)
{
    m_viewportMargins = margins;
}

qsizetype ListLayout::itemCount() const
{
    return m_sizeIndex ? m_sizeIndex->count() : 0;
}

int ListLayout::itemSize(qsizetype item) const
{
    return m_sizeIndex ? m_sizeIndex->sizeOf(item) : 0;
}

qint64 ListLayout::offsetOf(qsizetype item) const
{
    return m_sizeIndex ? m_sizeIndex->offsetOf(item) : 0;
}

qsizetype ListLayout::indexAtOffset(qint64 offset) const
{
    return m_sizeIndex ? m_sizeIndex->indexAt(offset) : 0;
}

qint64 ListLayout::contentExtent() const
{
    const qint64 total = m_sizeIndex ? m_sizeIndex->totalSize() : 0;
    const qint64 margins = m_orientation == Qt::Vertical
        ? m_viewportMargins.top() + m_viewportMargins.bottom()
        : m_viewportMargins.left() + m_viewportMargins.right();
    return total + margins;
}

void ListLayout::setCrossExtent(int extent)
{
    m_crossExtent = qMax(0, extent);
}

qint64 ListLayout::clampToIntRange(qint64 value) const
{
    return qBound<qint64>(-kGeometryClamp, value, kGeometryClamp);
}

QRect ListLayout::itemRect(qsizetype item, qint64 scrollOffset) const
{
    const int size = itemSize(item);
    if (size <= 0 || item < 0 || item >= itemCount())
        return QRect();

    const qint64 start = offsetOf(item) - scrollOffset;
    const qint64 crossExtent = qMax(0, m_crossExtent);

    if (m_orientation == Qt::Horizontal)
        return QRect(int(clampToIntRange(start + m_viewportMargins.left())),
                     m_viewportMargins.top(), size,
                     int(qMax<qint64>(0, crossExtent - m_viewportMargins.top() - m_viewportMargins.bottom())));

    const int top = int(clampToIntRange(start + m_viewportMargins.top()));
    const int left = m_viewportMargins.left();
    const int width = int(qMax<qint64>(0, crossExtent - m_viewportMargins.left() - m_viewportMargins.right()));
    return QRect(left, top, width, size);
}

qsizetype ListLayout::itemAtPoint(const QPoint &viewportPos, qint64 scrollOffset) const
{
    if (!m_sizeIndex || itemCount() == 0)
        return -1;

    if (m_orientation == Qt::Horizontal) {
        if (viewportPos.y() < 0 || viewportPos.y() >= m_crossExtent)
            return -1;
        const qint64 offset = scrollOffset + viewportPos.x() - m_viewportMargins.left();
        if (offset < 0 || offset >= m_sizeIndex->totalSize())
            return -1;
        return m_sizeIndex->indexAt(offset);
    }

    if (viewportPos.x() < m_viewportMargins.left() || viewportPos.x() >= m_crossExtent - m_viewportMargins.right())
        return -1;
    const qint64 offset = scrollOffset + viewportPos.y() - m_viewportMargins.top();
    if (offset < 0 || offset >= m_sizeIndex->totalSize())
        return -1;
    return m_sizeIndex->indexAt(offset);
}

bool ListLayout::isVariableSized() const
{
    return m_sizeIndex && !m_sizeIndex->isUniform();
}

void ListLayout::resetItems(qsizetype count, int estimate)
{
    if (estimate > 0)
        m_estimate = estimate;
    if (m_sizeIndex)
        m_sizeIndex->reset(count, m_estimate);
}

void ListLayout::insertItems(qsizetype index, qsizetype count, int estimate)
{
    if (!m_sizeIndex || count <= 0)
        return;
    const int effective = estimate > 0 ? estimate : m_estimate;
    m_sizeIndex->insert(index, count, effective);
}

void ListLayout::removeItems(qsizetype index, qsizetype count)
{
    if (!m_sizeIndex || count <= 0)
        return;
    m_sizeIndex->remove(index, count);
}

void ListLayout::moveItems(qsizetype from, qsizetype count, qsizetype to)
{
    if (!m_sizeIndex || count <= 0)
        return;
    const qsizetype total = itemCount();
    const qsizetype start = qBound<qsizetype>(qsizetype(0), from, total);
    const qsizetype moved = qMin(count, total - start);
    if (moved <= 0)
        return;

    QList<int> sizes;
    sizes.reserve(moved);
    for (qsizetype i = 0; i < moved; ++i)
        sizes.append(itemSize(start + i));

    removeItems(start, moved);

    // \a to is expressed in pre-move coordinates, like
    // QAbstractItemModel::beginMoveRows().
    qsizetype destination = to > start ? to - moved : to;
    destination = qBound<qsizetype>(qsizetype(0), destination, itemCount());

    const int firstSize = sizes.isEmpty() ? m_estimate : sizes.first();
    insertItems(destination, moved, firstSize);
    for (qsizetype i = 0; i < sizes.size(); ++i)
        setItemSize(destination + i, sizes.at(i));
}

void ListLayout::setItemSize(qsizetype item, int size)
{
    if (!m_sizeIndex)
        return;
    m_sizeIndex->setSize(item, size);
}

} // namespace viv
