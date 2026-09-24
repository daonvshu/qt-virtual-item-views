#include <virtualitemviews/sizeindex.h>

#include <algorithm>

namespace viv {

// ---------------------------------------------------------------------------
// FixedSizeIndex
// ---------------------------------------------------------------------------

FixedSizeIndex::FixedSizeIndex(qsizetype count, int size)
    : m_count(qMax<qsizetype>(0, count))
    , m_size(qMax(0, size))
{
}

void FixedSizeIndex::setItemSize(int size)
{
    m_size = qMax(0, size);
}

qint64 FixedSizeIndex::totalSize() const
{
    return qint64(m_count) * qint64(m_size);
}

qint64 FixedSizeIndex::offsetOf(qsizetype index) const
{
    const qsizetype clamped = qBound<qsizetype>(qsizetype(0), index, m_count);
    return qint64(clamped) * qint64(m_size);
}

qsizetype FixedSizeIndex::indexAt(qint64 offset) const
{
    if (m_count == 0 || m_size <= 0)
        return m_count;
    if (offset < 0)
        return 0;
    const qint64 index = offset / qint64(m_size);
    return index >= qint64(m_count) ? m_count : qsizetype(index);
}

int FixedSizeIndex::sizeOf(qsizetype index) const
{
    if (index < 0 || index >= m_count)
        return 0;
    return m_size;
}

void FixedSizeIndex::reset(qsizetype count, int estimatedSize)
{
    m_count = qMax<qsizetype>(0, count);
    if (estimatedSize > 0)
        m_size = estimatedSize;
}

void FixedSizeIndex::setSize(qsizetype index, int size)
{
    // A uniform index has exactly one size: an explicit size replaces the
    // current one (used by "fit to content" style adaptations).
    if (index < 0 || index >= m_count || size <= 0)
        return;
    m_size = size;
}

void FixedSizeIndex::insert(qsizetype index, qsizetype count, int estimate)
{
    Q_UNUSED(index);
    if (count <= 0)
        return;
    if (estimate > 0)
        m_size = estimate;
    m_count += count;
}

void FixedSizeIndex::remove(qsizetype index, qsizetype count)
{
    Q_UNUSED(index);
    if (count <= 0)
        return;
    m_count = qMax<qsizetype>(0, m_count - count);
}

// ---------------------------------------------------------------------------
// BlockSizeIndex
// ---------------------------------------------------------------------------

BlockSizeIndex::BlockSizeIndex(qsizetype blockCapacity)
    : m_blockCapacity(qMax<qsizetype>(1, blockCapacity))
{
    m_blocks.append(Block());
    markOffsetsDirty();
}

BlockSizeIndex::BlockSizeIndex(qsizetype count, int estimatedSize, qsizetype blockCapacity)
    : m_blockCapacity(qMax<qsizetype>(1, blockCapacity))
{
    m_blocks.append(Block());
    markOffsetsDirty();
    reset(count, estimatedSize);
}

QVector<int> BlockSizeIndex::sizes() const
{
    QVector<int> result;
    result.reserve(int(m_count));
    for (const Block &block : m_blocks)
        result.append(block.sizes);
    return result;
}

qsizetype BlockSizeIndex::blockCount() const
{
    return m_blocks.size();
}

void BlockSizeIndex::markOffsetsDirty()
{
    m_offsetsDirty = true;
}

void BlockSizeIndex::ensureOffsets() const
{
    if (!m_offsetsDirty)
        return;

    const qsizetype blockCount = m_blocks.size();
    m_blockOffsets.resize(int(blockCount) + 1);
    m_blockStartRows.resize(int(blockCount) + 1);

    qint64 offset = 0;
    qsizetype row = 0;
    for (qsizetype i = 0; i < blockCount; ++i) {
        m_blockOffsets[i] = offset;
        m_blockStartRows[i] = row;
        offset += m_blocks.at(i).sum;
        row += m_blocks.at(i).sizes.size();
    }
    m_blockOffsets[blockCount] = offset;
    m_blockStartRows[blockCount] = row;
    m_offsetsDirty = false;
}

qsizetype BlockSizeIndex::blockForRow(qsizetype index, qsizetype *localOffset) const
{
    ensureOffsets();

    const qsizetype clamped = qBound<qsizetype>(qsizetype(0), index, m_count);
    // Last block whose start row is <= clamped; the trailing sentinel makes
    // index == m_count select the last block.
    const auto begin = m_blockStartRows.constBegin();
    const auto it = std::upper_bound(begin, m_blockStartRows.constEnd(), clamped);
    // The trailing sentinel makes upper_bound return end() for index == count,
    // so clamp into the block list.
    const qsizetype blockIndex = qMin(qMax<qsizetype>(0, (it - begin) - 1),
                                      qsizetype(m_blocks.size()) - 1);

    if (localOffset) {
        const qsizetype startRow = m_blockStartRows.at(blockIndex);
        *localOffset = qMax<qsizetype>(0, clamped - startRow);
    }
    return blockIndex;
}

qsizetype BlockSizeIndex::blockForOffset(qint64 offset) const
{
    ensureOffsets();

    const auto begin = m_blockOffsets.constBegin();
    const auto it = std::upper_bound(begin, m_blockOffsets.constEnd(), offset);
    const qsizetype blockIndex = qMax<qsizetype>(0, (it - begin) - 1);
    return qMin(blockIndex, qsizetype(m_blocks.size()) - 1);
}

void BlockSizeIndex::rebuildFrom(const QVector<int> &sizes)
{
    m_blocks.clear();
    m_blocks.append(Block());
    qint64 total = 0;
    for (int size : sizes) {
        Block &block = m_blocks.last();
        block.sizes.append(size);
        block.sum += size;
        total += size;
        if (block.sizes.size() >= m_blockCapacity)
            m_blocks.append(Block());
    }
    if (m_blocks.size() > 1 && m_blocks.constLast().sizes.isEmpty())
        m_blocks.removeLast();
    m_count = sizes.size();
    m_totalSize = total;
    markOffsetsDirty();
}

void BlockSizeIndex::splitBlockIfNeeded(qsizetype blockIndex)
{
    Block &block = m_blocks[blockIndex];
    if (block.sizes.size() <= m_blockCapacity * 2)
        return;

    const qsizetype half = block.sizes.size() / 2;
    QVector<int> tail = block.sizes.mid(int(half));
    block.sizes.resize(int(half));

    qint64 tailSum = 0;
    for (int size : tail)
        tailSum += size;
    block.sum -= tailSum;

    Block newBlock;
    newBlock.sizes = tail;
    newBlock.sum = tailSum;
    m_blocks.insert(blockIndex + 1, newBlock);
    markOffsetsDirty();
}

void BlockSizeIndex::reset(qsizetype count, int estimatedSize)
{
    m_estimatedSize = qMax(0, estimatedSize);
    const qsizetype clamped = qMax<qsizetype>(0, count);
    if (clamped == 0) {
        m_blocks.clear();
        m_blocks.append(Block());
        m_count = 0;
        m_totalSize = 0;
        markOffsetsDirty();
        return;
    }

    QVector<int> sizes;
    sizes.reserve(int(clamped));
    sizes.fill(m_estimatedSize, int(clamped));
    rebuildFrom(sizes);
}

qint64 BlockSizeIndex::totalSize() const
{
    return m_totalSize;
}

qint64 BlockSizeIndex::offsetOf(qsizetype index) const
{
    const qsizetype clamped = qBound<qsizetype>(qsizetype(0), index, m_count);
    if (clamped == m_count)
        return m_totalSize;

    qsizetype local = 0;
    const qsizetype blockIndex = blockForRow(clamped, &local);
    ensureOffsets();

    qint64 offset = m_blockOffsets.at(blockIndex);
    const Block &block = m_blocks.at(blockIndex);
    const qsizetype end = qMin(local, qsizetype(block.sizes.size()));
    for (qsizetype i = 0; i < end; ++i)
        offset += block.sizes.at(i);
    return offset;
}

qsizetype BlockSizeIndex::indexAt(qint64 offset) const
{
    if (m_count == 0)
        return 0;
    if (offset >= m_totalSize)
        return m_count;

    ensureOffsets();

    // Offsets <= 0 start at the first block so that zero-sized items at the top
    // of the index are skipped instead of being reported as "the item at 0".
    const bool fromStart = offset <= 0;
    qsizetype blockIndex = fromStart ? 0 : blockForOffset(offset);
    qint64 remaining = fromStart ? 0 : offset - m_blockOffsets.at(blockIndex);
    if (remaining < 0)
        remaining = 0;

    while (blockIndex < m_blocks.size()) {
        const Block &block = m_blocks.at(blockIndex);
        const qsizetype startRow = m_blockStartRows.at(blockIndex);
        for (qsizetype i = 0; i < block.sizes.size(); ++i) {
            const int size = block.sizes.at(i);
            if (remaining < size)
                return startRow + i;
            remaining -= size;
        }
        ++blockIndex;
    }
    return m_count;
}

int BlockSizeIndex::sizeOf(qsizetype index) const
{
    if (index < 0 || index >= m_count)
        return 0;
    qsizetype local = 0;
    const qsizetype blockIndex = blockForRow(index, &local);
    const Block &block = m_blocks.at(blockIndex);
    if (local < 0 || local >= block.sizes.size())
        return 0;
    return block.sizes.at(local);
}

void BlockSizeIndex::setSize(qsizetype index, int size)
{
    if (index < 0 || index >= m_count)
        return;
    const int clampedSize = qMax(0, size);

    qsizetype local = 0;
    const qsizetype blockIndex = blockForRow(index, &local);
    Block &block = m_blocks[blockIndex];
    if (local < 0 || local >= block.sizes.size())
        return;

    const int previous = block.sizes.at(local);
    if (previous == clampedSize)
        return;

    block.sizes[local] = clampedSize;
    block.sum += qint64(clampedSize) - qint64(previous);
    m_totalSize += qint64(clampedSize) - qint64(previous);
    markOffsetsDirty();
}

void BlockSizeIndex::insert(qsizetype index, qsizetype count, int estimate)
{
    if (count <= 0)
        return;
    const int size = estimate > 0 ? estimate : m_estimatedSize;
    if (estimate > 0)
        m_estimatedSize = estimate;

    const qsizetype clamped = qBound<qsizetype>(qsizetype(0), index, m_count);
    if (m_count == 0) {
        QVector<int> sizes;
        sizes.reserve(int(count));
        sizes.fill(size, int(count));
        rebuildFrom(sizes);
        return;
    }

    qsizetype local = 0;
    const qsizetype blockIndex = blockForRow(clamped, &local);
    Block &block = m_blocks[blockIndex];
    local = qBound<qsizetype>(qsizetype(0), local, qsizetype(block.sizes.size()));
    block.sizes.insert(int(local), int(count), size);
    block.sum += qint64(size) * qint64(count);
    m_count += count;
    m_totalSize += qint64(size) * qint64(count);
    splitBlockIfNeeded(blockIndex);
    markOffsetsDirty();
}

void BlockSizeIndex::remove(qsizetype index, qsizetype count)
{
    if (count <= 0 || m_count == 0)
        return;

    const qsizetype first = qBound<qsizetype>(qsizetype(0), index, m_count);
    const qsizetype removed = qMin(count, m_count - first);
    if (removed <= 0)
        return;

    qsizetype local = 0;
    qsizetype blockIndex = blockForRow(first, &local);
    qsizetype remaining = removed;

    while (remaining > 0 && blockIndex < m_blocks.size()) {
        Block &block = m_blocks[blockIndex];
        if (local >= block.sizes.size()) {
            ++blockIndex;
            local = 0;
            continue;
        }

        const qsizetype take = qMin(remaining, qsizetype(block.sizes.size()) - local);
        qint64 removedSum = 0;
        for (qsizetype i = 0; i < take; ++i)
            removedSum += block.sizes.at(local + i);

        block.sizes.remove(int(local), int(take));
        block.sum -= removedSum;
        block.sum = qMax<qint64>(0, block.sum);
        m_totalSize -= removedSum;
        m_count -= take;
        remaining -= take;

        if (block.sizes.isEmpty() && m_blocks.size() > 1) {
            m_blocks.removeAt(blockIndex);
        } else if (remaining > 0) {
            ++blockIndex;
        }
        local = 0;
    }

    if (m_blocks.isEmpty())
        m_blocks.append(Block());
    if (m_count == 0) {
        m_blocks.clear();
        m_blocks.append(Block());
        m_totalSize = 0;
    }
    markOffsetsDirty();
}

} // namespace viv
