#include <virtualitemviews/widgetrecycler.h>
#include <virtualitemviews/virtuallistview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 24;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;

QStringList numberedRows(int count, const QString &prefix = QStringLiteral("row"))
{
    QStringList rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i)
        rows.append(QStringLiteral("%1-%2").arg(prefix).arg(i));
    return rows;
}
} // namespace

class TestVirtualItemView : public QObject
{
    Q_OBJECT

private slots:
    void materializesOnlyVisibleAndOverscan();
    void overscanControlsMaterializedWindow();
    void scrollingReusesWidgetsWithoutAllocating();
    void widgetIdentityIsUniquePerIndex();
    void dataChangedRebindsOnlyAffectedWidgets();
    void rowsInsertedKeepsWidgetIdentity();
    void rowsRemovedRecyclesWidgetsWithoutStaleData();
    void rowsMovedKeepsWidgetOfIdentity();
    void modelResetRebuildsEverything();
    void layoutChangedKeepsAnchor();
    void proxyModelCanBeUsedAsModel();
    void selectionModelDrivesCurrentIndex();
    void pinnedItemSurvivesScrolling();
    void pinWidgetKeepsTheOwningItemAlive();
    void statsReportVirtualizationState();
    void selectionModeControlsSelection();
    void selectionBehaviorSelectsWholeRows();
    void lifecycleLogIsBounded();
    void resizeUpdatesVisibleRange();
    void scrollToKeepsItemVisible();
};

void TestVirtualItemView::materializesOnlyVisibleAndOverscan()
{
    NumericListModel model(1000000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setOverscan(2, 2);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QCOMPARE(view.viewport()->height(), kViewHeight);
    // Visible rows 0..12 (300 / 24 = 12.5) plus two overscan rows on each side.
    QCOMPARE(view.materializedItemCount(), qsizetype(15));
    // setModel() runs a pass at the default widget size (640x480 => 22 rows)
    // before the test resizes the view, so the pool holds the surplus. Nothing
    // is leaked: created == materialized + pooled.
    QCOMPARE(qsizetype(adapter.createdCount()),
             view.materializedItemCount() + view.pooledWidgetCount());
    QVERIFY(adapter.createdCount() <= 22);
    QVERIFY(view.contentExtent() == qint64(1000000) * kRowHeight);
    QCOMPARE(view.verticalOffset(), qint64(0));
}

void TestVirtualItemView::overscanControlsMaterializedWindow()
{
    NumericListModel model(10000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QCOMPARE(view.overscanBefore(), 2);
    const qsizetype defaultCount = view.materializedItemCount();

    view.setOverscan(0, 0);
    view.flushPendingRelayout();
    QCOMPARE(view.materializedItemCount(), qsizetype(13));
    QCOMPARE(defaultCount, qsizetype(15));

    view.setOverscan(10, 10);
    view.flushPendingRelayout();
    QCOMPARE(view.materializedItemCount(), qsizetype(23));
}

void TestVirtualItemView::scrollingReusesWidgetsWithoutAllocating()
{
    NumericListModel model(100000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const int createdAfterFirstPass = adapter.createdCount();
    QVERIFY(createdAfterFirstPass > 0);

    for (int step = 1; step <= 60; ++step) {
        view.verticalScrollBar()->setValue(step * kRowHeight);
        view.flushPendingRelayout();
        // The window is visible rows (13) + overscan (2 + 2), minus the rows the
        // overscan cannot reach at the top of the model.
        QVERIFY(view.materializedItemCount() >= qsizetype(15));
        QVERIFY(view.materializedItemCount() <= qsizetype(17));
    }

    // Steady state scrolling must not create (or destroy) widgets.
    QCOMPARE(adapter.createdCount(), createdAfterFirstPass);
    QCOMPARE(view.destroyedWidgetCount(), qsizetype(0));
    QVERIFY(view.pooledWidgetCount() > 0);
}

void TestVirtualItemView::widgetIdentityIsUniquePerIndex()
{
    NumericListModel model(10000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QSet<const QWidget *> widgets;
    QSet<QPersistentModelIndex> indexes;
    for (const MaterializedItem &item : view.materializedItems()) {
        QVERIFY(item.isValid());
        QVERIFY(!widgets.contains(item.widget));
        QVERIFY(!indexes.contains(item.index));
        widgets.insert(item.widget);
        indexes.insert(item.index);
        QCOMPARE(view.widgetForIndex(QModelIndex(item.index)), item.widget);
        QCOMPARE(view.indexForWidget(item.widget), QModelIndex(item.index));
    }
    QCOMPARE(widgets.size(), int(view.materializedItemCount()));
    QCOMPARE(adapter.boundWidgetCount(), int(view.materializedItemCount()));
}

void TestVirtualItemView::dataChangedRebindsOnlyAffectedWidgets()
{
    StringListModel model(numberedRows(200));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const int bindsBefore = adapter.bindCount();
    model.setRowText(3, QStringLiteral("changed-3"));
    view.flushPendingRelayout();

    QCOMPARE(adapter.bindCount(), bindsBefore + 1);
    QWidget *widget = adapter.widgetForRow(3);
    QVERIFY(widget != nullptr);
    QCOMPARE(static_cast<TestRowWidget *>(widget)->text(), QStringLiteral("changed-3"));
    QCOMPARE(view.materializedItemCount(), qsizetype(15));
}

void TestVirtualItemView::rowsInsertedKeepsWidgetIdentity()
{
    StringListModel model(numberedRows(50));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QWidget *widgetOfRowZero = adapter.widgetForRow(0);
    const int createdBefore = adapter.createdCount();

    model.insertRowsAt(0, {QStringLiteral("new-0"), QStringLiteral("new-1")});
    view.flushPendingRelayout();

    // The old first row moved to row 2 but keeps its widget.
    QCOMPARE(adapter.widgetForRow(2), widgetOfRowZero);
    QCOMPARE(static_cast<TestRowWidget *>(widgetOfRowZero)->text(), QStringLiteral("row-0"));
    QCOMPARE(adapter.createdCount(), createdBefore);
    QCOMPARE(static_cast<TestRowWidget *>(adapter.widgetForRow(0))->text(), QStringLiteral("new-0"));
    // The anchored top item stays at the top of the viewport.
    QCOMPARE(view.verticalOffset(), qint64(2) * kRowHeight);
}

void TestVirtualItemView::rowsRemovedRecyclesWidgetsWithoutStaleData()
{
    StringListModel model(numberedRows(50));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const int unboundBefore = adapter.unbindCount();
    const QString removedText = static_cast<TestRowWidget *>(adapter.widgetForRow(1))->text();

    QVERIFY(model.removeRowsAt(0, 3));
    view.flushPendingRelayout();

    QCOMPARE(adapter.unbindCount(), unboundBefore + 3);
    QCOMPARE(view.verticalOffset(), qint64(0));

    // No materialized widget may keep showing a removed row.
    for (const MaterializedItem &item : view.materializedItems()) {
        const QString expected = model.data(QModelIndex(item.index), Qt::DisplayRole).toString();
        const QString actual = static_cast<TestRowWidget *>(item.widget)->text();
        QCOMPARE(actual, expected);
        QVERIFY(actual != removedText);
    }
    QCOMPARE(adapter.boundWidgetCount(), int(view.materializedItemCount()));
}

void TestVirtualItemView::rowsMovedKeepsWidgetOfIdentity()
{
    StringListModel model(numberedRows(50));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QWidget *moved = adapter.widgetForRow(10);
    QVERIFY(moved != nullptr);
    const int createdBefore = adapter.createdCount();

    QVERIFY(model.moveRow(10, 0));
    view.flushPendingRelayout();

    QCOMPARE(adapter.widgetForRow(0), moved);
    QCOMPARE(static_cast<TestRowWidget *>(moved)->text(), QStringLiteral("row-10"));
    QCOMPARE(adapter.createdCount(), createdBefore);
}

void TestVirtualItemView::modelResetRebuildsEverything()
{
    StringListModel model(numberedRows(50));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.scrollTo(model.index(20, 0), VirtualItemView::PositionAtTop);
    const int createdBefore = adapter.createdCount();
    QVERIFY(view.verticalOffset() > 0);

    model.replaceAll(numberedRows(30, QStringLiteral("fresh")));
    view.flushPendingRelayout();

    QCOMPARE(view.verticalOffset(), qint64(0));
    QCOMPARE(view.materializedItemCount(), qsizetype(15));
    QCOMPARE(adapter.createdCount(), createdBefore);
    for (const MaterializedItem &item : view.materializedItems()) {
        const QString expected = model.data(QModelIndex(item.index), Qt::DisplayRole).toString();
        QCOMPARE(static_cast<TestRowWidget *>(item.widget)->text(), expected);
        QVERIFY(expected.startsWith(QStringLiteral("fresh-")));
    }
}

void TestVirtualItemView::layoutChangedKeepsAnchor()
{
    StringListModel model(numberedRows(40));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.scrollTo(model.index(20, 0), VirtualItemView::PositionAtTop);
    QWidget *topWidget = nullptr;
    for (const MaterializedItem &item : view.materializedItems()) {
        if (item.geometry.top() <= 0 && item.geometry.bottom() >= 0)
            topWidget = item.widget;
    }
    QVERIFY(topWidget != nullptr);
    const QString topText = static_cast<TestRowWidget *>(topWidget)->text();
    QCOMPARE(topText, QStringLiteral("row-20"));

    model.reverseKeepingPersistentIndexes();
    view.flushPendingRelayout();

    // Row 20 becomes row 19 after the reversal; the anchored item must stay at
    // the top of the viewport instead of jumping.
    QCOMPARE(view.verticalOffset(), qint64(19) * kRowHeight);
    QWidget *newTop = nullptr;
    for (const MaterializedItem &item : view.materializedItems()) {
        if (item.geometry.top() <= 0 && item.geometry.bottom() >= 0)
            newTop = item.widget;
    }
    QVERIFY(newTop != nullptr);
    QCOMPARE(static_cast<TestRowWidget *>(newTop)->text(), topText);
}

void TestVirtualItemView::proxyModelCanBeUsedAsModel()
{
    StringListModel source(numberedRows(300));
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setFilterFixedString(QStringLiteral("row-1"));

    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&proxy);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QVERIFY(view.materializedItemCount() > 0);
    for (const MaterializedItem &item : view.materializedItems()) {
        const QString expected = proxy.data(QModelIndex(item.index), Qt::DisplayRole).toString();
        QVERIFY(expected.startsWith(QStringLiteral("row-1")));
        QCOMPARE(static_cast<TestRowWidget *>(item.widget)->text(), expected);
    }

    // Changing the filter must not require a manual reload.
    proxy.setFilterFixedString(QStringLiteral("row-2"));
    view.flushPendingRelayout();
    QVERIFY(view.materializedItemCount() > 0);
    for (const MaterializedItem &item : view.materializedItems()) {
        const QString expected = proxy.data(QModelIndex(item.index), Qt::DisplayRole).toString();
        QVERIFY(expected.startsWith(QStringLiteral("row-2")));
        QCOMPARE(static_cast<TestRowWidget *>(item.widget)->text(), expected);
    }
}

void TestVirtualItemView::selectionModelDrivesCurrentIndex()
{
    StringListModel model(numberedRows(100));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QVERIFY(view.selectionModel() != nullptr);
    QCOMPARE(view.selectionModel()->model(), static_cast<QAbstractItemModel *>(&model));
    QVERIFY(!view.currentIndex().isValid());

    view.setCurrentIndex(model.index(3, 0));
    QCOMPARE(view.currentIndex(), model.index(3, 0));
    QVERIFY(view.selectionModel()->isSelected(model.index(3, 0)));

    view.selectionModel()->select(model.index(4, 0), QItemSelectionModel::Select);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 2);

    QItemSelectionModel external(&model);
    view.setSelectionModel(&external);
    QCOMPARE(view.selectionModel(), &external);
    QVERIFY(!view.currentIndex().isValid());
}

void TestVirtualItemView::pinnedItemSurvivesScrolling()
{
    StringListModel model(numberedRows(500));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QWidget *pinned = adapter.widgetForRow(0);
    view.setItemPinned(model.index(0, 0), true);
    QVERIFY(view.isItemPinned(model.index(0, 0)));

    view.scrollTo(model.index(200, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();

    QCOMPARE(view.materializedItemCount(), qsizetype(18));
    QCOMPARE(view.pinnedItemCount(), qsizetype(1));
    QCOMPARE(view.widgetForIndex(model.index(0, 0)), pinned);
    QVERIFY(view.visualRect(model.index(0, 0)).top() < 0);

    view.setItemPinned(model.index(0, 0), false);
    view.flushPendingRelayout();
    QCOMPARE(view.widgetForIndex(model.index(0, 0)), nullptr);
    QCOMPARE(view.materializedItemCount(), qsizetype(17));
}

void TestVirtualItemView::resizeUpdatesVisibleRange()
{
    NumericListModel model(10000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, 200));

    const qsizetype smallCount = view.materializedItemCount();
    QCOMPARE(smallCount, qsizetype(11));
    const QRect firstRect = view.visualRect(model.index(0, 0));
    QCOMPARE(firstRect.top(), 0);
    QCOMPARE(firstRect.height(), kRowHeight);

    view.resize(kViewWidth, 600);
    view.flushPendingRelayout();
    QCOMPARE(view.viewport()->height(), 600);
    QVERIFY(view.materializedItemCount() > smallCount);
    QCOMPARE(view.visualRect(model.index(0, 0)), firstRect);
}

void TestVirtualItemView::pinWidgetKeepsTheOwningItemAlive()
{
    StringListModel model(numberedRows(500));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setMaxPinnedItems(1);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QWidget *first = adapter.widgetForRow(0);
    QVERIFY(first != nullptr);
    view.pinWidget(first);
    QVERIFY(view.isItemPinned(model.index(0, 0)));

    // The pinned widget also stays alive when the scroll position changes.
    view.scrollTo(model.index(300, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QCOMPARE(view.widgetForIndex(model.index(0, 0)), first);
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));

    // The soft limit only warns: both widgets stay pinned.
    QWidget *second = adapter.widgetForRow(300);
    QVERIFY(second != nullptr);
    view.pinWidget(second);
    QVERIFY(view.isItemPinned(model.index(300, 0)));
    view.flushPendingRelayout();
    QCOMPARE(view.maxPinnedItems(), 1);
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(2));

    view.unpinWidget(first);
    view.unpinWidget(second);
    view.flushPendingRelayout();
    QCOMPARE(view.widgetForIndex(model.index(0, 0)), nullptr);
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestVirtualItemView::statsReportVirtualizationState()
{
    NumericListModel model(100000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    VirtualViewStats stats = view.stats();
    QCOMPARE(stats.logicalItems, qsizetype(100000));
    QCOMPARE(stats.materializedItems, view.materializedItemCount());
    QCOMPARE(stats.pooledWidgets, view.pooledWidgetCount());
    QCOMPARE(stats.pinnedWidgets, view.pinnedItemCount());
    QCOMPARE(stats.createCount, quint64(adapter.createdCount()));
    QVERIFY(stats.bindCount >= quint64(stats.materializedItems));
    // The model is set before the view is resized, so the first pass at the
    // default size is recycled into the pool: every recycled widget is pooled
    // and nothing was destroyed.
    QCOMPARE(stats.recycleCount, quint64(stats.pooledWidgets));
    QCOMPARE(view.destroyedWidgetCount(), qsizetype(0));

    const quint64 createBefore = stats.createCount;
    const quint64 bindBefore = stats.bindCount;
    for (int step = 1; step <= 10; ++step) {
        view.verticalScrollBar()->setValue(step * kRowHeight);
        view.flushPendingRelayout();
    }
    stats = view.stats();
    QCOMPARE(stats.createCount, createBefore); // steady state: no new widgets
    QVERIFY(stats.bindCount > bindBefore);
    QVERIFY(stats.recycleCount > 0);
    QVERIFY(stats.pooledWidgets >= 0);
}

void TestVirtualItemView::scrollToKeepsItemVisible()
{
    NumericListModel model(100000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const QModelIndex target = model.index(5000, 0);
    view.scrollTo(target, VirtualItemView::EnsureVisible);
    QRect rect = view.visualRect(target);
    QVERIFY(rect.top() >= 0);
    QVERIFY(rect.bottom() < view.viewport()->height());
    const qint64 offset = view.verticalOffset();

    // Already visible: EnsureVisible must not move the viewport.
    view.scrollTo(target, VirtualItemView::EnsureVisible);
    QCOMPARE(view.verticalOffset(), offset);

    view.scrollTo(target, VirtualItemView::PositionAtTop);
    QCOMPARE(view.visualRect(target).top(), 0);
    QCOMPARE(view.widgetForIndex(target) != nullptr, true);

    view.scrollTo(target, VirtualItemView::PositionAtBottom);
    QCOMPARE(view.visualRect(target).bottom(), view.viewport()->height() - 1);

    view.scrollTo(target, VirtualItemView::PositionAtCenter);
    const QRect centered = view.visualRect(target);
    QVERIFY(qAbs(centered.center().y() - view.viewport()->height() / 2) <= 1);
}

void TestVirtualItemView::selectionModeControlsSelection()
{
    NumericListModel model(1000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // NoSelection: the current index moves, nothing is ever selected.
    view.setSelectionMode(VirtualItemView::SelectionMode::NoSelection);
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(0, 0));
    QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());

    // SingleSelection: modifiers never extend the selection.
    view.setSelectionMode(VirtualItemView::SelectionMode::SingleSelection);
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 1);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 1);
    // The current index still moves; only the selection stays single.
    QCOMPARE(view.currentIndex(), model.index(3, 0));

    // MultiSelection: plain navigation replaces, Shift extends.
    view.setSelectionMode(VirtualItemView::SelectionMode::MultiSelection);
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 1);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 2);

    // ExtendedSelection (default): Ctrl+arrow moves the current index only.
    view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    const QModelIndex before = view.currentIndex();
    QTest::keyClick(&view, Qt::Key_Down, Qt::ControlModifier);
    QVERIFY(view.currentIndex() != before);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 2);
}

void TestVirtualItemView::selectionBehaviorSelectsWholeRows()
{
    QStandardItemModel model(5, 3);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 3; ++column)
            model.setItem(row, column, new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
    }

    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // SelectItems (default): only the current index is selected.
    view.setCurrentIndex(model.index(2, 1));
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 1);

    // SelectRows: the whole row follows the current index.
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectRows);
    view.setCurrentIndex(model.index(3, 1));
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 3);
    for (int column = 0; column < 3; ++column)
        QVERIFY(view.selectionModel()->isSelected(model.index(3, column)));
}

void TestVirtualItemView::lifecycleLogIsBounded()
{
    NumericListModel model(5000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QVERIFY(!view.isLifecycleLoggingEnabled());
    QVERIFY(view.lifecycleLog().isEmpty());

    view.setLifecycleLoggingEnabled(true);
    for (int step = 1; step <= 300; ++step) {
        view.verticalScrollBar()->setValue(step * kRowHeight);
        view.flushPendingRelayout();
    }

    const QStringList log = view.lifecycleLog();
    QCOMPARE(log.size(), VirtualItemView::kLifecycleLogCapacity);
    bool sawBind = false;
    bool sawRecycle = false;
    for (const QString &entry : log) {
        sawBind = sawBind || entry.startsWith(QStringLiteral("bind row="));
        sawRecycle = sawRecycle || entry.startsWith(QStringLiteral("recycle type="));
    }
    QVERIFY(sawBind);
    QVERIFY(sawRecycle);

    view.setLifecycleLoggingEnabled(false);
    QVERIFY(view.lifecycleLog().isEmpty());
}

QTEST_MAIN(TestVirtualItemView)

#include "tst_virtualitemview.moc"
