#include <virtualitemviews/itempane.h>
#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QAbstractItemView>
#include <QBuffer>
#include <QDataStream>
#include <QLabel>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 20;
constexpr int kColumnWidth = 100;
constexpr int kRows = 1000;
constexpr int kColumns = 3;
constexpr int kViewWidth = 360;
constexpr int kViewHeight = 200;

/// 行控件：每列一个 ColumnHost，框架负责摆放（行 pane 由内核裁剪）。
class FrozenRowWidget : public QWidget
{
public:
    explicit FrozenRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        for (int column = 0; column < kColumns; ++column)
            m_hosts.append(new ColumnHost(column, this));
    }

    ColumnHost *host(int column) const { return m_hosts.value(column); }
    QSize sizeHint() const override { return QSize(kColumnWidth * kColumns, kRowHeight); }

private:
    QVector<ColumnHost *> m_hosts;
};

class FrozenTableAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new FrozenRowWidget(parent);
    }
    void bindWidget(QWidget *, const QModelIndex &) override {}
    void unbindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * kColumns, kRowHeight);
    }
};

class FrozenCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *, const QModelIndex &) override {}
    void unbindCellWidget(QWidget *, const QModelIndex &) override {}
};

/// 表格模型：行高固定，列数固定。
QStandardItemModel *tableModel(QObject *parent)
{
    auto *model = new QStandardItemModel(kRows, kColumns, parent);
    return model;
}

/// 行号条：可见的垂直表头就是当前每个行 pane 一条。
QList<QHeaderView *> verticalStrips(VirtualTableView &view)
{
    QList<QHeaderView *> strips;
    for (QHeaderView *header : view.findChildren<QHeaderView *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (header->orientation() == Qt::Vertical && header->isVisible())
            strips.append(header);
    }
    return strips;
}

/// 第一个完整落在 \a pane 里的行（-1 = 没有），用来检查"行号贴合行"。
int firstRowInside(VirtualTableView &view, QAbstractItemModel *model, const ItemPane &pane)
{
    for (int row = int(pane.firstRow); row <= int(pane.lastRow); ++row) {
        const QRect rect = view.visualRect(model->index(row, 0));
        if (rect.y() >= pane.viewportRect.y() && rect.bottom() <= pane.viewportRect.bottom())
            return row;
    }
    return -1;
}

/// 每个行 pane 都必须有一条行号条贴在它的矩形上，并且这条带子里选中行的行号与 body
/// 对齐（§31 行方向：行高只有一份，三条带子各自持有自己的偏移）。
void verifyRowStripsAreGlued(VirtualTableView &view, QAbstractItemModel *model)
{
    // Pane rects are viewport relative, the strip widgets live in the view: the viewport's
    // own origin is the bridge between the two.
    const QRect viewportRect = view.viewport()->geometry();
    const QVector<ItemPane> panes = view.itemPanes();
    const QList<QHeaderView *> strips = verticalStrips(view);
    QCOMPARE(strips.size(), panes.size());
    for (const ItemPane &pane : panes) {
        const int paneTop = viewportRect.y() + pane.viewportRect.y();
        QHeaderView *strip = nullptr;
        for (QHeaderView *candidate : strips) {
            const QRect rect = candidate->geometry();
            if (rect.y() == paneTop && rect.height() == pane.viewportRect.height())
                strip = candidate;
        }
        QVERIFY(strip != nullptr);
        const int row = firstRowInside(view, model, pane);
        QVERIFY(row >= 0);
        const QRect rowRect = view.visualRect(model->index(row, 0));
        QCOMPARE(strip->geometry().y() + strip->sectionViewportPosition(row),
                 viewportRect.y() + rowRect.y());
    }
}
} // namespace

/// 行冻结（§31 行方向，docs/row-freezing.md）：冻结行钉在上下边缘、不产生额外滚动
/// 空间，物化只多出冻结行本身。
class TestFrozenRows : public QObject
{
    Q_OBJECT

private slots:
    void noFrozenRowsKeepsEverythingAsBefore();
    void frozenRowsStayWhileTheRestScrolls();
    void frozenRowsAddNoScrollSpace();
    void onlyTheFrozenAndWindowRowsAreMaterialized();
    void hitTestingFollowsTheRowPanes();
    void keyboardWalksAcrossTheFrozenBoundary();
    void scrollToAFrozenRowDoesNotScroll();
    void bottomFrozenRowsArePinnedToTheBottom();
    void rowWidgetsAreClippedAtTheRowPanes();
    void cellsAreClippedByBothPaneDirections();
    void verticalHeaderIsSplitPerRowPane();
    void frozenRowsSurviveTheStateRoundTrip();
    void rowBoundaryLooksLikeTheColumnBoundary();
    void frozenRowsAreClampedToWhatFits();
};

void TestFrozenRows::noFrozenRowsKeepsEverythingAsBefore()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QCOMPARE(view.frozenRows(), 0);
    QCOMPARE(view.frozenBottomRows(), 0);
    QVERIFY(!view.isRowFrozen(0));
    const QVector<ItemPane> panes = view.itemPanes();
    QCOMPARE(panes.size(), 1);
    QCOMPARE(panes.first().type, ItemPane::Type::Scrollable);
    QCOMPARE(panes.first().viewportRect, view.viewport()->rect());
    QCOMPARE(panes.first().firstRow, qsizetype(0));
    QCOMPARE(panes.first().lastRow, qsizetype(kRows - 1));
    // No frozen rows: nothing is clipped and no line is drawn.
    QVERIFY(view.scrollingPaneHost() == nullptr);
    QCOMPARE(view.itemPaneSeparatorRects().size(), 0);
    QCOMPARE(view.visibleItemRanges().size(), 1);
    QCOMPARE(view.visibleItemRanges().first().first, view.visibleItemRange().first);
    QCOMPARE(view.maximumVerticalOffset(),
             qint64(kRows * kRowHeight) - view.viewport()->height());
}

void TestFrozenRows::frozenRowsStayWhileTheRestScrolls()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenRows(2);
    view.flushPendingRelayout();
    QCOMPARE(view.frozenRows(), 2);
    QVERIFY(view.isRowFrozen(0));
    QVERIFY(view.isRowFrozen(1));
    QVERIFY(!view.isRowFrozen(2));

    const QVector<ItemPane> panes = view.itemPanes();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes.at(0).type, ItemPane::Type::FrozenTop);
    QCOMPARE(panes.at(0).viewportRect, QRect(0, 0, view.viewport()->width(), 2 * kRowHeight));
    QCOMPARE(panes.at(0).firstRow, qsizetype(0));
    QCOMPARE(panes.at(0).lastRow, qsizetype(1));
    QCOMPARE(panes.at(1).type, ItemPane::Type::Scrollable);
    QCOMPARE(panes.at(1).viewportRect.y(), 2 * kRowHeight);
    QCOMPARE(panes.at(1).viewportRect.height(), view.viewport()->height() - 2 * kRowHeight);
    QCOMPARE(panes.at(1).firstRow, qsizetype(2));
    QCOMPARE(view.itemPaneSeparatorRects().size(), 1);
    QVERIFY(view.scrollingPaneHost() != nullptr);

    QWidget *frozen = adapter.widgetForRow(0);
    QWidget *secondFrozen = adapter.widgetForRow(1);
    QVERIFY(frozen != nullptr);
    QVERIFY(secondFrozen != nullptr);
    // Frozen rows are plain viewport children at their own y, they are not clipped.
    QCOMPARE(frozen->parentWidget(), view.viewport());
    QCOMPARE(secondFrozen->parentWidget(), view.viewport());
    QCOMPARE(frozen->y(), 0);
    QCOMPARE(secondFrozen->y(), kRowHeight);

    view.setVerticalOffset(10 * kRowHeight);
    view.flushPendingRelayout();
    QCOMPARE(view.verticalOffset(), qint64(10 * kRowHeight));
    QCOMPARE(frozen->y(), 0);
    QCOMPARE(secondFrozen->y(), kRowHeight);

    // The scrolling rows are clipped into the scrolling pane and follow the offset.
    QWidget *firstVisible = adapter.widgetForRow(12); // content 240 = offset 200 + frozen 40
    QVERIFY(firstVisible != nullptr);
    QCOMPARE(firstVisible->parentWidget(), view.scrollingPaneHost());
    QCOMPARE(firstVisible->mapTo(view.viewport(), QPoint(0, 0)).y(), 2 * kRowHeight);
    QWidget *secondVisible = adapter.widgetForRow(13);
    QVERIFY(secondVisible != nullptr);
    QCOMPARE(secondVisible->mapTo(view.viewport(), QPoint(0, 0)).y(), 3 * kRowHeight);

    // A row that scrolled behind the frozen band keeps its content position and is
    // clipped by the container: that is what keeps it from showing through.
    QWidget *behindTheBand = adapter.widgetForRow(10);
    QVERIFY(behindTheBand != nullptr);
    QCOMPARE(behindTheBand->parentWidget(), view.scrollingPaneHost());
    QVERIFY(behindTheBand->mapTo(view.viewport(), QPoint(0, 0)).y() < 2 * kRowHeight);
}

void TestFrozenRows::frozenRowsAddNoScrollSpace()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const qint64 plain = view.maximumVerticalOffset();
    QCOMPARE(plain, qint64(kRows * kRowHeight) - view.viewport()->height());
    view.setFrozenRows(3);
    view.setFrozenBottomRows(2);
    view.flushPendingRelayout();
    // Freezing redistributes the viewport: the scrolling pane shrinks by exactly the
    // frozen height, so the range stays the same (no phantom scroll space).
    QCOMPARE(view.maximumVerticalOffset(), plain);

    view.setVerticalOffset(plain);
    view.flushPendingRelayout();
    QCOMPARE(view.verticalOffset(), plain);
    // At the end of the range the frozen bottom band covers exactly the last rows and
    // the last row ends at the viewport's bottom edge: the content stays contiguous
    // with the scrolling pane, and the range never grew.
    const QRect bottom = view.itemPaneRect(ItemPane::Type::FrozenBottom);
    const QRect last = view.visualRect(model.index(kRows - 1, 0));
    QCOMPARE(last.top(), bottom.top() + kRowHeight);
    QCOMPARE(last.bottom() + 1, view.viewport()->height());
    const QRect secondToLast = view.visualRect(model.index(kRows - 2, 0));
    QCOMPARE(secondToLast.bottom() + 1, last.top());
}

void TestFrozenRows::onlyTheFrozenAndWindowRowsAreMaterialized()
{
    // A million rows with a frozen first row: the invariant of the whole library
    // holds for the frozen band as well.
    NumericListModel model(1000000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenRows(1);
    view.flushPendingRelayout();
    view.setVerticalOffset(500000 * kRowHeight);
    view.flushPendingRelayout();

    QVERIFY(view.materializedItemCount() < 40);
    QVERIFY(adapter.widgetForRow(0) != nullptr); // the frozen row is always there
}

void TestFrozenRows::hitTestingFollowsTheRowPanes()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenRows(2);
    view.setFrozenBottomRows(2);
    view.flushPendingRelayout();
    view.setVerticalOffset(10 * kRowHeight);
    view.flushPendingRelayout();

    const QVector<ItemPane> panes = view.itemPanes();
    QCOMPARE(panes.size(), 3);
    // The frozen band keeps its rows, whatever the offset is.
    QCOMPARE(view.indexAt(QPoint(5, 5)).row(), 0);
    QCOMPARE(view.indexAt(QPoint(5, kRowHeight + 5)).row(), 1);
    // The scrolling pane shows the rows of the window.
    // The frozen band hides the first rows of the content, so the top of the
    // scrolling pane shows row `offset / rowHeight + frozenRows` - the Excel
    // behaviour: scrolling by one row moves the content by one row.
    QCOMPARE(view.indexAt(QPoint(5, panes.at(1).viewportRect.y() + 5)).row(), 12);
    QCOMPARE(view.indexAt(QPoint(5, panes.at(1).viewportRect.y() + kRowHeight + 5)).row(), 13);
    // The bottom band is pinned to the last rows.
    QCOMPARE(view.indexAt(QPoint(5, panes.at(2).viewportRect.y() + 5)).row(), kRows - 2);
    QCOMPARE(view.indexAt(QPoint(5, panes.at(2).viewportRect.bottom() - 5)).row(), kRows - 1);
}

void TestFrozenRows::keyboardWalksAcrossTheFrozenBoundary()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenRows(2);
    view.flushPendingRelayout();
    view.setFocus();
    view.setCurrentIndex(model.index(1, 0));
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex().row(), 2); // the first scrolling row
    QTest::keyClick(&view, Qt::Key_Up);
    QCOMPARE(view.currentIndex().row(), 1); // back into the frozen band
}

void TestFrozenRows::scrollToAFrozenRowDoesNotScroll()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenRows(2);
    view.flushPendingRelayout();
    view.setVerticalOffset(20 * kRowHeight);
    view.flushPendingRelayout();
    const qint64 offset = view.verticalOffset();

    view.scrollTo(model.index(0, 0), VirtualItemView::EnsureVisible);
    QCOMPARE(view.verticalOffset(), offset); // already visible: it is pinned
    view.scrollTo(model.index(50, 0), VirtualItemView::EnsureVisible);
    QVERIFY(view.verticalOffset() != offset);
    // The requested row ends up inside the scrolling pane, not under the frozen band.
    const QRect rect = view.visualRect(model.index(50, 0));
    const QRect pane = view.itemPaneRect(ItemPane::Type::Scrollable);
    QVERIFY(rect.top() >= pane.top());
    QVERIFY(rect.bottom() <= pane.bottom());
}

void TestFrozenRows::bottomFrozenRowsArePinnedToTheBottom()
{
    NumericListModel model(kRows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenBottomRows(2);
    view.flushPendingRelayout();
    QCOMPARE(view.frozenRows(), 0);
    QCOMPARE(view.frozenBottomRows(), 2);
    QVERIFY(view.isRowFrozen(kRows - 1));
    QVERIFY(!view.isRowFrozen(kRows - 3));
    const QRect pane = view.itemPaneRect(ItemPane::Type::FrozenBottom);
    QCOMPARE(pane.bottom() + 1, view.viewport()->height());

    QWidget *last = adapter.widgetForRow(kRows - 1);
    QVERIFY(last != nullptr);
    QCOMPARE(last->mapTo(view.viewport(), QPoint(0, 0)).y(), pane.y() + kRowHeight);

    view.setVerticalOffset(10 * kRowHeight);
    view.flushPendingRelayout();
    // The band does not move with the offset.
    QCOMPARE(last->mapTo(view.viewport(), QPoint(0, 0)).y(), pane.y() + kRowHeight);
}

void TestFrozenRows::rowWidgetsAreClippedAtTheRowPanes()
{
    auto *model = tableModel(this);
    FrozenTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenRows(2);
    view.flushPendingRelayout();

    // The frozen row widgets stay in the viewport (and above the scrolling pane).
    QWidget *firstFrozen = nullptr;
    QWidget *firstScrolling = nullptr;
    for (const MaterializedItem &item : view.materializedItems()) {
        if (item.index.row() == 0)
            firstFrozen = item.widget;
        if (item.index.row() == 5)
            firstScrolling = item.widget;
    }
    QVERIFY(firstFrozen != nullptr);
    QVERIFY(firstScrolling != nullptr);
    QCOMPARE(firstFrozen->parentWidget(), view.viewport());
    QCOMPARE(firstScrolling->parentWidget(), view.scrollingPaneHost());
    // Column hosts are still positioned by the framework inside whichever parent the
    // row widget ended up with (§27 + §31 row direction).
    auto *row = static_cast<FrozenRowWidget *>(firstScrolling);
    const QRect pane = view.itemPaneRect(ItemPane::Type::Scrollable);
    QCOMPARE(view.scrollingPaneHost()->geometry(), pane);
    // The row widget keeps its content position; the clip container is what hides the
    // part that scrolled above the pane.
    QCOMPARE(row->mapTo(view.viewport(), QPoint(0, 0)).y(), 5 * kRowHeight);
    for (int column = 0; column < kColumns; ++column) {
        QWidget *host = row->host(column);
        QVERIFY(host != nullptr);
        QCOMPARE(host->mapTo(row, QPoint(0, 0)).x(), view.columnGeometry(column).viewportX);
    }
}

void TestFrozenRows::cellsAreClippedByBothPaneDirections()
{
    auto *model = tableModel(this);
    FrozenCellAdapter adapter;
    VirtualTableView view;
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Frozen columns and frozen rows at the same time: every cell is clipped by the
    // intersection of its row pane and its column pane.
    view.setFrozenColumns({0});
    view.setFrozenRows(2);
    view.flushPendingRelayout();

    const QVector<ItemPane> rowPanes = view.itemPanes();
    const QVector<TablePane> columnPanes = view.panes();
    const QList<QWidget *> hosts = view.viewport()->findChildren<QWidget *>(
        QStringLiteral("vivPaneClipHost"), Qt::FindDirectChildrenOnly);
    QVERIFY(!hosts.isEmpty());
    // Every container is an intersection of one row pane and one column pane.
    for (QWidget *host : hosts) {
        const QRect rect = host->geometry();
        QVERIFY(!rect.isEmpty());
        bool inRowPane = false;
        for (const ItemPane &pane : rowPanes) {
            if (pane.viewportRect.intersected(rect) == rect)
                inRowPane = true;
        }
        bool inColumnPane = false;
        for (const TablePane &pane : columnPanes) {
            if (pane.viewportRect.intersected(rect) == rect)
                inColumnPane = true;
        }
        QVERIFY(inRowPane);
        QVERIFY(inColumnPane);
    }

    // The cell of the frozen rows / frozen column sits in the top-left intersection;
    // a cell of a scrolling row sits in the intersection of the scrolling row pane
    // with its column pane - that is what clips it vertically.
    QWidget *frozenCell = view.cellWidget(model->index(0, 0));
    QVERIFY(frozenCell != nullptr);
    QCOMPARE(frozenCell->parentWidget()->geometry().top(), 0);
    QCOMPARE(frozenCell->parentWidget()->geometry().left(), 0);
    QWidget *scrollingCell = view.cellWidget(model->index(5, 0));
    QVERIFY(scrollingCell != nullptr);
    QCOMPARE(scrollingCell->parentWidget()->geometry().top(),
             view.itemPaneRect(ItemPane::Type::Scrollable).top());
}
void TestFrozenRows::verticalHeaderIsSplitPerRowPane()
{
    auto *model = tableModel(this);
    FrozenTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Without frozen rows the strip is a single renderer that follows the geometry.
    QCOMPARE(verticalStrips(view).size(), 1);
    verifyRowStripsAreGlued(view, model);

    view.setFrozenRows(3);
    view.setFrozenBottomRows(2);
    view.flushPendingRelayout();
    QApplication::processEvents();
    QCOMPARE(view.itemPanes().size(), 3);
    verifyRowStripsAreGlued(view, model);

    // Scrolling moves the middle band only: every band still shows its own rows.
    view.setVerticalOffset(20 * kRowHeight);
    view.flushPendingRelayout();
    QApplication::processEvents();
    verifyRowStripsAreGlued(view, model);

    // Row heights change -> the two frozen bands keep their rows, the scrolling band
    // follows the new geometry (the explicit pane offset survives the geometry change).
    view.setRowHeight(0, kRowHeight * 2);
    view.flushPendingRelayout();
    QApplication::processEvents();
    verifyRowStripsAreGlued(view, model);

    // Switching the freezing off drops the extra strips again: an unused feature must
    // change nothing at all.
    view.setFrozenRows(0);
    view.setFrozenBottomRows(0);
    view.flushPendingRelayout();
    QApplication::processEvents();
    QCOMPARE(verticalStrips(view).size(), 1);
    QCOMPARE(view.itemPanes().size(), 1);
}
void TestFrozenRows::frozenRowsSurviveTheStateRoundTrip()
{
    auto *model = tableModel(this);
    FrozenTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenColumns({0});
    view.setFrozenRows(3);
    view.setFrozenBottomRows(2);
    view.setColumnWidth(1, kColumnWidth + 40);
    view.flushPendingRelayout();
    const QByteArray state = view.saveHeaderState();
    QVERIFY(!state.isEmpty());

    // A second view restores the same state: frozen rows, frozen columns and the column
    // width all come back, and the row-number strips are split accordingly.
    auto *otherModel = tableModel(this);
    FrozenTableAdapter otherAdapter;
    VirtualTableView other;
    other.setTableAdapter(&otherAdapter);
    other.setUniformItemHeight(kRowHeight);
    other.setDefaultColumnWidth(kColumnWidth);
    other.setModel(otherModel);
    showView(&other, QSize(kViewWidth, kViewHeight));

    QVERIFY(other.restoreHeaderState(state));
    QCOMPARE(other.frozenRows(), 3);
    QCOMPARE(other.frozenBottomRows(), 2);
    QCOMPARE(other.frozenColumns(), QVector<int>({0}));
    QCOMPARE(other.columnWidth(1), kColumnWidth + 40);
    QCOMPARE(other.itemPanes().size(), 3);
    verifyRowStripsAreGlued(other, otherModel);

    // A version 1 state (no frozen row counts) still restores: its columns come back and
    // the rows simply stay unfrozen.
    QByteArray legacy = state.left(state.size() - 8);
    QBuffer buffer(&legacy);
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    QDataStream patch(&buffer);
    patch.setVersion(QDataStream::Qt_5_15);
    patch.skipRawData(4); // magic
    patch << quint32(1);  // version
    buffer.close();

    auto *legacyModel = tableModel(this);
    FrozenTableAdapter legacyAdapter;
    VirtualTableView legacyView;
    legacyView.setTableAdapter(&legacyAdapter);
    legacyView.setUniformItemHeight(kRowHeight);
    legacyView.setDefaultColumnWidth(kColumnWidth);
    legacyView.setModel(legacyModel);
    showView(&legacyView, QSize(kViewWidth, kViewHeight));
    QVERIFY(legacyView.restoreHeaderState(legacy));
    QCOMPARE(legacyView.frozenColumns(), QVector<int>({0}));
    QCOMPARE(legacyView.frozenRows(), 0);
    QCOMPARE(legacyView.frozenBottomRows(), 0);
    QCOMPARE(legacyView.panes().size(), 2);      // frozen column pane + scrolling pane
    QCOMPARE(legacyView.itemPanes().size(), 1);  // no frozen rows in a v1 state
}

void TestFrozenRows::rowBoundaryLooksLikeTheColumnBoundary()
{
    auto *model = tableModel(this);
    FrozenTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenColumns({0});
    view.setFrozenRows(2);
    view.flushPendingRelayout();
    QApplication::processEvents();

    // Same colour as the column boundary: what the current style paints a section separator
    // with (the table probes it once and both directions use it).
    QCOMPARE(view.itemPaneSeparatorColor(), NativeHeaderView::sectionSeparatorColor(&view));

    // The row boundary line crosses the row-number strip, exactly like the column boundary
    // line crosses the header strip.
    const QList<QHeaderView *> strips = verticalStrips(view);
    QVERIFY(!strips.isEmpty());
    const QVector<QRect> rowLines = view.itemPaneSeparatorRects();
    QCOMPARE(rowLines.size(), view.itemPanes().size() - 1);
    for (const QRect &line : rowLines) {
        QVERIFY(line.left() <= strips.first()->geometry().left());
        QCOMPARE(line.right() + 1, view.viewport()->geometry().right() + 1);
    }

    // One knob for both directions: the table's separator style reaches the row boundary as
    // well (width, pen style and an optional explicit colour).
    PaneSeparatorStyle custom;
    custom.width = 3;
    custom.lineStyle = Qt::DashLine;
    custom.color = QColor(200, 30, 30);
    view.setPaneSeparatorStyle(custom);
    view.flushPendingRelayout();
    QApplication::processEvents();
    QCOMPARE(view.itemPaneSeparatorStyle(), custom);
    for (const QRect &line : view.itemPaneSeparatorRects())
        QCOMPARE(line.height(), custom.width);
}

void TestFrozenRows::frozenRowsAreClampedToWhatFits()
{
    NumericListModel model(10);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, 100));

    // Asking for more frozen rows than the model has, or more than fit into the
    // viewport, is clamped: a frozen pane never shows a row that sticks out of it.
    view.setFrozenRows(1000);
    view.flushPendingRelayout();
    QCOMPARE(view.frozenRows(), 5); // 100 px viewport / 20 px rows
    view.setFrozenRows(20);
    view.flushPendingRelayout();
    QCOMPARE(view.frozenRows(), 5);
    // The frozen band and the scrolling pane always add up to the viewport.
    const QVector<ItemPane> panes = view.itemPanes();
    int height = 0;
    for (const ItemPane &pane : panes)
        height += pane.viewportRect.height();
    QCOMPARE(height, view.viewport()->height());
}

QTEST_MAIN(TestFrozenRows)

#include "tst_frozenrows.moc"
