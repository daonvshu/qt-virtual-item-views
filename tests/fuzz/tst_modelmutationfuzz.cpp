#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/virtuallistview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QScrollBar>
#include <QApplication>
#include <QWidget>

#include <random>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 24;

QStringList uniqueRows(int count, int generation)
{
    QStringList rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i)
        rows.append(QStringLiteral("gen%1-row%2").arg(generation).arg(i));
    return rows;
}
} // namespace

/// Random mutation fuzzing: after every mutation the view must agree with the
/// model, without any manual reload.
class TestModelMutationFuzz : public QObject
{
    Q_OBJECT

private slots:
    void randomMutationsKeepViewConsistent();
    void randomResetsKeepViewConsistent();
    void widgetCountStaysBounded();
    void longRunningScrollingKeepsResourcesStable();

private:
    void verifyConsistent(VirtualListView &view, TestAdapter &adapter, StringListModel &model,
                          const char *context);
};

void TestModelMutationFuzz::verifyConsistent(VirtualListView &view, TestAdapter &adapter,
                                            StringListModel &model, const char *context)
{
    view.flushPendingRelayout();

    // The layout must always describe exactly the model.
    QCOMPARE(view.listLayout()->itemCount(), qsizetype(model.rowCount()));

    QSet<const QWidget *> widgets;
    QSet<QPersistentModelIndex> indexes;
    for (const MaterializedItem &item : view.materializedItems()) {
        QVERIFY2(item.isValid(), context);
        QVERIFY2(item.index.row() < model.rowCount(), context);
        QVERIFY2(!widgets.contains(item.widget), context);
        QVERIFY2(!indexes.contains(item.index), context);
        widgets.insert(item.widget);
        indexes.insert(item.index);

        const QString expected = model.data(QModelIndex(item.index), Qt::DisplayRole).toString();
        const QString actual = static_cast<TestRowWidget *>(item.widget)->text();
        QVERIFY2(actual == expected, context);
        QCOMPARE(view.widgetForIndex(QModelIndex(item.index)), item.widget);
    }
    QCOMPARE(adapter.boundWidgetCount(), int(view.materializedItemCount()));

    // The materialized window must be exactly visible + overscan.
    const qsizetype count = view.listLayout()->itemCount();
    if (count > 0) {
        const qint64 viewExtent = view.viewport()->height();
        qsizetype first = qMin(view.listLayout()->indexAtOffset(view.verticalOffset()), count - 1);
        qsizetype last = qMin(view.listLayout()->indexAtOffset(view.verticalOffset() + viewExtent - 1),
                              count - 1);
        first = qMax<qsizetype>(0, first - view.overscanBefore());
        last = qMin<qsizetype>(count - 1, qMax<qsizetype>(first, last) + view.overscanAfter());
        qsizetype expectedCount = last - first + 1;
        QCOMPARE(view.materializedItemCount(), expectedCount);
    } else {
        QCOMPARE(view.materializedItemCount(), qsizetype(0));
    }
}

void TestModelMutationFuzz::randomMutationsKeepViewConsistent()
{
    StringListModel model(uniqueRows(80, 0));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(420, 320));

    std::mt19937 rng(20260923);
    int generation = 1;
    for (int step = 0; step < 300; ++step) {
        const int operation = int(rng() % 8);
        const int rowCount = model.rowCount();
        switch (operation) {
        case 0: // append
            model.insertRowsAt(rowCount, uniqueRows(1 + int(rng() % 4), generation++));
            break;
        case 1: // insert somewhere
            model.insertRowsAt(rowCount == 0 ? 0 : int(rng() % unsigned(rowCount + 1)),
                               uniqueRows(1 + int(rng() % 3), generation++));
            break;
        case 2: // remove
            if (rowCount > 0) {
                const int first = int(rng() % unsigned(rowCount));
                const int count = qMin(1 + int(rng() % 4), rowCount - first);
                QVERIFY(model.removeRowsAt(first, count));
            }
            break;
        case 3: // move
            if (rowCount > 1) {
                const int from = int(rng() % unsigned(rowCount));
                const int to = int(rng() % unsigned(rowCount + 1));
                model.moveRow(from, to);
            }
            break;
        case 4: // change data
            if (rowCount > 0)
                model.setRowText(int(rng() % unsigned(rowCount)),
                                 QStringLiteral("mut-%1").arg(step));
            break;
        case 5: // scroll
            if (rowCount > 0)
                view.scrollTo(model.index(int(rng() % unsigned(rowCount)), 0),
                              VirtualItemView::EnsureVisible);
            break;
        case 6: // resize
            view.resize(420, 120 + int(rng() % 480));
            break;
        default: // jump to a position
            if (rowCount > 0)
                view.scrollTo(model.index(int(rng() % unsigned(rowCount)), 0),
                              VirtualItemView::PositionAtTop);
            break;
        }
        verifyConsistent(view, adapter, model, "random mutation");
    }

    // Whatever happened, the pool must have served the scrolling: no widget per
    // model row was ever created.
    QVERIFY(adapter.createdCount() < 200);
    QCOMPARE(view.destroyedWidgetCount(), qsizetype(0));
}

void TestModelMutationFuzz::randomResetsKeepViewConsistent()
{
    StringListModel model(uniqueRows(50, 0));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(420, 320));

    std::mt19937 rng(4242);
    int generation = 100;
    for (int step = 0; step < 40; ++step) {
        const int size = int(rng() % 200);
        model.replaceAll(uniqueRows(size, generation++));
        verifyConsistent(view, adapter, model, "model reset");
        QCOMPARE(view.verticalOffset(), qint64(0));

        if (size > 0) {
            view.scrollTo(model.index(int(rng() % unsigned(size)), 0),
                          VirtualItemView::EnsureVisible);
            verifyConsistent(view, adapter, model, "scroll after reset");
        }
    }
}

void TestModelMutationFuzz::widgetCountStaysBounded()
{
    StringListModel model(uniqueRows(500, 0));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(420, 320));

    std::mt19937 rng(7);
    for (int step = 0; step < 500; ++step) {
        const int row = int(rng() % 500);
        view.verticalScrollBar()->setValue(row * kRowHeight);
        view.flushPendingRelayout();
    }

    // A 320 px viewport with 24 px rows materializes at most ~16 widgets; the
    // pool must stay in the same order of magnitude for 500 scroll steps.
    QVERIFY(adapter.createdCount() <= 24);
    QVERIFY(view.pooledWidgetCount() <= 64);
    QCOMPARE(adapter.boundWidgetCount(), int(view.materializedItemCount()));
}

void TestModelMutationFuzz::longRunningScrollingKeepsResourcesStable()
{
    // Long-running stability: thousands of scroll + mutation steps must not
    // grow the widget count, the pool, the viewport children or the number of
    // top level windows.
    StringListModel model(uniqueRows(2000, 0));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(420, 320));

    const int topLevelWindowsBefore = QApplication::topLevelWidgets().size();
    const int maximum = qMax(1, view.verticalScrollBar()->maximum());
    std::mt19937 rng(4711);
    int generation = 5000;

    for (int step = 0; step < 4000; ++step) {
        // Mostly continuous scrolling, with occasional jumps.
        if (step % 250 == 249)
            view.verticalScrollBar()->setValue(int(rng() % unsigned(maximum)));
        else
            view.verticalScrollBar()->setValue((step * kRowHeight) % maximum);
        view.flushPendingRelayout();

        // Interleave bursts of model mutations.
        if (step % 500 == 499) {
            model.insertRowsAt(int(rng() % unsigned(model.rowCount() + 1)),
                               uniqueRows(3, generation++));
        } else if (step % 500 == 249 && model.rowCount() > 10) {
            model.removeRowsAt(int(rng() % unsigned(model.rowCount() - 1)), 1);
        } else if (step % 100 == 99 && model.rowCount() > 0) {
            model.setRowText(int(rng() % unsigned(model.rowCount())),
                             QStringLiteral("long-%1").arg(step));
        }

        if (step % 500 == 0)
            verifyConsistent(view, adapter, model, "long running");
    }

    verifyConsistent(view, adapter, model, "long running (final)");

    // Resource counts stay bounded and consistent.
    QVERIFY2(adapter.createdCount() <= 64,
             qPrintable(QStringLiteral("created=%1").arg(adapter.createdCount())));
    QCOMPARE(view.destroyedWidgetCount(), qsizetype(0));
    QCOMPARE(qsizetype(adapter.createdCount()),
             view.materializedItemCount() + view.pooledWidgetCount() + view.destroyedWidgetCount());
    QCOMPARE(view.viewport()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).size(),
             int(view.materializedItemCount() + view.pooledWidgetCount()));
    QCOMPARE(QApplication::topLevelWidgets().size(), topLevelWindowsBefore);

    const VirtualViewStats stats = view.stats();
    QVERIFY(stats.bindCount > 0);
    QVERIFY(stats.recycleCount > 0);
    QCOMPARE(stats.createCount, quint64(adapter.createdCount()));
    QVERIFY(stats.materializedItems < 100);
}

QTEST_MAIN(TestModelMutationFuzz)

#include "tst_modelmutationfuzz.moc"
