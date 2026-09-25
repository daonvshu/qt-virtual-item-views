#pragma once

#include <virtualitemviews/global.h>

#include <QList>
#include <QVector>
#include <QtGlobal>

namespace viv {

/// Abstract size/offset index of a virtual layout.
///
/// The index owns the mapping between an item's logical position (row) and its
/// pixel range along the scrolling axis. The public interface is stable; the
/// data structure behind it is not (see docs/performance.md).
///
/// All operations accept indices in [0, count()]; \c count() is a valid index
/// for \c offsetOf() (it returns totalSize()) and for insert() (appending).
class VIRTUALITEMVIEWS_EXPORT SizeIndex
{
public:
    virtual ~SizeIndex() = default;

    qsizetype count() const { return itemCount(); }

    /// Sum of all item sizes.
    virtual qint64 totalSize() const = 0;

    /// Offset of the start of \a index. \a index may be count(), which yields
    /// totalSize(). Offsets of out-of-range indices are clamped.
    virtual qint64 offsetOf(qsizetype index) const = 0;

    /// Index of the item containing \a offset. Returns count() when \a offset
    /// is at or beyond totalSize(), and 0 for empty indexes or offsets <= 0.
    virtual qsizetype indexAt(qint64 offset) const = 0;

    virtual int sizeOf(qsizetype index) const = 0;

    virtual void reset(qsizetype count, int estimatedSize) = 0;
    virtual void setSize(qsizetype index, int size) = 0;
    virtual void insert(qsizetype index, qsizetype count, int estimate) = 0;
    virtual void remove(qsizetype index, qsizetype count) = 0;

    /// True when every item has the same size.
    virtual bool isUniform() const = 0;

    /// Item size of a uniform index (\c isUniform() == true), otherwise 0.
    virtual int uniformSize() const { return 0; }

protected:
    virtual qsizetype itemCount() const = 0;
};

/// Size index for fixed-height content. All queries are O(1).
class VIRTUALITEMVIEWS_EXPORT FixedSizeIndex : public SizeIndex
{
public:
    FixedSizeIndex() = default;
    FixedSizeIndex(qsizetype count, int size);

    void setItemSize(int size);
    int itemSize() const { return m_size; }

    qint64 totalSize() const override;
    qint64 offsetOf(qsizetype index) const override;
    qsizetype indexAt(qint64 offset) const override;
    int sizeOf(qsizetype index) const override;

    void reset(qsizetype count, int estimatedSize) override;
    void setSize(qsizetype index, int size) override;
    void insert(qsizetype index, qsizetype count, int estimate) override;
    void remove(qsizetype index, qsizetype count) override;

    bool isUniform() const override { return true; }
    int uniformSize() const override { return m_size; }

protected:
    qsizetype itemCount() const override { return m_count; }

private:
    qsizetype m_count = 0;
    int m_size = 0;
};

/// Size index for dynamic content, backed by fixed-capacity blocks.
///
/// Each block stores one *base* size plus a sparse, sorted table of exceptions:
/// the rows whose measured size differs from that base. Rows that were never
/// measured therefore cost nothing at all, so uniform content keeps one entry
/// per block instead of 4 bytes per row (ten million rows: ~40 MB before, well
/// under 1 MB now). A measured size is never rewritten by a later estimate
/// change, nor by inserts/removals of other rows.
///
/// A lazily rebuilt prefix table over the block sums provides O(log B) block
/// lookup (B = block count); the row inside a block is found by binary
/// searching its exceptions and then walking the (usually empty) remainder.
/// blockForRow()/indexAt()/setSize() are O(log B + E_b) with E_b the exceptions
/// below the row, setSize() additionally pays the vector shift inside the
/// block, insert() is O(log B + capacity) and remove() O(B + capacity).
/// Splitting a block above 2 * capacity and merging neighbours that fit into
/// one capacity keeps E_b <= 2 * capacity, so the worst case stays the
/// O(log B + capacity) of the previous representation while the common case (a
/// block nobody ever measured) is O(log B). This matches the "first version may
/// be simpler than O(log N)" guidance of the architecture document; the public
/// API is ready to be backed by a Fenwick tree or an implicit balanced tree
/// later on.
class VIRTUALITEMVIEWS_EXPORT BlockSizeIndex : public SizeIndex
{
public:
    /// Capacity of the blocks used to split the item sizes.
    static constexpr qsizetype kDefaultBlockCapacity = 1024;

    explicit BlockSizeIndex(qsizetype blockCapacity = kDefaultBlockCapacity);
    BlockSizeIndex(qsizetype count, int estimatedSize,
                   qsizetype blockCapacity = kDefaultBlockCapacity);

    qint64 totalSize() const override;
    qint64 offsetOf(qsizetype index) const override;
    qsizetype indexAt(qint64 offset) const override;
    int sizeOf(qsizetype index) const override;

    void reset(qsizetype count, int estimatedSize) override;
    void setSize(qsizetype index, int size) override;
    void insert(qsizetype index, qsizetype count, int estimate) override;
    void remove(qsizetype index, qsizetype count) override;

    bool isUniform() const override { return false; }

    qsizetype blockCapacity() const { return m_blockCapacity; }
    qsizetype blockCount() const;
    /// Largest number of rows any block holds (diagnostics/tests). The split
    /// policy keeps it at or below 2 * blockCapacity().
    qsizetype maxBlockRowCount() const;
    /// Size used for items whose size was never measured.
    int estimatedSize() const { return m_estimatedSize; }
    /// Sizes of all items (mainly for tests and diagnostics).
    QVector<int> sizes() const;
    /// Number of rows whose measured size differs from the estimate of the
    /// block that owns them (tests and diagnostics). A freshly reset index
    /// reports 0, and a setSize() either adds one or removes one.
    qsizetype explicitSizeCount() const;

protected:
    qsizetype itemCount() const override { return m_count; }

private:
    /// A row of a block whose measured size differs from the block base.
    struct Exception
    {
        /// Row relative to the first row of the block.
        qsizetype row = 0;
        int size = 0;
    };

    struct Block
    {
        /// Size of every row that has no exception of its own.
        int baseSize = 0;
        /// Sum of the sizes of all rows of the block.
        qint64 sum = 0;
        qsizetype rowCount = 0;
        /// QVector (not QList) so that the Qt 5 vector API (resize/remove) is
        /// available; Qt 6 aliases QVector to QList. Sorted by \c row.
        QVector<Exception> exceptions;
    };

    static bool exceptionRowLess(const Exception &exception, qsizetype row);
    /// Index of the first exception at or after \a local.
    static qsizetype exceptionPosition(const Block &block, qsizetype local);
    static int blockSizeAt(const Block &block, qsizetype local);
    /// Sum of the sizes of the rows [0, \a local) of \a block.
    static qint64 blockSumBelow(const Block &block, qsizetype local);
    /// Local row of \a block that contains \a offset (relative to the block
    /// start). Returns \c block.rowCount when no row of the block covers it,
    /// which only happens for zero-sized rows.
    static qsizetype blockRowAt(const Block &block, qint64 offset);

    void ensureOffsets() const;
    void markOffsetsDirty();
    qsizetype blockForRow(qsizetype index, qsizetype *localOffset) const;
    qsizetype blockForOffset(qint64 offset) const;
    /// Replaces the whole index by \a count rows of \a size.
    void fillUniform(qsizetype count, int size);
    /// Splits the block so that a new block starts at \a local; returns its
    /// index, or -1 when the split point is already a block boundary.
    qsizetype splitBlockAt(qsizetype blockIndex, qsizetype local);
    void splitBlockIfNeeded(qsizetype blockIndex);
    bool tryMergeWithNext(qsizetype blockIndex);
    void compactBlocks();

    /// Pixel offset of every block start, plus a trailing sentinel (totalSize).
    mutable QVector<qint64> m_blockOffsets;
    /// First row of every block, plus a trailing sentinel (count).
    mutable QVector<qsizetype> m_blockStartRows;
    mutable bool m_offsetsDirty = false;
    QList<Block> m_blocks;
    qsizetype m_blockCapacity = kDefaultBlockCapacity;
    qsizetype m_count = 0;
    qint64 m_totalSize = 0;
    int m_estimatedSize = 0;
};

} // namespace viv
