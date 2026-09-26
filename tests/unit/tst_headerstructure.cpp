#include <virtualitemviews/tablepane.h>
#include <virtualitemviews/virtualtableview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QAbstractTableModel>

#include <algorithm>

using namespace viv;
using namespace vivtest;

namespace {

/// Column model with real insert / remove / move support: the subject of this
/// test is what the *view* does with the model's structure signals.
class ColumnModel : public QAbstractTableModel
{
public:
    explicit ColumnModel(int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_columns(columns)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 3;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return m_names.value(index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return QVariant();
        return m_names.value(section);
    }

    void insertNewColumn(int at, const QString &name)
    {
        beginInsertColumns(QModelIndex(), at, at);
        m_names.insert(at, name);
        ++m_columns;
        endInsertColumns();
    }

    void removeOneColumn(int at)
    {
        beginRemoveColumns(QModelIndex(), at, at);
        m_names.removeAt(at);
        --m_columns;
        endRemoveColumns();
    }

    /// Moves the column at \a from so that it ends up at \a to in the new order.
    void moveOneColumn(int from, int to)
    {
        if (from == to || from < 0 || from >= m_columns || to < 0 || to >= m_columns)
            return;
        // The model signal wants "the column before which to insert", in
        // pre-move coordinates, which is one further down for a downward move.
        const int destination = to > from ? to + 1 : to;
        if (!beginMoveColumns(QModelIndex(), from, from, QModelIndex(), destination))
            return;
        m_names.move(from, to);
        endMoveColumns();
    }

    /// Name of the column currently at \a logicalIndex (its identity).
    QString nameAt(int logicalIndex) const { return m_names.value(logicalIndex); }

private:
    int m_columns = 0;
    QStringList m_names;
};

} // namespace

/// P1-1: a model column insert / remove / move must keep the *state* (width,
/// visibility, explicit size, frozen set, pane spec, sort indicator) with the
/// column it describes - not with the number it happened to have.
class TestHeaderStructure : public QObject
{
    Q_OBJECT

    ColumnModel *m_model = nullptr;
    VirtualTableView *m_view = nullptr;

private slots:
    void init();
    void cleanup();

    void insertingAColumnInTheMiddleKeepsTheOtherColumnState();
    void removingAColumnInTheMiddleKeepsTheOtherColumnState();
    void movingAColumnKeepsTheSortIndicatorAndTheFrozenSet();
    void paneCacheIsUpToDateImmediatelyAfterAColumnInsert();
    void structuralRemapDoesNotResortTheModel();
    void explicitPaneSpecsFollowColumnStructureChanges();
};

void TestHeaderStructure::init()
{
    m_model = new ColumnModel(0, this);
    m_view = new VirtualTableView;
    m_view->setModel(m_model);
    // Columns a/b/c/d/e: the names let the assertions talk about identity.
    for (int column = 0; column < 5; ++column)
        m_model->insertNewColumn(column, QString(QChar('a' + column)));
    m_view->setUniformItemHeight(30);
    showView(m_view);
}

void TestHeaderStructure::cleanup()
{
    delete m_view;
    m_view = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestHeaderStructure::insertingAColumnInTheMiddleKeepsTheOtherColumnState()
{
    m_view->setColumnWidth(0, 100);
    m_view->setColumnWidth(1, 200);
    m_view->setColumnWidth(2, 300);
    m_view->setColumnWidth(3, 400);
    m_view->setColumnHidden(2, true);
    m_view->setSortIndicator(3, Qt::DescendingOrder);

    m_model->insertNewColumn(1, QStringLiteral("X"));

    // Identity: a b X c d e -> the state of b, c, d followed them.
    QCOMPARE(m_model->nameAt(1), QStringLiteral("X"));
    QCOMPARE(m_view->columnWidth(0), 100);
    QCOMPARE(m_view->columnWidth(1), m_view->defaultColumnWidth());   // X is new
    QCOMPARE(m_view->columnWidth(2), 200);                            // b
    // c is hidden: its width is 0 by definition, the *stored* size is what moves.
    QCOMPARE(m_view->horizontalHeaderGeometry()->storedSectionSize(3), 300);
    QVERIFY(m_view->isColumnHidden(3));                               // c is still hidden
    QVERIFY(!m_view->isColumnHidden(2));
    QCOMPARE(m_view->columnWidth(4), 400);                            // d
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 4); // d
}

void TestHeaderStructure::removingAColumnInTheMiddleKeepsTheOtherColumnState()
{
    m_view->setColumnWidth(1, 200);
    m_view->setColumnWidth(3, 400);
    m_view->setColumnHidden(1, true);
    m_view->setSortIndicator(3, Qt::AscendingOrder);

    m_model->removeOneColumn(2);                 // drops column c

    // a b d e: d keeps its width and takes c's old number, and the sort
    // indicator follows d.
    QCOMPARE(m_model->nameAt(2), QStringLiteral("d"));
    QCOMPARE(m_view->horizontalHeaderGeometry()->storedSectionSize(1), 200);   // b, hidden
    QVERIFY(m_view->isColumnHidden(1));
    QCOMPARE(m_view->columnWidth(2), 400);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 2);
}

void TestHeaderStructure::movingAColumnKeepsTheSortIndicatorAndTheFrozenSet()
{
    m_view->setFrozenColumns({1});                // b is frozen (left)
    m_view->setSortIndicator(2, Qt::DescendingOrder);   // c is sorted
    QVERIFY(m_view->isColumnFrozen(1));

    // Move c (logical 2) to the front: c a b d e.
    m_model->moveOneColumn(2, 0);

    QCOMPARE(m_model->nameAt(0), QStringLiteral("c"));
    QCOMPARE(m_model->nameAt(2), QStringLiteral("b"));
    // The frozen set names the *column*, so b is still frozen (now logical 2).
    QVERIFY(m_view->isColumnFrozen(2));
    QVERIFY(!m_view->isColumnFrozen(1));
    QCOMPARE(m_view->frozenColumns(), QVector<int>({2}));
    // ... and the sort indicator follows c to logical 0.
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 0);
}

void TestHeaderStructure::paneCacheIsUpToDateImmediatelyAfterAColumnInsert()
{
    // P1-1 of the second review: the model handler mutated the header geometry first, and
    // that emits geometryChanged synchronously - which rebuilds the pane cache from the
    // *old* frozen set. The remap of the pane state happened afterwards, so every answer
    // the cache gives (paneOfColumn, paneIndexOfColumn, the pane's column list, the column
    // x derived from the pane prefix sums) stayed one structure change behind.
    m_view->setFrozenColumns({0, 1}); // a and b
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->panes().at(0).logicalColumns, QVector<int>({0, 1}));
    QCOMPARE(m_view->paneIndexOfColumn(1), m_view->paneIndexOfColumn(0));

    m_model->insertNewColumn(1, QStringLiteral("X")); // a X b c d e
    m_view->flushPendingRelayout();

    // The frozen set names the columns, so a and b are still frozen - now logical 0 and 2 -
    // and the new column X is not. The cache has to agree immediately.
    QCOMPARE(m_view->frozenColumns(), QVector<int>({0, 2}));
    QCOMPARE(m_view->panes().at(0).logicalColumns, QVector<int>({0, 2}));
    // The scrolling pane holds every non-frozen column; its *order* is visual order, which
    // is what the new column's visual slot decides (see the P1-4 test below).
    QVector<int> scrolling = m_view->panes().at(1).logicalColumns;
    std::sort(scrolling.begin(), scrolling.end());
    QCOMPARE(scrolling, QVector<int>({1, 3, 4, 5}));
    QCOMPARE(m_view->paneIndexOfColumn(2), m_view->paneIndexOfColumn(0));
    QVERIFY(m_view->paneIndexOfColumn(1) != m_view->paneIndexOfColumn(0));
    QCOMPARE(m_view->paneIndexOfColumn(1), m_view->paneIndexOfColumn(3));

    // The x of the newly inserted column is computed inside its own (scrolling) pane: a
    // stale cache would have put it in the frozen pane's coordinate space instead.
    const QRect scrollRect = m_view->panes().at(m_view->paneIndexOfColumn(1)).viewportRect;
    QVERIFY(m_view->columnGeometry(1).viewportX >= scrollRect.x());
    QVERIFY(m_view->columnGeometry(1).width > 0);
}

void TestHeaderStructure::structuralRemapDoesNotResortTheModel()
{
    // P1-6, table side: HeaderGeometry now reports the renamed sort indicator through
    // sortIndicatorChanged, and in this view that signal means "the user asked for a sort".
    // A structural remap must not make the view sort a model that has already changed.
    m_view->setSortingEnabled(true);
    m_view->setSortIndicator(3, Qt::AscendingOrder);
    QSignalSpy requested(m_view, &VirtualTableView::sortIndicatorRequested);
    QVERIFY(requested.isValid());

    // Insert in front of the sorted column: its number shifts, without a sort request.
    m_model->insertNewColumn(1, QStringLiteral("X"));
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 4);
    QCOMPARE(requested.count(), 0);

    // Removing in front of it shifts it down, again without a request.
    m_model->removeOneColumn(0);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 3);
    QCOMPARE(requested.count(), 0);

    // Moving a column changes it too.
    m_model->moveOneColumn(3, 0);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 0);
    QCOMPARE(requested.count(), 0);

    // A click still sorts (the guard is scoped to the structural remap).
    m_view->setSortIndicator(1, Qt::AscendingOrder);
    QCOMPARE(requested.count(), 1);
}

void TestHeaderStructure::explicitPaneSpecsFollowColumnStructureChanges()
{
    TablePaneSpec frozen;
    frozen.logicalColumns = {0};
    frozen.scroll = PaneScroll::Frozen;
    TablePaneSpec scrolling;
    scrolling.logicalColumns = {1, 2, 3, 4};
    scrolling.scrollGroup = 0;
    m_view->setPanes({frozen, scrolling});

    m_model->insertNewColumn(0, QStringLiteral("X"));   // X a b c d e
    // The pane spec named the columns, so a is now logical 1 and X (new) is not
    // part of any explicit pane.
    const QVector<TablePaneSpec> specs = m_view->paneSpecs();
    QCOMPARE(specs.size(), 2);
    QCOMPARE(specs.at(0).logicalColumns, QVector<int>({1}));
    QCOMPARE(specs.at(1).logicalColumns, QVector<int>({2, 3, 4, 5}));

    m_model->removeOneColumn(1);                        // drops a
    const QVector<TablePaneSpec> afterRemove = m_view->paneSpecs();
    QCOMPARE(afterRemove.at(0).logicalColumns, QVector<int>({}));   // a is gone
    QCOMPARE(afterRemove.at(1).logicalColumns, QVector<int>({1, 2, 3, 4}));
}

QTEST_MAIN(TestHeaderStructure)

#include "tst_headerstructure.moc"
