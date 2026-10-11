#include "../../src/layout/masonrylayout_p.h"

#include <QtTest>
#include <limits>

class MasonryLayoutTest : public QObject
{
    Q_OBJECT
private slots:
    void shortestColumnPlacement();
    void exactColumnWidthsAndMargins();
    void fixedColumnCountRespectsMinimumWidth();
    void zeroWidthAndEmptyModelAreSafe();
    void invalidSpacingAndHeightsClamp();
    void extremeGeometryArithmeticDoesNotOverflow();
    void hugeViewportDoesNotCreateEmptyColumns();
    void aspectAndEstimatedHeights();
    void invalidEstimatesUseConfiguredFallback();
    void sizeUpdateRebuildsOnlySuffixGeometry();
    void estimateUpdateKeepsFallbackForUnestimatedPrefix();
    void sparseEstimatesRebuildFromFirstChangedItem();
    void hitTestingRejectsGaps();
    void rangeQueryIsSorted();
    void rangeOverscanIncludesColumnNeighbors();
    void gapDropCandidatesUseColumnSearch();
    void mutationsPreserveMetadata();
    void pageNeighborUsesGeometry();
    void largeModelGeometryRemainsBounded();
    void largeOffsetsKeepQueriesAndSuffixRebuilds_data();
    void largeOffsetsKeepQueriesAndSuffixRebuilds();
    void extremeHeightConversionAndProjectionAreSafe();
};

void MasonryLayoutTest::shortestColumnPlacement()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(620);
    layout.setWidthRange(100, 200);
    layout.setSpacing(10, 10);
    layout.resetItems(5, 100);
    layout.setItemSize(0, 100);
    layout.setItemSize(1, 200);
    layout.setItemSize(2, 80);
    layout.setItemSize(3, 120);
    layout.setItemSize(4, 70);

    QCOMPARE(layout.columnCount(), 3);
    QCOMPARE(layout.itemColumn(0), 0);
    QCOMPARE(layout.itemColumn(1), 1);
    QCOMPARE(layout.itemColumn(2), 2);
    QCOMPARE(layout.itemColumn(3), 2);
    QCOMPARE(layout.itemColumn(4), 0);
    QCOMPARE(layout.logicalItemRect(3).top(), 90);
    QCOMPARE(layout.logicalItemRect(4).top(), 110);
    QCOMPARE(layout.contentExtent(), qint64(210));
}

void MasonryLayoutTest::exactColumnWidthsAndMargins()
{
    viv::MasonryLayout layout;
    layout.setMargins(QMargins(8, 4, 12, 6));
    layout.setSpacing(0, 7);
    layout.setCrossExtent(501);
    layout.setWidthRange(100, 200);
    layout.resetItems(4, 100);

    QCOMPARE(layout.columnCount(), 3);
    QCOMPARE(layout.columnWidth(0), 156);
    QCOMPARE(layout.columnWidth(1), 156);
    QCOMPARE(layout.columnWidth(2), 155);
    QCOMPARE(layout.columnX(0), 8);
    QCOMPARE(layout.columnX(1), 171);
    QCOMPARE(layout.columnX(2), 334);
    QCOMPARE(layout.columnX(2) + layout.columnWidth(2), 489);
}

void MasonryLayoutTest::fixedColumnCountRespectsMinimumWidth()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(420);
    layout.setWidthRange(100, 200);
    layout.setSpacing(10, 10);
    layout.setColumnCount(20);
    layout.resetItems(2, 100);

    QCOMPARE(layout.columnCount(), 3);
    for (int column = 0; column < layout.columnCount(); ++column)
        QVERIFY(layout.columnWidth(column) >= 100);
}

void MasonryLayoutTest::zeroWidthAndEmptyModelAreSafe()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(0);
    layout.setWidthRange(100, 200);
    layout.resetItems(4, 120);

    QCOMPARE(layout.columnCount(), 0);
    QCOMPARE(layout.contentExtent(), qint64(0));
    QCOMPARE(layout.itemAtPoint(QPoint(0, 0), 0), qsizetype(-1));

    layout.setCrossExtent(320);
    layout.resetItems(0, 120);
    QVERIFY(layout.columnCount() > 0);
    QCOMPARE(layout.contentExtent(), qint64(0));
    QVERIFY(layout.itemsInVerticalRange(0, 100).isEmpty());
}

void MasonryLayoutTest::invalidSpacingAndHeightsClamp()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(320);
    layout.setSpacing(-10, -20);
    layout.setFixedHeight(-5);
    layout.resetItems(1, 100);

    QCOMPARE(layout.rowSpacing(), 0);
    QCOMPARE(layout.columnSpacing(), 0);
    QCOMPARE(layout.fixedHeight(), 1);
    QCOMPARE(layout.itemSize(0), 1);

    const qreal ratio = layout.aspectRatio();
    layout.setAspectRatio(0);
    QCOMPARE(layout.aspectRatio(), ratio);
}

void MasonryLayoutTest::extremeGeometryArithmeticDoesNotOverflow()
{
    viv::MasonryLayout layout;
    const int maxInt = std::numeric_limits<int>::max();
    layout.setCrossExtent(maxInt);
    layout.setWidthRange(maxInt, maxInt);
    layout.setSpacing(0, maxInt);
    layout.resetItems(1, 100);

    QCOMPARE(layout.columnCount(), 1);
    QCOMPARE(layout.columnX(0), 0);
    QCOMPARE(layout.columnWidth(0), maxInt);
    QVERIFY(layout.itemAtPoint(QPoint(0, 0), 0) >= 0);

    layout.setMargins(QMargins(maxInt, 0, maxInt, 0));
    QCOMPARE(layout.columnCount(), 0);
    QCOMPARE(layout.columnWidth(0), 0);
    QCOMPARE(layout.columnX(0), -1);
    QCOMPARE(layout.itemAtPoint(QPoint(0, 0), 0), qsizetype(-1));
}

void MasonryLayoutTest::hugeViewportDoesNotCreateEmptyColumns()
{
    viv::MasonryLayout layout;
    layout.setWidthRange(100, 200);
    layout.setCrossExtent(std::numeric_limits<int>::max());
    layout.resetItems(2, 100);

    QVERIFY(layout.columnCount() > 0);
    QVERIFY(layout.columnCount() <= 4096);
    QVERIFY(layout.columnWidth(0) > 0);
    QVERIFY(layout.columnWidth(1) > 0);

    layout.resetItems(0, 100);
    QVERIFY(layout.columnCount() > 0);
    QCOMPARE(layout.contentExtent(), qint64(0));
}

void MasonryLayoutTest::aspectAndEstimatedHeights()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(400);
    layout.setWidthRange(100, 200);
    layout.resetItems(3, 100);
    layout.setHeightMode(viv::MasonryLayout::HeightMode::AspectRatio);
    layout.setAspectRatio(2.0);
    QCOMPARE(layout.columnCount(), 2);
    QCOMPARE(layout.itemSize(0), 100);

    layout.setHeightMode(viv::MasonryLayout::HeightMode::Estimated);
    layout.setEstimatedSize(0, QSize(100, 300));
    layout.setEstimatedSize(1, QSize(200, 100));
    QCOMPARE(layout.itemSize(0), 600);
    QCOMPARE(layout.itemSize(1), 100);
}

void MasonryLayoutTest::invalidEstimatesUseConfiguredFallback()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(200);
    layout.setColumnCount(1);
    layout.setFixedHeight(73);
    layout.setHeightMode(viv::MasonryLayout::HeightMode::Estimated);
    layout.resetItems(5, 900);
    for (int item = 0; item < 5; ++item)
        QCOMPARE(layout.itemSize(item), 73);
    layout.setEstimatedSizes({QSize(200, 400), QSize(0, 999), QSize(-1, 90), QSize(200, 0)});
    QCOMPARE(layout.itemSize(0), 400);
    for (int item = 1; item < 5; ++item)
        QCOMPARE(layout.itemSize(item), 73);
    layout.setItemSize(2, 111);
    layout.setFixedHeight(91);
    QCOMPARE(layout.itemSize(0), 400);
    QCOMPARE(layout.itemSize(1), 91);
    QCOMPARE(layout.itemSize(2), 111);
    layout.setEstimatedSize(0, QSize());
    QCOMPARE(layout.itemSize(0), 91);
    layout.setItemEstimates({{1, QSize(200, 160)}, {3, QSize(200, -1)}});
    QCOMPARE(layout.itemSize(1), 160);
    QCOMPARE(layout.itemSize(3), 91);
    layout.insertItems(1, 1, 600);
    QCOMPARE(layout.itemSize(1), 91);
    QCOMPARE(layout.itemSize(2), 160);
    QCOMPARE(layout.itemSize(3), 111);
    layout.resetItems(2, 700);
    QCOMPARE(layout.itemSize(0), 91);
    QCOMPARE(layout.itemSize(1), 91);
    layout.setEstimatedSizes({QSize(-100, 1000)});
    QCOMPARE(layout.itemSize(0), 91);
    QCOMPARE(layout.itemSize(1), 91);
}

void MasonryLayoutTest::sizeUpdateRebuildsOnlySuffixGeometry()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(620);
    layout.setWidthRange(100, 200);
    layout.setSpacing(10, 10);
    layout.resetItems(8, 100);

    const QVector<QRect> prefixBefore = {
        layout.logicalItemRect(0), layout.logicalItemRect(1), layout.logicalItemRect(2)};
    layout.setItemSize(5, 240);

    for (int item = 0; item < prefixBefore.size(); ++item)
        QCOMPARE(layout.logicalItemRect(item), prefixBefore.at(item));
    QCOMPARE(layout.itemSize(5), 240);
    QVERIFY(layout.contentExtent() >= layout.logicalItemRect(5).bottom() + 1);
}

void MasonryLayoutTest::estimateUpdateKeepsFallbackForUnestimatedPrefix()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(420);
    layout.setWidthRange(100, 200);
    layout.setHeightMode(viv::MasonryLayout::HeightMode::Estimated);
    layout.resetItems(4, 100);
    QCOMPARE(layout.itemSize(0), 100);

    layout.setEstimatedSize(3, QSize(100, 180));
    QCOMPARE(layout.itemSize(0), 100);
    QCOMPARE(layout.estimatedSize(3), QSize(100, 180));
}

void MasonryLayoutTest::sparseEstimatesRebuildFromFirstChangedItem()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(420);
    layout.setColumnCount(2);
    layout.setHeightMode(viv::MasonryLayout::HeightMode::Estimated);
    layout.setFixedHeight(80);
    layout.resetItems(8, 80);
    const QRect prefix = layout.logicalItemRect(0);
    layout.setItemEstimates({{3, QSize(210, 140)}, {1, QSize(210, 180)}});

    QCOMPARE(layout.logicalItemRect(0), prefix);
    QCOMPARE(layout.itemSize(0), 80);
    QCOMPARE(layout.itemSize(1), 180);
    QCOMPARE(layout.itemSize(3), 140);
    QCOMPARE(layout.logicalItemRect(2).top(), 80);
    QCOMPARE(layout.logicalItemRect(3).top(), 160);
    QCOMPARE(layout.logicalItemRect(4).top(), 180);
}

void MasonryLayoutTest::hitTestingRejectsGaps()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(420);
    layout.setWidthRange(100, 200);
    layout.setSpacing(12, 12);
    layout.resetItems(4, 100);
    layout.setItemSize(0, 100);
    layout.setItemSize(1, 200);

    const QRect first = layout.logicalItemRect(0);
    QCOMPARE(layout.itemAtPoint(first.center(), 0), qsizetype(0));
    QCOMPARE(layout.itemAtPoint(QPoint(first.right() + 1, first.center().y()), 0), qsizetype(-1));
    QCOMPARE(layout.itemAtPoint(QPoint(first.center().x(), first.bottom() + 1), 0), qsizetype(-1));
    QCOMPARE(layout.itemAtPoint(first.center() - QPoint(0, 30), 30), qsizetype(0));
}

void MasonryLayoutTest::rangeQueryIsSorted()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(620);
    layout.setWidthRange(100, 200);
    layout.resetItems(6, 100);
    layout.setItemSize(0, 200);
    layout.setItemSize(1, 80);
    layout.setItemSize(2, 120);
    const QVector<qsizetype> items = layout.itemsInVerticalRange(90, 210);
    QCOMPARE(items, QVector<qsizetype>({0, 2, 3, 4, 5}));
}

void MasonryLayoutTest::rangeOverscanIncludesColumnNeighbors()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(400);
    layout.setColumnCount(2);
    layout.setSpacing(20, 0);
    layout.setFixedHeight(80);
    layout.resetItems(12, 80);

    QCOMPARE(layout.itemsInVerticalRange(110, 179), QVector<qsizetype>({2, 3}));
    QCOMPARE(layout.itemsInVerticalRange(110, 179, 1, 2),
             QVector<qsizetype>({0, 1, 2, 3, 4, 5, 6, 7}));
    QVERIFY(layout.itemsInVerticalRange(80, 99).isEmpty());
    QCOMPARE(layout.itemsInVerticalRange(80, 99, 1, 1),
             QVector<qsizetype>({0, 1, 2, 3}));
    QCOMPARE(layout.itemsInVerticalRange(110, 179, std::numeric_limits<int>::max(),
                                        std::numeric_limits<int>::max()).size(), 12);
    QVERIFY(layout.itemsInVerticalRange(179, 110, 1, 1).isEmpty());
}

void MasonryLayoutTest::gapDropCandidatesUseColumnSearch()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(420);
    layout.setWidthRange(100, 200);
    layout.setSpacing(12, 12);
    layout.setColumnCount(1);
    layout.resetItems(4, 80);

    const QRect first = layout.logicalItemRect(0);
    const qsizetype next = layout.nextItemInColumn(0);
    QVERIFY(next >= 0);
    const qint64 gap = first.bottom() + 1;
    QCOMPARE(layout.itemBeforeOffset(gap, 0), qsizetype(0));
    QCOMPARE(layout.itemAfterOffset(gap, 0), next);
    QCOMPARE(layout.itemBeforeOffset(gap), qsizetype(0));
    QCOMPARE(layout.itemAfterOffset(gap), next);
}

void MasonryLayoutTest::mutationsPreserveMetadata()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(400);
    layout.setWidthRange(100, 200);
    layout.resetItems(3, 100);
    layout.setEstimatedSize(1, QSize(100, 250));
    layout.insertItems(1, 1, 100);
    QCOMPARE(layout.itemCount(), qsizetype(4));
    QCOMPARE(layout.estimatedSize(2), QSize(100, 250));
    layout.moveItems(2, 1, 0);
    QCOMPARE(layout.estimatedSize(0), QSize(100, 250));
    layout.removeItems(0, 1);
    QCOMPARE(layout.itemCount(), qsizetype(3));
    QCOMPARE(layout.estimatedSize(1), QSize());
}

void MasonryLayoutTest::pageNeighborUsesGeometry()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(420);
    layout.setWidthRange(100, 200);
    layout.setSpacing(10, 10);
    layout.resetItems(8, 100);
    layout.setItemSize(0, 180);
    layout.setItemSize(1, 60);
    layout.setItemSize(2, 120);

    const qsizetype pageDown = layout.nearestByVerticalOffset(0, 150);
    const qsizetype pageUp = layout.nearestByVerticalOffset(pageDown, -150);
    QVERIFY(pageDown >= 0);
    QVERIFY(pageUp >= 0);
    QVERIFY(pageDown != 0);
    QCOMPARE(layout.logicalItemRect(pageUp).center().y(),
             layout.logicalItemRect(0).center().y());
}

void MasonryLayoutTest::largeModelGeometryRemainsBounded()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(1600);
    layout.setWidthRange(100, 200);
    layout.setSpacing(8, 8);
    layout.resetItems(10000, 120);

    QCOMPARE(layout.columnCount(), 8);
    QVERIFY(layout.contentExtent() > 0);
    QVERIFY(layout.contentExtent() < qint64(1) << 31);
    const QVector<qsizetype> visible = layout.itemsInVerticalRange(0, 1000);
    QVERIFY(!visible.isEmpty());
    for (int i = 1; i < visible.size(); ++i)
        QVERIFY(visible.at(i - 1) < visible.at(i));
    for (qsizetype item : visible) {
        QVERIFY(layout.itemColumn(item) >= 0);
        QVERIFY(layout.logicalItemRect(item).isValid());
    }
}

void MasonryLayoutTest::largeOffsetsKeepQueriesAndSuffixRebuilds_data()
{
    QTest::addColumn<int>("height");
    QTest::addColumn<int>("count");
    QTest::newRow("beyond-widget-coordinates") << 1000000 << 126;
    QTest::newRow("beyond-int-coordinates") << 1000000000 << 30;
}

void MasonryLayoutTest::largeOffsetsKeepQueriesAndSuffixRebuilds()
{
    QFETCH(int, height);
    QFETCH(int, count);
    viv::MasonryLayout layout;
    layout.setCrossExtent(620);
    layout.setColumnCount(3);
    layout.setSpacing(7, 10);
    layout.setFixedHeight(height);
    layout.resetItems(count, height);
    const qint64 stride = qint64(height) + 7;
    const qsizetype target = height == 1000000 ? 90 : 18;
    const qint64 top = qint64(target / 3) * stride;
    QVERIFY(top > (qint64(1) << 24));
    if (height == 1000000000)
        QVERIFY(top > std::numeric_limits<int>::max());
    QCOMPARE(layout.offsetOf(target), top);
    QCOMPARE(layout.contentExtent(), qint64(count / 3) * stride - 7);
    QCOMPARE(layout.itemRect(target, top), QRect(0, 0, 200, height));
    QCOMPARE(layout.itemAtPoint(QPoint(10, 17), top), target);
    QCOMPARE(layout.itemsInVerticalRange(top + 17, top + 100),
             QVector<qsizetype>({target, target + 1, target + 2}));
    QCOMPARE(layout.itemsAtOrAfterOffset(top + 17),
             QVector<qsizetype>({target, target + 1, target + 2}));
    QCOMPARE(layout.indexAtOffset(top + 17), target);
    QCOMPARE(layout.lastIndexAtOffset(top + 17), target + 2);
    QCOMPARE(layout.nearestVertical(target, true), target + 3);
    QCOMPARE(layout.nearestVertical(target, false), target - 3);
    QCOMPARE(layout.nearestHorizontal(target, true), target + 1);
    QCOMPARE(layout.nearestHorizontal(target + 1, false), target);
    QCOMPARE(layout.nearestByVerticalOffset(target, stride * 2), target + 6);
    QCOMPARE(layout.nearestByVerticalOffset(target, -stride * 2), target - 6);
    QCOMPARE(layout.nearestByVerticalOffset(target, std::numeric_limits<qint64>::max()),
             qsizetype(count - 3));
    QCOMPARE(layout.nearestByVerticalOffset(target, std::numeric_limits<qint64>::min()), qsizetype(0));
    const qint64 gap = top + height + 2;
    QCOMPARE(layout.itemAtPoint(QPoint(10, 0), gap), qsizetype(-1));
    QCOMPARE(layout.itemBeforeOffset(gap, 0), target);
    QCOMPARE(layout.itemAfterOffset(gap, 0), target + 3);
    QCOMPARE(layout.nextItemInColumn(target), target + 3);

    const qint64 prefix = layout.offsetOf(target - 1);
    layout.setItemSize(target, height + 31);
    layout.setItemSize(target + 1, height - 23);
    QCOMPARE(layout.offsetOf(target - 1), prefix);
    QCOMPARE(layout.itemColumn(target + 3), 1);
    QCOMPARE(layout.offsetOf(target + 3), top + stride - 23);
    QCOMPARE(layout.itemColumn(target + 4), 2);
    QCOMPARE(layout.offsetOf(target + 4), top + stride);
    QCOMPARE(layout.itemColumn(target + 5), 0);
    QCOMPARE(layout.offsetOf(target + 5), top + stride + 31);
    QCOMPARE(layout.contentExtent(), qint64(count / 3) * stride - 7 + 31);
    QCOMPARE(layout.itemsInVerticalRange(top + stride + 50, top + stride + 100),
             QVector<qsizetype>({target + 3, target + 4, target + 5}));
    QCOMPARE(layout.itemAtPoint(QPoint(layout.columnX(1) + 10, 0), top + stride - 23),
             target + 3);
}

void MasonryLayoutTest::extremeHeightConversionAndProjectionAreSafe()
{
    viv::MasonryLayout layout;
    layout.setCrossExtent(200);
    layout.setColumnCount(1);
    layout.setHeightMode(viv::MasonryLayout::HeightMode::AspectRatio);
    layout.setAspectRatio(std::numeric_limits<qreal>::min());
    layout.resetItems(3, 100);
    const int maxHeight = std::numeric_limits<int>::max();
    QCOMPARE(layout.itemSize(0), maxHeight);
    QCOMPARE(layout.offsetOf(2), qint64(maxHeight) * 2);
    QCOMPARE(layout.contentExtent(), qint64(maxHeight) * 3);
    const qint64 offset = layout.offsetOf(2);
    QCOMPARE(layout.itemRect(2, offset).top(), 0);
    QCOMPARE(layout.itemRect(2, offset).height(), maxHeight);
    QCOMPARE(layout.itemAtPoint(QPoint(10, 10), offset), qsizetype(2));
    QVERIFY(!layout.itemRect(0, offset).contains(QPoint(10, 10)));
    QVERIFY(!layout.itemRect(2, 0).contains(QPoint(10, 10)));
    QVERIFY(!layout.itemRect(0, std::numeric_limits<qint64>::max()).contains(QPoint(10, 10)));
    QVERIFY(!layout.itemRect(0, std::numeric_limits<qint64>::min()).contains(QPoint(10, 10)));

    layout.setHeightMode(viv::MasonryLayout::HeightMode::Estimated);
    layout.setEstimatedSizes({QSize(1, maxHeight), QSize(1, maxHeight), QSize(1, maxHeight)});
    QCOMPARE(layout.itemSize(2), maxHeight);
    QCOMPARE(layout.contentExtent(), qint64(maxHeight) * 3);
    layout.setCrossExtent(0);
    QCOMPARE(layout.contentExtent(), qint64(0));
    QCOMPARE(layout.offsetOf(0), qint64(0));
    QCOMPARE(layout.itemSize(0), 0);
    QCOMPARE(layout.itemColumn(0), -1);
    QVERIFY(layout.itemRect(0, 0).isEmpty());
    layout.setCrossExtent(200);
    QCOMPARE(layout.itemSize(2), maxHeight);
}

QTEST_MAIN(MasonryLayoutTest)
#include "tst_masonrylayout.moc"
