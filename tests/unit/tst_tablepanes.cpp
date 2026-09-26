#include <virtualitemviews/tablepane.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/nativeheaderview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
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

/// Section adapter of the header pane-cache test: one label per section.
class HeaderSectionAdapter : public HeaderWidgetAdapter
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setText(QStringLiteral("section"));
        return label;
    }
    void bindSection(QWidget *widget, int logicalIndex) override
    {
        static_cast<QLabel *>(widget)->setText(QStringLiteral("s%1").arg(logicalIndex));
    }
    void unbindSection(QWidget *widget, int) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

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

/// Counts the diagnostics of setPaneSpecs(): the review asks for "warning + reject
/// or first wins", so the tests assert both the normalization and the warning count.
int g_paneSpecWarnings = 0;

void countPaneSpecWarnings(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg && message.contains(QStringLiteral("setPaneSpecs")))
        ++g_paneSpecWarnings;
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
    void secondScrollingGroupScrollsIndependently();
    void secondScrollingGroupKeepsItsOwnClipContainer();
    void cellModeClipsEveryScrollingPane();
    void nonPrimaryPaneHeaderFollowsItsOwnGroup();
    void scrollingGroupCannotStealHitsFromItsNeighbour();
    void spansAreClippedAtEveryPaneBoundary();
    void resettingThePanesRestoresTheDefault();
    void keyboardNavigationScrollsTheGroupOfTheColumn();
    void horizontalOffsetSurvivesBeyondTheIntRange();
    void scrollingDoesNotWalkEveryColumn();
    void invalidPaneSpecsAreNormalizedWithOneWarningEach();
    void scrollingPanesShareTheWidthProportionally();
    void nonPrimaryGroupScrollDoesNotRunTheStructuralPass();
    void headerPaneCacheIsNotRebuiltWhileScrolling();
    void nativePaneHeaderKeepsItsOffsetCheap();
    void frozenColumnsFollowThePaneWindow();
    void sparseExplicitPaneMaterializesOnlyItsWindow();
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

void TestTablePanes::horizontalOffsetSurvivesBeyondTheIntRange()
{
    // 30,000 columns x 100,000 px = 3e9 px: the logical extent does not fit into
    // the int-based scroll bar, so the bar has to be compressed (ScrollMapper)
    // instead of truncating the offset back into the int range.
    auto *model = new QStandardItemModel(1, 30000, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(100000);
    view.setModel(model);
    // No widget layout here: QHeaderView and the scroll bar themselves work in
    // checked int arithmetic, so this test covers *our* 64-bit layer (geometry,
    // pane layout, offset clamping) which is what the review's finding is about.
    view.resize(kViewWidth, kViewHeight);

    QVERIFY(view.horizontalContentExtent() > qint64(std::numeric_limits<int>::max()));
    const qint64 maximum = view.maximumHorizontalOffset();
    QVERIFY(maximum > qint64(std::numeric_limits<int>::max()));

    // Scrolling to the very end reaches the logical maximum, even though the bar
    // only carries an int.
    view.setHorizontalOffset(maximum);
    QCOMPARE(view.horizontalOffset(), maximum);

    // ... the middle is exact as well (the mapper's window is re-centred).
    const qint64 middle = maximum / 2;
    view.setHorizontalOffset(middle);
    QCOMPARE(view.horizontalOffset(), middle);

    // The far end still resolves to the last column, whose right edge is at the
    // right edge of the viewport (the column is wider than the window, so its left
    // edge is off screen - the reported x stays bounded).
    view.setHorizontalOffset(maximum);
    const ColumnGeometry geometry = view.columnGeometry(29999);
    QVERIFY(geometry.isValid());
    QVERIFY(geometry.viewportX <= 0);
    QVERIFY(geometry.viewportX + geometry.width >= view.viewport()->width());
}

void TestTablePanes::scrollingDoesNotWalkEveryColumn()
{
    constexpr int kColumns = 20000;
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    // The structural pass may walk every column...
    const qsizetype structuralVisits = view.horizontalLayoutColumnVisits();
    QVERIFY(structuralVisits >= qsizetype(kColumns));

    // ... but a scroll must not: it binary searches each pane's prefix sums and
    // only then touches the window. Without that, 20,000 columns would be walked
    // on every single wheel step.
    view.setHorizontalOffset(500 * kColumnWidth);
    const qsizetype scrollVisits = view.horizontalLayoutColumnVisits();
    QVERIFY(scrollVisits < 100);
    QVERIFY(scrollVisits * 100 < structuralVisits);

    // The visible window really moved, so the cheap path was not just skipped.
    const QVector<int> visible = view.visibleColumnLogicalIndexes();
    QVERIFY(!visible.isEmpty());
    QVERIFY(visible.first() > 0);
    QVERIFY(visible.last() < kColumns);
}

void TestTablePanes::invalidPaneSpecsAreNormalizedWithOneWarningEach()
{
    // docs/spans.md §5: one column belongs to one pane, a scroll group is never
    // negative and the panes of one group have to be neighbours. Each broken
    // configuration is normalized (first wins / clamp / keep but warn) and reports
    // exactly one diagnostic per problem per call.
    auto *model = new QStandardItemModel(20, kColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // (a) A column in two panes: the first pane keeps it, the second one loses it.
    const QVector<TablePaneSpec> duplicated
        = {frozenPane({0, 1}), scrollablePane({2, 3, 4}), frozenPane({5}),
           frozenPane({0, 6})};
    g_paneSpecWarnings = 0;
    QtMessageHandler defaultHandler = qInstallMessageHandler(countPaneSpecWarnings);
    view.setPanes(duplicated);
    qInstallMessageHandler(defaultHandler);
    QCOMPARE(g_paneSpecWarnings, 1);
    QCOMPARE(view.paneSpecs().size(), 4);
    QCOMPARE(view.paneSpecs().at(3).logicalColumns, QVector<int>({6}));
    QCOMPARE(view.paneSpecs().at(0).logicalColumns, QVector<int>({0, 1}));
    QCOMPARE(view.paneIndexOfColumn(0), 0);
    QCOMPARE(view.paneIndexOfColumn(6), 3);

    // The normalized list is what paneSpecs() returns, so passing the same broken
    // list again is a no-op: no second warning, no second relayout.
    g_paneSpecWarnings = 0;
    defaultHandler = qInstallMessageHandler(countPaneSpecWarnings);
    view.setPanes(duplicated);
    qInstallMessageHandler(defaultHandler);
    QCOMPARE(g_paneSpecWarnings, 0);
    QCOMPARE(view.paneSpecs().size(), 4);

    // (b) A negative scroll group is clamped to 0 (the group the view scrolls with
    // its own horizontal scroll bar).
    g_paneSpecWarnings = 0;
    defaultHandler = qInstallMessageHandler(countPaneSpecWarnings);
    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3, 4, 5, 6, 7, 8, 9}, -3)});
    qInstallMessageHandler(defaultHandler);
    view.flushPendingRelayout();
    QCOMPARE(g_paneSpecWarnings, 1);
    QCOMPARE(view.paneSpecs().at(1).scrollGroup, 0);
    QVERIFY(view.maximumHorizontalOffset(0) > 0);
    QCOMPARE(view.maximumHorizontalOffset(-3), qint64(0));
    view.setHorizontalOffset(view.maximumHorizontalOffset(0));
    QVERIFY(view.horizontalOffset(0) > 0);

    // (c) One group split in two places: kept as written (pane indexes must not
    // move) but reported once.
    g_paneSpecWarnings = 0;
    defaultHandler = qInstallMessageHandler(countPaneSpecWarnings);
    view.setPanes(twoGroupPanes());
    qInstallMessageHandler(defaultHandler);
    QCOMPARE(g_paneSpecWarnings, 0); // legal: each group is one run
    g_paneSpecWarnings = 0;
    defaultHandler = qInstallMessageHandler(countPaneSpecWarnings);
    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3}, 0), frozenPane({4}),
                   scrollablePane({5, 6, 7, 8, 9}, 0)});
    qInstallMessageHandler(defaultHandler);
    view.flushPendingRelayout();
    QCOMPARE(g_paneSpecWarnings, 1);
    QCOMPARE(view.paneSpecs().size(), 4);
    QCOMPARE(view.paneSpecs().at(1).scrollGroup, 0);
    QCOMPARE(view.paneSpecs().at(3).scrollGroup, 0);
    QCOMPARE(view.paneIndexOfColumn(5), 3);
}

void TestTablePanes::scrollingPanesShareTheWidthProportionally()
{
    // Three scrolling groups with the same extent have to get the same width (P2-7).
    // The old formula divided the *remaining* width by the total scrolling extent, so
    // every pane was proportionally smaller than its predecessor: with a 900 px
    // viewport that is 134 / 100 / 66 instead of 100 / 100 / 100 - and with more
    // groups the tail collapses to nothing.
    auto *model = new QStandardItemModel(20, 12, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setPanes({scrollablePane({0, 1, 2}, 0), scrollablePane({3, 4, 5}, 1),
                   scrollablePane({6, 7, 8}, 2)});
    view.flushPendingRelayout();

    const QVector<TablePane> panes = view.panes();
    QCOMPARE(panes.size(), 3);
    const int total = view.viewport()->width();
    const int share = total / 3; // equal extents: exactly a third, rounded down
    QCOMPARE(panes.at(1).viewportRect.width(), share);
    QCOMPARE(panes.at(2).viewportRect.width(), share);
    // The primary pane (the first scrolling group) absorbs the rounding, so the
    // widths still add up to the viewport and the panes stay adjacent.
    QCOMPARE(panes.at(0).viewportRect.width(), total - 2 * share);
    QCOMPARE(panes.at(0).viewportRect.x(), 0);
    QCOMPARE(panes.at(1).viewportRect.x(), panes.at(0).viewportRect.width());
    QCOMPARE(panes.at(2).viewportRect.x(),
             panes.at(0).viewportRect.width() + panes.at(1).viewportRect.width());
    QCOMPARE(panes.at(2).viewportRect.x() + panes.at(2).viewportRect.width(), total);

    // Different extents still follow the ratio (3 : 3 : 4 columns of equal width).
    view.setPanes({scrollablePane({0, 1, 2}, 0), scrollablePane({3, 4, 5}, 1),
                   scrollablePane({6, 7, 8, 9}, 2)});
    view.flushPendingRelayout();
    const QVector<TablePane> ratioPanes = view.panes();
    const int threeTenths = total * 3 / 10;
    const int fourTenths = total * 4 / 10;
    QCOMPARE(ratioPanes.at(1).viewportRect.width(), threeTenths);
    QCOMPARE(ratioPanes.at(2).viewportRect.width(), fourTenths);
    QCOMPARE(ratioPanes.at(0).viewportRect.width(), total - threeTenths - fourTenths);
    QCOMPARE(ratioPanes.at(0).viewportRect.width() + ratioPanes.at(1).viewportRect.width()
                 + ratioPanes.at(2).viewportRect.width(),
             total);
}

void TestTablePanes::nonPrimaryGroupScrollDoesNotRunTheStructuralPass()
{
    // P1-9 of the second review: the primary group scrolls through the fast path
    // (refreshScrollWindows), a second group through a full structural pass - O(total
    // columns) per step. Both take the same path now.
    constexpr int kManyColumns = 20000;
    auto *model = new QStandardItemModel(20, kManyColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVector<int> secondGroup;
    for (int column = 5; column < kManyColumns; ++column)
        secondGroup.append(column);
    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3}, 0), frozenPane({4}),
                   scrollablePane(secondGroup, 1)});
    view.flushPendingRelayout();
    QVERIFY(view.maximumHorizontalOffset(1) > 0);
    const qsizetype structuralVisits = view.horizontalLayoutColumnVisits();
    QVERIFY(structuralVisits >= qsizetype(kManyColumns));

    // One step of the non-primary group must not walk every column.
    view.setHorizontalOffset(1, kColumnWidth);
    const qsizetype scrollVisits = view.horizontalLayoutColumnVisits();
    QVERIFY(scrollVisits < 100);
    QVERIFY(scrollVisits * 100 < structuralVisits);

    // ... and it really moved that group's window.
    QCOMPARE(view.horizontalOffset(1), qint64(kColumnWidth));
    const QRect paneRect = view.panes().at(view.paneIndexOfColumn(5)).viewportRect;
    const ColumnGeometry geometry = view.columnGeometry(5);
    QVERIFY(geometry.viewportX < paneRect.x());
}

void TestTablePanes::headerPaneCacheIsNotRebuiltWhileScrolling()
{
    // P1-8 of the second review: a pane-filtered header rebuilt (and sorted) its pane's
    // column list inside every sectionX() call and scanned the filter in every
    // isFiltered() - O(N^2) per pass for a 100k column pane. The cache is now rebuilt only
    // when the filter or the geometry changed, so scrolling must not touch it.
    constexpr int kWideColumns = 20000;
    auto *model = new QStandardItemModel(5, kWideColumns, this);
    PaneTableAdapter adapter;               // rows of the body (not the header)
    HeaderSectionAdapter sectionAdapter;    // one label per header section
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&sectionAdapter);
    view.setHorizontalHeader(header);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenColumns({0});            // the primary pane gets a filter + follows geometry
    view.flushPendingRelayout();

    const quint64 rebuildsAfterStructure = header->paneCacheRebuildCount();
    QVERIFY(rebuildsAfterStructure > 0);   // the pane cache had to be built once
    QVERIFY(!header->materializedSections().isEmpty());

    for (int step = 1; step <= 50; ++step) {
        // A remainder keeps the first visible column partially scrolled out.
        view.setHorizontalOffset(kColumnWidth / 2 + step * kColumnWidth);
        view.flushPendingRelayout();
    }
    QCOMPARE(header->paneCacheRebuildCount(), rebuildsAfterStructure);

    // The sections still follow the new offset (the cheap path is not "skip the layout"):
    // the column at the pane's left edge is partially scrolled out, so its x is negative.
    bool partiallyScrolled = false;
    for (int logical : header->materializedSections()) {
        if (QWidget *widget = header->sectionWidget(logical))
            partiallyScrolled = partiallyScrolled || widget->x() < 0;
    }
    QVERIFY(partiallyScrolled);

    // A real structure change does rebuild it.
    view.setFrozenColumns({0, 1});
    view.flushPendingRelayout();
    QVERIFY(header->paneCacheRebuildCount() > rebuildsAfterStructure);
}

void TestTablePanes::nativePaneHeaderKeepsItsOffsetCheap()
{
    // P1 of the third review: the *body* of a non-primary group scrolled through the
    // window fast path, but its native header pane re-read the whole geometry on every
    // single step - so one wheel step still cost O(total sections). A pane offset only
    // shifts the sections (QHeaderView::setOffset), it never changes width, order or
    // visibility, so the full sync must not run here.
    constexpr int kManyColumns = 20000;
    auto *model = new QStandardItemModel(20, kManyColumns, this);
    PaneTableAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVector<int> secondGroup;
    for (int column = 5; column < kManyColumns; ++column)
        secondGroup.append(column);
    view.setPanes({frozenPane({0}), scrollablePane({1, 2, 3}, 0), frozenPane({4}),
                   scrollablePane(secondGroup, 1)});
    view.flushPendingRelayout();
    QVERIFY(view.maximumHorizontalOffset(1) > 0);

    // The pane renderer of the second group is a native header on the pane's rectangle.
    const QRect paneRect = view.panes().at(view.paneIndexOfColumn(5)).viewportRect;
    const int paneX = view.viewport()->geometry().x() + paneRect.x();
    NativeHeaderView *paneHeader = nullptr;
    for (NativeHeaderView *candidate : view.findChildren<NativeHeaderView *>()) {
        if (candidate->orientation() != Qt::Horizontal || !candidate->isVisible())
            continue;
        if (candidate->geometry().x() == paneX
            && candidate->geometry().width() == paneRect.width()) {
            paneHeader = candidate;
            break;
        }
    }
    QVERIFY(paneHeader);

    const quint64 syncsBefore = paneHeader->fullSyncCount();
    for (int step = 1; step <= 100; ++step)
        view.setHorizontalOffset(1, qint64(step) * kColumnWidth);

    // The offset really moved: the group's first visible column is 100 slots to the
    // right (column 5 leads the group, the columns are 100 px wide).
    QCOMPARE(view.horizontalOffset(1), qint64(100) * kColumnWidth);
    QCOMPARE(view.columnAtViewportX(paneRect.x() + 5), 105);
    // ... without a single full re-read of the geometry.
    QCOMPARE(paneHeader->fullSyncCount(), syncsBefore);
}

void TestTablePanes::frozenColumnsFollowThePaneWindow()
{
    // P1 of the third review: the cell pass materialized *every* frozen column, so a
    // 50,000 column frozen pane cost 50,000 cells per visible row even though the pane
    // shows a few dozen. The frozen pane has the same window machinery as a scrolling
    // one (offset 0), so the body is bounded by the viewport like the header.
    constexpr int kManyColumns = 20000;
    constexpr int kFrozen = 10000;
    constexpr int kRows = 3;
    auto *model = new QStandardItemModel(kRows, kManyColumns, this);
    PaneCellAdapter adapter;
    VirtualTableView view;
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // (a) A frozen pane that fits: the frozen prefix plus the scrolling window.
    view.setFrozenColumns({0, 1, 2});
    view.flushPendingRelayout();
    const QVector<int> narrow = view.visibleColumnLogicalIndexes();
    QVERIFY(narrow.contains(0) && narrow.contains(1) && narrow.contains(2));
    QVERIFY(narrow.size() <= 3 + kViewWidth / kColumnWidth + 2);

    // (b) A frozen pane with thousands of columns: the pane takes the whole viewport
    //     (that is the pane width rule), and only what it can show is materialized.
    QVector<int> frozen;
    frozen.reserve(kFrozen);
    for (int column = 0; column < kFrozen; ++column)
        frozen.append(column);
    view.setFrozenColumns(frozen);
    view.flushPendingRelayout();

    const QVector<int> wide = view.visibleColumnLogicalIndexes();
    const qsizetype bound = qsizetype(kViewWidth / kColumnWidth + 4);
    QVERIFY(!wide.isEmpty());
    QVERIFY(wide.size() <= bound);
    QCOMPARE(wide.first(), 0);
    // Only the leading columns fit into the pane: 10,000 of them would be 1,000,000 px.
    QVERIFY(wide.last() < 100);
    // The cell pass follows the same window, per visible row - not per frozen column.
    QVERIFY(view.materializedCellCount() <= bound * kRows);

    // And a scroll still takes the window fast path (no walk over the frozen columns).
    view.setHorizontalOffset(1, kColumnWidth);           // the frozen pane ignores it
    view.setHorizontalOffset(kColumnWidth * 5);
    QVERIFY(view.horizontalLayoutColumnVisits() < 100);
}

void TestTablePanes::sparseExplicitPaneMaterializesOnlyItsWindow()
{
    // P2 of the third review: the columns of one pane are adjacent *slots*, but their
    // global visual order may span the whole table. A pane of {0, 50000, 99999} used to
    // turn the header pass into a walk over 100,000 visuals; the pane's own slot window
    // is the right measure.
    constexpr int kManyColumns = 100000;
    auto *model = new QStandardItemModel(5, kManyColumns, this);
    PaneTableAdapter adapter;               // rows of the body (not the header)
    HeaderSectionAdapter sectionAdapter;    // one label per header section
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&sectionAdapter);
    view.setHorizontalHeader(header);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QVector<int> scrollable;
    for (int column = 1; column < kManyColumns; ++column) {
        if (column != 50000 && column != 99999)
            scrollable.append(column);
    }
    view.setPanes({frozenPane({0, 50000, 99999}), scrollablePane(scrollable, 0)});
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    // The sparse pane has its own renderer (the primary pane keeps the installed one).
    VirtualHeaderView *paneHeader = nullptr;
    for (VirtualHeaderView *candidate : view.findChildren<VirtualHeaderView *>()) {
        if (candidate != header && candidate->orientation() == Qt::Horizontal) {
            paneHeader = candidate;
            break;
        }
    }
    QVERIFY(paneHeader);
    // Three columns, three sections - and a pass that looked at three slots, not at the
    // 100,000 visuals between the first and the last of them.
    QCOMPARE(paneHeader->materializedSections().size(), 3);
    QVERIFY(paneHeader->materializationVisits() <= 16);
    QVERIFY(paneHeader->paneCacheRebuildCount() > 0);
}

QTEST_MAIN(TestTablePanes)
#include "tst_tablepanes.moc"
