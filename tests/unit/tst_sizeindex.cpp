#include <virtualitemviews/sizeindex.h>

#include <QtTest>

#include <QList>
#include <random>

using namespace viv;

namespace {
/// Small block capacity so that block boundaries are exercised by the tests.
constexpr qsizetype kTestBlockCapacity = 4;
}

class TestSizeIndex : public QObject
{
    Q_OBJECT

private slots:
    void fixedIndexGeometry();
    void fixedIndexClampsOutOfRange();
    void fixedIndexInsertRemoveResize();
    void fixedIndexIsUniform();

    void blockIndexResetGeometry();
    void blockIndexSetSizeShiftsOffsets();
    void blockIndexIndexAtBoundaries();
    void blockIndexSkipsZeroSizedItems();
    void blockIndexInsertAtFrontKeepsFollowingSizes();
    void blockIndexInsertInMiddleSplitsBlocks();
    void blockIndexAppendAtEnd();
    void blockIndexRemoveAcrossBlocks();
    void blockIndexRemoveEverything();
    void blockIndexMatchesReferenceModel();
    void blockIndexHandlesMillionItems();
    void blockIndexStaysCompactWithoutMeasuredSizes();
    void blockIndexKeepsMeasuredSizesAcrossInserts();
    void blockIndexMeasuredSizesSurviveSplices();
    void blockIndexAppendManyRowsKeepsBlockCountBounded();
    void blockIndexBoundsEveryBlockAfterAHugeInsert();
};

void TestSizeIndex::fixedIndexGeometry()
{
    FixedSizeIndex index(1000, 24);
    QCOMPARE(index.count(), qsizetype(1000));
    QCOMPARE(index.totalSize(), qint64(24000));
    QCOMPARE(index.offsetOf(0), qint64(0));
    QCOMPARE(index.offsetOf(10), qint64(240));
    QCOMPARE(index.offsetOf(1000), qint64(24000));
    QCOMPARE(index.sizeOf(999), 24);
    QCOMPARE(index.indexAt(0), qsizetype(0));
    QCOMPARE(index.indexAt(23), qsizetype(0));
    QCOMPARE(index.indexAt(24), qsizetype(1));
    QCOMPARE(index.indexAt(23999), qsizetype(999));
    QCOMPARE(index.indexAt(24000), qsizetype(1000));
}

void TestSizeIndex::fixedIndexClampsOutOfRange()
{
    FixedSizeIndex index(10, 10);
    QCOMPARE(index.offsetOf(-5), qint64(0));
    QCOMPARE(index.offsetOf(1000), qint64(100));
    QCOMPARE(index.sizeOf(-1), 0);
    QCOMPARE(index.sizeOf(10), 0);
    QCOMPARE(index.indexAt(-100), qsizetype(0));
    QCOMPARE(index.indexAt(100000), qsizetype(10));

    FixedSizeIndex empty;
    QCOMPARE(empty.count(), qsizetype(0));
    QCOMPARE(empty.totalSize(), qint64(0));
    QCOMPARE(empty.indexAt(0), qsizetype(0));
    QCOMPARE(empty.indexAt(500), qsizetype(0));
    QCOMPARE(empty.offsetOf(0), qint64(0));
}

void TestSizeIndex::fixedIndexInsertRemoveResize()
{
    FixedSizeIndex index(10, 20);
    index.insert(0, 5, 20);
    QCOMPARE(index.count(), qsizetype(15));
    QCOMPARE(index.totalSize(), qint64(300));

    index.remove(0, 5);
    QCOMPARE(index.count(), qsizetype(10));
    QCOMPARE(index.totalSize(), qint64(200));

    index.reset(3, 8);
    QCOMPARE(index.count(), qsizetype(3));
    QCOMPARE(index.totalSize(), qint64(24));
    QCOMPARE(index.sizeOf(0), 8);

    // Removing more than available must not underflow.
    index.remove(0, 100);
    QCOMPARE(index.count(), qsizetype(0));
    QCOMPARE(index.totalSize(), qint64(0));
}

void TestSizeIndex::fixedIndexIsUniform()
{
    FixedSizeIndex index(5, 12);
    QVERIFY(index.isUniform());
    QCOMPARE(index.uniformSize(), 12);

    BlockSizeIndex variable(5, 12, kTestBlockCapacity);
    QVERIFY(!variable.isUniform());
}

void TestSizeIndex::blockIndexResetGeometry()
{
    BlockSizeIndex index(0, 10, kTestBlockCapacity);
    index.reset(10, 30);

    QCOMPARE(index.count(), qsizetype(10));
    QCOMPARE(index.totalSize(), qint64(300));
    QCOMPARE(index.estimatedSize(), 30);
    for (qsizetype row = 0; row <= 10; ++row)
        QCOMPARE(index.offsetOf(row), qint64(row) * 30);
    for (qsizetype row = 0; row < 10; ++row)
        QCOMPARE(index.indexAt(row * 30), row);
    QCOMPARE(index.indexAt(299), qsizetype(9));
    QCOMPARE(index.indexAt(300), qsizetype(10));
}

void TestSizeIndex::blockIndexSetSizeShiftsOffsets()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(6, 20);
    index.setSize(2, 50);

    QCOMPARE(index.sizeOf(2), 50);
    QCOMPARE(index.totalSize(), qint64(5 * 20 + 50));
    QCOMPARE(index.offsetOf(0), qint64(0));
    QCOMPARE(index.offsetOf(2), qint64(40));
    QCOMPARE(index.offsetOf(3), qint64(90));
    QCOMPARE(index.offsetOf(6), qint64(150));
    QCOMPARE(index.indexAt(40), qsizetype(2));
    QCOMPARE(index.indexAt(89), qsizetype(2));
    QCOMPARE(index.indexAt(90), qsizetype(3));

    index.setSize(0, 0);
    QCOMPARE(index.sizeOf(0), 0);
    QCOMPARE(index.offsetOf(1), qint64(0));
    QCOMPARE(index.indexAt(0), qsizetype(1));
}

void TestSizeIndex::blockIndexIndexAtBoundaries()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(3, 10);
    index.setSize(1, 40);
    index.setSize(2, 20);

    QCOMPARE(index.totalSize(), qint64(70));
    QCOMPARE(index.indexAt(-10), qsizetype(0));
    QCOMPARE(index.indexAt(0), qsizetype(0));
    QCOMPARE(index.indexAt(9), qsizetype(0));
    QCOMPARE(index.indexAt(10), qsizetype(1));
    QCOMPARE(index.indexAt(49), qsizetype(1));
    QCOMPARE(index.indexAt(50), qsizetype(2));
    QCOMPARE(index.indexAt(69), qsizetype(2));
    QCOMPARE(index.indexAt(70), qsizetype(3));
    QCOMPARE(index.indexAt(1000), qsizetype(3));

    BlockSizeIndex empty(0, 0, kTestBlockCapacity);
    QCOMPARE(empty.indexAt(0), qsizetype(0));
    QCOMPARE(empty.indexAt(100), qsizetype(0));
    QCOMPARE(empty.totalSize(), qint64(0));
}

void TestSizeIndex::blockIndexSkipsZeroSizedItems()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(3, 0);
    index.setSize(1, 10);
    index.setSize(2, 10);

    QCOMPARE(index.totalSize(), qint64(20));
    QCOMPARE(index.offsetOf(0), qint64(0));
    QCOMPARE(index.offsetOf(1), qint64(0));
    QCOMPARE(index.indexAt(0), qsizetype(1));
    QCOMPARE(index.indexAt(9), qsizetype(1));
    QCOMPARE(index.indexAt(10), qsizetype(2));
    QCOMPARE(index.indexAt(19), qsizetype(2));
}

void TestSizeIndex::blockIndexInsertAtFrontKeepsFollowingSizes()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(3, 10);
    index.setSize(0, 30);
    index.setSize(1, 40);
    index.setSize(2, 50);

    index.insert(0, 2, 5);

    QCOMPARE(index.count(), qsizetype(5));
    QCOMPARE(index.sizeOf(0), 5);
    QCOMPARE(index.sizeOf(1), 5);
    QCOMPARE(index.sizeOf(2), 30);
    QCOMPARE(index.sizeOf(3), 40);
    QCOMPARE(index.sizeOf(4), 50);
    QCOMPARE(index.totalSize(), qint64(130));
    QCOMPARE(index.offsetOf(2), qint64(10));
    QCOMPARE(index.indexAt(10), qsizetype(2));
}

void TestSizeIndex::blockIndexInsertInMiddleSplitsBlocks()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    QVector<int> reference;
    for (int i = 0; i < 10; ++i) {
        index.insert(index.count(), 1, 10 + i);
        reference.append(10 + i);
    }

    // Insert many rows at the same position: the block has to split and keep
    // the row order intact.
    index.insert(3, 20, 7);
    for (int i = 0; i < 20; ++i)
        reference.insert(3, 7);

    QCOMPARE(index.count(), qsizetype(reference.size()));
    QCOMPARE(index.sizes(), reference);
    QVERIFY(index.blockCount() > 1);

    qint64 offset = 0;
    for (qsizetype row = 0; row < index.count(); ++row) {
        QCOMPARE(index.offsetOf(row), offset);
        QCOMPARE(index.indexAt(offset), row);
        offset += index.sizeOf(row);
    }
    QCOMPARE(index.totalSize(), offset);
}

void TestSizeIndex::blockIndexAppendAtEnd()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(2, 10);
    index.insert(index.count(), 3, 25);

    QCOMPARE(index.count(), qsizetype(5));
    QCOMPARE(index.totalSize(), qint64(95));
    QCOMPARE(index.sizeOf(4), 25);
    QCOMPARE(index.offsetOf(2), qint64(20));
    QCOMPARE(index.indexAt(94), qsizetype(4));
}

void TestSizeIndex::blockIndexRemoveAcrossBlocks()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    QVector<int> reference;
    for (int i = 0; i < 25; ++i) {
        index.insert(index.count(), 1, 10);
        reference.append(10);
    }
    index.setSize(5, 50);
    reference[5] = 50;

    index.remove(3, 10);
    for (int i = 0; i < 10; ++i)
        reference.removeAt(3);

    QCOMPARE(index.count(), qsizetype(reference.size()));
    QCOMPARE(index.sizes(), reference);
    // 25 rows of 10 with row 5 raised to 50, minus 10 removed rows (one of them
    // the 50 px row): 250 + 40 - 140 = 150.
    QCOMPARE(index.totalSize(), qint64(150));

    qint64 offset = 0;
    for (qsizetype row = 0; row < index.count(); ++row) {
        QCOMPARE(index.offsetOf(row), offset);
        QCOMPARE(index.indexAt(offset), row);
        offset += index.sizeOf(row);
    }
}

void TestSizeIndex::blockIndexRemoveEverything()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(10, 15);
    index.remove(0, 10);

    QCOMPARE(index.count(), qsizetype(0));
    QCOMPARE(index.totalSize(), qint64(0));
    QCOMPARE(index.offsetOf(0), qint64(0));
    QCOMPARE(index.indexAt(0), qsizetype(0));

    index.remove(0, 5);
    QCOMPARE(index.count(), qsizetype(0));

    index.insert(0, 2, 12);
    QCOMPARE(index.count(), qsizetype(2));
    QCOMPARE(index.totalSize(), qint64(24));
}

void TestSizeIndex::blockIndexMatchesReferenceModel()
{
    std::mt19937 rng(20260923);
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    QVector<int> reference;

    auto verify = [&index, &reference]() {
        QCOMPARE(index.count(), qsizetype(reference.size()));
        qint64 total = 0;
        for (int size : reference)
            total += size;
        QCOMPARE(index.totalSize(), total);
        QCOMPARE(index.sizes(), reference);

        qint64 offset = 0;
        for (qsizetype row = 0; row < qsizetype(reference.size()); ++row) {
            QCOMPARE(index.offsetOf(row), offset);
            QCOMPARE(index.sizeOf(row), reference.at(row));
            if (reference.at(row) > 0) {
                QCOMPARE(index.indexAt(offset), row);
                QCOMPARE(index.indexAt(offset + reference.at(row) - 1), row);
            }
            offset += reference.at(row);
        }
        QCOMPARE(index.offsetOf(index.count()), total);
        if (!reference.isEmpty())
            QCOMPARE(index.indexAt(total), index.count());
    };

    for (int step = 0; step < 400; ++step) {
        const int operation = int(rng() % 5);
        const int count = int(reference.size());
        switch (operation) {
        case 0: { // set size
            if (count == 0)
                break;
            const qsizetype row = qsizetype(rng() % unsigned(count));
            const int size = int(rng() % 60);
            index.setSize(row, size);
            reference[int(row)] = size;
            break;
        }
        case 1: { // insert
            const qsizetype row = count == 0 ? 0 : qsizetype(rng() % unsigned(count + 1));
            const qsizetype added = qsizetype(rng() % 5) + 1;
            const int size = int(rng() % 40) + 1;
            index.insert(row, added, size);
            for (qsizetype i = 0; i < added; ++i)
                reference.insert(int(row), size);
            break;
        }
        case 2: { // remove
            if (count == 0)
                break;
            const qsizetype row = qsizetype(rng() % unsigned(count));
            const qsizetype removed = qMin<qsizetype>(qsizetype(rng() % 5) + 1, count - row);
            index.remove(row, removed);
            for (qsizetype i = 0; i < removed; ++i)
                reference.removeAt(int(row));
            break;
        }
        case 3: // reset
            if (count == 0)
                break;
            index.reset(qsizetype(rng() % 30) + 1, int(rng() % 25) + 1);
            reference.clear();
            for (qsizetype i = 0; i < index.count(); ++i)
                reference.append(index.sizeOf(i));
            break;
        default:
            verify();
            break;
        }
        verify();
    }
}

void TestSizeIndex::blockIndexHandlesMillionItems()
{
    BlockSizeIndex index(1000000, 48, 1024);
    QCOMPARE(index.count(), qsizetype(1000000));
    QCOMPARE(index.totalSize(), qint64(48000000));
    QCOMPARE(index.offsetOf(0), qint64(0));
    QCOMPARE(index.offsetOf(500000), qint64(24000000));
    QCOMPARE(index.offsetOf(999999), qint64(47999952));
    QCOMPARE(index.indexAt(47999999), qsizetype(999999));
    QCOMPARE(index.indexAt(24000000), qsizetype(500000));

    // Mixed sizes must keep offsets exact for 64-bit ranges as well.
    index.setSize(999999, 1000000);
    QCOMPARE(index.totalSize(), qint64(48000000 - 48 + 1000000));
    QCOMPARE(index.offsetOf(999999), qint64(47999952));
    QCOMPARE(index.indexAt(47999952), qsizetype(999999));
}

void TestSizeIndex::blockIndexStaysCompactWithoutMeasuredSizes()
{
    // Rows nobody measured yet must not be stored one by one: the index only
    // pays for the blocks (1,000,000 / 1024 rounded up) plus the exceptions.
    BlockSizeIndex index(1000000, 48, 1024);
    QCOMPARE(index.explicitSizeCount(), qsizetype(0));
    QCOMPARE(index.blockCount(), qsizetype(977));
    QCOMPARE(index.sizeOf(0), 48);
    QCOMPARE(index.sizeOf(999999), 48);
    QCOMPARE(index.sizes().size(), 1000000);

    // Measuring one row records exactly one exception.
    index.setSize(123456, 90);
    QCOMPARE(index.explicitSizeCount(), qsizetype(1));
    QCOMPARE(index.sizeOf(123456), 90);
    QCOMPARE(index.totalSize(), qint64(999999) * 48 + 90);
    QCOMPARE(index.offsetOf(123457), qint64(123456) * 48 + 90);
    QCOMPARE(index.indexAt(qint64(123456) * 48 + 90), qsizetype(123457));

    // Measuring it back to the estimate releases the entry again.
    index.setSize(123456, 48);
    QCOMPARE(index.explicitSizeCount(), qsizetype(0));
    QCOMPARE(index.totalSize(), qint64(1000000) * 48);
}

void TestSizeIndex::blockIndexKeepsMeasuredSizesAcrossInserts()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    index.reset(6, 20);
    index.setSize(2, 50);
    index.setSize(4, 30);
    QCOMPARE(index.explicitSizeCount(), qsizetype(2));

    // insert() moves the estimate. Rows measured before the change must keep
    // their size, and rows that were never measured keep the reset estimate.
    index.insert(6, 2, 12);
    QCOMPARE(index.count(), qsizetype(8));
    QCOMPARE(index.estimatedSize(), 12);
    QCOMPARE(index.explicitSizeCount(), qsizetype(2));
    QCOMPARE(index.sizeOf(0), 20);
    QCOMPARE(index.sizeOf(2), 50);
    QCOMPARE(index.sizeOf(4), 30);
    QCOMPARE(index.sizeOf(5), 20);
    QCOMPARE(index.sizeOf(6), 12);
    QCOMPARE(index.sizeOf(7), 12);
    QCOMPARE(index.totalSize(), qint64(4 * 20 + 50 + 30 + 2 * 12));
    QCOMPARE(index.offsetOf(2), qint64(40));
    QCOMPARE(index.indexAt(89), qsizetype(2));
    QCOMPARE(index.indexAt(90), qsizetype(3));

    // A row measured to a value that happens to equal its block base carries no
    // exception (the entry is released), even when it was explicitly set.
    index.setSize(2, 20);
    QCOMPARE(index.explicitSizeCount(), qsizetype(1));
    QCOMPARE(index.sizeOf(2), 20);
    QCOMPARE(index.totalSize(), qint64(6 * 20 + 10 + 2 * 12));
}

void TestSizeIndex::blockIndexMeasuredSizesSurviveSplices()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    QVector<int> reference;
    index.reset(10, 10);
    for (int i = 0; i < 10; ++i)
        reference.append(10);
    index.setSize(7, 55);
    reference[7] = 55;

    // Removing rows in front of a measured row, shifting its block and then
    // inserting before it must all keep that row's measured size.
    index.remove(0, 3);
    reference.remove(0, 3);
    index.insert(1, 4, 9);
    for (int i = 0; i < 4; ++i)
        reference.insert(1, 9);
    index.setSize(10, 0);
    reference[10] = 0;

    QCOMPARE(index.count(), qsizetype(reference.size()));
    QCOMPARE(index.sizes(), reference);
    QCOMPARE(index.sizeOf(8), 55); // the measured row moved from 7 to 8

    qint64 offset = 0;
    for (qsizetype row = 0; row < index.count(); ++row) {
        QCOMPARE(index.offsetOf(row), offset);
        if (index.sizeOf(row) > 0)
            QCOMPARE(index.indexAt(offset), row);
        offset += index.sizeOf(row);
    }
    QCOMPARE(index.totalSize(), offset);
}

void TestSizeIndex::blockIndexAppendManyRowsKeepsBlockCountBounded()
{
    BlockSizeIndex index(0, 0, kTestBlockCapacity);
    for (int i = 0; i < 100; ++i)
        index.insert(index.count(), 1, 10);

    QCOMPARE(index.count(), qsizetype(100));
    // Appending row by row must not fragment the index into 100 blocks.
    QVERIFY(index.blockCount() <= 100 / kTestBlockCapacity + 1);
    QCOMPARE(index.explicitSizeCount(), qsizetype(0));
    QCOMPARE(index.totalSize(), qint64(1000));
    QCOMPARE(index.offsetOf(99), qint64(990));
    QCOMPARE(index.indexAt(999), qsizetype(99));
}

void TestSizeIndex::blockIndexBoundsEveryBlockAfterAHugeInsert()
{
    // Splitting a huge block in half leaves a huge *tail* behind: the split loop
    // has to keep processing what it creates, otherwise the "per-block scan stays
    // bounded" invariant is broken by a single big insert.
    {
        BlockSizeIndex small(100, 10, kTestBlockCapacity);
        small.insert(50, 10000, 7);
        QCOMPARE(small.count(), qsizetype(10100));
        QVERIFY(small.maxBlockRowCount() <= 2 * kTestBlockCapacity);
        QCOMPARE(small.sizeOf(0), 10);
        QCOMPARE(small.sizeOf(49), 10);
        QCOMPARE(small.sizeOf(50), 7);
        QCOMPARE(small.sizeOf(10049), 7);
        QCOMPARE(small.sizeOf(10050), 10);
        QCOMPARE(small.totalSize(), qint64(50 * 10 + 10000 * 7 + 50 * 10));
        QCOMPARE(small.offsetOf(10050), qint64(50 * 10 + 10000 * 7));
        QCOMPARE(small.indexAt(50 * 10 + 10000 * 7), qsizetype(10050));
    }
    {
        // The same invariant at a realistic capacity: one million rows at once.
        BlockSizeIndex index(100, 10);
        index.insert(50, 1000000, 7);
        QCOMPARE(index.count(), qsizetype(1000100));
        QVERIFY(index.maxBlockRowCount() <= 2 * index.blockCapacity());
        QCOMPARE(index.sizeOf(50), 7);
        QCOMPARE(index.sizeOf(1000049), 7);
        QCOMPARE(index.sizeOf(1000050), 10);
        QCOMPARE(index.totalSize(), qint64(50 * 10 + 1000000 * 7 + 50 * 10));
        QCOMPARE(index.offsetOf(1000050), qint64(50 * 10 + 1000000 * 7));
        QCOMPARE(index.indexAt(50 * 10 + 1000000 * 7), qsizetype(1000050));
        QCOMPARE(index.indexAt(50 * 10 + 1000000 * 7 - 1), qsizetype(1000049));
    }
}

QTEST_APPLESS_MAIN(TestSizeIndex)

#include "tst_sizeindex.moc"
