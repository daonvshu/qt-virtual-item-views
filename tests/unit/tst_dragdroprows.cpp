#include "../../examples/drag_drop/dragmodels.h"
#include <QScopedPointer>
#include <QtTest>

class TestDragDropRows : public QObject
{
    Q_OBJECT
private slots:
    void internalMove_data();
    void internalMove();
    void multipleRowsKeepHeights();
    void externalMoveKeepsDestinationRows();
    void copyKeepsSource();
};

static void populate(dragdemo::DraggableTableModel &model)
{
    model.setRowDropEnabled(true);
    for (const QString &text : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                                QStringLiteral("D"), QStringLiteral("E")})
        model.appendRow(new QStandardItem(text));
}

static QStringList rowTexts(const QStandardItemModel &model)
{
    QStringList texts;
    for (int row = 0; row < model.rowCount(); ++row)
        texts.append(model.index(row, 0).data().toString());
    return texts;
}

void TestDragDropRows::internalMove_data()
{
    QTest::addColumn<int>("source");
    QTest::addColumn<int>("destination");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("up") << 3 << 1 << QStringList({"A", "D", "B", "C", "E"});
    QTest::newRow("down") << 1 << 4 << QStringList({"A", "C", "D", "B", "E"});
    QTest::newRow("append") << 0 << 5 << QStringList({"B", "C", "D", "E", "A"});
}

void TestDragDropRows::internalMove()
{
    QFETCH(int, source);
    QFETCH(int, destination);
    QFETCH(QStringList, expected);
    dragdemo::DraggableTableModel model(0, 1);
    populate(model);
    const QPersistentModelIndex original(model.index(source, 0));
    QScopedPointer<QMimeData> mime(model.mimeData({original}));
    QVERIFY(model.dropMimeData(mime.data(), Qt::MoveAction, destination, 0, {}));
    QCOMPARE(rowTexts(model), expected);
    QVERIFY(!original.isValid());
}

void TestDragDropRows::externalMoveKeepsDestinationRows()
{
    dragdemo::DraggableTableModel source(0, 1);
    dragdemo::DraggableTableModel target(0, 1);
    populate(source);
    populate(target);
    QScopedPointer<QMimeData> mime(source.mimeData({source.index(3, 0)}));
    QVERIFY(target.dropMimeData(mime.data(), Qt::MoveAction, 1, 0, {}));
    QCOMPARE(rowTexts(target), QStringList({"A", "D", "B", "C", "D", "E"}));
    QCOMPARE(source.rowCount(), 5); // Source cleanup belongs to its view after exec().
}

void TestDragDropRows::multipleRowsKeepHeights()
{
    dragdemo::DraggableTableModel model(0, 1);
    populate(model);
    model.rememberRowHeight(1, 41);
    model.rememberRowHeight(2, 63);
    QList<QPair<int, int>> heights;
    connect(&model, &dragdemo::DraggableTableModel::insertedRowHeights, &model,
            [&heights](const QList<QPair<int, int>> &entries) { heights = entries; });
    QScopedPointer<QMimeData> mime(model.mimeData({model.index(1, 0), model.index(2, 0)}));
    QVERIFY(model.dropMimeData(mime.data(), Qt::MoveAction, 4, 0, {}));
    QCOMPARE(rowTexts(model), QStringList({"A", "D", "B", "C", "E"}));
    const QList<QPair<int, int>> expected{{2, 41}, {3, 63}};
    QCOMPARE(heights, expected);
}

void TestDragDropRows::copyKeepsSource()
{
    dragdemo::DraggableTableModel model(0, 1);
    populate(model);
    const QPersistentModelIndex original(model.index(3, 0));
    QScopedPointer<QMimeData> mime(model.mimeData({original}));
    QVERIFY(model.dropMimeData(mime.data(), Qt::CopyAction, 1, 0, {}));
    QCOMPARE(rowTexts(model), QStringList({"A", "D", "B", "C", "D", "E"}));
    QVERIFY(original.isValid());
    QCOMPARE(original.row(), 4);
}

QTEST_APPLESS_MAIN(TestDragDropRows)
#include "tst_dragdroprows.moc"
