#include <virtualitemviews/tablepane.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QHeaderView>
#include <QLabel>
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

/// Two scroll groups with a frozen column on each side: the primary group (group
/// 0) sits in the middle, the second group scrolls on its own (§43).
QVector<TablePaneSpec> twoGroupPanes()
{
    return {frozenPane({0}), scrollablePane({1, 2, 3}, 0), frozenPane({4}),
            scrollablePane({5, 6, 7, 8, 9}, 1)};
}

/// Row widget of a materialized row (the row widgets cover the viewport).
QWidget *rowWidgetFor(VirtualTableView &view, int row)
{
    for (const MaterializedItem &item : view.materializedItems()) {
        if (int(item.index.row()) == row)
            return item.widget;
    }
    return nullptr;
}

/// Smallest CellWidgetAdapter: one label per cell, enough to see where a cell
/// widget ends up.
class PaneCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *, const QModelIndex &) override {}
    void unbindCellWidget(QWidget *, const QModelIndex &) override {}
};
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
    void secondScrollingGroupScrollsIndependently();
    void secondScrollingGroupKeepsItsOwnClipContainer();
    void cellModeClipsEveryScrollingPane();
    void nonPrimaryPaneHeaderFollowsItsOwnGroup();
    void scrollingGroupCannotStealHitsFromItsNeighbour();
    void spansAreClippedAtEveryPaneBoundary();
    void resettingThePanesRestoresTheDefault();
    void keyboardNavigationScrollsTheGroupOfTheColumn();
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

void TestTablePanes::secondScrollingGroupScrollsIndependently()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes(twoGroupPanes());
    view.flushPendingRelayout();
    QCOMPARE(view.panes().size(), 4);
    QCOMPARE(view.scrollGroups(), QVector<int>({0, 1}));
    // The primary group is the one the header geometry and the scroll bar drive.
    QCOMPARE(view.primaryScrollGroup(), 0);
    // Both scrolling panes get a share of the viewport, so both can scroll.
    QVERIFY(view.maximumHorizontalOffset(0) > 0);
    QVERIFY(view.maximumHorizontalOffset(1) > 0);

    const QVector<TablePane> panes = view.panes();
    QCOMPARE(view.paneIndexOfColumn(1), 1);
    QCOMPARE(view.paneIndexOfColumn(6), 3);
    const int primaryColumnX = view.columnGeometry(1).viewportX;
    const int secondColumnX = view.columnGeometry(6).viewportX;

    // Group 1 moves on its own: the primary group and the frozen columns stay put.
    const qint64 step = qMin<qint64>(40, view.maximumHorizontalOffset(1));
    QVERIFY(step > 0);
    view.setHorizontalOffset(1, step);
    view.flushPendingRelayout();
    QCOMPARE(view.horizontalOffset(1), step);
    QCOMPARE(view.horizontalOffset(0), qint64(0));
    QCOMPARE(view.columnGeometry(0).viewportX, 0);
    QCOMPARE(view.columnGeometry(1).viewportX, primaryColumnX);
    QCOMPARE(view.columnGeometry(4).viewportX, panes.at(2).viewportRect.x());
    QCOMPARE(view.columnGeometry(6).viewportX, secondColumnX - int(step));

    // The primary group keeps being driven by the view (geometry + scroll bar).
    view.setHorizontalOffset(view.maximumHorizontalOffset());
    view.flushPendingRelayout();
    QCOMPARE(view.horizontalOffset(), view.maximumHorizontalOffset());
    QCOMPARE(view.horizontalOffset(1), step); // untouched by the primary group
    QCOMPARE(view.columnGeometry(6).viewportX, secondColumnX - int(step));
}

void TestTablePanes::secondScrollingGroupKeepsItsOwnClipContainer()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes(twoGroupPanes());
    view.flushPendingRelayout();

    QWidget *rowWidget = rowWidgetFor(view, 0);
    QVERIFY(rowWidget != nullptr);
    auto *row = static_cast<PaneRowWidget *>(rowWidget);

    // One clip container per scrolling pane: without it the second group would
    // paint over the frozen column that sits between the two groups.
    const QList<QWidget *> clipHosts = rowWidget->findChildren<QWidget *>(
        QStringLiteral("vivPaneClipHost"), Qt::FindDirectChildrenOnly);
    QCOMPARE(clipHosts.size(), 2);

    const QVector<TablePane> panes = view.panes();
    for (int column = 0; column < kColumns; ++column) {
        const int paneIndex = view.paneIndexOfColumn(column);
        QVERIFY(paneIndex >= 0);
        const TablePane pane = panes.at(paneIndex);
        ColumnHost *host = row->host(column);
        QVERIFY(host != nullptr);
        if (pane.type == TablePane::Type::Scrollable) {
            QVERIFY(host->parentWidget() != rowWidget);
            // The container sits exactly on the pane the column belongs to.
            QCOMPARE(host->parentWidget()->geometry().x(), pane.viewportRect.x());
            QCOMPARE(host->parentWidget()->geometry().width(), pane.viewportRect.width());
        } else {
            // A frozen column is never clipped: it keeps the row widget as parent.
            QCOMPARE(host->parentWidget(), static_cast<QWidget *>(rowWidget));
        }
    }
}

void TestTablePanes::cellModeClipsEveryScrollingPane()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneCellAdapter adapter;
    VirtualTableView view;
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes(twoGroupPanes());
    view.flushPendingRelayout();

    // Cell Widget Mode gets one clip container per scrolling pane as well.
    const QList<QWidget *> clipHosts = view.viewport()->findChildren<QWidget *>(
        QStringLiteral("vivPaneClipHost"), Qt::FindDirectChildrenOnly);
    QCOMPARE(clipHosts.size(), 2);

    for (QWidget *clipHost : clipHosts) {
        const QRect rect = clipHost->geometry();
        bool found = false;
        for (const TablePane &pane : view.panes()) {
            if (pane.type == TablePane::Type::Scrollable && pane.viewportRect == rect) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
}

void TestTablePanes::nonPrimaryPaneHeaderFollowsItsOwnGroup()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes(twoGroupPanes());
    view.flushPendingRelayout();
    view.setHorizontalOffset(1, view.maximumHorizontalOffset(1));
    view.flushPendingRelayout();
    QApplication::processEvents();
    QVERIFY(view.horizontalOffset(1) > 0);

    const int viewportX = view.viewport()->geometry().x();
    const QVector<TablePane> panes = view.panes();
    QVector<QHeaderView *> headers;
    for (QHeaderView *header : view.findChildren<QHeaderView *>()) {
        if (header->orientation() == Qt::Horizontal && header->isVisible())
            headers.append(header);
    }
    QCOMPARE(headers.size(), panes.size());

    // Header and body agree pixel for pixel inside every pane (§45.1) - also in
    // the pane of the second scroll group, which lays out with its own offset.
    for (const TablePane &pane : panes) {
        QHeaderView *header = nullptr;
        for (QHeaderView *candidate : headers) {
            if (candidate->geometry().x() == viewportX + pane.viewportRect.x()
                && candidate->width() == pane.viewportRect.width()) {
                header = candidate;
                break;
            }
        }
        QVERIFY(header != nullptr);
        for (int column : pane.logicalColumns) {
            if (header->isSectionHidden(column))
                continue;
            const ColumnGeometry geometry = view.columnGeometry(column);
            QCOMPARE(header->geometry().x() + header->sectionViewportPosition(column),
                     geometry.viewportX + viewportX);
        }
    }
}

void TestTablePanes::scrollingGroupCannotStealHitsFromItsNeighbour()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes(twoGroupPanes());
    view.flushPendingRelayout();
    view.setHorizontalOffset(1, view.maximumHorizontalOffset(1));
    view.flushPendingRelayout();

    // Scrolled to its end, the content of group 1 starts left of its own pane and
    // sticks out into the frozen pane in between: that strip belongs to the frozen
    // column, not to the pane that scrolled it there.
    const VirtualItemView &asView = view;
    const QRect frozenPane = view.panes().at(2).viewportRect;
    const QModelIndex onFrozenStrip = asView.indexAt(QPoint(frozenPane.x() + 5, 5));
    QVERIFY(onFrozenStrip.isValid());
    QCOMPARE(onFrozenStrip.column(), 4);

    // Inside the pane of group 1 the hit belongs to group 1.
    const QRect secondPane = view.panes().at(3).viewportRect;
    const QModelIndex inSecondPane = asView.indexAt(QPoint(secondPane.x() + 5, 5));
    QVERIFY(inSecondPane.isValid());
    QCOMPARE(view.paneIndexOfColumn(inSecondPane.column()), 3);
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
    // Inside the scrolling pane the merge spans its own columns. It sits in
    // another row: overlapping spans are illegal, so one row holds one merge.
    view.setSpan(1, 2, 1, 2);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model->index(1, 2)).width(), 2 * kColumnWidth);
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

void TestTablePanes::keyboardNavigationScrollsTheGroupOfTheColumn()
{
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    // frozen {0} | group 0 {1,2,3} | frozen {4} | group 1 {5..9}
    view.setPanes(twoGroupPanes());
    view.flushPendingRelayout();
    QVERIFY(view.maximumHorizontalOffset(1) > 0);

    // A frozen column is always visible: navigating onto it must not scroll the
    // primary group (or any other).
    view.setHorizontalOffset(view.maximumHorizontalOffset());
    const qint64 primaryBefore = view.horizontalOffset(0);
    QVERIFY(primaryBefore > 0);
    view.setCurrentIndex(model->index(0, 1));
    QTest::keyClick(&view, Qt::Key_Left);            // column 1 -> frozen column 0
    view.flushPendingRelayout();
    QCOMPARE(view.currentIndex(), model->index(0, 0));
    QVERIFY(view.isColumnFrozen(0));
    QCOMPARE(view.horizontalOffset(0), primaryBefore);
    QCOMPARE(view.horizontalOffset(1), qint64(0));

    // Navigating to the far end of the *second* group has to move that group, and
    // leave the primary one alone.
    view.setCurrentIndex(model->index(0, 5));
    QTest::keyClick(&view, Qt::Key_Right);
    QTest::keyClick(&view, Qt::Key_Right);
    QTest::keyClick(&view, Qt::Key_Right);
    QTest::keyClick(&view, Qt::Key_Right);           // column 5 -> 9
    view.flushPendingRelayout();
    QCOMPARE(view.currentIndex(), model->index(0, 9));
    QVERIFY(view.horizontalOffset(1) > 0);
    QCOMPARE(view.horizontalOffset(0), primaryBefore);

    // ... and the column really is inside its own pane now.
    const QRect paneRect = view.panes().at(view.paneIndexOfColumn(9)).viewportRect;
    const ColumnGeometry geometry = view.columnGeometry(9);
    QVERIFY(geometry.viewportX >= paneRect.x());
    QVERIFY(geometry.viewportX + geometry.width <= paneRect.right() + 1);
}

QTEST_MAIN(TestTablePanes)
#include "tst_tablepanes.moc"
