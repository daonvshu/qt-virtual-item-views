#include <virtualitemviews/reorderabletablemodel.h>

#include <QtTest>

using namespace viv;

namespace {

/// The base class is meant to be *derived*; this subclass only supplies the cells (reading
/// the stable identity, exactly like the header's documentation asks for).
class IdentityModel : public ReorderableTableModel
{
public:
    IdentityModel(int rows, int columns, QObject *parent = nullptr)
        : ReorderableTableModel(rows, columns, parent)
    {
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("r%1c%2").arg(sourceRow(index.row())).arg(index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || section < 0 || section >= rowCount())
            return QVariant();
        return orientation == Qt::Vertical ? QStringLiteral("R%1").arg(sourceRow(section))
                                           : QStringLiteral("c%1").arg(section);
    }
};

} // namespace

class TestReorderableTableModel : public QObject
{
    Q_OBJECT

private slots:
    void initialStateIsTheDataOrder();
    void movingARowRecordsIt();
    void movingARunOfRowsKeepsTheirOrder();
    void refusedMovesLeaveTheOrderAlone();
    void insertAndRemoveKeepTheIdentities();
    void resettingTheOrderRestoresTheDataOrder();
    void theBaseIsUsableWithoutData();
};

void TestReorderableTableModel::initialStateIsTheDataOrder()
{
    IdentityModel model(4, 2);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.columnCount(), 2);
    QVERIFY(model.isIdentityOrder());
    for (int row = 0; row < 4; ++row) {
        QCOMPARE(model.sourceRow(row), row);
        QCOMPARE(model.viewRow(row), row);
        QCOMPARE(model.index(row, 1).data().toString(), QStringLiteral("r%1c1").arg(row));
    }

    // Out-of-range queries answer -1 instead of asserting: a renderer asks about rows that
    // may just have left the window.
    QCOMPARE(model.sourceRow(-1), -1);
    QCOMPARE(model.sourceRow(4), -1);
    QCOMPARE(model.viewRow(99), -1);
}

void TestReorderableTableModel::movingARowRecordsIt()
{
    IdentityModel model(4, 1);
    QSignalSpy orderSpy(&model, &ReorderableTableModel::rowOrderChanged);
    QSignalSpy movedSpy(&model, &QAbstractItemModel::rowsMoved);

    // Move view row 0 to the end (Qt's "insert before" convention).
    QVERIFY(model.moveRows(QModelIndex(), 0, 1, QModelIndex(), 4));
    QCOMPARE(orderSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 1);
    QVERIFY(!model.isIdentityOrder());
    QCOMPARE(model.sourceRow(0), 1);
    QCOMPARE(model.sourceRow(3), 0);
    QCOMPARE(model.viewRow(0), 3);
    // The data moved with its identity, it was not renumbered in place.
    QCOMPARE(model.index(0, 0).data().toString(), QStringLiteral("r1c0"));
    QCOMPARE(model.index(3, 0).data().toString(), QStringLiteral("r0c0"));

    // ... and back to the front.
    QVERIFY(model.moveRows(QModelIndex(), 3, 1, QModelIndex(), 0));
    QCOMPARE(orderSpy.count(), 2);
    QVERIFY(model.isIdentityOrder());
    QCOMPARE(model.sourceRow(0), 0);
}

void TestReorderableTableModel::movingARunOfRowsKeepsTheirOrder()
{
    IdentityModel model(5, 1);
    // Rows 1..2 to the end: 0, 3, 4, 1, 2.
    QVERIFY(model.moveRows(QModelIndex(), 1, 2, QModelIndex(), 5));
    QCOMPARE(model.sourceRow(0), 0);
    QCOMPARE(model.sourceRow(1), 3);
    QCOMPARE(model.sourceRow(2), 4);
    QCOMPARE(model.sourceRow(3), 1);
    QCOMPARE(model.sourceRow(4), 2);

    // ... and the same run back in front: 1, 2, 0, 3, 4.
    QVERIFY(model.moveRows(QModelIndex(), 3, 2, QModelIndex(), 0));
    QCOMPARE(model.sourceRow(0), 1);
    QCOMPARE(model.sourceRow(1), 2);
    QCOMPARE(model.sourceRow(2), 0);
    QCOMPARE(model.sourceRow(3), 3);
    QCOMPARE(model.sourceRow(4), 4);
}

void TestReorderableTableModel::refusedMovesLeaveTheOrderAlone()
{
    IdentityModel model(4, 1);
    QSignalSpy orderSpy(&model, &ReorderableTableModel::rowOrderChanged);
    const auto unchanged = [&model]() {
        for (int row = 0; row < 4; ++row) {
            if (model.sourceRow(row) != row)
                return false;
        }
        return true;
    };

    QVERIFY(unchanged());
    QVERIFY(!model.moveRows(QModelIndex(), 1, 1, QModelIndex(), 1));   // onto itself
    QVERIFY(!model.moveRows(QModelIndex(), 1, 1, QModelIndex(), 2));   // just behind itself
    QVERIFY(!model.moveRows(QModelIndex(), 3, 2, QModelIndex(), 4));   // past the end
    QVERIFY(!model.moveRows(QModelIndex(), -1, 1, QModelIndex(), 2));
    QVERIFY(!model.moveRows(QModelIndex(), 0, 1, QModelIndex(), -1));
    QVERIFY(!model.moveRows(QModelIndex(), 0, 0, QModelIndex(), 2));
    // A move *into* a sub-tree is not what this model describes (it is flat).
    QVERIFY(!model.moveRows(model.index(0, 0), 0, 1, QModelIndex(), 2));
    QVERIFY(!model.moveRows(QModelIndex(), 0, 1, model.index(0, 0), 2));
    QVERIFY(unchanged());
    QCOMPARE(orderSpy.count(), 0);
}

void TestReorderableTableModel::insertAndRemoveKeepTheIdentities()
{
    IdentityModel model(3, 1);
    QVERIFY(model.insertRows(1, 2));
    QCOMPARE(model.rowCount(), 5);
    // The new rows get fresh identities; the ones already there keep theirs.
    QCOMPARE(model.sourceRow(0), 0);
    QCOMPARE(model.sourceRow(1), 3);
    QCOMPARE(model.sourceRow(2), 4);
    QCOMPARE(model.sourceRow(3), 1);
    QCOMPARE(model.sourceRow(4), 2);
    QCOMPARE(model.viewRow(4), 2);

    // Move a fresh row to the front: identities never collide, so the order stays exact.
    QVERIFY(model.moveRows(QModelIndex(), 2, 1, QModelIndex(), 0));
    QCOMPARE(model.sourceRow(0), 4);
    QCOMPARE(model.viewRow(4), 0);

    QVERIFY(model.removeRows(0, 1));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.viewRow(4), -1);      // it is gone, not somewhere else
    QCOMPARE(model.sourceRow(0), 0);

    // setRowCount() is the coarse version of the same thing.
    model.setRowCount(6);
    QCOMPARE(model.rowCount(), 6);
    model.setRowCount(2);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(!model.insertRows(3, 1));    // past the end
    QVERIFY(!model.removeRows(1, 5));    // past the end
}

void TestReorderableTableModel::resettingTheOrderRestoresTheDataOrder()
{
    IdentityModel model(4, 1);
    QVERIFY(model.moveRows(QModelIndex(), 3, 1, QModelIndex(), 0));
    QCOMPARE(model.sourceRow(0), 3);
    model.resetRowOrder();
    QVERIFY(model.isIdentityOrder());
    for (int row = 0; row < 4; ++row)
        QCOMPARE(model.sourceRow(row), row);
    // A reset on an identity order is a no-op (no bogus model reset for the views).
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.resetRowOrder();
    QCOMPARE(resetSpy.count(), 0);
}

void TestReorderableTableModel::theBaseIsUsableWithoutData()
{
    // The view instantiates the base itself when the row drag is switched on without a
    // model (VirtualTableView::setVerticalHeaderDragEnabled()): it has to be concrete and
    // has to answer "no data" rather than assert.
    ReorderableTableModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 0);
    QVERIFY(!model.index(0, 0).isValid());
    QVERIFY(!model.data(model.index(0, 0)).isValid());
    // No labels of its own - neither direction (a subclass supplies them).
    QVERIFY(!model.headerData(0, Qt::Horizontal, Qt::DisplayRole).isValid());
    QVERIFY(!model.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());

    model.setColumnCount(2);
    QCOMPARE(model.columnCount(), 2);
    model.setRowCount(3);
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.insertRows(0, 1));
    QCOMPARE(model.sourceRow(0), 3);        // a fresh identity, not a duplicate
    QVERIFY(model.moveRows(QModelIndex(), 0, 1, QModelIndex(), 4));
    QCOMPARE(model.viewRow(3), 3);
    model.setColumnCount(1);
    QCOMPARE(model.columnCount(), 1);
}

QTEST_MAIN(TestReorderableTableModel)

#include "tst_reorderabletablemodel.moc"
