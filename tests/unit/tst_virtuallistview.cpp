#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/virtuallistview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QStandardItemModel>
#include <QScrollBar>
#include <QWheelEvent>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;
constexpr int kUniformHeight = 24;

QStringList numberedRows(int count, const QString &prefix = QStringLiteral("row"))
{
    QStringList rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i)
        rows.append(QStringLiteral("%1-%2").arg(prefix).arg(i));
    return rows;
}
} // namespace

class TestVirtualListView : public QObject
{
    Q_OBJECT

private slots:
    void uniformHeightFitsFirstItemAutomatically();
    void variableHeightsAreMeasuredFromWidgets();
    void estimatedHeightUsedForNewRows();
    void keyboardNavigationUpdatesCurrentIndex();
    void pageKeysSkipVisibleItems();
    void homeEndKeysReachTheEnds();
    void wheelScrollsByConfiguredItems();
    void wheelScrollsByPixelsByDefault();
    void pixelOffsetApiMovesTheViewport();
    void rootIndexRestrictsMaterialization();
};

void TestVirtualListView::uniformHeightFitsFirstItemAutomatically()
{
    StringListModel model(numberedRows(100));
    model.setRowHeight(0, 36);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(0); // fit to the first materialized item
    QCOMPARE(view.uniformItemHeight(), 0);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();
    settle();

    QCOMPARE(view.uniformItemHeight(), 36);
    QCOMPARE(view.visualRect(model.index(5, 0)).height(), 36);
    QCOMPARE(view.contentExtent(), qint64(100) * 36);
}

void TestVirtualListView::variableHeightsAreMeasuredFromWidgets()
{
    StringListModel model(numberedRows(20));
    for (int row = 0; row < 20; ++row)
        model.setRowHeight(row, 20 + (row % 4) * 10);

    TestAdapter adapter(20);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(VirtualListView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(20);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();
    settle();

    QVERIFY(view.itemHeightMode() == VirtualListView::ItemHeightMode::Variable);
    // Every row that was materialized at least once has been measured.
    QSet<int> measuredRows;
    for (const QString &entry : adapter.lifecycleLog()) {
        if (entry.startsWith(QStringLiteral("bind ")))
            measuredRows.insert(entry.mid(5).toInt());
    }
    QVERIFY(!measuredRows.isEmpty());

    qint64 expectedOffset = 0;
    for (qsizetype row = 0; row < 20; ++row) {
        const int size = view.listLayout()->itemSize(row);
        const int expected = measuredRows.contains(int(row)) ? 20 + (int(row) % 4) * 10 : 20;
        QCOMPARE(size, expected);
        QCOMPARE(view.listLayout()->offsetOf(row), expectedOffset);
        expectedOffset += expected;
    }
    QCOMPARE(view.contentExtent(), expectedOffset);

    // The measured geometry matches the size index for the currently bound rows.
    const QList<int> boundRows = adapter.boundRows();
    QVERIFY(!boundRows.isEmpty());
    for (int row : boundRows) {
        const QRect rect = view.visualRect(model.index(row, 0));
        QCOMPARE(rect.height(), view.listLayout()->itemSize(row));
        QCOMPARE(rect.height(), 20 + (row % 4) * 10);
    }
}

void TestVirtualListView::estimatedHeightUsedForNewRows()
{
    StringListModel model(numberedRows(10));
    TestAdapter adapter(20);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(VirtualListView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(32);
    // Measurement is covered by variableHeightsAreMeasuredFromWidgets(); here
    // only the adapter estimate of never materialized rows is checked.
    view.setAutoMeasureItemHeight(false);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    adapter.setEstimatedHeight(64);
    QStringList fresh;
    for (int i = 0; i < 100; ++i)
        fresh.append(QStringLiteral("fresh-%1").arg(i));
    model.insertRowsAt(10, fresh);
    view.flushPendingRelayout();

    QCOMPARE(view.listLayout()->itemCount(), qsizetype(110));
    for (qsizetype row = 10; row < 110; ++row)
        QCOMPARE(view.listLayout()->itemSize(row), 64);
    // The estimate is also used for the rows that stay outside the window.
    QVERIFY(view.materializedItemCount() < 20);
}

void TestVirtualListView::keyboardNavigationUpdatesCurrentIndex()
{
    NumericListModel model(1000);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(0, 0));
    QVERIFY(view.selectionModel()->isSelected(model.index(0, 0)));

    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(1, 0));
    QVERIFY(!view.selectionModel()->isSelected(model.index(0, 0)));

    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(2, 0));
    QVERIFY(view.selectionModel()->isSelected(model.index(1, 0)));
    QVERIFY(view.selectionModel()->isSelected(model.index(2, 0)));

    // Consecutive Shift+Down extends from the anchor instead of restarting.
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(3, 0));
    QVERIFY(view.selectionModel()->isSelected(model.index(1, 0)));
    QVERIFY(view.selectionModel()->isSelected(model.index(2, 0)));
    QVERIFY(view.selectionModel()->isSelected(model.index(3, 0)));

    // A plain click resets the anchor.
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(4, 0));
    QVERIFY(!view.selectionModel()->isSelected(model.index(1, 0)));
    QVERIFY(view.selectionModel()->isSelected(model.index(4, 0)));
}

void TestVirtualListView::pageKeysSkipVisibleItems()
{
    NumericListModel model(1000);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QTest::keyClick(&view, Qt::Key_Down);
    QTest::keyClick(&view, Qt::Key_PageDown);
    const QModelIndex pageDown = view.currentIndex();
    QCOMPARE(pageDown.row(), 13); // one viewport of 13 rows below row 0
    QVERIFY(view.visualRect(pageDown).top() >= 0);
    QVERIFY(view.visualRect(pageDown).bottom() < view.viewport()->height());

    QTest::keyClick(&view, Qt::Key_PageUp);
    QCOMPARE(view.currentIndex().row(), 0);
    QCOMPARE(view.verticalOffset(), qint64(0));
}

void TestVirtualListView::homeEndKeysReachTheEnds()
{
    NumericListModel model(1000);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.currentIndex().row(), 999);
    QVERIFY(view.visualRect(view.currentIndex()).top() >= 0);
    QVERIFY(view.visualRect(view.currentIndex()).bottom() < view.viewport()->height());

    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.currentIndex().row(), 0);
    QCOMPARE(view.verticalOffset(), qint64(0));
}

void TestVirtualListView::wheelScrollsByConfiguredItems()
{
    NumericListModel model(1000);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setWheelScrollItems(3);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QWheelEvent down(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120),
                     Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    // Wheel events are delivered to the widget under the cursor, which is the
    // viewport, and reach the view through the viewport event filter.
    QCoreApplication::sendEvent(view.viewport(), &down);
    QVERIFY(down.isAccepted());
    QCOMPARE(view.verticalOffset(), qint64(3) * kUniformHeight);

    QWheelEvent up(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(view.viewport(), &up);
    QCOMPARE(view.verticalOffset(), qint64(0));
    QCOMPARE(view.visualRect(model.index(0, 0)).top(), 0);
}

void TestVirtualListView::wheelScrollsByPixelsByDefault()
{
    NumericListModel model(1000);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Default: pixels, independent of the row height.
    QCOMPARE(view.wheelScrollMode(), VirtualItemView::WheelScrollMode::Pixels);
    QCOMPARE(view.wheelScrollPixels(), 48);

    QWheelEvent notchDown(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(view.viewport(), &notchDown);
    QCOMPARE(view.verticalOffset(), qint64(48));
    // 48 px is not a multiple of the 24 px row height: rows are never the unit.

    view.setWheelScrollPixels(17);
    QCOMPARE(view.wheelScrollMode(), VirtualItemView::WheelScrollMode::Pixels);
    QWheelEvent notchDown2(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(view.viewport(), &notchDown2);
    QCOMPARE(view.verticalOffset(), qint64(48 + 17));

    // Touchpads/high resolution wheels deliver pixel deltas and are applied 1:1
    // (negative y scrolls down, exactly like angleDelta).
    QWheelEvent smooth(QPointF(10, 10), QPointF(10, 10), QPoint(0, -7), QPoint(),
                       Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(view.viewport(), &smooth);
    QCOMPARE(view.verticalOffset(), qint64(48 + 17 + 7));

    // Scrolling up uses the same pixel granularity.
    QWheelEvent notchUp(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                        Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(view.viewport(), &notchUp);
    QCOMPARE(view.verticalOffset(), qint64(48 + 17 + 7 - 17));
}

void TestVirtualListView::pixelOffsetApiMovesTheViewport()
{
    NumericListModel model(1000);
    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setVerticalOffset(123);
    QCOMPARE(view.verticalOffset(), qint64(123));
    QCOMPARE(view.visualRect(model.index(0, 0)).top(), -123);
    QCOMPARE(view.verticalScrollBar()->value(), 123);

    view.scrollByPixels(77);
    QCOMPARE(view.verticalOffset(), qint64(200));

    view.scrollByPixels(-1000);
    QCOMPARE(view.verticalOffset(), qint64(0));
    QCOMPARE(view.visualRect(model.index(0, 0)).top(), 0);

    view.setVerticalOffset(qint64(1000000000));
    QCOMPARE(view.verticalOffset(), view.maximumVerticalOffset());
}

void TestVirtualListView::rootIndexRestrictsMaterialization()
{
    QStandardItemModel model;
    auto *root = new QStandardItem(QStringLiteral("root"));
    for (int i = 0; i < 3; ++i)
        root->appendRow(new QStandardItem(QStringLiteral("child-%1").arg(i)));
    model.appendRow(root);

    TestAdapter adapter(kUniformHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kUniformHeight);
    view.setModel(&model);
    view.setRootIndex(model.index(0, 0));
    showView(&view, QSize(kViewWidth, kViewHeight));

    QCOMPARE(view.materializedItemCount(), qsizetype(3));
    QCOMPARE(view.indexAt(QPoint(10, 5)), model.index(0, 0, model.index(0, 0)));
    QCOMPARE(static_cast<TestRowWidget *>(adapter.widgetForRow(0))->text(), QStringLiteral("child-0"));

    view.setRootIndex(QModelIndex());
    view.flushPendingRelayout();
    QCOMPARE(view.materializedItemCount(), qsizetype(1));
    QCOMPARE(view.indexAt(QPoint(10, 5)), model.index(0, 0));
}

QTEST_MAIN(TestVirtualListView)

#include "tst_virtuallistview.moc"
