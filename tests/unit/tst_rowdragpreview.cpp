#include "../../src/core/rowdragpreview_p.h"
#include <QtTest>

class TestRowDragPreview : public QObject
{
    Q_OBJECT
private slots:
    void insertionOffsets_data();
    void insertionOffsets();
    void retargetKeepsContinuity();
    void newlyVisibleRowsAndClear();
};

void TestRowDragPreview::insertionOffsets_data()
{
    QTest::addColumn<int>("source");
    QTest::addColumn<int>("destination");
    QTest::addColumn<int>("row");
    QTest::addColumn<qreal>("expected");
    QTest::newRow("up-first") << 4 << 1 << 1 << qreal(43);
    QTest::newRow("up-neighbor") << 4 << 1 << 3 << qreal(43);
    QTest::newRow("up-outside") << 4 << 1 << 0 << qreal(0);
    QTest::newRow("down-neighbor") << 1 << 5 << 2 << qreal(-43);
    QTest::newRow("down-last") << 1 << 5 << 4 << qreal(-43);
    QTest::newRow("destination-exclusive") << 1 << 5 << 5 << qreal(0);
    QTest::newRow("source-stays") << 1 << 5 << 1 << qreal(0);
    QTest::newRow("same-position") << 1 << 1 << 2 << qreal(0);
    QTest::newRow("adjacent-noop") << 1 << 2 << 2 << qreal(0);
}

void TestRowDragPreview::insertionOffsets()
{
    QFETCH(int, source);
    QFETCH(int, destination);
    QFETCH(int, row);
    QFETCH(qreal, expected);
    viv::RowDragPreview preview;
    preview.retarget(source, destination, 43, {row}, 1);
    QCOMPARE(preview.offset(row, 0), qreal(0));
    QCOMPARE(preview.offset(row, 0.5), expected / 2);
    QCOMPARE(preview.offset(row, 1), expected);
    QCOMPARE(preview.offset(row, 2), expected);
}

void TestRowDragPreview::retargetKeepsContinuity()
{
    viv::RowDragPreview preview;
    preview.retarget(3, 0, 31, {0, 1, 2, 3, 4, 5}, 1);
    const qreal before = preview.offset(1, 0.4);
    preview.retarget(3, 6, 31, {0, 1, 2, 3, 4, 5}, 0.4);
    QCOMPARE(preview.offset(1, 0), before);
    QCOMPARE(preview.offset(1, 1), qreal(0));
    QCOMPARE(preview.offset(4, 0), qreal(0));
    QCOMPARE(preview.offset(4, 1), qreal(-31));
    const qreal reversing = preview.offset(4, 0.25);
    preview.retarget(3, 1, 52, {0, 1, 2, 3, 4, 5}, 0.25);
    QCOMPARE(preview.offset(4, 0), reversing);
    QCOMPARE(preview.offset(4, 1), qreal(0));
    QCOMPARE(preview.offset(1, 1), qreal(52));
}

void TestRowDragPreview::newlyVisibleRowsAndClear()
{
    viv::RowDragPreview preview;
    QVERIFY(!preview.isActive());
    preview.retarget(1, 100, 47, {1, 2, 3}, 1);
    QCOMPARE(preview.offset(70, 0), qreal(-47));
    preview.clear();
    QVERIFY(!preview.isActive());
    QCOMPARE(preview.offset(70, 0.5), qreal(0));
    preview.retarget(3, 0, 29, {1}, 1);
    QCOMPARE(preview.offset(1, 0), qreal(0));
    QCOMPARE(preview.offset(1, 1), qreal(29));
}

QTEST_APPLESS_MAIN(TestRowDragPreview)
#include "tst_rowdragpreview.moc"
