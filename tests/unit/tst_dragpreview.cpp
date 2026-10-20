#include "../../src/core/dragpreview_p.h"
#include <virtualitemviews/types.h>

#include <QtTest>

class TestDragPreview : public QObject
{
    Q_OBJECT

private slots:
    void omittedPressPositionUsesFallback();
    void explicitPressPosition_data();
    void explicitPressPosition();
    void pressPositionCopiesAndResets();
    void emptyRectCapturesWholeWidget();
    void captureClipsToWidgetBounds();
    void disjointRectDoesNotBecomeWholeWidget();
    void hotspotUsesClippedCaptureOrigin();
    void hotspotPreservesOutsidePressOffset();
    void centerUsesLogicalPixels_data();
    void centerUsesLogicalPixels();
};

void TestDragPreview::omittedPressPositionUsesFallback()
{
    const viv::DragOptions options;
    QVERIFY(!options.globalPressPosition.has_value());
    QCOMPARE(options.globalPressPosition.value_or(QPoint(42, 81)), QPoint(42, 81));
}

void TestDragPreview::explicitPressPosition_data()
{
    QTest::addColumn<QPoint>("position");
    QTest::newRow("origin") << QPoint(0, 0);
    QTest::newRow("negative") << QPoint(-1920, -240);
    QTest::newRow("positive") << QPoint(42, 81);
}

void TestDragPreview::explicitPressPosition()
{
    QFETCH(QPoint, position);
    viv::DragOptions options;
    options.globalPressPosition = position;
    QVERIFY(options.globalPressPosition.has_value());
    QCOMPARE(options.globalPressPosition.value_or(QPoint(12, 34)), position);
    const viv::OptionalPoint constructed(position);
    QVERIFY(constructed.has_value());
    QCOMPARE(constructed.value_or(QPoint(12, 34)), position);
}

void TestDragPreview::pressPositionCopiesAndResets()
{
    viv::DragOptions original;
    original.globalPressPosition = QPoint(0, 0);
    viv::DragOptions copy = original;
    original.globalPressPosition.reset();
    QVERIFY(!original.globalPressPosition.has_value());
    QCOMPARE(original.globalPressPosition.value_or(QPoint(12, 34)), QPoint(12, 34));
    QVERIFY(copy.globalPressPosition.has_value());
    QCOMPARE(copy.globalPressPosition.value_or(QPoint(12, 34)), QPoint(0, 0));
    copy = original;
    QVERIFY(!copy.globalPressPosition.has_value());
    copy.globalPressPosition = QPoint(-5, 9);
    QCOMPARE(copy.globalPressPosition.value_or(QPoint(12, 34)), QPoint(-5, 9));
}

void TestDragPreview::emptyRectCapturesWholeWidget()
{
    const QRect widget(0, 0, 500, 300);
    QCOMPARE(viv::dragPreviewRect(widget, {}), widget);
}

void TestDragPreview::captureClipsToWidgetBounds()
{
    const QRect widget(0, 0, 500, 300);
    QCOMPARE(viv::dragPreviewRect(widget, QRect(-20, -10, 220, 50)), QRect(0, 0, 200, 40));
    QCOMPARE(viv::dragPreviewRect(widget, QRect(20, 280, 600, 60)), QRect(20, 280, 480, 20));
    QCOMPARE(viv::dragPreviewRect(widget, QRect(30, 40, 100, 20)), QRect(30, 40, 100, 20));
}

void TestDragPreview::disjointRectDoesNotBecomeWholeWidget()
{
    const QRect widget(0, 0, 500, 300);
    QVERIFY(viv::dragPreviewRect(widget, QRect(0, 300, 100, 40)).isEmpty());
    QVERIFY(viv::dragPreviewRect(widget, QRect(-100, -100, 50, 50)).isEmpty());
}

void TestDragPreview::hotspotUsesClippedCaptureOrigin()
{
    const QRect capture = viv::dragPreviewRect(QRect(0, 0, 500, 300), QRect(-20, 50, 220, 40));
    QCOMPARE(capture, QRect(0, 50, 200, 40));
    QCOMPARE(viv::dragPreviewHotSpot(capture, QPoint(16, 62)), QPoint(16, 12));
}

void TestDragPreview::hotspotPreservesOutsidePressOffset()
{
    QCOMPARE(viv::dragPreviewHotSpot(QRect(30, 50, 200, 40), QPoint(10, 100)), QPoint(-20, 50));
}

void TestDragPreview::centerUsesLogicalPixels_data()
{
    QTest::addColumn<QSize>("pixelSize");
    QTest::addColumn<qreal>("ratio");
    QTest::newRow("100-percent") << QSize(200, 40) << qreal(1.0);
    QTest::newRow("125-percent") << QSize(250, 50) << qreal(1.25);
    QTest::newRow("150-percent") << QSize(300, 60) << qreal(1.5);
    QTest::newRow("200-percent") << QSize(400, 80) << qreal(2.0);
}

void TestDragPreview::centerUsesLogicalPixels()
{
    QFETCH(QSize, pixelSize);
    QFETCH(qreal, ratio);
    QCOMPARE(viv::dragPreviewCenter(pixelSize, ratio), QPoint(99, 19));
}

QTEST_APPLESS_MAIN(TestDragPreview)

#include "tst_dragpreview.moc"
