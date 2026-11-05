#include "../../src/layout/flowtablelayout_p.h"
#include <virtualitemviews/listlayout.h>
#include <QtTest>
#include <limits>

class TestFlowTableLayout : public QObject
{
    Q_OBJECT
private slots:
    void fillsWidthAtThresholds()
    {
        viv::FlowTableLayout layout;
        layout.resetItems(100, 1);
        for (int spacing : {0, 1, 12, 150}) {
            layout.setSpacing(0, spacing);
            for (int width = 1; width <= 1200; ++width) {
                layout.setCrossExtent(width);
                int sum = 0;
                int minimum = std::numeric_limits<int>::max();
                int maximum = 0;
                for (int column = 0; column < layout.columnCount(); ++column) {
                    const int cell = layout.columnWidth(column);
                    QVERIFY(cell > 0);
                    minimum = qMin(minimum, cell);
                    maximum = qMax(maximum, cell);
                    QCOMPARE(layout.columnX(column), sum);
                    sum += cell + (column + 1 < layout.columnCount() ? spacing : 0);
                }
                QCOMPARE(sum, width);
                QVERIFY(maximum - minimum <= 1);
            }
        }
        layout.setSpacing(0, 0);
        for (const auto &example : {qMakePair(300, 2), qMakePair(400, 2), qMakePair(401, 3),
                                    qMakePair(600, 3), qMakePair(601, 4)}) {
            layout.setCrossExtent(example.first);
            QCOMPARE(layout.columnCount(), example.second);
        }
        layout.setWidthRange(1, 1);
        layout.setSpacing(0, 2);
        layout.setCrossExtent(2);
        QCOMPARE(layout.columnCount(), 1);
        QCOMPARE(layout.columnWidth(0), 2);
    }

    void hitTestingAndPartialLastRow()
    {
        viv::FlowTableLayout layout;
        layout.setCrossExtent(601);
        layout.setFixedHeight(30);
        layout.setSpacing(7, 9);
        layout.setMargins(QMargins(3, 5, 4, 6));
        layout.resetItems(10, 999);
        QCOMPARE(layout.columnCount(), 3);
        for (qsizetype item = 0; item < layout.itemCount(); ++item) {
            const QRect rect = layout.itemRect(item, 13);
            QCOMPARE(layout.itemAtPoint(rect.center(), 13), item);
            QCOMPARE(rect.height(), 30);
            if (item % layout.columnCount() + 1 < layout.columnCount())
                QCOMPARE(layout.itemAtPoint(QPoint(rect.right() + 1, rect.center().y()), 13), qsizetype(-1));
            if (item + layout.columnCount() < layout.itemCount())
                QCOMPARE(layout.itemAtPoint(QPoint(rect.center().x(), rect.bottom() + 1), 13), qsizetype(-1));
        }
        const QRect first = layout.itemRect(0, 0);
        const QRect last = layout.itemRect(9, 0);
        QCOMPARE(last.width(), first.width());
        QCOMPARE(layout.itemAtPoint(QPoint(layout.columnX(1) + 1, last.center().y()), 0), qsizetype(-1));
        QCOMPARE(layout.contentExtent(), qint64(5 + 4 * 30 + 3 * 7 + 6));
    }

    void heightModesAndVisibleRowBounds()
    {
        viv::FlowTableLayout layout;
        layout.setCrossExtent(401);
        layout.resetItems(10, 20);
        layout.setProportional(true);
        layout.setAspectRatio(2);
        QCOMPARE(layout.itemSize(0), 67);
        QCOMPARE(layout.itemSize(2), 67);
        QCOMPARE(layout.indexAtOffset(0), qsizetype(0));
        QCOMPARE(layout.lastIndexAtOffset(0), qsizetype(2));
        QCOMPARE(layout.indexAtOffset(67), qsizetype(3));
        QCOMPARE(layout.lastIndexAtOffset(67), qsizetype(5));
        QCOMPARE(layout.lastIndexAtOffset(layout.contentExtent() - 1), qsizetype(9));
        QCOMPARE(layout.indexAtOffset(layout.contentExtent()), qsizetype(10));
        layout.setFixedHeight(43);
        layout.setProportional(false);
        QCOMPARE(layout.itemSize(0), 43);
        layout.setItemSize(0, 200);
        QCOMPARE(layout.itemSize(0), 43);
        layout.setAspectRatio(0);
        QCOMPARE(layout.aspectRatio(), qreal(2));
        viv::ListLayout list;
        list.resetItems(10, 43);
        QCOMPARE(list.lastIndexAtOffset(44), list.indexAtOffset(44));
    }

    void outerHalfSpacingAndModeSwitch()
    {
        viv::FlowTableLayout layout;
        layout.setCrossExtent(610);
        layout.setFixedHeight(30);
        layout.setSpacing(19, 19);
        layout.setMargins(QMargins(3, 5, 7, 11));
        layout.resetItems(4, 1);
        const QRect edgeFirst = layout.itemRect(0, 0);
        const qint64 edgeExtent = layout.contentExtent();

        layout.setOuterSpacingEnabled(true);
        QCOMPARE(layout.outerSpacing(), QMargins(9, 9, 10, 10));
        QCOMPARE(layout.margins(), QMargins(3, 5, 7, 11));
        QCOMPARE(layout.columnCount(), 3);
        QCOMPARE(layout.itemRect(0, 0), QRect(12, 14, 181, 30));
        QCOMPARE(layout.itemRect(3, 0), QRect(12, 63, 181, 30));
        QCOMPARE(layout.contentExtent(), qint64(114));
        QCOMPARE(layout.offsetOf(layout.itemCount()), layout.contentExtent());
        QCOMPARE(layout.columnX(2) + layout.columnWidth(2) + layout.outerSpacing().right()
                     + layout.margins().right(), layout.crossExtent());

        // Half-gaps and the absent last-row cells are not interactive item areas.
        for (const QPoint &point : {QPoint(3, 20), QPoint(20, 5), QPoint(600, 20),
                                   QPoint(20, 100), QPoint(200, 20), QPoint(20, 50), QPoint(220, 70)}) {
            QCOMPARE(layout.itemAtPoint(point, 0), qsizetype(-1));
        }
        for (qsizetype item = 0; item < layout.itemCount(); ++item) {
            const QRect rect = layout.itemRect(item, 13);
            QCOMPARE(layout.itemAtPoint(rect.center(), 13), item);
        }
        QCOMPARE(layout.indexAtOffset(5), qsizetype(0));
        QCOMPARE(layout.indexAtOffset(63), qsizetype(3));
        QCOMPARE(layout.lastIndexAtOffset(100), qsizetype(3));

        layout.setOuterSpacingEnabled(false);
        QCOMPARE(layout.outerSpacing(), QMargins());
        QCOMPARE(layout.itemRect(0, 0), edgeFirst);
        QCOMPARE(layout.contentExtent(), edgeExtent);
        layout.setOuterSpacingEnabled(true);
        layout.setSpacing(0, 0);
        QCOMPARE(layout.outerSpacing(), QMargins());
        QCOMPARE(layout.itemRect(0, 0).topLeft(), QPoint(3, 5));
        layout.resetItems(0, 1);
        QCOMPARE(layout.contentExtent(), qint64(0));
    }

    void outerHalfSpacingAtNarrowWidths()
    {
        viv::FlowTableLayout layout;
        layout.setOuterSpacingEnabled(true);
        layout.setSpacing(19, 19);
        layout.resetItems(20, 1);
        for (int width : {1, 2, 9, 18, 19, 20, 201, 610}) {
            layout.setCrossExtent(width);
            int used = layout.outerSpacing().left();
            for (int column = 0; column < layout.columnCount(); ++column) {
                QCOMPARE(layout.columnX(column), used);
                QVERIFY(layout.columnWidth(column) > 0);
                used += layout.columnWidth(column);
                if (column + 1 < layout.columnCount())
                    used += layout.columnSpacing();
            }
            QCOMPARE(used + layout.outerSpacing().right(), width);
        }
        layout.setCrossExtent(0);
        QCOMPARE(layout.contentExtent(), qint64(0));
    }

    void mutationsEmptyAndLargeContent()
    {
        viv::FlowTableLayout layout;
        layout.setCrossExtent(800);
        layout.resetItems(10, 1);
        layout.insertItems(3, 4, 99);
        QCOMPARE(layout.itemCount(), qsizetype(14));
        layout.removeItems(12, 20);
        QCOMPARE(layout.itemCount(), qsizetype(12));
        layout.moveItems(0, 2, 8);
        QCOMPARE(layout.itemCount(), qsizetype(12));
        layout.resetItems(1000000, 1);
        QCOMPARE(layout.rowCount(), qsizetype(250000));
        QCOMPARE(layout.contentExtent(), qint64(25000000));
        const QRect last = layout.itemRect(999999, layout.offsetOf(999999));
        QCOMPARE(layout.itemAtPoint(last.center(), layout.offsetOf(999999)), qsizetype(999999));
        layout.setCrossExtent(0);
        QCOMPARE(layout.contentExtent(), qint64(0));
        QVERIFY(layout.itemRect(0, 0).isEmpty());
        layout.resetItems(0, 1);
        QCOMPARE(layout.itemAtPoint(QPoint(), 0), qsizetype(-1));
        layout.setCrossExtent(std::numeric_limits<int>::max());
        layout.setFixedHeight(std::numeric_limits<int>::max());
        layout.resetItems(std::numeric_limits<qsizetype>::max(), 1);
        QVERIFY(layout.contentExtent() > 0);
        QVERIFY(layout.offsetOf(layout.itemCount() - 1) >= 0);
    }
};

QTEST_APPLESS_MAIN(TestFlowTableLayout)
#include "tst_flowtablelayout.moc"
