#include "../../src/core/dragmoveselection_p.h"

#include <QStandardItemModel>
#include <QMimeData>
#include <QScopedPointer>
#include <QtTest>

class TestDragMoveSelection : public QObject
{
    Q_OBJECT
private slots:
    void singleRowMove_data();
    void singleRowMove();
    void disjointRowsKeepTheirSelectedColumns();
    void unselectedAndRejectedDropsDoNotSelect();
    void ambiguousInsertionDoesNotSelect();
    void sourceRemovalFailureDoesNotSelect();
    void crossModelMovePreservesUnrelatedSelection();
    void resetOrReplacedSelectionCancelsTransfer();
    void treeMoveTracksTheNewParent();
};

void TestDragMoveSelection::singleRowMove_data()
{
    QTest::addColumn<int>("destination");
    QTest::newRow("up") << 0;
    QTest::newRow("down") << 4;
}

void TestDragMoveSelection::singleRowMove()
{
    QFETCH(int, destination);
    QStandardItemModel model;
    for (const QString &text : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")})
        model.appendRow(new QStandardItem(text));
    QItemSelectionModel selection(&model);
    const QPersistentModelIndex source(model.index(1, 0));
    selection.setCurrentIndex(source, QItemSelectionModel::ClearAndSelect);
    const QScopedPointer<QMimeData> mime(model.mimeData({source}));
    viv::DragMoveSelection transfer(&selection, {source}, &selection, {}, destination, -1);
    QVERIFY(model.dropMimeData(mime.data(), Qt::MoveAction, destination, -1, {}));
    transfer.finishDrop(true);
    QVERIFY(model.removeRow(source.row()));
    const QModelIndex current = transfer.restore([] { return true; });
    const int expected = destination > 1 ? destination - 1 : destination;
    QCOMPARE(selection.selectedIndexes(), QModelIndexList({model.index(expected, 0)}));
    QCOMPARE(current, model.index(expected, 0));
    QCOMPARE(selection.currentIndex().data().toString(), QStringLiteral("b"));
}

void TestDragMoveSelection::disjointRowsKeepTheirSelectedColumns()
{
    QStandardItemModel model(6, 4);
    model.setData(model.index(1, 1), QStringLiteral("first"));
    model.setData(model.index(4, 2), QStringLiteral("second"));
    QItemSelectionModel selection(&model);
    const QPersistentModelIndex first(model.index(1, 1));
    const QPersistentModelIndex second(model.index(4, 2));
    selection.select(first, QItemSelectionModel::Select);
    selection.setCurrentIndex(second, QItemSelectionModel::Select);
    const QScopedPointer<QMimeData> mime(model.mimeData({second, first}));
    viv::DragMoveSelection transfer(&selection, {second, first}, &selection, {}, 0, 1);
    QVERIFY(model.dropMimeData(mime.data(), Qt::MoveAction, 0, 1, {}));
    transfer.finishDrop(true);
    QVERIFY(model.removeRow(second.row()));
    QVERIFY(model.removeRow(first.row()));
    transfer.restore([] { return true; });
    QCOMPARE(selection.selectedIndexes().size(), 2);
    QVERIFY(selection.isSelected(model.index(0, 1)));
    QVERIFY(selection.isSelected(model.index(1, 2)));
    QVERIFY(!selection.isSelected(model.index(0, 2)));
    QCOMPARE(model.index(0, 1).data().toString(), QStringLiteral("first"));
    QCOMPARE(model.index(1, 2).data().toString(), QStringLiteral("second"));
    QCOMPARE(selection.currentIndex(), model.index(1, 2));
}

void TestDragMoveSelection::unselectedAndRejectedDropsDoNotSelect()
{
    for (bool selected : {false, true}) {
        QStandardItemModel model(4, 1);
        QItemSelectionModel selection(&model);
        const QPersistentModelIndex source(model.index(1, 0));
        if (selected)
            selection.select(source, QItemSelectionModel::Select);
        viv::DragMoveSelection transfer(&selection, {source}, &selection, {}, 0, 0);
        QVERIFY(model.insertRow(0));
        transfer.finishDrop(!selected);
        QVERIFY(model.removeRow(source.row()));
        transfer.restore([] { return true; });
        QVERIFY(selection.selectedIndexes().isEmpty());
    }
}

void TestDragMoveSelection::ambiguousInsertionDoesNotSelect()
{
    for (int scenario : {0, 1, 2}) {
        QStandardItemModel model(5, 1);
        QItemSelectionModel selection(&model);
        const QPersistentModelIndex source(model.index(2, 0));
        selection.select(source, QItemSelectionModel::Select);
        viv::DragMoveSelection transfer(&selection, {source}, &selection, {}, 0, 0);
        if (scenario == 0)
            QVERIFY(model.insertRows(0, 2)); // An unrelated extra row prevents a one-to-one mapping.
        else if (scenario == 1)
            QVERIFY(model.insertRow(1)); // The model used a different destination.
        // scenario 2 changes no rows, as with an existing-cell overwrite.
        transfer.finishDrop(true);
        QVERIFY(model.removeRow(source.row()));
        transfer.restore([] { return true; });
        QVERIFY(selection.selectedIndexes().isEmpty());
    }
}

void TestDragMoveSelection::sourceRemovalFailureDoesNotSelect()
{
    QStandardItemModel model(4, 1);
    QItemSelectionModel selection(&model);
    const QPersistentModelIndex source(model.index(2, 0));
    selection.select(source, QItemSelectionModel::Select);
    viv::DragMoveSelection transfer(&selection, {source}, &selection, {}, 0, 0);
    QVERIFY(model.insertRow(0));
    transfer.finishDrop(true);
    transfer.restore([] { return true; }); // The original row still exists.
    QCOMPARE(selection.selectedIndexes(), QModelIndexList({source}));
    QVERIFY(!selection.isSelected(model.index(0, 0)));
}

void TestDragMoveSelection::crossModelMovePreservesUnrelatedSelection()
{
    QStandardItemModel sourceModel(3, 1), targetModel(3, 1);
    QItemSelectionModel sourceSelection(&sourceModel), targetSelection(&targetModel);
    const QPersistentModelIndex source(sourceModel.index(1, 0));
    const QPersistentModelIndex unrelated(targetModel.index(0, 0));
    sourceSelection.setCurrentIndex(source, QItemSelectionModel::ClearAndSelect);
    targetSelection.select(unrelated, QItemSelectionModel::Select);
    viv::DragMoveSelection transfer(&sourceSelection, {source}, &targetSelection, {}, 1, 0);
    QVERIFY(targetModel.insertRow(1));
    transfer.finishDrop(true);
    QVERIFY(sourceModel.removeRow(source.row()));
    transfer.restore([] { return true; });
    QVERIFY(sourceSelection.selectedIndexes().isEmpty());
    QCOMPARE(targetSelection.selectedIndexes().size(), 2);
    QVERIFY(targetSelection.isSelected(unrelated));
    QVERIFY(targetSelection.isSelected(targetModel.index(1, 0)));
    QCOMPARE(targetSelection.currentIndex(), targetModel.index(1, 0));
}

void TestDragMoveSelection::resetOrReplacedSelectionCancelsTransfer()
{
    for (bool reset : {false, true}) {
        QStandardItemModel sourceModel(3, 1), targetModel(3, 1);
        QItemSelectionModel sourceSelection(&sourceModel), targetSelection(&targetModel);
        const QPersistentModelIndex source(sourceModel.index(1, 0));
        sourceSelection.select(source, QItemSelectionModel::Select);
        viv::DragMoveSelection transfer(&sourceSelection, {source}, &targetSelection, {}, 0, 0);
        QVERIFY(targetModel.insertRow(0));
        transfer.finishDrop(true);
        if (reset) {
            sourceModel.clear(); // A reset must not be mistaken for successful source removal.
        } else {
            QVERIFY(sourceModel.removeRow(source.row()));
            QItemSelectionModel replacement(&targetModel);
            QVERIFY(!transfer.matches(&sourceSelection, &replacement));
        }
        transfer.restore([reset] { return reset; });
        QVERIFY(targetSelection.selectedIndexes().isEmpty());
    }
}

void TestDragMoveSelection::treeMoveTracksTheNewParent()
{
    QStandardItemModel model;
    auto *firstParent = new QStandardItem(QStringLiteral("first"));
    firstParent->appendRow(new QStandardItem(QStringLiteral("child")));
    auto *secondParent = new QStandardItem(QStringLiteral("second"));
    model.appendRow(firstParent);
    model.appendRow(secondParent);
    QItemSelectionModel selection(&model);
    const QPersistentModelIndex source(firstParent->child(0)->index());
    selection.setCurrentIndex(source, QItemSelectionModel::ClearAndSelect);
    viv::DragMoveSelection transfer(&selection, {source}, &selection, secondParent->index(), 0, 0);
    secondParent->appendRow(new QStandardItem(QStringLiteral("child")));
    transfer.finishDrop(true);
    QVERIFY(model.removeRow(source.row(), source.parent()));
    transfer.restore([] { return true; });
    QCOMPARE(selection.selectedIndexes(), QModelIndexList({secondParent->child(0)->index()}));
    QCOMPARE(selection.currentIndex().parent(), secondParent->index());
}

QTEST_APPLESS_MAIN(TestDragMoveSelection)
#include "tst_dragmoveselection.moc"
