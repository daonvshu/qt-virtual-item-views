#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/listlayout.h>

#include <QtTest>

using namespace viv;

namespace {
/// Counts destructions so a test can prove that an index handed to a policy was
/// released (docs/api-stability.md: no "accepted but ignored" entry points).
class CountingSizeIndex : public BlockSizeIndex
{
public:
    CountingSizeIndex() : BlockSizeIndex(0, 10, 4) {}
    ~CountingSizeIndex() override { ++destroyed; }

    static int destroyed;
};

int CountingSizeIndex::destroyed = 0;

/// A policy with no size index at all: it ignores the model but still owns what
/// it is told to own.
class BarePolicy : public LayoutPolicy
{
public:
    Qt::Orientation orientation() const override { return Qt::Vertical; }
    qsizetype itemCount() const override { return 0; }
    int itemSize(qsizetype) const override { return 0; }
    qint64 offsetOf(qsizetype) const override { return 0; }
    qsizetype indexAtOffset(qint64) const override { return 0; }
    qint64 contentExtent() const override { return 0; }
    int crossExtent() const override { return 0; }
    void setCrossExtent(int) override {}
    QRect itemRect(qsizetype, qint64) const override { return QRect(); }
    qsizetype itemAtPoint(const QPoint &, qint64) const override { return -1; }
    bool isVariableSized() const override { return false; }
    void resetItems(qsizetype, int) override {}
    void insertItems(qsizetype, qsizetype, int) override {}
    void removeItems(qsizetype, qsizetype) override {}
    void moveItems(qsizetype, qsizetype, qsizetype) override {}
    void setItemSize(qsizetype, int) override {}
};
} // namespace

class TestListLayout : public QObject
{
    Q_OBJECT

private slots:
    void uniformItemGeometry();
    void contentExtentIncludesMargins();
    void variableSizesFollowSizeIndex();
    void itemAtPointHitTest();
    void itemAtPointOutsideContent();
    void insertAndRemoveKeepOffsets();
    void moveItemsKeepsSizes();
    void resetItemsUsesEstimate();
    void horizontalOrientationUsesWidth();
    void policyWithoutSizeIndexReleasesOwnership();
};

void TestListLayout::uniformItemGeometry()
{
    ListLayout layout(new FixedSizeIndex(0, 40), Qt::Vertical);
    layout.setCrossExtent(300);
    layout.resetItems(100, 40);

    QCOMPARE(layout.itemCount(), qsizetype(100));
    QCOMPARE(layout.contentExtent(), qint64(4000));
    QCOMPARE(layout.itemSize(5), 40);
    QCOMPARE(layout.itemRect(0, 0), QRect(0, 0, 300, 40));
    QCOMPARE(layout.itemRect(2, 0), QRect(0, 80, 300, 40));
    QCOMPARE(layout.itemRect(2, 100), QRect(0, -20, 300, 40));
    QVERIFY(!layout.isVariableSized());
}

void TestListLayout::contentExtentIncludesMargins()
{
    ListLayout layout(new FixedSizeIndex(0, 10), Qt::Vertical);
    layout.setViewportMargins(QMargins(4, 6, 4, 8));
    layout.setCrossExtent(200);
    layout.resetItems(10, 10);

    QCOMPARE(layout.contentExtent(), qint64(114));
    QCOMPARE(layout.itemRect(0, 0), QRect(4, 6, 192, 10));
    QCOMPARE(layout.itemRect(1, 0), QRect(4, 16, 192, 10));
}

void TestListLayout::variableSizesFollowSizeIndex()
{
    auto *index = new BlockSizeIndex(0, 20, 4);
    ListLayout layout(index, Qt::Vertical);
    layout.setCrossExtent(400);
    layout.resetItems(5, 20);
    index->setSize(1, 60);

    QVERIFY(layout.isVariableSized());
    QCOMPARE(layout.offsetOf(0), qint64(0));
    QCOMPARE(layout.offsetOf(1), qint64(20));
    QCOMPARE(layout.offsetOf(2), qint64(80));
    QCOMPARE(layout.contentExtent(), qint64(140));
    QCOMPARE(layout.indexAtOffset(79), qsizetype(1));
    QCOMPARE(layout.indexAtOffset(80), qsizetype(2));
}

void TestListLayout::itemAtPointHitTest()
{
    ListLayout layout(new FixedSizeIndex(0, 25), Qt::Vertical);
    layout.setCrossExtent(200);
    layout.resetItems(10, 25);

    QCOMPARE(layout.itemAtPoint(QPoint(10, 0), 0), qsizetype(0));
    QCOMPARE(layout.itemAtPoint(QPoint(10, 24), 0), qsizetype(0));
    QCOMPARE(layout.itemAtPoint(QPoint(10, 25), 0), qsizetype(1));
    QCOMPARE(layout.itemAtPoint(QPoint(10, 30), 100), qsizetype(5));
}

void TestListLayout::itemAtPointOutsideContent()
{
    ListLayout layout(new FixedSizeIndex(0, 25), Qt::Vertical);
    layout.setCrossExtent(200);
    layout.resetItems(4, 25);

    QCOMPARE(layout.itemAtPoint(QPoint(-1, 10), 0), qsizetype(-1));
    QCOMPARE(layout.itemAtPoint(QPoint(250, 10), 0), qsizetype(-1));
    QCOMPARE(layout.itemAtPoint(QPoint(10, -5), 0), qsizetype(-1));
    QCOMPARE(layout.itemAtPoint(QPoint(10, 100), 0), qsizetype(-1));
    QCOMPARE(layout.itemAtPoint(QPoint(10, 0), 1000), qsizetype(-1));

    ListLayout empty(new FixedSizeIndex(0, 25), Qt::Vertical);
    empty.setCrossExtent(200);
    QCOMPARE(empty.itemAtPoint(QPoint(10, 10), 0), qsizetype(-1));
}

void TestListLayout::insertAndRemoveKeepOffsets()
{
    auto *index = new FixedSizeIndex(0, 30);
    ListLayout layout(index, Qt::Vertical);
    layout.setCrossExtent(100);
    layout.resetItems(4, 30);

    layout.insertItems(2, 2, 30);
    QCOMPARE(layout.itemCount(), qsizetype(6));
    QCOMPARE(layout.contentExtent(), qint64(180));

    layout.removeItems(0, 3);
    QCOMPARE(layout.itemCount(), qsizetype(3));
    QCOMPARE(layout.contentExtent(), qint64(90));
    QCOMPARE(layout.offsetOf(1), qint64(30));
}

void TestListLayout::moveItemsKeepsSizes()
{
    auto *index = new BlockSizeIndex(0, 10, 4);
    ListLayout layout(index, Qt::Vertical);
    layout.setCrossExtent(100);
    layout.resetItems(4, 10);
    index->setSize(0, 10);
    index->setSize(1, 20);
    index->setSize(2, 30);
    index->setSize(3, 40);

    // Move row 0 before row 3 (pre-move coordinates) => 1, 2, 0, 3
    layout.moveItems(0, 1, 3);
    QCOMPARE(layout.itemCount(), qsizetype(4));
    QCOMPARE(layout.itemSize(0), 20);
    QCOMPARE(layout.itemSize(1), 30);
    QCOMPARE(layout.itemSize(2), 10);
    QCOMPARE(layout.itemSize(3), 40);
    QCOMPARE(layout.contentExtent(), qint64(100));
}

void TestListLayout::resetItemsUsesEstimate()
{
    auto *index = new BlockSizeIndex(0, 10, 4);
    ListLayout layout(index, Qt::Vertical);
    layout.setCrossExtent(100);
    layout.resetItems(3, 15);

    QCOMPARE(layout.estimate(), 15);
    QCOMPARE(layout.contentExtent(), qint64(45));

    layout.insertItems(0, 2, 0); // no estimate: falls back to the layout estimate
    QCOMPARE(layout.itemCount(), qsizetype(5));
    QCOMPARE(layout.contentExtent(), qint64(75));
}

void TestListLayout::horizontalOrientationUsesWidth()
{
    ListLayout layout(new FixedSizeIndex(0, 50), Qt::Horizontal);
    layout.setCrossExtent(120);
    layout.resetItems(4, 50);

    QCOMPARE(layout.contentExtent(), qint64(200));
    QCOMPARE(layout.itemRect(1, 20), QRect(30, 0, 50, 120));
    QCOMPARE(layout.itemAtPoint(QPoint(30, 10), 20), qsizetype(1));
    QCOMPARE(layout.itemAtPoint(QPoint(30, 200), 20), qsizetype(-1));
}

void TestListLayout::policyWithoutSizeIndexReleasesOwnership()
{
    CountingSizeIndex::destroyed = 0;
    BarePolicy policy;

    policy.setSizeIndex(new CountingSizeIndex(), true);
    QCOMPARE(CountingSizeIndex::destroyed, 1);

    // Without ownership the caller keeps the object, so the policy must not
    // delete it.
    auto *kept = new CountingSizeIndex();
    policy.setSizeIndex(kept, false);
    QCOMPARE(policy.sizeIndex(), nullptr);
    QCOMPARE(CountingSizeIndex::destroyed, 1);
    delete kept;
    QCOMPARE(CountingSizeIndex::destroyed, 2);

    // A layout that does have an index model keeps and uses it.
    ListLayout layout(new CountingSizeIndex(), Qt::Vertical);
    layout.setCrossExtent(100);
    layout.resetItems(3, 10);
    QCOMPARE(layout.itemCount(), qsizetype(3));
    QCOMPARE(CountingSizeIndex::destroyed, 2);
}

QTEST_APPLESS_MAIN(TestListLayout)

#include "tst_listlayout.moc"
