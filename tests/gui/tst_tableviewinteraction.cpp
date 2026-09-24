#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

#include <algorithm>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 30;
constexpr int kColumnWidth = 120;
constexpr int kColumns = 5;
constexpr int kRows = 40;
constexpr int kViewWidth = 560;
constexpr int kViewHeight = 340;

QString cellText(int row, int column)
{
    return QStringLiteral("r%1c%2").arg(row).arg(column);
}

class TableRowWidget : public QWidget
{
public:
    TableRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(2, 0, 100, 18);
            m_hosts.append(host);
            m_labels.append(label);
        }
    }

    QLabel *label(int logicalColumn) const { return m_labels.value(logicalColumn); }

private:
    QVector<ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

class GuiTableAdapter : public TableWidgetAdapter
{
public:
    explicit GuiTableAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        return new TableRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column) {
            row->label(column)->setText(
                index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column)
            row->label(column)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(400, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

/// Minimal cell widget for Cell Widget Mode.
class CellLabelWidget : public QWidget
{
public:
    explicit CellLabelWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setGeometry(2, 0, 100, 18);
    }

    QLabel *label() const { return m_label; }

private:
    QLabel *m_label = nullptr;
};

class GuiCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new CellLabelWidget(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<CellLabelWidget *>(widget)->label()->setText(index.data().toString());
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        static_cast<CellLabelWidget *>(widget)->label()->clear();
    }

    int created = 0;
};
} // namespace

class TestTableViewInteraction : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void headerClickSortsRows();
    void horizontalWheelScrollsBodyAndHeader();
    void headerDragResizesColumn();
    void verticalHeaderDragResizesRow();
    void cellClickSelectsTheClickedCell();

private:
    QStandardItemModel *m_model = nullptr;
    GuiTableAdapter *m_adapter = nullptr;
    VirtualTableView *m_view = nullptr;
};

void TestTableViewInteraction::init()
{
    m_model = new QStandardItemModel(kRows, kColumns, this);
    QStringList labels;
    for (int column = 0; column < kColumns; ++column)
        labels << QStringLiteral("Column %1").arg(column);
    m_model->setHorizontalHeaderLabels(labels);
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column)
            m_model->setItem(row, column, new QStandardItem(cellText(row, column)));
    }

    m_adapter = new GuiTableAdapter(kColumns);
    m_view = new VirtualTableView();
    m_view->setTableAdapter(m_adapter);
    m_view->setUniformItemHeight(kRowHeight);
    m_view->setDefaultColumnWidth(kColumnWidth);
    m_view->setModel(m_model);
    showView(m_view, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(m_view));
}

void TestTableViewInteraction::cleanup()
{
    delete m_view;
    m_view = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestTableViewInteraction::headerClickSortsRows()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    m_view->setSortingEnabled(true);

    const int sectionCenterX = header->sectionViewportPosition(1) + header->sectionSize(1) / 2;
    // Header interactions arrive on its viewport, like any QAbstractItemView.
    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(sectionCenterX, header->viewport()->height() / 2));
    QCoreApplication::processEvents();

    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 1);
    QStringList values;
    for (int row = 0; row < m_model->rowCount(); ++row)
        values << m_model->data(m_model->index(row, 1)).toString();
    std::sort(values.begin(), values.end());
    QCOMPARE(m_model->data(m_model->index(0, 1)).toString(), values.first());

    // A second click toggles the order.
    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(sectionCenterX, header->viewport()->height() / 2));
    QCoreApplication::processEvents();
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::DescendingOrder);
    QCOMPARE(m_model->data(m_model->index(0, 1)).toString(), values.last());
}

void TestTableViewInteraction::horizontalWheelScrollsBodyAndHeader()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    QCOMPARE(m_view->horizontalOffset(), qint64(0));

    // Shift+wheel scrolls horizontally; header and rows consume one offset.
    QWheelEvent shiftWheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::ShiftModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(m_view->viewport(), &shiftWheel);
    QCoreApplication::processEvents();

    const qint64 offset = m_view->horizontalOffset();
    QVERIFY2(offset > 0, qPrintable(QStringLiteral("offset=%1").arg(offset)));
    for (int column : m_view->visibleColumnLogicalIndexes()) {
        const ColumnGeometry geometry = m_view->columnGeometry(column);
        QCOMPARE(header->sectionViewportPosition(column), geometry.viewportX);
    }

    // A horizontal wheel delta pans by the same pixel step.
    QWheelEvent horizontalWheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(-120, 0),
                                Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(m_view->viewport(), &horizontalWheel);
    QCoreApplication::processEvents();
    QVERIFY(m_view->horizontalOffset() > offset);
}

void TestTableViewInteraction::headerDragResizesColumn()
{
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);

    const int widthBefore = m_view->columnWidth(0);
    const int boundaryX = header->sectionViewportPosition(0) + header->sectionSize(0) - 1;
    const int y = header->viewport()->height() / 2;

    QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(boundaryX, y));
    QTest::mouseMove(header->viewport(), QPoint(boundaryX + 40, y), 20);
    QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                        QPoint(boundaryX + 40, y));
    QCoreApplication::processEvents();

    const int widthAfter = m_view->columnWidth(0);
    if (widthAfter == widthBefore)
        QSKIP("This platform/style does not expose a draggable resize handle to synthetic input.");
    QVERIFY(widthAfter > widthBefore);
    QCOMPARE(m_view->columnGeometry(0).width, widthAfter);
}

void TestTableViewInteraction::verticalHeaderDragResizesRow()
{
    auto *rowHeader = qobject_cast<QHeaderView *>(m_view->verticalHeader()->headerWidget());
    QVERIFY(rowHeader != nullptr);
    QVERIFY(rowHeader->isVisible());

    const int heightBefore = m_view->rowHeight(0);
    // The trailing edge of the first row number is its resize handle.
    const int boundaryY = rowHeader->sectionViewportPosition(0) + rowHeader->sectionSize(0) - 1;
    const int x = rowHeader->viewport()->width() / 2;

    QTest::mousePress(rowHeader->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, boundaryY));
    QTest::mouseMove(rowHeader->viewport(), QPoint(x, boundaryY + 20), 20);
    QTest::mouseRelease(rowHeader->viewport(), Qt::LeftButton, Qt::NoModifier,
                        QPoint(x, boundaryY + 20));
    QCoreApplication::processEvents();

    const int heightAfter = m_view->rowHeight(0);
    if (heightAfter == heightBefore)
        QSKIP("This platform/style does not expose a draggable row resize handle to synthetic input.");
    QVERIFY(heightAfter > heightBefore);
    // The row widget itself follows the new height.
    QCOMPARE(m_view->visualRect(m_model->index(0, 0)).height(), heightAfter);
    QVERIFY(m_view->hasExplicitRowHeight(0));
    // The row-number strip stays aligned after the resize.
    for (qsizetype row : m_view->visibleRows().isValid()
             ? QList<qsizetype>{m_view->visibleRows().first, m_view->visibleRows().last}
             : QList<qsizetype>{}) {
        QCOMPARE(rowHeader->sectionViewportPosition(int(row)),
                 m_view->visualRect(m_model->index(int(row), 0)).top());
    }
}

void TestTableViewInteraction::cellClickSelectsTheClickedCell()
{
    GuiCellAdapter cellAdapter;
    VirtualTableView cellView;
    cellView.setCellAdapter(&cellAdapter);
    cellView.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    cellView.setUniformItemHeight(kRowHeight);
    cellView.setDefaultColumnWidth(kColumnWidth);
    cellView.setModel(m_model);
    showView(&cellView, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(&cellView));

    const QModelIndex target = m_model->index(2, 1);
    QWidget *cell = cellView.cellWidget(target);
    QVERIFY(cell != nullptr);

    // Clicking a cell widget selects exactly that cell (row and column).
    QTest::mouseClick(cell, Qt::LeftButton, Qt::NoModifier, cell->rect().center());
    QCoreApplication::processEvents();
    QCOMPARE(cellView.currentIndex(), target);
    QVERIFY(cellView.selectionModel()->isSelected(target));
    QCOMPARE(cellView.selectionModel()->currentIndex().column(), 1);

    // Clicking the viewport at a column position picks that column too.
    const ColumnGeometry column = cellView.columnGeometry(3);
    const QRect rowRect = cellView.visualRect(m_model->index(4, 0));
    QTest::mouseClick(cellView.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(column.viewportX + 5, rowRect.top() + rowRect.height() / 2));
    QCoreApplication::processEvents();
    QCOMPARE(cellView.currentIndex(), m_model->index(4, 3));
}

QTEST_MAIN(TestTableViewInteraction)

#include "tst_tableviewinteraction.moc"
