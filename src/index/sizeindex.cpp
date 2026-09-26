#include <virtualitemviews/sizeindex.h>

#include <algorithm>

namespace viv {

// A shared build has to export this public constant (see virtualitemview.cpp).
namespace {
[[maybe_unused]] const void *const kExportedConstants[] = {
    &BlockSizeIndex::kDefaultBlockCapacity,
};
} // namespace

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
    for (const Block &block : m_blocks) {
        qsizetype row = 0;
        for (const Exception &exception : block.exceptions) {
            for (; row < exception.row; ++row)
                result.append(block.baseSize);
            result.append(exception.size);
            row = exception.row + 1;
        }
        for (; row < block.rowCount; ++row)
            result.append(block.baseSize);
    }
    return result;
}

qsizetype BlockSizeIndex::explicitSizeCount() const
{
    qsizetype total = 0;
    for (const Block &block : m_blocks)
        total += block.exceptions.size();
    return total;
}

bool BlockSizeIndex::exceptionRowLess(const Exception &exception, qsizetype row)
{
    return exception.row < row;
}

qsizetype BlockSizeIndex::exceptionPosition(const Block &block, qsizetype local)
{
    const auto it = std::lower_bound(block.exceptions.constBegin(), block.exceptions.constEnd(),
                                     local, &BlockSizeIndex::exceptionRowLess);
    return it - block.exceptions.constBegin();
}

int BlockSizeIndex::blockSizeAt(const Block &block, qsizetype local)
{
    const qsizetype position = exceptionPosition(block, local);
    if (position < block.exceptions.size() && block.exceptions.at(position).row == local)
        return block.exceptions.at(position).size;
    return block.baseSize;
}

qint64 BlockSizeIndex::blockSumBelow(const Block &block, qsizetype local)
{
    const qsizetype rows = qBound<qsizetype>(qsizetype(0), local, block.rowCount);
    qint64 sum = qint64(rows) * qint64(block.baseSize);
    const qsizetype end = exceptionPosition(block, rows);
    for (qsizetype i = 0; i < end; ++i)
        sum += qint64(block.exceptions.at(i).size) - qint64(block.baseSize);
    return sum;
}

qsizetype BlockSizeIndex::blockRowAt(const Block &block, qint64 offset)
{
    // Rows between two exceptions share the base size, so the containing row is
    // one division away; only the sparse exceptions need a walk.
    qsizetype row = 0;
    qint64 remaining = offset;
    for (const Exception &exception : block.exceptions) {
        if (exception.row > row) {
            const qint64 runSize = qint64(exception.row - row) * qint64(block.baseSize);
            if (block.baseSize > 0 && remaining < runSize)
                return row + qsizetype(remaining / qint64(block.baseSize));
            remaining -= runSize;
        }
        row = exception.row;
        if (remaining < exception.size)
            return row;
        remaining -= exception.size;
        row = exception.row + 1;
    }
    if (block.baseSize <= 0)
        return block.rowCount; // only zero-sized rows are left
    return qMin(row + qsizetype(remaining / qint64(block.baseSize)), block.rowCount);
}

qsizetype BlockSizeIndex::blockCount() const
{
    return m_blocks.size();
}

qsizetype BlockSizeIndex::maxBlockRowCount() const
{
    qsizetype maximum = 0;
    for (const Block &block : m_blocks)
        maximum = qMax(maximum, block.rowCount);
    return maximum;
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
        row += m_blocks.at(i).rowCount;
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

void BlockSizeIndex::fillUniform(qsizetype count, int size)
{
    m_blocks.clear();
    m_count = qMax<qsizetype>(0, count);
    m_totalSize = qint64(m_count) * qint64(size);
    if (m_count == 0) {
        m_blocks.append(Block());
    } else {
        qsizetype remaining = m_count;
        while (remaining > 0) {
            const qsizetype chunk = qMin(remaining, m_blockCapacity);
            Block block;
            block.baseSize = size;
            block.rowCount = chunk;
            block.sum = qint64(chunk) * qint64(size);
            m_blocks.append(block);
            remaining -= chunk;
        }
    }
    markOffsetsDirty();
}

qsizetype BlockSizeIndex::splitBlockAt(qsizetype blockIndex, qsizetype local)
{
    const Block &block = m_blocks.at(blockIndex);
    local = qBound<qsizetype>(qsizetype(0), local, block.rowCount);
    if (local <= 0 || local >= block.rowCount)
        return -1;

    Block head;
    head.baseSize = block.baseSize;
    head.rowCount = local;
    head.sum = blockSumBelow(block, local);

    Block tail;
    tail.baseSize = block.baseSize;
    tail.rowCount = block.rowCount - local;
    tail.sum = block.sum - head.sum;

    for (const Exception &exception : block.exceptions) {
        Exception moved = exception;
        if (moved.row < local) {
            head.exceptions.append(moved);
        } else {
            moved.row -= local;
            tail.exceptions.append(moved);
        }
    }

    m_blocks[blockIndex] = head;
    m_blocks.insert(blockIndex + 1, tail);
    markOffsetsDirty();
    return blockIndex + 1;
}

void BlockSizeIndex::splitBlockIfNeeded(qsizetype blockIndex)
{
    // Blocks are allowed to grow to twice the capacity before they split, so the
    // per-block scan (and the exception table) stays bounded without paying a
    // rebuild per inserted row. Every block the splits *create* has to be checked
    // as well: splitting one huge block in half leaves a huge tail behind, and a
    // single 1,000,000-row insert would otherwise keep a block of half a million
    // rows (which breaks the bound the rest of the index relies on).
    QVector<qsizetype> pending;
    pending.append(blockIndex);
    while (!pending.isEmpty()) {
        const qsizetype index = pending.takeLast();
        if (index < 0 || index >= m_blocks.size())
            continue;
        const qsizetype rows = m_blocks.at(index).rowCount;
        if (rows <= m_blockCapacity * 2)
            continue;
        const qsizetype tail = splitBlockAt(index, rows / 2);
        if (tail < 0)
            continue;
        pending.append(index);
        pending.append(tail);
    }
}

bool BlockSizeIndex::tryMergeWithNext(qsizetype blockIndex)
{
    if (blockIndex < 0 || blockIndex + 1 >= m_blocks.size())
        return false;

    Block &head = m_blocks[blockIndex];
    Block &tail = m_blocks[blockIndex + 1];
    if (head.baseSize != tail.baseSize || head.rowCount + tail.rowCount > m_blockCapacity
        || head.exceptions.size() + tail.exceptions.size() > m_blockCapacity) {
        return false;
    }

    const qsizetype offset = head.rowCount;
    head.exceptions.reserve(head.exceptions.size() + tail.exceptions.size());
    for (const Exception &exception : tail.exceptions) {
        Exception moved = exception;
        moved.row += offset;
        head.exceptions.append(moved);
    }
    head.rowCount += tail.rowCount;
    head.sum += tail.sum;
    m_blocks.removeAt(blockIndex + 1);
    markOffsetsDirty();
    return true;
}

void BlockSizeIndex::compactBlocks()
{
    qsizetype blockIndex = 0;
    while (blockIndex < m_blocks.size()) {
        if (m_blocks.at(blockIndex).rowCount == 0) {
            if (m_blocks.size() == 1)
                break;
            m_blocks.removeAt(blockIndex);
            markOffsetsDirty();
            continue;
        }
        if (!tryMergeWithNext(blockIndex))
            ++blockIndex;
    }
}

void BlockSizeIndex::reset(qsizetype count, int estimatedSize)
{
    m_estimatedSize = qMax(0, estimatedSize);
    fillUniform(count, m_estimatedSize);
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
    return m_blockOffsets.at(blockIndex) + blockSumBelow(m_blocks.at(blockIndex), local);
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
        const qsizetype local = blockRowAt(block, remaining);
        if (local < block.rowCount)
            return startRow + local;
        // The block holds only zero-sized rows: the offset belongs to a later
        // block (total size guarantees it exists).
        remaining -= block.sum;
        if (remaining < 0)
            remaining = 0;
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
    if (local < 0 || local >= block.rowCount)
        return 0;
    return blockSizeAt(block, local);
}

void BlockSizeIndex::setSize(qsizetype index, int size)
{
    if (index < 0 || index >= m_count)
        return;
    const int clampedSize = qMax(0, size);

    qsizetype local = 0;
    const qsizetype blockIndex = blockForRow(index, &local);
    Block &block = m_blocks[blockIndex];
    if (local < 0 || local >= block.rowCount)
        return;

    const int previous = blockSizeAt(block, local);
    if (previous == clampedSize)
        return;

    // Rows whose measured size happens to equal the block base carry no
    // exception, so measuring them back to the estimate releases the entry.
    const qsizetype position = exceptionPosition(block, local);
    const bool hadException =
        position < block.exceptions.size() && block.exceptions.at(position).row == local;
    if (clampedSize == block.baseSize) {
        if (hadException)
            block.exceptions.removeAt(int(position));
    } else if (hadException) {
        block.exceptions[position].size = clampedSize;
    } else {
        Exception exception;
        exception.row = local;
        exception.size = clampedSize;
        block.exceptions.insert(int(position), exception);
    }

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

    if (m_count == 0) {
        fillUniform(count, size);
        return;
    }

    const qsizetype clamped = qBound<qsizetype>(qsizetype(0), index, m_count);
    qsizetype local = 0;
    const qsizetype blockIndex = blockForRow(clamped, &local);
    local = qBound<qsizetype>(qsizetype(0), local, m_blocks.at(blockIndex).rowCount);

    // Inserting a uniform run is a split plus one new block; the rows themselves
    // are never materialised.
    qsizetype at = blockIndex;
    const qsizetype tail = splitBlockAt(blockIndex, local);
    if (tail >= 0)
        at = tail;
    else if (local > 0)
        at = blockIndex + 1; // the run starts at the end of its block

    Block block;
    block.baseSize = size;
    block.rowCount = count;
    block.sum = qint64(size) * qint64(count);
    m_blocks.insert(at, block);
    m_count += count;
    m_totalSize += block.sum;

    splitBlockIfNeeded(at);
    if (at > 0 && tryMergeWithNext(at - 1))
        --at;
    tryMergeWithNext(at);
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

    // Cut the range out on block boundaries first, so that the rest of the work
    // is "drop whole blocks" and the exceptions of the surviving neighbours do
    // not have to be shifted one by one.
    qsizetype local = 0;
    const qsizetype endBlock = blockForRow(first + removed, &local);
    if (local > 0)
        splitBlockAt(endBlock, local);

    const qsizetype startBlock = blockForRow(first, &local);
    if (local > 0)
        splitBlockAt(startBlock, local);

    const qsizetype from = blockForRow(first, nullptr);
    const qsizetype to = first + removed >= m_count ? m_blocks.size()
                                                    : blockForRow(first + removed, nullptr);
    qint64 removedSum = 0;
    for (qsizetype i = from; i < to; ++i)
        removedSum += m_blocks.at(i).sum;
    for (qsizetype i = to - 1; i >= from; --i)
        m_blocks.removeAt(i);

    m_count -= removed;
    m_totalSize = qMax<qint64>(0, m_totalSize - removedSum);

    if (m_count == 0) {
        m_blocks.clear();
        m_blocks.append(Block());
        m_totalSize = 0;
    } else {
        compactBlocks();
    }
    markOffsetsDirty();
}

} // namespace viv
