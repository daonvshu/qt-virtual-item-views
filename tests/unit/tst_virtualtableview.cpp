#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QPainter>
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

namespace {

/// Colour a scrollable column would bleed with if it were not clipped.
QColor paneProbeColor(int column)
{
    static const QColor colors[] = {
        QColor(200, 0, 0), QColor(0, 0, 200), QColor(200, 140, 0),
        QColor(160, 0, 160), QColor(0, 150, 150),
    };
    return colors[column % 5];
}

/// Child of a column host: paints one thin stripe so a leak is easy to spot.
class PaneProbe : public QWidget
{
public:
    PaneProbe(int column, QWidget *parent)
        : QWidget(parent)
        , m_column(column)
    {
        setGeometry(0, 0, kColumnWidth, kRowHeight);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(0, 4 + m_column * 3, width(), 2), paneProbeColor(m_column));
    }

private:
    int m_column = 0;
};

/// Row widget with a custom opaque background: the framework must not paint over
/// it when the row is inside a frozen pane.
class PaintedRowWidget : public QWidget
{
public:
    PaintedRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new ColumnHost(column, this);
            new PaneProbe(column, host);
        }
    }

    static QColor background()
    {
        return QColor(0, 128, 0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), background());
    }
};

class PaintedRowAdapter : public TableWidgetAdapter
{
public:
    explicit PaintedRowAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new PaintedRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * 2, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

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
    void columnMoveReordersTheHeader();
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
    void frozenColumnsStayWhileTheScrollablePaneScrolls();
    void frozenPanesDoNotAddScrollSpace();
    void frozenColumnsFollowTheSingleGeometry();
    void frozenRightPaneIsPinnedToTheRightEdge();
    void frozenPanesSplitTheHeader();
    void frozenPaneIsUnaffectedByScrolling();
    void frozenPaneKeepsTheRowBackground();
    void frozenPanesDrawBodySeparatorLines();
    void paneSeparatorStyleIsCustomizable();

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

void TestVirtualTableView::columnMoveReordersTheHeader()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);

    const auto visualOrderOf = [this](const QHeaderView *view) {
        QVector<int> order;
        for (int visual = 0; visual < m_view->columnCount(); ++visual)
            order.append(view->logicalIndex(visual));
        return order;
    };
    const auto geometryOrder = [this]() {
        QVector<int> order;
        HeaderGeometry *geometry = m_view->horizontalHeaderGeometry();
        for (int visual = 0; visual < m_view->columnCount(); ++visual)
            order.append(geometry->logicalIndex(visual));
        return order;
    };
    QCOMPARE(visualOrderOf(header), QVector<int>({0, 1, 2, 3, 4, 5}));

    // Moving a column through the geometry has to reorder the header as well,
    // otherwise header and body disagree (§45.1).
    m_view->moveColumn(4, 0);
    QCOMPARE(geometryOrder(), QVector<int>({4, 0, 1, 2, 3, 5}));
    QCOMPARE(visualOrderOf(header), geometryOrder());

    // Header and body stay pixel aligned for every column.
    const int viewportX = m_view->viewport()->geometry().x();
    for (int column = 0; column < m_view->columnCount(); ++column) {
        QCOMPARE(header->geometry().x() + header->sectionViewportPosition(column),
                 m_view->columnGeometry(column).viewportX + viewportX);
    }

    // restoreHeaderState() restores the header order through the same path.
    const QByteArray state = m_view->saveHeaderState();
    m_view->moveColumn(0, 4);
    QVERIFY(m_view->restoreHeaderState(state));
    QCOMPARE(geometryOrder(), QVector<int>({4, 0, 1, 2, 3, 5}));
    QCOMPARE(visualOrderOf(header), geometryOrder());
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

void TestVirtualTableView::frozenColumnsStayWhileTheScrollablePaneScrolls()
{
    // 6 columns of 100 px: freeze the first two (§31).
    m_view->setFrozenColumns(QVector<int>({0, 1}));
    QCOMPARE(m_view->frozenColumns(), QVector<int>({0, 1}));
    QVERIFY(m_view->isColumnFrozen(0));
    QVERIFY(m_view->isColumnFrozen(1));
    QVERIFY(!m_view->isColumnFrozen(2));

    const QVector<TablePane> panes = m_view->panes();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes.at(0).type, TablePane::Type::FrozenLeft);
    QCOMPARE(panes.at(0).viewportRect.x(), 0);
    QCOMPARE(panes.at(0).viewportRect.width(), 2 * kColumnWidth);
    QCOMPARE(panes.at(0).logicalColumns, QVector<int>({0, 1}));
    QCOMPARE(panes.at(1).type, TablePane::Type::Scrollable);
    QCOMPARE(panes.at(1).viewportRect.x(), 2 * kColumnWidth);

    // The scrollable pane starts behind the frozen columns.
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    QCOMPARE(m_view->columnGeometry(1).viewportX, kColumnWidth);
    QCOMPARE(m_view->columnGeometry(2).viewportX, 2 * kColumnWidth);

    m_view->setHorizontalOffset(150);
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    QCOMPARE(m_view->columnGeometry(1).viewportX, kColumnWidth);
    QCOMPARE(m_view->columnGeometry(2).viewportX, 2 * kColumnWidth - 150);

    // The row widget's column hosts follow the same committed geometry, so the
    // frozen host keeps its pane position.
    auto *row = static_cast<TableRowWidget *>(m_view->widgetForIndex(m_model->index(0, 0)));
    QVERIFY(row != nullptr);
    QVERIFY(row->host(0) != nullptr);
    // The scrollable hosts live in the framework's clip container, so their own
    // x is relative to it; the viewport position is what has to match the body.
    const auto hostViewportX = [this, row](int column) {
        return row->host(column)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    };
    QCOMPARE(hostViewportX(0), 0);
    QCOMPARE(hostViewportX(1), kColumnWidth);
    QCOMPARE(hostViewportX(2), 2 * kColumnWidth - 150);
    // Frozen columns stay visible; a scrollable column that is completely under
    // the frozen pane is hidden, a fully scrollable one is not.
    QVERIFY(row->host(0)->isVisible());
    QVERIFY(row->host(1)->isVisible());
    QVERIFY(!row->host(2)->isVisible());
    QVERIFY(row->host(4)->isVisible());
    QVERIFY(row->host(0)->parentWidget() == row);
    QVERIFY(row->host(2)->parentWidget() != row); // inside the clip container

    // The framework clips the scrollable columns with a container instead of
    // repainting them, so a row widget keeps its own background and nothing else
    // has to change: no masks, no background fills.
    QVERIFY(row->host(0)->mask().isEmpty());
    QVERIFY(row->host(2)->mask().isEmpty());
    QWidget *clipHost = row->host(2)->parentWidget();
    QVERIFY(clipHost != nullptr && clipHost != row);
    QCOMPARE(clipHost->geometry().x(), 2 * kColumnWidth);
    QCOMPARE(clipHost->geometry().width(), m_view->viewport()->width() - 2 * kColumnWidth);
    // Hidden/fully scrolled-out scrollable columns are hidden, not repainted.
    QCOMPARE(row->host(3)->mapTo(m_view->viewport(), QPoint(0, 0)).x(), 3 * kColumnWidth - 150);
}

void TestVirtualTableView::frozenPanesDoNotAddScrollSpace()
{
    const int viewportWidth = m_view->viewport()->width();
    const qint64 plain = m_view->maximumHorizontalOffset();
    QCOMPARE(plain, qMax<qint64>(0, 6 * kColumnWidth - viewportWidth));

    // Freezing redistributes the viewport: the scrollable content shrinks by
    // exactly the frozen width, so the range stays the same (no phantom scroll
    // space), only the scrollable pane gets narrower.
    m_view->setFrozenColumns(QVector<int>({0}));
    QCOMPARE(m_view->panes().at(1).viewportRect.width(), viewportWidth - kColumnWidth);
    QCOMPARE(m_view->maximumHorizontalOffset(), plain);

    m_view->setHorizontalOffset(plain);
    QCOMPARE(m_view->horizontalOffset(), plain);
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    // At the end of the range the last scrollable column ends exactly at the
    // right edge of the scrollable pane.
    const ColumnGeometry last = m_view->columnGeometry(5);
    QCOMPARE(last.viewportX + last.width, viewportWidth);

    // A frozen right pane stops the scrollable pane before it.
    m_view->setFrozenRightColumns(QVector<int>({5}));
    QCOMPARE(m_view->panes().at(1).viewportRect.width(), viewportWidth - 2 * kColumnWidth);
    QCOMPARE(m_view->maximumHorizontalOffset(), plain);
    m_view->setHorizontalOffset(plain);
    QCOMPARE(m_view->columnGeometry(5).viewportX, viewportWidth - kColumnWidth);
    const ColumnGeometry previous = m_view->columnGeometry(4);
    QCOMPARE(previous.viewportX + previous.width, viewportWidth - kColumnWidth);

    m_view->clearFrozenColumns();
    QVERIFY(m_view->frozenColumns().isEmpty());
    QVERIFY(m_view->frozenRightColumns().isEmpty());
    QCOMPARE(m_view->panes().size(), 1);
    QCOMPARE(m_view->panes().at(0).type, TablePane::Type::Scrollable);
}

void TestVirtualTableView::frozenColumnsFollowTheSingleGeometry()
{
    m_view->setFrozenColumns(QVector<int>({0}));
    const int scrollableBefore = m_view->columnGeometry(1).viewportX;
    QCOMPARE(scrollableBefore, kColumnWidth);

    // Resizing a frozen column grows the pane: there is no width copy, the
    // scrollable pane simply starts further right.
    m_view->setColumnWidth(0, kColumnWidth + 40);
    QCOMPARE(m_view->panes().at(0).viewportRect.width(), kColumnWidth + 40);
    QCOMPARE(m_view->columnGeometry(1).viewportX, scrollableBefore + 40);
    QCOMPARE(m_view->columnGeometry(0).width, kColumnWidth + 40);

    // A hidden frozen column leaves the pane, the others close the gap.
    m_view->setColumnHidden(0, true);
    QCOMPARE(m_view->panes().size(), 1);
    QCOMPARE(m_view->panes().at(0).type, TablePane::Type::Scrollable);
    QCOMPARE(m_view->columnGeometry(1).viewportX, 0);
    QVERIFY(!m_view->isColumnFrozen(0));

    m_view->setColumnHidden(0, false);
    QCOMPARE(m_view->panes().size(), 2);
    QCOMPARE(m_view->panes().at(0).viewportRect.width(), kColumnWidth + 40);
}

void TestVirtualTableView::frozenRightPaneIsPinnedToTheRightEdge()
{
    m_view->setFrozenRightColumns(QVector<int>({5}));
    const int viewportWidth = m_view->viewport()->width();

    const QVector<TablePane> panes = m_view->panes();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes.at(0).type, TablePane::Type::Scrollable);
    QCOMPARE(panes.at(1).type, TablePane::Type::FrozenRight);
    QCOMPARE(panes.at(1).logicalColumns, QVector<int>({5}));
    QCOMPARE(panes.at(1).viewportRect.x() + panes.at(1).viewportRect.width(), viewportWidth);
    QCOMPARE(m_view->columnGeometry(5).viewportX, viewportWidth - kColumnWidth);

    // Even at the end of the scroll range the frozen right column stays visible.
    m_view->setHorizontalOffset(m_view->maximumHorizontalOffset());
    QCOMPARE(m_view->columnGeometry(5).viewportX, viewportWidth - kColumnWidth);
    QVERIFY(m_view->columnGeometry(4).viewportX < viewportWidth - kColumnWidth);
}

void TestVirtualTableView::frozenPanesSplitTheHeader()
{
    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->setHorizontalOffset(150);
    QApplication::processEvents();

    // Each pane has its own native header renderer, all driven by the same
    // HeaderGeometry (§31).
    QHeaderView *frozenHeader = nullptr;
    QHeaderView *scrollableHeader = nullptr;
    NativeHeaderView *frozenPaneHeader = nullptr;
    for (QHeaderView *header : m_view->findChildren<QHeaderView *>()) {
        if (header->orientation() != Qt::Horizontal || !header->isVisible())
            continue;
        if (header->width() == 2 * kColumnWidth) {
            frozenHeader = header;
            frozenPaneHeader = qobject_cast<NativeHeaderView *>(header);
        } else {
            scrollableHeader = header;
        }
    }
    QVERIFY(frozenHeader != nullptr);
    QVERIFY(scrollableHeader != nullptr);
    QVERIFY(frozenPaneHeader != nullptr);

    const int viewportX = m_view->viewport()->geometry().x();
    // The frozen pane header sits on the frozen pane and never scrolls.
    QCOMPARE(frozenHeader->geometry().x(), viewportX);
    QCOMPARE(frozenHeader->sectionViewportPosition(0), 0);
    QCOMPARE(frozenHeader->sectionViewportPosition(1), kColumnWidth);
    QVERIFY(frozenHeader->isSectionHidden(2));

    // The scrollable pane header starts behind it, shows the scrollable columns
    // and moves with the offset - the same offset the body uses.
    QCOMPARE(scrollableHeader->geometry().x(), viewportX + 2 * kColumnWidth);
    QVERIFY(scrollableHeader->isSectionHidden(0));
    QVERIFY(scrollableHeader->isSectionHidden(1));
    QVERIFY(!scrollableHeader->isSectionHidden(2));
    // QHeaderView only separates sections inside a header, so the frozen pane
    // header draws the line at the pane boundary itself (§31).
    QCOMPARE(frozenPaneHeader->paneSeparatorEdge(), Qt::RightEdge);
    QCOMPARE(int(qobject_cast<NativeHeaderView *>(scrollableHeader)->paneSeparatorEdge()), 0);
    // Header and body agree pixel for pixel inside every pane (§45.1): the pane
    // header's position is relative to its own widget.
    for (int column = 0; column < m_view->columnCount(); ++column) {
        const ColumnGeometry geometry = m_view->columnGeometry(column);
        const QHeaderView *header = m_view->isColumnFrozen(column) ? frozenHeader : scrollableHeader;
        // Body x is viewport relative, the header lives in view coordinates.
        QCOMPARE(header->geometry().x() + header->sectionViewportPosition(column),
                 geometry.viewportX + viewportX);
    }

    // A frozen column resize moves both panes (single source of truth).
    m_view->setColumnWidth(1, kColumnWidth + 30);
    QCOMPARE(frozenHeader->width(), 2 * kColumnWidth + 30);
    QCOMPARE(scrollableHeader->geometry().x(), viewportX + 2 * kColumnWidth + 30);
}

void TestVirtualTableView::frozenPaneIsUnaffectedByScrolling()
{
    // The rendered frozen pane must not depend on the horizontal offset: if the
    // scrollable columns bleed through (or the frozen columns move), the two
    // renders differ.
    // A translucent table background must not defeat the cover (the Windows 11
    // style of Qt 6.8 hands out a translucent QPalette::Base).
    QPalette translucent = m_view->palette();
    translucent.setColor(QPalette::Base, QColor(255, 255, 255, 180));
    m_view->setPalette(translucent);

    m_view->setFrozenColumns(QVector<int>({0, 1}));
    const auto renderPane = [this](qsizetype offset) {
        m_view->setHorizontalOffset(offset);
        m_view->flushPendingRelayout();
        QApplication::processEvents();
        const QImage full = m_view->viewport()->grab().toImage();
        return full.copy(QRect(0, 0, 2 * kColumnWidth, full.height()));
    };

    const QImage first = renderPane(150);
    const QImage second = renderPane(157);
    QCOMPARE(first.size(), second.size());

    int differentPixels = 0;
    QRect diffRect;
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            if (first.pixel(x, y) == second.pixel(x, y))
                continue;
            ++differentPixels;
            diffRect = diffRect.isNull() ? QRect(x, y, 1, 1) : diffRect.united(QRect(x, y, 1, 1));
        }
    }
    QVERIFY2(differentPixels == 0,
             qPrintable(QStringLiteral("frozen pane changed while scrolling: %1 pixels, first diff at (%2,%3)")
                            .arg(differentPixels)
                            .arg(diffRect.x())
                            .arg(diffRect.y())));
}

void TestVirtualTableView::frozenPaneKeepsTheRowBackground()
{
    // The row widget paints its own background: the frozen pane must show it
    // unchanged (no base coloured fill on top) while the scrollable columns stay
    // clipped away.
    auto *model = buildModel(20, 4, this);
    PaintedRowAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenColumns(QVector<int>({0}));
    view.setHorizontalOffset(150); // column 2 sits under the frozen pane
    view.flushPendingRelayout();
    QApplication::processEvents();

    const QImage image = view.viewport()->grab().toImage();
    int rowBackground = 0;
    int leaked = 0;
    int frozenContent = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < kColumnWidth; ++x) {
            const QColor pixel(image.pixel(x, y));
            if (pixel == PaintedRowWidget::background())
                ++rowBackground;
            if (pixel == paneProbeColor(0))
                ++frozenContent;
            for (int column = 1; column < 4; ++column) {
                if (pixel == paneProbeColor(column))
                    ++leaked;
            }
        }
    }
    QVERIFY2(rowBackground > 0, "the row background was painted over in the frozen pane");
    // The frozen column itself is painted: it is not clipped away.
    QVERIFY2(frozenContent > 0, "the frozen column disappeared");
    QCOMPARE(leaked, 0);
}
void TestVirtualTableView::frozenPanesDrawBodySeparatorLines()
{
    const auto lines = [this]() {
        return m_view->findChildren<QWidget *>(QStringLiteral("vivPaneSeparatorLine"));
    };

    // Nothing frozen: no line at all, the body is untouched.
    QCOMPARE(lines().size(), 0);

    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->flushPendingRelayout();
    QApplication::processEvents();

    QCOMPARE(lines().size(), 1);
    const int viewportX = m_view->viewport()->geometry().x();
    const int viewportY = m_view->viewport()->geometry().y();
    // The band lies inside the frozen pane, like the header line does.
    QCOMPARE(lines().first()->geometry().x(), 2 * kColumnWidth - 1);
    QCOMPARE(lines().first()->width(), 1);
    QCOMPARE(lines().first()->height(), m_view->viewport()->height());
    QVERIFY(lines().first()->isVisible());

    // The body line uses the colour the style paints section separators with, so
    // it matches the lines between the other columns.
    const QImage image = m_view->grab().toImage();
    const QColor separator = NativeHeaderView::sectionSeparatorColor(m_view);
    QCOMPARE(image.pixelColor(viewportX + 2 * kColumnWidth - 1, viewportY + 20), separator);

    // A frozen right pane adds the line of the other boundary.
    m_view->setFrozenRightColumns(QVector<int>({5}));
    m_view->flushPendingRelayout();
    QCOMPARE(lines().size(), 2);
    QCOMPARE(lines().at(1)->geometry().x(), m_view->viewport()->width() - kColumnWidth);

    m_view->clearFrozenColumns();
    m_view->flushPendingRelayout();
    QCOMPARE(lines().size(), 0);
}

void TestVirtualTableView::paneSeparatorStyleIsCustomizable()
{
    m_view->setFrozenColumns(QVector<int>({0}));
    m_view->flushPendingRelayout();
    QApplication::processEvents();
    const int boundary = kColumnWidth;
    const int viewportY = m_view->viewport()->geometry().y();

    // Default: 1 px, style coloured.
    QCOMPARE(m_view->paneSeparatorStyle().width, 1);
    QVERIFY(!m_view->paneSeparatorStyle().color.isValid());

    // A custom colour and width apply to the header line and the body line.
    PaneSeparatorStyle custom;
    custom.width = 3;
    custom.color = QColor(200, 0, 0);
    m_view->setPaneSeparatorStyle(custom);
    m_view->flushPendingRelayout();
    QApplication::processEvents();
    QCOMPARE(m_view->paneSeparatorStyle().color, QColor(200, 0, 0));

    const QImage image = m_view->grab().toImage();
    const int viewportX = m_view->viewport()->geometry().x();
    // The band sits inside the frozen pane, continuous from header to body.
    for (int dx = 1; dx <= 3; ++dx) {
        QCOMPARE(image.pixelColor(viewportX + boundary - dx, viewportY + 20), QColor(200, 0, 0));
        QCOMPARE(image.pixelColor(viewportX + boundary - dx, 8), QColor(200, 0, 0));
    }
    QCOMPARE(image.pixelColor(viewportX + boundary, viewportY + 20), QColor(Qt::white));

    // Width 0 hides the boundary line (header and body).
    PaneSeparatorStyle hidden;
    hidden.width = 0;
    m_view->setPaneSeparatorStyle(hidden);
    m_view->flushPendingRelayout();
    const QImage without = m_view->grab().toImage();
    QCOMPARE(without.pixelColor(viewportX + boundary - 1, viewportY + 20), QColor(Qt::white));
    QVERIFY(without.pixelColor(viewportX + boundary - 1, 8) != QColor(200, 0, 0));
}

QTEST_MAIN(TestVirtualTableView)

#include "tst_virtualtableview.moc"
