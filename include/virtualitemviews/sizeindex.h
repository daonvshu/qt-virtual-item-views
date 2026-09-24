#pragma once

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
class SizeIndex
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
class FixedSizeIndex : public SizeIndex
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
/// Each block stores the sizes of the items it owns plus their sum; a lazily
/// rebuilt prefix table over the block sums provides O(log B) block lookup.
/// blockForRow()/indexAt() are O(log B + capacity) and setSize() is O(1) until
/// the prefix tables are rebuilt (O(B)) on the next query. insert() costs
/// O(capacity) and remove() O(capacity + B). This matches the "first version
/// may be simpler than O(log N)" guidance of the architecture document; the
/// public API is ready to be backed by a Fenwick tree or an implicit balanced
/// tree later on.
class BlockSizeIndex : public SizeIndex
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
    /// Size used for items whose size was never measured.
    int estimatedSize() const { return m_estimatedSize; }
    /// Sizes of all items (mainly for tests and diagnostics).
    QVector<int> sizes() const;

protected:
    qsizetype itemCount() const override { return m_count; }

private:
    struct Block
    {
        qint64 sum = 0;
        /// QVector (not QList) so that the Qt 5 vector API (resize/fill/remove)
        /// is available; Qt 6 aliases QVector to QList.
        QVector<int> sizes;
    };

    void ensureOffsets() const;
    void markOffsetsDirty();
    qsizetype blockForRow(qsizetype index, qsizetype *localOffset) const;
    qsizetype blockForOffset(qint64 offset) const;
    void splitBlockIfNeeded(qsizetype blockIndex);
    void rebuildFrom(const QVector<int> &sizes);

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
