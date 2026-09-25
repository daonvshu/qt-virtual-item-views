#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/virtuallistview.h>

#include "vivtestfixtures.h"

#include <QtTest>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kEstimatedHeight = 20;
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

/// The item drawn at \a viewportY, plus how far into it that position is.
///
/// A row is placed at viewport y = offsetOf(row) - verticalOffset(), so the
/// content offset under \a viewportY is verticalOffset() + viewportY - also when
/// frozen rows cover the top of the viewport.
struct ItemAt
{
    QModelIndex index;
    int offsetInsideItem = 0;
};

ItemAt itemAt(VirtualListView &view, int viewportY)
{
    ItemAt result;
    if (!view.listLayout() || !view.model())
        return result;
    const qint64 contentOffset = view.verticalOffset() + viewportY;
    const qsizetype row = view.listLayout()->indexAtOffset(contentOffset);
    if (row < 0 || row >= view.listLayout()->itemCount())
        return result;
    result.index = view.model()->index(int(row), 0);
    result.offsetInsideItem = int(contentOffset - view.listLayout()->offsetOf(row));
    return result;
}

int scrollPaneTop(VirtualListView &view)
{
    return view.itemPaneRect(ItemPane::Type::Scrollable).top();
}

/// Every row reports \a height, so that measuring them is a no-op and the offsets
/// in the test are exactly predictable.
void setUniformRowHeights(StringListModel &model, int rows, int height)
{
    for (int row = 0; row < rows; ++row)
        model.setRowHeight(row, height);
}

} // namespace

/// P1-2: when a row *above* the viewport changes height (measurement feedback of
/// an overscan row, async image, expand/collapse), the item the user is looking
/// at must stay where it is. That has to hold with frozen rows too, where the
/// scrolling pane's top edge is frozenTopExtent() below the viewport top.
class TestDynamicAnchor : public QObject
{
    Q_OBJECT

private slots:
    void overscanRowAboveTheViewportKeepsTheTopItem();
    void frozenTopRowsDoNotChangeTheAnchor();
    void frozenBottomRowsDoNotChangeTheAnchor();
};

void TestDynamicAnchor::overscanRowAboveTheViewportKeepsTheTopItem()
{
    StringListModel model(numberedRows(1000));
    setUniformRowHeights(model, 1000, kEstimatedHeight);
    TestAdapter adapter(kEstimatedHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kEstimatedHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setVerticalOffset(500 * kEstimatedHeight);
    view.flushPendingRelayout();
    settle();

    const ItemAt before = itemAt(view, 0);
    QVERIFY(before.index.isValid());
    QCOMPARE(before.offsetInsideItem, 0);

    // A row in the overscan above the viewport grows by 200 px: this is what the
    // measurement feedback of an asynchronously loaded row does.
    const qsizetype grownRow = before.index.row() - 2;
    QVERIFY(view.widgetForIndex(model.index(int(grownRow), 0)) != nullptr);
    model.setRowHeight(int(grownRow), kEstimatedHeight + 200);
    view.flushPendingRelayout();
    settle();
    view.flushPendingRelayout();

    const ItemAt after = itemAt(view, 0);
    QCOMPARE(after.index, before.index);
    QCOMPARE(after.offsetInsideItem, before.offsetInsideItem);
    // The viewport moved by exactly the grown height, so the same row is still
    // at the top.
    QCOMPARE(view.verticalOffset(), qint64(500 * kEstimatedHeight + 200));
}

void TestDynamicAnchor::frozenTopRowsDoNotChangeTheAnchor()
{
    StringListModel model(numberedRows(1000));
    setUniformRowHeights(model, 1000, kEstimatedHeight);
    TestAdapter adapter(kEstimatedHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kEstimatedHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenRows(3);
    view.flushPendingRelayout();
    settle();
    view.setVerticalOffset(400 * kEstimatedHeight);
    view.flushPendingRelayout();
    settle();

    const int paneTop = scrollPaneTop(view);
    QVERIFY(paneTop > 0);
    const ItemAt before = itemAt(view, paneTop);
    QVERIFY(before.index.isValid());

    const qsizetype grownRow = before.index.row() - 1;
    QVERIFY(view.widgetForIndex(model.index(int(grownRow), 0)) != nullptr);
    model.setRowHeight(int(grownRow), kEstimatedHeight + 120);
    view.flushPendingRelayout();
    settle();
    view.flushPendingRelayout();

    const ItemAt after = itemAt(view, paneTop);
    QCOMPARE(after.index, before.index);
    QCOMPARE(after.offsetInsideItem, before.offsetInsideItem);

    // The frozen band itself never moves.
    const QVector<ItemPane> panes = view.itemPanes();
    QVERIFY(!panes.isEmpty());
    QCOMPARE(panes.first().type, ItemPane::Type::FrozenTop);
    QCOMPARE(panes.first().firstRow, qsizetype(0));
    QCOMPARE(panes.first().viewportRect.top(), 0);
}

void TestDynamicAnchor::frozenBottomRowsDoNotChangeTheAnchor()
{
    StringListModel model(numberedRows(1000));
    setUniformRowHeights(model, 1000, kEstimatedHeight);
    TestAdapter adapter(kEstimatedHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kEstimatedHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenBottomRows(2);
    view.flushPendingRelayout();
    settle();
    view.setVerticalOffset(300 * kEstimatedHeight);
    view.flushPendingRelayout();
    settle();

    const ItemAt before = itemAt(view, scrollPaneTop(view));
    QVERIFY(before.index.isValid());

    const qsizetype grownRow = before.index.row() - 1;
    QVERIFY(view.widgetForIndex(model.index(int(grownRow), 0)) != nullptr);
    model.setRowHeight(int(grownRow), kEstimatedHeight + 80);
    view.flushPendingRelayout();
    settle();
    view.flushPendingRelayout();

    const ItemAt after = itemAt(view, scrollPaneTop(view));
    QCOMPARE(after.index, before.index);
    QCOMPARE(after.offsetInsideItem, before.offsetInsideItem);
}

QTEST_MAIN(TestDynamicAnchor)

#include "tst_dynamicanchor.moc"
