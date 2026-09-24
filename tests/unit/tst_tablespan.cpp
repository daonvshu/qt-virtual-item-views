#include <virtualitemviews/tablespan.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 20;
constexpr int kColumnWidth = 100;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 220;

/// Table body adapter: one row widget per row (§26). Spans never touch the
/// widgets of Row Widget Mode, so a label is enough here.
class SpanTableAdapter : public TableWidgetAdapter
{
public:
    explicit SpanTableAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new TestRowWidget(parent, kRowHeight);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<TestRowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<TestRowWidget *>(widget)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * m_columnCount, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

/// Cell Widget Mode adapter (§28): one widget per visible cell.
class SpanCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("cellLabel"));
        return label;
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

/// Row widget with one ColumnHost per column: the framework positions the hosts
/// (§26/§27), which is exactly what spans change in Row Widget Mode.
class SpanRowWidget : public QWidget
{
public:
    SpanRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setGeometry(0, 0, kColumnWidth, kRowHeight);
            m_hosts.append(host);
        }
    }

    ColumnHost *host(int column) const { return m_hosts.value(column); }

    QSize sizeHint() const override { return QSize(kColumnWidth * 4, kRowHeight); }

private:
    QVector<ColumnHost *> m_hosts;
};

class SpanHostAdapter : public TableWidgetAdapter
{
public:
    explicit SpanHostAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new SpanRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}
    void unbindWidget(QWidget *, const QModelIndex &) override {}

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * m_columnCount, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

QStandardItemModel *buildModel(int rows, int columns, QObject *parent)
{
    auto *model = new QStandardItemModel(rows, columns, parent);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            model->setItem(row, column,
                           new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
        }
    }
    return model;
}
} // namespace

/// §43 "spans" (see docs/spans.md): a span is projection information only. The
/// merged rectangle always comes from the committed column geometry and the row
/// layout, hit testing folds to the anchor, and the cell mode only materializes
/// anchors.
class TestTableSpan : public QObject
{
    Q_OBJECT

private slots:
    void withoutProviderNothingIsMerged();
    void mapProviderResolvesAnchorAndCoveredCells();
    void mergedRectIsTheUnionOfTheCommittedGeometry();
    void mergedRectClampsToTheModelAndThePane();
    void hiddenColumnInsideASpanDoesNotCompensate();
    void indexAtFoldsToTheAnchor();
    void spansFollowTheAnchorItemThroughModelChanges();
    void cellWidgetModeMaterializesOnlyAnchors();
    void spansFollowTheCommittedGeometry();
    void dropTargetFoldsToTheAnchorColumn();
    void rowWidgetModeFoldsTheColumnHosts();
};

void TestTableSpan::withoutProviderNothingIsMerged()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const QModelIndex index = model.index(1, 1);
    QVERIFY(!view.spanProvider());
    QCOMPARE(view.spanAt(index), TableSpan());
    QCOMPARE(view.anchorIndex(index), index);
    QVERIFY(!view.isSpanCovered(index));
    QCOMPARE(view.spanRect(index), view.cellRect(index));
    QCOMPARE(view.spanRect(index).size(), QSize(kColumnWidth, kRowHeight));
    QCOMPARE(view.spanRect(index).x(), view.columnGeometry(1).viewportX);
    QCOMPARE(view.spanRect(index).top(), view.visualRect(model.index(1, 0)).top());
}

void TestTableSpan::mapProviderResolvesAnchorAndCoveredCells()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // A 2x2 merge anchored at (1, 1).
    view.setSpan(1, 1, 2, 2);

    QCOMPARE(view.spanAt(model.index(1, 1)), TableSpan({2, 2}));
    QVERIFY(view.isSpanCovered(model.index(1, 2)));
    QVERIFY(view.isSpanCovered(model.index(2, 1)));
    QVERIFY(view.isSpanCovered(model.index(2, 2)));
    QVERIFY(!view.isSpanCovered(model.index(1, 1)));
    QVERIFY(!view.isSpanCovered(model.index(3, 2)));
    // Only the anchor owns the merge: a covered cell reports 1x1 to the provider
    // contract, so "who is merged" has exactly one answer.
    QCOMPARE(view.spanAt(model.index(2, 2)), TableSpan());
    QCOMPARE(view.anchorIndex(model.index(2, 2)), model.index(1, 1));
    QCOMPARE(view.anchorIndex(model.index(0, 0)), model.index(0, 0));
    // A covered cell has no rectangle of its own.
    QVERIFY(view.cellRect(model.index(2, 2)).isEmpty());

    view.removeSpan(1, 1);
    QVERIFY(!view.isSpanCovered(model.index(2, 2)));
    view.setSpan(1, 1, 2, 2);
    view.clearSpans();
    QVERIFY(!view.isSpanCovered(model.index(2, 2)));
    QCOMPARE(view.spanAt(model.index(1, 1)), TableSpan());
}

void TestTableSpan::mergedRectIsTheUnionOfTheCommittedGeometry()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setSpan(1, 1, 1, 3);
    view.flushPendingRelayout();
    const QRect merged = view.spanRect(model.index(1, 1));
    QCOMPARE(merged.width(), 3 * kColumnWidth);
    QCOMPARE(merged.height(), kRowHeight);
    QCOMPARE(merged.x(), view.columnGeometry(1).viewportX);
    QCOMPARE(merged.top(), view.visualRect(model.index(1, 0)).top());

    // A row span adds the covered rows' heights.
    view.setSpan(1, 1, 2, 3);
    const QRect block = view.spanRect(model.index(1, 1));
    QCOMPARE(block.width(), 3 * kColumnWidth);
    QCOMPARE(block.height(), 2 * kRowHeight);

    // The merged width follows a column resize (the geometry stays the single
    // source of truth).
    view.setColumnWidth(2, 140);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model.index(1, 1)).width(), kColumnWidth + 140 + kColumnWidth);
}

void TestTableSpan::mergedRectClampsToTheModelAndThePane()
{
    QStandardItemModel model(3, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // A span that runs past the last column/row is clamped, not invalid.
    view.setSpan(2, 3, 5, 5);
    const QRect clamped = view.spanRect(model.index(2, 3));
    QCOMPARE(clamped.width(), kColumnWidth);
    QCOMPARE(clamped.height(), kRowHeight);

    // A span never crosses a pane boundary: it is clipped to the anchor's pane.
    view.setSpan(0, 1, 1, 3);
    view.setFrozenColumns({0, 1});
    view.flushPendingRelayout();
    const QRect clipped = view.spanRect(model.index(0, 1));
    QCOMPARE(clipped.width(), kColumnWidth); // the frozen pane holds columns 0..1
    QCOMPARE(clipped.x(), view.columnGeometry(1).viewportX);
    // The very same span anchored in the scrollable pane uses both columns.
    view.setSpan(0, 2, 1, 2);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model.index(0, 2)).width(), 2 * kColumnWidth);
}

void TestTableSpan::hiddenColumnInsideASpanDoesNotCompensate()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setSpan(1, 1, 1, 3);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model.index(1, 1)).width(), 3 * kColumnWidth);

    // Hiding a covered column makes the merge narrower: the geometry is the
    // single source of truth, a span never compensates.
    view.setColumnHidden(2, true);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model.index(1, 1)).width(), 2 * kColumnWidth);
    QCOMPARE(view.spanAt(model.index(1, 1)), TableSpan({1, 3}));

    view.setColumnHidden(2, false);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model.index(1, 1)).width(), 3 * kColumnWidth);
}

void TestTableSpan::indexAtFoldsToTheAnchor()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setSpan(0, 0, 2, 2);
    view.flushPendingRelayout();

    // Anywhere inside the merged area reports the anchor, so click, hover,
    // context menu, drag & drop and accessibility all see one target.
    const VirtualItemView &asView = view;
    const QPoint inside(2 + kColumnWidth - 4, kRowHeight - 4);
    const QModelIndex hit = asView.indexAt(inside);
    QCOMPARE(hit, model.index(0, 0));
    QCOMPARE(asView.indexAt(QPoint(kColumnWidth + 5, 2)), model.index(0, 0));
    QCOMPARE(asView.indexAt(QPoint(5, kRowHeight + 5)), model.index(0, 0));
    // Outside the merged area the hit stays the plain cell.
    QCOMPARE(asView.indexAt(QPoint(2 * kColumnWidth + 5, 2)), model.index(0, 2));
    QCOMPARE(asView.indexAt(QPoint(5, 2 * kRowHeight + 5)), model.index(2, 0));
}

void TestTableSpan::spansFollowTheAnchorItemThroughModelChanges()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // The anchor is the *item* (QPersistentModelIndex), so inserting above it
    // keeps the merge on the same row content: the row moved from 3 to 4.
    view.setSpan(3, 1, 1, 2);
    QCOMPARE(view.spanAt(model.index(3, 1)), TableSpan({1, 2}));

    model.insertRow(0);
    view.flushPendingRelayout();
    QCOMPARE(view.spanAt(model.index(4, 1)), TableSpan({1, 2}));
    QCOMPARE(view.spanAt(model.index(3, 1)), TableSpan());
    QCOMPARE(view.anchorIndex(model.index(4, 2)), model.index(4, 1));
    QCOMPARE(view.spanRect(model.index(4, 1)).width(), 2 * kColumnWidth);
    QCOMPARE(view.spanRect(model.index(4, 1)).top(), view.visualRect(model.index(4, 0)).top());

    // Removing the anchor drops the merge (the node itself is gone).
    model.removeRow(4);
    view.flushPendingRelayout();
    QVERIFY(!view.isSpanCovered(model.index(3, 2)));
}

void TestTableSpan::cellWidgetModeMaterializesOnlyAnchors()
{
    QStandardItemModel model(20, 6);
    for (int row = 0; row < 20; ++row) {
        for (int column = 0; column < 6; ++column) {
            model.setItem(row, column,
                          new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
        }
    }
    SpanCellAdapter cellAdapter;
    VirtualTableView view;
    view.setCellAdapter(&cellAdapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const qsizetype withoutSpans = view.materializedCellCount();
    QVERIFY(withoutSpans > 0);

    // A 2x2 merge replaces four cell widgets by one.
    view.setSpan(1, 1, 2, 2);
    view.flushPendingRelayout();
    QCOMPARE(view.materializedCellCount(), withoutSpans - 3);
    QWidget *anchorWidget = view.cellWidget(model.index(1, 1));
    QVERIFY(anchorWidget);
    QVERIFY(!view.cellWidget(model.index(1, 2)));
    QVERIFY(!view.cellWidget(model.index(2, 1)));
    QVERIFY(!view.cellWidget(model.index(2, 2)));
    // The anchor widget covers the whole merged rectangle.
    QCOMPARE(anchorWidget->size(), QSize(2 * kColumnWidth, 2 * kRowHeight));

    // Clearing the spans brings the covered cells back.
    view.clearSpans();
    view.flushPendingRelayout();
    QCOMPARE(view.materializedCellCount(), withoutSpans);
    QVERIFY(view.cellWidget(model.index(2, 2)));
}

void TestTableSpan::spansFollowTheCommittedGeometry()
{
    QStandardItemModel model(10, 8);
    SpanTableAdapter adapter(8);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setSpan(1, 2, 1, 3);
    view.flushPendingRelayout();

    // Scrolling the scrollable pane moves the merged rect with the columns.
    const QRect before = view.spanRect(model.index(1, 2));
    view.setHorizontalOffset(150);
    view.flushPendingRelayout();
    const QRect after = view.spanRect(model.index(1, 2));
    QCOMPARE(after.width(), before.width());
    QCOMPARE(after.x(), before.x() - 150);

    // Row heights are part of the merged rect as well.
    view.setRowHeight(2, 44);
    view.setSpan(1, 2, 2, 3);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(model.index(1, 2)).height(), kRowHeight + 44);
}

void TestTableSpan::dropTargetFoldsToTheAnchorColumn()
{
    QStandardItemModel model(10, 4);
    SpanTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    // Cell drops resolve the column under the cursor, so that is the path where
    // a covered cell could hand the model a column that has no cell of its own.
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setSpan(0, 1, 1, 2); // columns 1..2 of row 0 are one cell
    view.flushPendingRelayout();

    // Hovering the covered column 2 reports the anchor column 1.
    const VirtualItemView::DropTarget inside
        = view.dropTargetAt(QPoint(2 * kColumnWidth + 5, 2));
    QCOMPARE(inside.row, 0);
    QCOMPARE(inside.column, 1);
    // The insertion line marks the merged area, not the covered column.
    const QRect merged = view.spanRect(model.index(0, 1));
    QCOMPARE(merged.x(), kColumnWidth);
    QCOMPARE(merged.width(), 2 * kColumnWidth);
    const QRect line = view.dropIndicatorRect(inside);
    QCOMPARE(line.x(), merged.x());
    QCOMPARE(line.width(), merged.width());
}

void TestTableSpan::rowWidgetModeFoldsTheColumnHosts()
{
    QStandardItemModel model(6, 4);
    SpanHostAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Without spans the framework places one host per column, exactly as before.
    auto *row = static_cast<SpanRowWidget *>(view.widgetForIndex(model.index(0, 0)));
    QVERIFY(row);
    for (int column = 0; column < 4; ++column) {
        QCOMPARE(row->host(column)->x(), view.columnGeometry(column).viewportX);
        QCOMPARE(row->host(column)->width(), kColumnWidth);
        QVERIFY(row->host(column)->isVisible());
    }

    // A span over columns 1..2: the anchor host takes the merged rectangle and
    // the covered host disappears (it has no cell of its own).
    view.setSpan(0, 1, 1, 2);
    view.flushPendingRelayout();
    row = static_cast<SpanRowWidget *>(view.widgetForIndex(model.index(0, 0)));
    QVERIFY(row);
    QCOMPARE(row->host(1)->x(), view.columnGeometry(1).viewportX);
    QCOMPARE(row->host(1)->width(), 2 * kColumnWidth);
    QVERIFY(!row->host(2)->isVisible());
    QVERIFY(row->host(0)->isVisible());
    QCOMPARE(row->host(3)->x(), view.columnGeometry(3).viewportX);
    QCOMPARE(row->host(3)->width(), kColumnWidth);
    // Another row is untouched by the span of row 0.
    auto *otherRow = static_cast<SpanRowWidget *>(view.widgetForIndex(model.index(1, 0)));
    QVERIFY(otherRow);
    QVERIFY(otherRow->host(2)->isVisible());
    QCOMPARE(otherRow->host(2)->width(), kColumnWidth);

    // Row spans cannot extend a host below its own row in this mode: the merged
    // rectangle is clipped to the row (docs/spans.md §3), the width still merges.
    view.setSpan(0, 1, 2, 2);
    view.flushPendingRelayout();
    row = static_cast<SpanRowWidget *>(view.widgetForIndex(model.index(0, 0)));
    QVERIFY(row);
    QCOMPARE(row->host(1)->height(), kRowHeight);
    QCOMPARE(row->host(1)->width(), 2 * kColumnWidth);
    QVERIFY(!row->host(2)->isVisible());

    // Clearing the spans brings every host back where it was.
    view.clearSpans();
    view.flushPendingRelayout();
    row = static_cast<SpanRowWidget *>(view.widgetForIndex(model.index(0, 0)));
    QVERIFY(row);
    for (int column = 0; column < 4; ++column) {
        QVERIFY(row->host(column)->isVisible());
        QCOMPARE(row->host(column)->x(), view.columnGeometry(column).viewportX);
        QCOMPARE(row->host(column)->width(), kColumnWidth);
    }
}

QTEST_MAIN(TestTableSpan)
#include "tst_tablespan.moc"
