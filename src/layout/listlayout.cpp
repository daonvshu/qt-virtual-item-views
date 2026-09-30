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
    m_spacings.reset(itemCount(), 0);
}

ListLayout::ListLayout(SizeIndex *sizeIndex, Qt::Orientation orientation)
    : m_sizeIndex(sizeIndex ? sizeIndex : new FixedSizeIndex())
    , m_orientation(orientation)
{
    m_spacings.reset(itemCount(), 0);
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
    m_spacings.reset(itemCount(), m_defaultSpacing);
    m_hasSpacingOverrides = false;
}

void ListLayout::setItemSpacing(int pixels)
{
    const int spacing = qMax(0, pixels);
    if (spacing == m_defaultSpacing && !m_hasSpacingOverrides)
        return;
    m_defaultSpacing = spacing;
    m_spacings.reset(itemCount(), spacing);
    m_hasSpacingOverrides = false;
}

void ListLayout::setSpacingAfter(qsizetype item, int pixels)
{
    if (item < 0 || item >= itemCount())
        return;
    const int spacing = qMax(0, pixels);
    if (spacing != m_defaultSpacing && !m_hasSpacingOverrides) {
        m_extents.reset(itemCount(),
                        (m_sizeIndex->isUniform() ? m_sizeIndex->uniformSize() : m_estimate)
                            + m_defaultSpacing);
        if (!m_sizeIndex->isUniform())
            for (qsizetype row = 0; row < itemCount(); ++row)
                m_extents.setSize(row, itemSize(row) + m_defaultSpacing);
        m_hasSpacingOverrides = true;
    }
    m_spacings.setSize(item, spacing);
    if (m_hasSpacingOverrides)
        m_extents.setSize(item, itemSize(item) + spacing);
}

int ListLayout::spacingAfter(qsizetype item) const
{
    return item >= 0 && item + 1 < itemCount() ? m_spacings.sizeOf(item) : 0;
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
    if (!m_sizeIndex)
        return 0;
    if (item >= itemCount())
        return m_hasSpacingOverrides
            ? m_extents.totalSize() - (itemCount() > 0 ? m_spacings.sizeOf(itemCount() - 1) : 0)
            : m_sizeIndex->totalSize()
                + qint64(qMax<qsizetype>(0, itemCount() - 1)) * m_defaultSpacing;
    if (m_hasSpacingOverrides)
        return m_extents.offsetOf(item);
    return m_sizeIndex->offsetOf(item)
        + qint64(qMax<qsizetype>(0, item)) * m_defaultSpacing;
}

qsizetype ListLayout::indexAtOffset(qint64 offset) const
{
    if (!m_sizeIndex || (m_defaultSpacing == 0 && !m_hasSpacingOverrides))
        return m_sizeIndex ? m_sizeIndex->indexAt(offset) : 0;
    if (offset >= offsetOf(itemCount()))
        return itemCount();
    if (m_hasSpacingOverrides)
        return m_extents.indexAt(offset);
    if (offset <= 0)
        return 0;
    if (m_sizeIndex->isUniform()) {
        const qint64 stride = qint64(m_sizeIndex->uniformSize()) + m_defaultSpacing;
        return stride > 0 ? qMin<qint64>(itemCount(), offset / stride) : itemCount();
    }
    qsizetype low = 0;
    qsizetype high = itemCount();
    while (low < high) {
        const qsizetype mid = low + (high - low + 1) / 2;
        if (offsetOf(mid) <= offset)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

qint64 ListLayout::contentExtent() const
{
    const qint64 total = m_hasSpacingOverrides
        ? m_extents.totalSize() - (itemCount() > 0 ? m_spacings.sizeOf(itemCount() - 1) : 0)
        : (m_sizeIndex ? m_sizeIndex->totalSize()
                            + qint64(qMax<qsizetype>(0, itemCount() - 1)) * m_defaultSpacing : 0);
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
        if (viewportPos.y() < m_viewportMargins.top()
            || viewportPos.y() >= m_crossExtent - m_viewportMargins.bottom())
            return -1;
        const qint64 offset = scrollOffset + viewportPos.x() - m_viewportMargins.left();
        if (offset < 0 || offset >= contentExtent())
            return -1;
        const qsizetype row = indexAtOffset(offset);
        return row < itemCount() && offset < offsetOf(row) + itemSize(row) ? row : -1;
    }

    if (viewportPos.x() < m_viewportMargins.left() || viewportPos.x() >= m_crossExtent - m_viewportMargins.right())
        return -1;
    const qint64 offset = scrollOffset + viewportPos.y() - m_viewportMargins.top();
    if (offset < 0 || offset >= contentExtent())
        return -1;
    const qsizetype row = indexAtOffset(offset);
    return row < itemCount() && offset < offsetOf(row) + itemSize(row) ? row : -1;
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
    m_spacings.reset(count, m_defaultSpacing);
    m_hasSpacingOverrides = false;
}

void ListLayout::insertItems(qsizetype index, qsizetype count, int estimate)
{
    if (!m_sizeIndex || count <= 0)
        return;
    const int effective = estimate > 0 ? estimate : m_estimate;
    const int previousUniformSize = m_sizeIndex->isUniform() ? m_sizeIndex->uniformSize() : -1;
    m_sizeIndex->insert(index, count, effective);
    m_spacings.insert(index, count, m_defaultSpacing);
    if (m_hasSpacingOverrides) {
        const int insertedSize = m_sizeIndex->isUniform() ? m_sizeIndex->uniformSize() : effective;
        m_extents.insert(index, count, insertedSize + m_defaultSpacing);
        if (m_sizeIndex->isUniform() && m_sizeIndex->uniformSize() != previousUniformSize) {
            m_extents.reset(itemCount(), m_sizeIndex->uniformSize() + m_defaultSpacing);
            for (qsizetype row = 0; row < itemCount(); ++row)
                m_extents.setSize(row, itemSize(row) + m_spacings.sizeOf(row));
        }
    }
}

void ListLayout::removeItems(qsizetype index, qsizetype count)
{
    if (!m_sizeIndex || count <= 0)
        return;
    m_sizeIndex->remove(index, count);
    m_spacings.remove(index, count);
    if (m_hasSpacingOverrides)
        m_extents.remove(index, count);
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
    QList<int> spacings;
    sizes.reserve(moved);
    for (qsizetype i = 0; i < moved; ++i)
        sizes.append(itemSize(start + i));
    for (qsizetype i = 0; i < moved; ++i)
        spacings.append(m_spacings.sizeOf(start + i));

    removeItems(start, moved);

    // \a to is expressed in pre-move coordinates, like
    // QAbstractItemModel::beginMoveRows().
    qsizetype destination = to > start ? to - moved : to;
    destination = qBound<qsizetype>(qsizetype(0), destination, itemCount());

    const int firstSize = sizes.isEmpty() ? m_estimate : sizes.first();
    insertItems(destination, moved, firstSize);
    for (qsizetype i = 0; i < sizes.size(); ++i)
        setItemSize(destination + i, sizes.at(i));
    for (qsizetype i = 0; i < spacings.size(); ++i)
        if (spacings.at(i) != m_defaultSpacing)
            setSpacingAfter(destination + i, spacings.at(i));
}

void ListLayout::setItemSize(qsizetype item, int size)
{
    if (!m_sizeIndex)
        return;
    m_sizeIndex->setSize(item, size);
    if (m_hasSpacingOverrides && m_sizeIndex->isUniform()) {
        m_extents.reset(itemCount(), m_sizeIndex->uniformSize() + m_defaultSpacing);
        for (qsizetype row = 0; row < itemCount(); ++row)
            m_extents.setSize(row, itemSize(row) + m_spacings.sizeOf(row));
    } else if (m_hasSpacingOverrides && item >= 0 && item < itemCount()) {
        m_extents.setSize(item, itemSize(item) + m_spacings.sizeOf(item));
    }
}

} // namespace viv
