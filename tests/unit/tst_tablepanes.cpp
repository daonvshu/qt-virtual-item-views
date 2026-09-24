#include <virtualitemviews/tablepane.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QHeaderView>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 24;
constexpr int kColumnWidth = 100;
/// Wide enough that the five panes of the tests keep their exact column widths
/// (the primary pane takes what is left, the others are never compressed here).
constexpr int kViewWidth = 900;
constexpr int kViewHeight = 220;
constexpr int kColumns = 10;

/// Row widget with one ColumnHost per column (the framework positions them).
class PaneRowWidget : public QWidget
{
public:
    PaneRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column)
            m_hosts.append(new ColumnHost(column, this));
    }

    ColumnHost *host(int column) const { return m_hosts.value(column); }

    QSize sizeHint() const override { return QSize(kColumnWidth * kColumns, kRowHeight); }

private:
    QVector<ColumnHost *> m_hosts;
};

class PaneTableAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new PaneRowWidget(parent, kColumns);
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}
    void unbindWidget(QWidget *, const QModelIndex &) override {}

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * kColumns, kRowHeight);
    }
};

TablePaneSpec frozenPane(const QVector<int> &columns)
{
    TablePaneSpec spec;
    spec.logicalColumns = columns;
    spec.scroll = PaneScroll::Frozen;
    return spec;
}

TablePaneSpec scrollablePane(const QVector<int> &columns, int group = 0)
{
    TablePaneSpec spec;
    spec.logicalColumns = columns;
    spec.scroll = PaneScroll::Scrollable;
    spec.scrollGroup = group;
    return spec;
}
} // namespace

/// §43 "advanced panes" (see docs/spans.md): the pane layout is an ordered list
/// instead of the fixed three panes, while setFrozenColumns() stays the shorthand
/// for the common case and behaves exactly as before.
class TestTablePanes : public QObject
{
    Q_OBJECT

private slots:
    void defaultLayoutIsStillTheThreePanes();
    void explicitPanesAreLaidOutInOrder();
    void everyPaneHasItsOwnHeader();
    void everyBoundaryHasItsOwnLine();
    void secondScrollingGroupIsRefusedForNow();
    void spansAreClippedAtEveryPaneBoundary();
    void resettingThePanesRestoresTheDefault();
};

void TestTablePanes::defaultLayoutIsStillTheThreePanes()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QVERIFY(view.paneSpecs().isEmpty());
    QCOMPARE(view.panes().size(), 1); // only the scrolling pane: nothing is frozen

    view.setFrozenColumns({0, 1});
    view.setFrozenRightColumns({kColumns - 1});
    view.flushPendingRelayout();
    const QVector<TablePane> panes = view.panes();
    QCOMPARE(panes.size(), 3);
    QCOMPARE(panes.at(0).type, TablePane::Type::FrozenLeft);
    QCOMPARE(panes.at(0).logicalColumns, QVector<int>({0, 1}));
    QCOMPARE(panes.at(1).type, TablePane::Type::Scrollable);
    QCOMPARE(panes.at(2).type, TablePane::Type::FrozenRight);
    QCOMPARE(panes.at(0).viewportRect.width(), 2 * kColumnWidth);
    QCOMPARE(panes.at(2).viewportRect.x() + panes.at(2).viewportRect.width(),
             view.viewport()->width());
    // The primary pane is the scrolling one; its group drives the scroll bar.
    QCOMPARE(view.scrollGroups(), QVector<int>({0}));
    QCOMPARE(view.paneIndexOfColumn(1), 0);
    QCOMPARE(view.paneIndexOfColumn(3), 1);
    QCOMPARE(view.paneIndexOfColumn(kColumns - 1), 2);
    QVERIFY(view.isColumnFrozen(0));
    QVERIFY(!view.isColumnFrozen(3));
}

void TestTablePanes::explicitPanesAreLaidOutInOrder()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Five panes: two frozen ones on each side and the scrolling pane in the
    // middle - more panes than the fixed three the framework started with.
    view.setPanes({frozenPane({0}), frozenPane({1}), scrollablePane({2, 3, 4, 5, 6}),
                   frozenPane({7}), frozenPane({8, 9})});
    view.flushPendingRelayout();

    const QVector<TablePane> panes = view.panes();
    QCOMPARE(panes.size(), 5);
    QCOMPARE(view.paneSpecs().size(), 5);
    int expectedX = 0;
    for (const TablePane &pane : panes) {
        QCOMPARE(pane.viewportRect.x(), expectedX);
        QCOMPARE(pane.viewportRect.y(), 0);
        QCOMPARE(pane.viewportRect.height(), view.viewport()->height());
        expectedX += pane.viewportRect.width();
    }
    QCOMPARE(panes.at(0).viewportRect.width(), kColumnWidth);
    QCOMPARE(panes.at(1).viewportRect.width(), kColumnWidth);
    QCOMPARE(panes.at(3).viewportRect.width(), kColumnWidth);
    QCOMPARE(panes.at(4).viewportRect.width(), 2 * kColumnWidth);
    // The scrolling pane takes what is left.
    QCOMPARE(panes.at(2).viewportRect.width(), view.viewport()->width() - 5 * kColumnWidth);
    for (int column = 0; column < kColumns; ++column) {
        const int paneIndex = view.paneIndexOfColumn(column);
        QVERIFY(paneIndex >= 0 && paneIndex < panes.size());
        QVERIFY(panes.at(paneIndex).logicalColumns.contains(column));
    }
    QCOMPARE(view.paneIndexOfColumn(0), 0);
    QCOMPARE(view.paneIndexOfColumn(7), 3);
    QCOMPARE(view.paneIndexOfColumn(9), 4);
    // Frozen panes are frozen no matter where they sit; the scrolling pane still
    // owns group 0.
    QVERIFY(view.isColumnFrozen(0));
    QVERIFY(view.isColumnFrozen(7));
    QVERIFY(!view.isColumnFrozen(2));
    QCOMPARE(view.scrollGroups(), QVector<int>({0}));
    QCOMPARE(view.horizontalOffset(0), qint64(0));
    QVERIFY(view.maximumHorizontalOffset(0) > 0);
    QCOMPARE(view.paneTypeForColumn(0), TablePane::Type::FrozenLeft);
    QCOMPARE(view.paneTypeForColumn(9), TablePane::Type::FrozenRight);
    // Every pane shows its columns at its own viewport x.
    QCOMPARE(view.columnGeometry(0).viewportX, 0);
    QCOMPARE(view.columnGeometry(1).viewportX, kColumnWidth);
    QCOMPARE(view.columnGeometry(7).viewportX, view.viewport()->width() - 3 * kColumnWidth);

    // Scrolling the primary group moves only its own columns.
    view.setHorizontalOffset(2 * kColumnWidth);
    view.flushPendingRelayout();
    const qint64 offset = view.horizontalOffset(0);
    QVERIFY(offset > 0);
    QCOMPARE(offset, qMin<qint64>(2 * kColumnWidth, view.maximumHorizontalOffset(0)));
    // The primary pane starts after the two frozen panes; its own content is
    // shifted by the offset of its group only.
    QCOMPARE(view.columnGeometry(2).viewportX, 2 * kColumnWidth - int(offset));
    QCOMPARE(view.columnGeometry(0).viewportX, 0);
    QCOMPARE(view.columnGeometry(7).viewportX, view.viewport()->width() - 3 * kColumnWidth);
}

void TestTablePanes::everyPaneHasItsOwnHeader()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3, 4, 5, 6}), frozenPane({7}),
                   frozenPane({8, 9})});
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    // The header is one renderer per pane (plus the vertical header), each one
    // covering exactly its own pane rectangle.
    QVector<QRect> horizontal;
    const QList<QHeaderView *> headers = view.findChildren<QHeaderView *>();
    for (QHeaderView *header : headers) {
        if (header->orientation() == Qt::Horizontal && header->isVisible())
            horizontal.append(header->geometry());
    }
    const QVector<TablePane> panes = view.panes();
    QCOMPARE(horizontal.size(), panes.size());
    for (const TablePane &pane : panes) {
        const int viewX = view.viewport()->geometry().x() + pane.viewportRect.x();
        bool found = false;
        for (const QRect &rect : horizontal) {
            if (rect.x() == viewX && rect.width() == pane.viewportRect.width()) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
}

void TestTablePanes::everyBoundaryHasItsOwnLine()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QCOMPARE(view.paneSeparatorRects().size(), 0);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({9});
    view.flushPendingRelayout();
    QCOMPARE(view.paneSeparatorRects().size(), 2);

    view.setPanes({frozenPane({0}), frozenPane({1}), scrollablePane({2, 3, 4, 5, 6}),
                   frozenPane({7}), frozenPane({8, 9})});
    view.flushPendingRelayout();
    const QVector<TablePane> panes = view.panes();
    const QVector<QRect> lines = view.paneSeparatorRects();
    QCOMPARE(lines.size(), panes.size() - 1);
    const int originX = view.viewport()->geometry().x();
    for (int index = 0; index + 1 < panes.size(); ++index) {
        const int boundary = originX + panes.at(index).viewportRect.right() + 1;
        QVERIFY(lines.at(index).x() <= boundary && boundary - lines.at(index).x() <= 1);
        QCOMPARE(lines.at(index).width(), view.paneSeparatorStyle().width);
    }
}

void TestTablePanes::secondScrollingGroupIsRefusedForNow()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QTest::ignoreMessage(QtWarningMsg,
                         "VirtualTableView::setPanes(): only one scrolling group is supported for "
                         "now (each group needs its own pane clip container); the call was "
                         "ignored.");
    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3}, 0), scrollablePane({4, 5, 6}, 1)});
    // Nothing changed: the API stays honest instead of misrendering.
    QVERIFY(view.paneSpecs().isEmpty());
    QCOMPARE(view.scrollGroups(), QVector<int>({0}));
    QCOMPARE(view.panes().size(), 1);
}

void TestTablePanes::spansAreClippedAtEveryPaneBoundary()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Frozen panes are pane boundaries just like the scrolling one, so a span
    // that would cross them is clipped (§43 "spans" + "advanced panes").
    view.setPanes({frozenPane({0}), frozenPane({1}), scrollablePane({2, 3, 4, 5, 6}),
                   frozenPane({7}), frozenPane({8, 9})});
    view.setSpan(0, 0, 1, 3);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model->index(0, 0)).width(), kColumnWidth);
    QCOMPARE(view.spanRect(model->index(0, 0)).x(), 0);
    // Inside the scrolling pane the merge spans its own columns.
    view.setSpan(0, 2, 1, 2);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model->index(0, 2)).width(), 2 * kColumnWidth);
}

void TestTablePanes::resettingThePanesRestoresTheDefault()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3, 4, 5, 6}), frozenPane({7})});
    view.flushPendingRelayout();
    QCOMPARE(view.panes().size(), 3);
    QCOMPARE(view.paneSpecs().size(), 3);

    view.setPanes({});
    view.flushPendingRelayout();
    QVERIFY(view.paneSpecs().isEmpty());
    // Back to the default: nothing frozen -> a single scrolling pane.
    QCOMPARE(view.panes().size(), 1);
    QCOMPARE(view.panes().first().type, TablePane::Type::Scrollable);
    QCOMPARE(view.columnGeometry(0).viewportX, 0);
    QCOMPARE(view.columnGeometry(3).viewportX, 3 * kColumnWidth);
}

QTEST_MAIN(TestTablePanes)
#include "tst_tablepanes.moc"
