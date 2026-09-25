#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtuallistview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QScrollBar>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 30;
constexpr int kColumnWidth = 100;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;

/// Counts the materialization passes the kernel runs: the observable effect of
/// "how many relayouts did this cost".
class CountingListView : public VirtualListView
{
public:
    void pokeDirty(int times)
    {
        for (int i = 0; i < times; ++i)
            markDirty();
    }

    int passes() const { return m_passes; }
    void resetPasses() { m_passes = 0; }

protected:
    void afterMaterialize() override
    {
        ++m_passes;
        VirtualListView::afterMaterialize();
    }

private:
    int m_passes = 0;
};

class CountingTableView : public VirtualTableView
{
public:
    int passes() const { return m_passes; }
    void resetPasses() { m_passes = 0; }

protected:
    void afterMaterialize() override
    {
        ++m_passes;
        VirtualTableView::afterMaterialize();
    }

private:
    int m_passes = 0;
};

class RowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(400, kRowHeight); }
};

class CellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *, const QModelIndex &) override {}
};

/// Counts the queued calls delivered to the view: the fix for P2-1 is "one queued
/// event per burst", which is only visible in the event stream, not in the passes.
class MetaCallCounter : public QObject
{
public:
    int count = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MetaCall)
            ++count;
        return QObject::eventFilter(watched, event);
    }
};
} // namespace

/// P2-1 / P2-8: invalidation is coalesced into one pass, and a purely horizontal
/// scroll must not pay for a vertical materialization pass.
class TestRelayoutQueue : public QObject
{
    Q_OBJECT

private slots:
    void manyInvalidationsCostOnePass();
    void manyInvalidationsPostOneQueuedCall();
    void horizontalScrollSkipsTheVerticalPass();
    void cellModeStillMaterializesTheHorizontalWindow();
};

void TestRelayoutQueue::manyInvalidationsCostOnePass()
{
    NumericListModel model(1000);
    TestAdapter adapter(kRowHeight);
    CountingListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    settle();
    view.resetPasses();

    view.pokeDirty(100);
    settle();

    QCOMPARE(view.passes(), 1);
}

void TestRelayoutQueue::manyInvalidationsPostOneQueuedCall()
{
    NumericListModel model(1000);
    TestAdapter adapter(kRowHeight);
    CountingListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    settle();

    // A burst of invalidations has to be one queued call, not one per markDirty():
    // 100 callbacks are 100 event-loop round trips for a single relayout.
    MetaCallCounter counter;
    view.installEventFilter(&counter);
    view.resetPasses();
    view.pokeDirty(100);
    settle();
    view.removeEventFilter(&counter);

    QCOMPARE(counter.count, 1);
    QCOMPARE(view.passes(), 1);
}

void TestRelayoutQueue::horizontalScrollSkipsTheVerticalPass()
{
    QStandardItemModel model(200, 40);
    RowAdapter adapter;
    CountingTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    settle();

    // The header and the body really do move...
    const int columnXBefore = view.columnGeometry(10).viewportX;
    view.resetPasses();
    view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->value() + 150);
    settle();
    QVERIFY(view.columnGeometry(10).viewportX != columnXBefore);
    // ... without a vertical materialization pass.
    QCOMPARE(view.passes(), 0);

    // A vertical scroll still runs one, of course.
    view.verticalScrollBar()->setValue(300);
    settle();
    QVERIFY(view.passes() >= 1);
}

void TestRelayoutQueue::cellModeStillMaterializesTheHorizontalWindow()
{
    QStandardItemModel model(200, 40);
    RowAdapter rowAdapter;
    CellAdapter cellAdapter;
    CountingTableView view;
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&view, QSize(kViewWidth, kViewHeight));
    settle();

    view.resetPasses();
    view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->value() + 20 * kColumnWidth);
    settle();

    // The horizontal window moved: new cells are materialized by a pass.
    QVERIFY(view.passes() >= 1);
    QVERIFY(view.materializedCellCount() > 0);
    // ... and only the window, never the whole model.
    QVERIFY(view.materializedCellCount()
            < qsizetype(model.rowCount() * model.columnCount()));
}

QTEST_MAIN(TestRelayoutQueue)

#include "tst_relayoutqueue.moc"
