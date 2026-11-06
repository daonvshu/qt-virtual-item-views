#include "../../src/layout/wrapitemlayout_p.h"
#include <QtTest>

class TestWrapLayout : public QObject
{
    Q_OBJECT
private slots:
    void packingAndBlankSpace()
    {
        viv::WrapItemLayout layout;
        auto settings = layout.settings();
        settings.height = 30;
        settings.itemSpacing = 10;
        settings.rowSpacing = 7;
        settings.margins = QMargins(5, 3, 5, 4);
        layout.setSettings(settings);
        layout.setCrossExtent(220);
        layout.resetItems(4, 1);
        layout.setWidths(0, {80, 120, 60, 500});
        QCOMPARE(layout.geometry().rows.size(), 3);
        QCOMPARE(layout.itemRect(0, 0), QRect(5, 3, 80, 30));
        QCOMPARE(layout.itemRect(1, 0), QRect(95, 3, 120, 30));
        QCOMPARE(layout.itemRect(2, 0), QRect(5, 40, 60, 30));
        QCOMPARE(layout.itemRect(3, 0), QRect(5, 77, 210, 30));
        QCOMPARE(layout.contentExtent(), qint64(111));
        QCOMPARE(layout.itemAtPoint(QPoint(90, 12), 0), qsizetype(-1));
        QCOMPARE(layout.itemAtPoint(QPoint(100, 50), 0), qsizetype(-1));
        QCOMPARE(layout.itemAtPoint(QPoint(30, 35), 0), qsizetype(-1));
        QCOMPARE(layout.indexAtOffset(12), qsizetype(0));
        QCOMPARE(layout.lastIndexAtOffset(12), qsizetype(1));
        QCOMPARE(layout.itemAtPoint(QPoint(10, 10), 37), qsizetype(2));
        QCOMPARE(layout.extentForWidth(100), qint64(148));
        QCOMPARE(layout.crossExtent(), 220);
    }
    void alignmentAndDirection()
    {
        viv::WrapItemLayout layout;
        layout.setCrossExtent(200);
        layout.resetItems(2, 1);
        layout.setWidths(0, {40, 60});
        auto settings = layout.settings();
        settings.itemSpacing = 10;
        for (Qt::LayoutDirection direction : {Qt::LeftToRight, Qt::RightToLeft}) {
            settings.direction = direction;
            for (Qt::Alignment align : {Qt::Alignment(Qt::AlignLeft), Qt::Alignment(Qt::AlignHCenter),
                                        Qt::Alignment(Qt::AlignRight), Qt::AlignLeft | Qt::AlignAbsolute}) {
                settings.alignment = align;
                layout.setSettings(settings);
                for (int item = 0; item < 2; ++item) {
                    const QRect rect = layout.itemRect(item, 0);
                    QCOMPARE(layout.itemAtPoint(rect.center(), 0), qsizetype(item));
                    QVERIFY(rect.left() >= 0);
                    QVERIFY(rect.right() < 200);
                }
                const QRect a = layout.itemRect(0, 0), b = layout.itemRect(1, 0);
                QVERIFY(direction == Qt::RightToLeft ? a.left() > b.right() : b.left() > a.right());
            }
        }
        settings.direction = Qt::RightToLeft;
        settings.alignment = Qt::AlignLeft;
        layout.setSettings(settings);
        QCOMPARE(layout.itemRect(0, 0).right(), 199);
        settings.alignment |= Qt::AlignAbsolute;
        layout.setSettings(settings);
        QCOMPARE(layout.itemRect(1, 0).left(), 0);
    }
    void incrementalMatchesFullPacking()
    {
        viv::WrapItemLayout layout;
        QVector<int> widths;
        for (int i = 0; i < 200; ++i) widths.append(20 + (i * 37) % 130);
        layout.setCrossExtent(430);
        layout.resetItems(widths.size(), 1);
        layout.setWidths(0, widths);
        for (int pass = 0; pass < 80; ++pass) {
            const int at = (pass * 29) % int(widths.size());
            layout.contentExtent(); // Commit the prefix before each mutation.
            if (pass % 3 == 0) {
                widths[at] = 11 + pass * 5;
                layout.setWidths(at, {widths[at]});
            } else if (pass % 3 == 1) {
                widths.insert(at, 85);
                layout.insertItems(at, 1, 1);
                layout.setWidths(at, {85});
            } else {
                widths.removeAt(at);
                layout.removeItems(at, 1);
            }
            viv::WrapItemLayout reference;
            reference.setCrossExtent(430);
            reference.resetItems(widths.size(), 1);
            reference.setWidths(0, widths);
            QCOMPARE(layout.contentExtent(), reference.contentExtent());
            for (int i = 0; i < widths.size(); ++i)
                QCOMPARE(layout.itemRect(i, 123), reference.itemRect(i, 123));
        }
    }
    void movesEmptyAndLargeOffsets()
    {
        viv::WrapItemLayout layout;
        layout.setCrossExtent(300);
        layout.resetItems(4, 1);
        layout.setWidths(0, {20, 40, 60, 80});
        layout.moveItems(0, 1, 4);
        QCOMPARE(layout.itemRect(0, 0).width(), 40);
        QCOMPARE(layout.itemRect(3, 0).width(), 20);
        layout.moveItems(3, 1, 0);
        QCOMPARE(layout.itemRect(0, 0).width(), 20);
        auto settings = layout.settings();
        settings.height = 1000000000;
        settings.rowSpacing = 1000000000;
        layout.setSettings(settings);
        layout.setCrossExtent(1);
        QCOMPARE(layout.offsetOf(3), qint64(6000000000LL));
        QCOMPARE(layout.itemAtPoint(QPoint(0, 12), 6000000000LL), qsizetype(3));
        QCOMPARE(layout.itemRect(3, 6000000000LL).top(), 0);
        layout.resetItems(0, 1);
        QCOMPARE(layout.contentExtent(), qint64(0));
        QCOMPARE(layout.itemAtPoint(QPoint(0, 0), 0), qsizetype(-1));
        QCOMPARE(layout.geometry().rows.size(), 0);
    }
    void scrollbarThresholdAndWidthLimits()
    {
        viv::WrapItemLayout layout;
        layout.setCrossExtent(140);
        layout.resetItems(2, 1);
        layout.setWidths(0, {70, 70});
        QCOMPARE(layout.contentExtent(), qint64(72));
        QCOMPARE(layout.extentForWidth(160), qint64(32));
        // Repeated sizing queries do not alter committed scroll geometry.
        QCOMPARE(layout.contentExtent(), qint64(72));
        auto settings = layout.settings();
        settings.minimumWidth = 80;
        settings.maximumWidth = 90;
        layout.setSettings(settings);
        layout.setWidths(0, {1, 500});
        layout.setCrossExtent(200);
        QCOMPARE(layout.itemRect(0, 0).width(), 80);
        QCOMPARE(layout.itemRect(1, 0).width(), 90);
        layout.setCrossExtent(30);
        QCOMPARE(layout.itemRect(0, 0).width(), 30);
        QCOMPARE(layout.itemRect(1, 0).width(), 30);
        QCOMPARE(layout.indexAtOffset(layout.contentExtent()), layout.itemCount());
    }
};

QTEST_APPLESS_MAIN(TestWrapLayout)
#include "tst_wraplayout.moc"
