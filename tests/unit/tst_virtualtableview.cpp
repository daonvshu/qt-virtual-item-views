#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QScrollBar>
#include <QStandardItemModel>

#include <algorithm>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 30;
constexpr int kColumnWidth = 100;
constexpr int kViewWidth = 460;
constexpr int kViewHeight = 320;

QString cellText(int row, int column)
{
    return QStringLiteral("r%1c%2").arg(row).arg(column);
}

/// Row widget with one ColumnHost per column: the framework owns the geometry.
class TableRowWidget : public QWidget
{
public:
    TableRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        m_hosts.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(2, 0, 80, 18);
            m_hosts.append(host);
            m_labels.append(label);
        }
    }

    ColumnHost *host(int logicalColumn) const { return m_hosts.value(logicalColumn); }
    QLabel *label(int logicalColumn) const { return m_labels.value(logicalColumn); }

    QSize sizeHint() const override { return QSize(400, kRowHeight); }

private:
    QVector<ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

class TableTestAdapter : public TableWidgetAdapter
{
public:
    explicit TableTestAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new TableRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++bound;
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column) {
            row->label(column)->setText(
                index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        ++unbound;
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column)
            row->label(column)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(400, kRowHeight);
    }

    int created = 0;
    int bound = 0;
    int unbound = 0;

private:
    int m_columnCount = 0;
};

QStandardItemModel *buildModel(int rows, int columns, QObject *parent)
{
    auto *model = new QStandardItemModel(rows, columns, parent);
    QStringList labels;
    for (int column = 0; column < columns; ++column)
        labels << QStringLiteral("Column %1").arg(column);
    model->setHorizontalHeaderLabels(labels);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column)
            model->setItem(row, column, new QStandardItem(cellText(row, column)));
    }
    return model;
}

/// Row/column model without per-cell storage: used for the million-row case.
class BigTableModel : public QAbstractTableModel
{
public:
    explicit BigTableModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(rows)
        , m_columns(columns)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return cellText(index.row(), index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("Column %1").arg(section)
                                             : QString::number(section + 1);
    }

private:
    int m_rows = 0;
    int m_columns = 0;
};

QWidget *rowWidgetFor(VirtualTableView &view, int row)
{
    for (const MaterializedItem &item : view.materializedItems()) {
        if (item.index.row() == row)
            return item.widget;
    }
    return nullptr;
}
} // namespace

class TestVirtualTableView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void materializesOnlyVisibleRows();
    void rowWidgetModeCreatesNoCellWidgets();
    void headerAndRowsAgreeOnColumnBoundaries();
    void columnResizeTouchesMaterializedRowsOnly();
    void geometryIsTheSingleAuthority();
    void columnMoveFollowsTheGeometry();
    void hiddenColumnHidesHost();
    void horizontalScrollKeepsHeaderAndRowsAligned();
    void headerStateRoundTrip();
    void hugeModelColumnResizeDoesNotWalkRows();
    void columnCountFollowsTheModel();
    void sortingRequestsAndAppliesModelSort();
    void verticalHeaderMirrorsRowHeights();
    void verticalHeaderFollowsVerticalScroll();
    void verticalHeaderDragChangesRowHeight();
    void rowSizePolicyControlsMeasurement();
    void keyboardMovesTheCurrentColumn();
    void visibleColumnRangeFollowsOverscanAndOffset();

private:
    QStandardItemModel *m_model = nullptr;
    TableTestAdapter *m_adapter = nullptr;
    VirtualTableView *m_view = nullptr;
};

void TestVirtualTableView::init()
{
    m_model = buildModel(50, 6, this);
    m_adapter = new TableTestAdapter(6);
    m_view = new VirtualTableView();
    m_view->setTableAdapter(m_adapter);
    m_view->setUniformItemHeight(kRowHeight);
    m_view->setDefaultColumnWidth(kColumnWidth);
    m_view->setModel(m_model);
    showView(m_view, QSize(kViewWidth, kViewHeight));
}

void TestVirtualTableView::cleanup()
{
    delete m_view;
    m_view = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestVirtualTableView::materializesOnlyVisibleRows()
{
    QCOMPARE(m_view->columnCount(), 6);
    // The horizontal header and the horizontal scrollbar reduce the viewport.
    QVERIFY(m_view->viewport()->height() < kViewHeight);
    QVERIFY(m_view->viewport()->height() > kViewHeight - 3 * m_view->headerHeight());

    // The window is visible rows + overscan; at the top of the model the "before"
    // overscan cannot be used.
    const int viewportHeight = m_view->viewport()->height();
    const qsizetype visibleRows = viewportHeight / kRowHeight + (viewportHeight % kRowHeight ? 1 : 0);
    QVERIFY(m_view->materializedItemCount() >= visibleRows);
    QVERIFY(m_view->materializedItemCount() <= visibleRows + 4);
    QCOMPARE(m_view->stats().createCount,
             quint64(m_view->materializedItemCount() + m_view->pooledWidgetCount()));
    QVERIFY(m_view->visibleRows().isValid());
    QCOMPARE(m_view->visibleRows().first, qsizetype(0));
    QCOMPARE(m_view->rowHeight(0), kRowHeight);
}

void TestVirtualTableView::rowWidgetModeCreatesNoCellWidgets()
{
    // Row Widget Mode: one widget per materialized row, no cell level widgets.
    QWidget *rowWidget = rowWidgetFor(*m_view, 0);
    QVERIFY(rowWidget != nullptr);
    QCOMPARE(rowWidget->findChildren<QWidget *>(QStringLiteral("cellLabel")).size(), 6);
    QCOMPARE(rowWidget->findChildren<ColumnHost *>().size(), 6);
    // The body owns row widgets only (materialized + pooled), never cells.
    QCOMPARE(m_view->viewport()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).size(),
             int(m_view->materializedItemCount() + m_view->pooledWidgetCount()));
    QCOMPARE(m_view->stats().createCount, quint64(m_adapter->created));
}

void TestVirtualTableView::headerAndRowsAgreeOnColumnBoundaries()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    QVERIFY(header->isVisible());

    // Column resize: header and every materialized row must agree pixel exactly.
    m_view->setColumnWidth(1, 170);
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 2));
    QVERIFY(rowWidget != nullptr);

    for (int column : m_view->visibleColumnLogicalIndexes()) {
        const ColumnGeometry geometry = m_view->columnGeometry(column);
        QCOMPARE(header->sectionViewportPosition(column), geometry.viewportX);
        QCOMPARE(header->sectionSize(column), geometry.width);

        ColumnHost *host = rowWidget->host(column);
        QVERIFY(host != nullptr);
        QCOMPARE(host->mapTo(m_view->viewport(), QPoint(0, 0)).x(), geometry.viewportX);
        QCOMPARE(host->width(), geometry.width);
    }
}

void TestVirtualTableView::columnResizeTouchesMaterializedRowsOnly()
{
    const int bindsBefore = m_adapter->bound;
    const int createdBefore = m_adapter->created;
    const qsizetype materializedBefore = m_view->materializedItemCount();

    m_view->setColumnWidth(2, 240);

    QCOMPARE(m_adapter->bound, bindsBefore);                       // no rebind
    QCOMPARE(m_view->materializedItemCount(), materializedBefore); // no rematerialize
    QCOMPARE(m_adapter->created, createdBefore);                   // no new widgets

    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 1));
    QVERIFY(rowWidget != nullptr);
    QCOMPARE(rowWidget->host(2)->width(), 240);
    QCOMPARE(m_view->columnGeometry(2).width, 240);
}

void TestVirtualTableView::geometryIsTheSingleAuthority()
{
    // Mutating only the HeaderGeometry must be visible in the body: the view
    // keeps no second authoritative column state (§45.10).
    HeaderGeometry *geometry = m_view->horizontalHeaderGeometry();
    geometry->resizeSection(1, 260);
    QCOMPARE(m_view->columnGeometry(1).width, 260);
    QCOMPARE(m_view->columnWidth(1), 260);

    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);
    QCOMPARE(rowWidget->host(1)->width(), 260);

    geometry->setSectionHidden(1, true);
    QVERIFY(m_view->isColumnHidden(1));
    QVERIFY(!rowWidget->host(1)->isVisible());
    geometry->setSectionHidden(1, false);
    QVERIFY(rowWidget->host(1)->isVisible());
}

void TestVirtualTableView::columnMoveFollowsTheGeometry()
{
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);
    const int xOfColumnThreeBefore = rowWidget->host(3)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    QVERIFY(xOfColumnThreeBefore > 0);

    m_view->moveColumn(0, 3); // move logical 0 to the visual position of logical 3

    const int xOfColumnThreeAfter = rowWidget->host(3)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    const int xOfColumnZero = rowWidget->host(0)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    QVERIFY(xOfColumnThreeAfter != xOfColumnThreeBefore);
    QVERIFY(xOfColumnZero > xOfColumnThreeAfter);
    QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), 1);
}

void TestVirtualTableView::hiddenColumnHidesHost()
{
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);
    const int xOfColumnTwo = rowWidget->host(2)->mapTo(m_view->viewport(), QPoint(0, 0)).x();

    m_view->setColumnHidden(1, true);
    QVERIFY(!rowWidget->host(1)->isVisible());
    QCOMPARE(m_view->columnWidth(1), 0);
    QVERIFY(rowWidget->host(2)->mapTo(m_view->viewport(), QPoint(0, 0)).x() < xOfColumnTwo);

    m_view->setColumnHidden(1, false);
    QVERIFY(rowWidget->host(1)->isVisible());
    QCOMPARE(rowWidget->host(2)->mapTo(m_view->viewport(), QPoint(0, 0)).x(), xOfColumnTwo);
}

void TestVirtualTableView::horizontalScrollKeepsHeaderAndRowsAligned()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);

    QCOMPARE(m_view->horizontalOffset(), qint64(0));
    m_view->setHorizontalOffset(180);
    QCOMPARE(m_view->horizontalOffset(), qint64(180));
    QCOMPARE(m_view->horizontalScrollBar()->value(), 180);

    for (int column : m_view->visibleColumnLogicalIndexes()) {
        const ColumnGeometry geometry = m_view->columnGeometry(column);
        QCOMPARE(header->sectionViewportPosition(column), geometry.viewportX);
        QCOMPARE(rowWidget->host(column)->mapTo(m_view->viewport(), QPoint(0, 0)).x(),
                 geometry.viewportX);
    }

    m_view->scrollByHorizontalPixels(-1000);
    QCOMPARE(m_view->horizontalOffset(), qint64(0));
    m_view->setHorizontalOffset(1000000);
    QCOMPARE(m_view->horizontalOffset(), m_view->maximumHorizontalOffset());
}

void TestVirtualTableView::headerStateRoundTrip()
{
    m_view->setColumnWidth(1, 150);
    m_view->setColumnHidden(4, true);
    m_view->moveColumn(0, 2);
    const QByteArray state = m_view->saveHeaderState();
    QVERIFY(!state.isEmpty());
    const qint64 extent = m_view->horizontalContentExtent();

    m_view->setColumnWidth(1, 400);
    m_view->setColumnHidden(4, false);
    m_view->moveColumn(2, 0);
    QVERIFY(m_view->horizontalContentExtent() != extent);

    QVERIFY(m_view->restoreHeaderState(state));
    QCOMPARE(m_view->columnWidth(1), 150);
    QVERIFY(m_view->isColumnHidden(4));
    QCOMPARE(m_view->horizontalContentExtent(), extent);
    QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), 1);
}

void TestVirtualTableView::hugeModelColumnResizeDoesNotWalkRows()
{
    BigTableModel model(1000000, 5, this);
    TableTestAdapter adapter(5);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const int bindsBefore = adapter.bound;
    const int createdBefore = adapter.created;
    view.setColumnWidth(3, 321);

    QCOMPARE(adapter.bound, bindsBefore);
    QCOMPARE(adapter.created, createdBefore);
    QVERIFY(view.materializedItemCount() < 40);
    QCOMPARE(view.columnGeometry(3).width, 321);
    QCOMPARE(view.verticalHeaderGeometry()->sectionCount(), 0); // uniform: default only
}

void TestVirtualTableView::columnCountFollowsTheModel()
{
    QCOMPARE(m_view->horizontalHeaderGeometry()->sectionCount(), 6);
    m_model->insertColumn(6);
    QCOMPARE(m_view->columnCount(), 7);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sectionCount(), 7);

    m_model->removeColumn(0);
    QCOMPARE(m_view->columnCount(), 6);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sectionCount(), 6);
}

void TestVirtualTableView::sortingRequestsAndAppliesModelSort()
{
    QSignalSpy sortSpy(m_view, &VirtualTableView::sortIndicatorRequested);
    m_view->setSortingEnabled(true);

    // A header click writes the sort indicator into the geometry; the view
    // reacts by sorting the model (§33).
    m_view->horizontalHeaderGeometry()->setSortIndicator(1, Qt::AscendingOrder);
    QCOMPARE(sortSpy.count(), 1);
    QCOMPARE(sortSpy.at(0).at(0).toInt(), 1);

    QStringList values;
    for (int row = 0; row < m_model->rowCount(); ++row)
        values << m_model->data(m_model->index(row, 1)).toString();
    std::sort(values.begin(), values.end());
    QCOMPARE(m_model->data(m_model->index(0, 1)).toString(), values.first());

    m_view->sortByColumn(1, Qt::DescendingOrder);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 1);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::DescendingOrder);
    QCOMPARE(m_model->data(m_model->index(0, 1)).toString(), values.last());

    m_view->setSortingEnabled(false);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), -1);
}

void TestVirtualTableView::verticalHeaderMirrorsRowHeights()
{
    auto *model = buildModel(20, 4, this);
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    HeaderGeometry *rowHeaders = view.verticalHeaderGeometry();
    QCOMPARE(rowHeaders->sectionCount(), 20);
    for (qsizetype row = 0; row < 20; ++row)
        QCOMPARE(rowHeaders->storedSectionSize(int(row)), view.rowHeight(row));

    // A user resize of the row-number strip becomes an explicit row height.
    rowHeaders->resizeSection(3, 64);
    QCOMPARE(view.rowHeight(3), 64);
    QVERIFY(view.hasExplicitRowHeight(3));
    QCOMPARE(rowHeaders->storedSectionSize(3), 64);
}

void TestVirtualTableView::verticalHeaderFollowsVerticalScroll()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->verticalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    QVERIFY(header->isVisible());

    for (int step = 1; step <= 4; ++step) {
        m_view->verticalScrollBar()->setValue(step * kRowHeight * 3);
        m_view->flushPendingRelayout();

        // The row-number strip consumes the same vertical offset as the body.
        QCOMPARE(m_view->verticalHeaderGeometry()->viewportOffset(), m_view->verticalOffset());
        QCOMPARE(qint64(header->offset()), m_view->verticalOffset());

        // Every visible row number sits exactly at its row's y position.
        const VisibleRange rows = m_view->visibleRows();
        QVERIFY(rows.isValid());
        for (qsizetype row = rows.first; row <= rows.last; ++row) {
            const QRect rect = m_view->visualRect(m_model->index(int(row), 0));
            QCOMPARE(header->sectionViewportPosition(int(row)), rect.top());
        }
    }
}

void TestVirtualTableView::verticalHeaderDragChangesRowHeight()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->verticalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    QCOMPARE(m_view->rowHeight(2), kRowHeight);
    QCOMPARE(m_view->itemHeightMode(), VirtualItemView::ItemHeightMode::Uniform);

    // A user drag on the row-number strip ends up in QHeaderView::resizeSection();
    // it must become an explicit row height (and switch the table to variable
    // heights) instead of being swallowed.
    header->resizeSection(2, 64);
    m_view->flushPendingRelayout();

    QCOMPARE(m_view->itemHeightMode(), VirtualItemView::ItemHeightMode::Variable);
    QCOMPARE(m_view->rowHeight(2), 64);
    QVERIFY(m_view->hasExplicitRowHeight(2));
    QCOMPARE(m_view->visualRect(m_model->index(2, 0)).height(), 64);
    // Neighbouring rows keep the height they had.
    QCOMPARE(m_view->rowHeight(1), kRowHeight);
    QCOMPARE(m_view->verticalHeaderGeometry()->storedSectionSize(2), 64);
    QCOMPARE(header->sectionSize(2), 64);
}

void TestVirtualTableView::rowSizePolicyControlsMeasurement()
{
    auto *model = buildModel(20, 4, this);
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    // ExplicitWins (default): measurement must not override the user's height.
    view.setRowHeight(2, 70);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(2), 70);
    QCOMPARE(view.rowSizePolicy(), VirtualTableView::RowSizePolicy::ExplicitWins);

    // MeasuredWins: the widget size hint (30) wins again.
    view.setRowSizePolicy(VirtualTableView::RowSizePolicy::MeasuredWins);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(2), kRowHeight);

    view.clearRowHeight(2);
    QVERIFY(!view.hasExplicitRowHeight(2));
}

void TestVirtualTableView::keyboardMovesTheCurrentColumn()
{
    m_view->setCurrentIndex(m_model->index(1, 1));
    QTest::keyClick(m_view, Qt::Key_Right);
    QCOMPARE(m_view->currentIndex().column(), 2);
    QCOMPARE(m_view->currentIndex().row(), 1);
    QTest::keyClick(m_view, Qt::Key_Left);
    QCOMPARE(m_view->currentIndex().column(), 1);
    QTest::keyClick(m_view, Qt::Key_Down);
    QCOMPARE(m_view->currentIndex().row(), 2);
    QCOMPARE(m_view->currentIndex().column(), 1);

    // Hidden columns are skipped by horizontal navigation.
    m_view->setColumnHidden(2, true);
    QTest::keyClick(m_view, Qt::Key_Right);
    QCOMPARE(m_view->currentIndex().column(), 3);
}

void TestVirtualTableView::visibleColumnRangeFollowsOverscanAndOffset()
{
    // 6 columns of 100 px in a ~460 px viewport: 5 visible, plus overscan.
    const QVector<int> tight = m_view->visibleColumnLogicalIndexes();
    QCOMPARE(tight.size(), 5);
    QCOMPARE(tight.first(), 0);

    m_view->setColumnOverscan(2);
    const QVector<int> wide = m_view->visibleColumnLogicalIndexes();
    QVERIFY(wide.size() >= tight.size());
    QCOMPARE(wide.first(), 0);
    QCOMPARE(wide.last(), 5);

    m_view->setColumnOverscan(0);
    m_view->setHorizontalOffset(200);
    const QVector<int> scrolled = m_view->visibleColumnLogicalIndexes();
    QVERIFY(!scrolled.isEmpty());
    QVERIFY(scrolled.first() >= 1);
    const VisibleRange visual = m_view->visibleColumns();
    QVERIFY(visual.isValid());
}

QTEST_MAIN(TestVirtualTableView)

#include "tst_virtualtableview.moc"
