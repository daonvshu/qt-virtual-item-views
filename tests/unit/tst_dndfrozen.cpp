#include <virtualitemviews/virtuallistview.h>

#include "vivtestfixtures.h"

#include <QtTest>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 30;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;

QStringList numberedRows(int count)
{
    QStringList rows;
    rows.reserve(count);
    for (int row = 0; row < count; ++row)
        rows << QStringLiteral("row-%1").arg(row);
    return rows;
}

class DropAdapter : public WidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, kRowHeight); }
};

/// The row the item drawn at \a viewportY belongs to, using the same pane mapping
/// the view uses (a frozen top row keeps its content offset).
qsizetype rowAt(VirtualListView &view, int viewportY)
{
    const QModelIndex index = view.indexAt(QPoint(10, viewportY));
    return index.isValid() ? index.row() : -1;
}

/// The y the indicator line sits on (the rect is centred on the boundary).
int lineY(const QRect &line)
{
    return line.top() + line.height() / 2;
}

} // namespace

/// P1-13: with frozen rows the drag & drop resolution has to use the same pane
/// mapping as the hit test, otherwise the drop target (and its indicator) refer to
/// a row the user is not pointing at.
class TestDndFrozen : public QObject
{
    Q_OBJECT

private slots:
    void dropTargetMatchesTheRowUnderTheCursorInEveryPane();
    void dropIndicatorSitsOnTheBoundaryOfItsOwnPane();
};

void TestDndFrozen::dropTargetMatchesTheRowUnderTheCursorInEveryPane()
{
    StringListModel model(numberedRows(200));
    DropAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDragEnabled(true);
    view.setDropIndicatorShown(true);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenRows(2);
    view.flushPendingRelayout();
    settle();
    view.setVerticalOffset(50 * kRowHeight);
    view.flushPendingRelayout();
    settle();

    const QVector<ItemPane> panes = view.itemPanes();
    // Only the panes that have rows are listed: frozen top + scrolling here.
    QCOMPARE(panes.size(), 2);
    const QRect topPane = view.itemPaneRect(ItemPane::Type::FrozenTop);
    const QRect scrollPane = view.itemPaneRect(ItemPane::Type::Scrollable);

    // Frozen top band: row 0 / row 1, not the scrolled row that would be there if
    // the coordinates were naive.
    const int middleOfFrozenRowZero = topPane.top() + kRowHeight / 2;
    QCOMPARE(rowAt(view, middleOfFrozenRowZero), qsizetype(0));
    VirtualItemView::DropTarget target = view.dropTargetAt(QPoint(10, middleOfFrozenRowZero));
    QVERIFY(target.isValid());
    QCOMPARE(target.row, 0);                      // first half of row 0 -> insert before it

    const int lowerHalfOfFrozenRowZero = topPane.top() + kRowHeight - 2;
    target = view.dropTargetAt(QPoint(10, lowerHalfOfFrozenRowZero));
    QCOMPARE(target.row, 1);

    // Scrolling band: the row under the cursor is the one the hit test reports.
    const int middleOfScrollPane = scrollPane.top() + kRowHeight / 2;
    const qsizetype scrolledRow = rowAt(view, middleOfScrollPane);
    QVERIFY(scrolledRow >= 50);
    target = view.dropTargetAt(QPoint(10, middleOfScrollPane));
    QVERIFY(target.isValid());
    QCOMPARE(target.row, int(scrolledRow));
}

void TestDndFrozen::dropIndicatorSitsOnTheBoundaryOfItsOwnPane()
{
    StringListModel model(numberedRows(200));
    DropAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDragEnabled(true);
    view.setDropIndicatorShown(true);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenRows(2);
    view.flushPendingRelayout();
    settle();
    view.setVerticalOffset(50 * kRowHeight);
    view.flushPendingRelayout();
    settle();

    const QRect topPane = view.itemPaneRect(ItemPane::Type::FrozenTop);
    const QRect scrollPane = view.itemPaneRect(ItemPane::Type::Scrollable);

    // Insert before the first frozen row: the line has to sit at the top of the
    // frozen band (content offset 0), not at "0 - scrolling offset".
    VirtualItemView::DropTarget firstRow;
    firstRow.parent = QModelIndex();
    firstRow.row = 0;
    const QRect frozenLine = view.dropIndicatorRect(firstRow);
    QVERIFY(!frozenLine.isEmpty());
    QCOMPARE(lineY(frozenLine), topPane.top());

    // Insert between the two frozen rows.
    VirtualItemView::DropTarget betweenFrozen;
    betweenFrozen.row = 1;
    const QRect betweenLine = view.dropIndicatorRect(betweenFrozen);
    QVERIFY(!betweenLine.isEmpty());
    QCOMPARE(lineY(betweenLine), topPane.top() + kRowHeight);

    // Insert before the first *scrolling* row the user sees.
    const qsizetype scrolledRow = rowAt(view, scrollPane.top());
    VirtualItemView::DropTarget scrolled;
    scrolled.row = int(scrolledRow);
    const QRect scrolledLine = view.dropIndicatorRect(scrolled);
    QVERIFY(!scrolledLine.isEmpty());
    QCOMPARE(lineY(scrolledLine), scrollPane.top());
}

QTEST_MAIN(TestDndFrozen)

#include "tst_dndfrozen.moc"
