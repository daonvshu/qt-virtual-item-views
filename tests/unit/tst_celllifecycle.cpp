#include <virtualitemviews/virtualtableview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {

/// Cell adapter that remembers the index every unbind saw: the point of the test
/// is that the index is still valid (row / column of the removed cell), because
/// business code usually keys its async work on it.
class RecordingCellAdapter : public CellWidgetAdapter
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
        ++bound;
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++unbound;
        if (!index.isValid()) {
            ++invalidUnbinds;
            return;
        }
        unboundRows.append(index.row());
        unboundColumns.append(index.column());
        static_cast<QLabel *>(widget)->setText(QString());
    }

    int bound = 0;
    int unbound = 0;
    int invalidUnbinds = 0;
    QList<int> unboundRows;
    QList<int> unboundColumns;
};

QStandardItemModel *makeModel(QObject *parent, int rows, int columns)
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

/// P1-6: in Cell Widget Mode the widgets have to be unbound *before* the model
/// invalidates their persistent indexes, otherwise business code loses the
/// identity (row / column) of the widget it is asked to clean up.
class TestCellLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void removingRowsUnbindsCellsWithTheOldRow();
    void removingColumnsUnbindsCellsWithTheOldColumn();
    void resettingTheModelUnbindsCellsWithAValidIndex();
};

void TestCellLifecycle::removingRowsUnbindsCellsWithTheOldRow()
{
    auto *model = makeModel(this, 60, 4);
    RecordingCellAdapter cells;      // declared first: it outlives the view
    VirtualTableView table;
    table.setModel(model);
    table.setUniformItemHeight(30);
    table.setCellAdapter(&cells);
    table.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&table);
    const int rowToRemove = 1;                    // inside the materialized window
    const QModelIndex first = table.cellIndexForWidget(
        table.cellWidget(model->index(rowToRemove, 0)));
    QVERIFY(first.isValid());
    QVERIFY(cells.bound > 0);
    const int unboundBefore = cells.unbound;

    QVERIFY(model->removeRows(rowToRemove, 1));
    settle();

    QVERIFY(cells.unbound > unboundBefore);
    QCOMPARE(cells.invalidUnbinds, 0);
    QVERIFY(cells.unboundRows.contains(rowToRemove));
}

void TestCellLifecycle::removingColumnsUnbindsCellsWithTheOldColumn()
{
    auto *model = makeModel(this, 60, 6);
    RecordingCellAdapter cells;
    VirtualTableView table;
    table.setModel(model);
    table.setUniformItemHeight(30);
    table.setDefaultColumnWidth(80);
    table.setCellAdapter(&cells);
    table.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&table);
    QVERIFY(cells.bound > 0);
    const int unboundBefore = cells.unbound;

    QVERIFY(model->removeColumns(1, 2));
    settle();

    QVERIFY(cells.unbound > unboundBefore);
    QCOMPARE(cells.invalidUnbinds, 0);
    QVERIFY(cells.unboundColumns.contains(1));
    QVERIFY(cells.unboundColumns.contains(2));
}

void TestCellLifecycle::resettingTheModelUnbindsCellsWithAValidIndex()
{
    auto *model = makeModel(this, 60, 4);
    RecordingCellAdapter cells;
    VirtualTableView table;
    table.setModel(model);
    table.setUniformItemHeight(30);
    table.setCellAdapter(&cells);
    table.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&table);
    QVERIFY(cells.bound > 0);
    const int unboundBefore = cells.unbound;

    model->clear();
    settle();

    QVERIFY(cells.unbound > unboundBefore);
    QCOMPARE(cells.invalidUnbinds, 0);
    QCOMPARE(table.materializedCellCount(), qsizetype(0));
}

QTEST_MAIN(TestCellLifecycle)

#include "tst_celllifecycle.moc"
