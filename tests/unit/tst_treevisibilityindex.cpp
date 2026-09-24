#include <virtualitemviews/treevisibilityindex.h>

#include <QtTest>

#include <QStandardItemModel>

using namespace viv;

namespace {
/// root
///  +- A (a0, a1)
///  +- B (b0 -> b00, b01)
///  +- C
QStandardItemModel *buildTree(QObject *parent)
{
    auto *model = new QStandardItemModel(parent);

    auto *a = new QStandardItem(QStringLiteral("A"));
    a->appendRow(new QStandardItem(QStringLiteral("a0")));
    a->appendRow(new QStandardItem(QStringLiteral("a1")));

    auto *b = new QStandardItem(QStringLiteral("B"));
    auto *b0 = new QStandardItem(QStringLiteral("b0"));
    b0->appendRow(new QStandardItem(QStringLiteral("b00")));
    b0->appendRow(new QStandardItem(QStringLiteral("b01")));
    auto *b1 = new QStandardItem(QStringLiteral("b1"));
    b->appendRow(b0);
    b->appendRow(b1);

    auto *c = new QStandardItem(QStringLiteral("C"));

    model->appendRow(a);
    model->appendRow(b);
    model->appendRow(c);
    return model;
}

QModelIndex indexOf(QStandardItemModel *model, const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QModelIndex();
    QStandardItem *item = model->findItems(parts.first(), Qt::MatchExactly).value(0);
    for (int i = 1; i < parts.size() && item; ++i) {
        QStandardItem *next = nullptr;
        for (int row = 0; row < item->rowCount(); ++row) {
            if (item->child(row, 0) && item->child(row, 0)->text() == parts.at(i)) {
                next = item->child(row, 0);
                break;
            }
        }
        item = next;
    }
    return item ? item->index() : QModelIndex();
}
} // namespace

class TestTreeVisibilityIndex : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void flattensTopLevelRowsOnlyWhileCollapsed();
    void expandInsertsVisibleSubtreeRows();
    void nestedExpansionKeepsDocumentOrder();
    void collapseRemovesSubtreeRows();
    void expandCollapsedItemIsNoop();
    void collapseAllShowsTopLevelRows();
    void rowLookupRoundTrip();
    void depthFollowsHierarchy();
    void indexAtVisibleRowOutOfRange();
    void rootIndexRestrictsTheFlattenedTree();
    void modelChangedKeepsExpansionState();
    void modelResetClearsExpansionState();
    void expandDoesNotWalkTheWholeTree();

private:
    QStandardItemModel *m_model = nullptr;
    TreeVisibilityIndex *m_index = nullptr;
};

void TestTreeVisibilityIndex::init()
{
    m_model = buildTree(this);
    m_index = new TreeVisibilityIndex(m_model);
}

void TestTreeVisibilityIndex::cleanup()
{
    delete m_index;
    m_index = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestTreeVisibilityIndex::flattensTopLevelRowsOnlyWhileCollapsed()
{
    QCOMPARE(m_index->visibleRowCount(), qsizetype(3));
    QCOMPARE(m_index->indexAtVisibleRow(0).data(Qt::DisplayRole).toString(), QStringLiteral("A"));
    QCOMPARE(m_index->indexAtVisibleRow(2).data(Qt::DisplayRole).toString(), QStringLiteral("C"));
    QCOMPARE(m_index->visibleRowForIndex(indexOf(m_model, QStringLiteral("B"))), qsizetype(1));
    QCOMPARE(m_index->visibleRowForIndex(indexOf(m_model, QStringLiteral("A/a0"))), qsizetype(-1));
    QVERIFY(!m_index->isVisible(indexOf(m_model, QStringLiteral("A/a0"))));
    QCOMPARE(m_index->expandedCount(), qsizetype(0));
}

void TestTreeVisibilityIndex::expandInsertsVisibleSubtreeRows()
{
    m_index->expand(indexOf(m_model, QStringLiteral("A")));

    QCOMPARE(m_index->isExpanded(indexOf(m_model, QStringLiteral("A"))), true);
    QCOMPARE(m_index->visibleRowCount(), qsizetype(5));
    QCOMPARE(m_index->indexAtVisibleRow(1).data(Qt::DisplayRole).toString(), QStringLiteral("a0"));
    QCOMPARE(m_index->indexAtVisibleRow(2).data(Qt::DisplayRole).toString(), QStringLiteral("a1"));
    QCOMPARE(m_index->indexAtVisibleRow(3).data(Qt::DisplayRole).toString(), QStringLiteral("B"));
    QCOMPARE(m_index->visibleRowForIndex(indexOf(m_model, QStringLiteral("C"))), qsizetype(4));

    // Expanding twice must not duplicate rows.
    m_index->expand(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(5));
}

void TestTreeVisibilityIndex::nestedExpansionKeepsDocumentOrder()
{
    m_index->expand(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(5)); // A, B, b0, b1, C
    m_index->expand(indexOf(m_model, QStringLiteral("B/b0")));

    QCOMPARE(m_index->visibleRowCount(), qsizetype(7));
    QStringList texts;
    for (qsizetype row = 0; row < m_index->visibleRowCount(); ++row)
        texts << m_index->indexAtVisibleRow(row).data(Qt::DisplayRole).toString();
    QCOMPARE(texts, QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("b0"),
                                 QStringLiteral("b00"), QStringLiteral("b01"), QStringLiteral("b1"),
                                 QStringLiteral("C")}));
    QCOMPARE(m_index->depth(indexOf(m_model, QStringLiteral("B/b0/b01"))), 2);
}

void TestTreeVisibilityIndex::collapseRemovesSubtreeRows()
{
    m_index->expand(indexOf(m_model, QStringLiteral("B")));
    m_index->expand(indexOf(m_model, QStringLiteral("B/b0")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(7));

    // Collapsing the outer item hides the whole sub-tree, including the rows of
    // the still expanded inner item.
    m_index->collapse(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(3));
    QVERIFY(m_index->isExpanded(indexOf(m_model, QStringLiteral("B/b0"))));
    QCOMPARE(m_index->visibleRowForIndex(indexOf(m_model, QStringLiteral("B/b0"))), qsizetype(-1));

    // Re-expanding restores the previous inner expansion state.
    m_index->expand(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(7));
    QCOMPARE(m_index->indexAtVisibleRow(3).data(Qt::DisplayRole).toString(), QStringLiteral("b00"));
}

void TestTreeVisibilityIndex::expandCollapsedItemIsNoop()
{
    m_index->collapse(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(3));

    // An item that is not visible stays untouched.
    m_index->expand(indexOf(m_model, QStringLiteral("A/a0")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(3));
    QVERIFY(!m_index->isVisible(indexOf(m_model, QStringLiteral("A/a0"))));
}

void TestTreeVisibilityIndex::collapseAllShowsTopLevelRows()
{
    m_index->expand(indexOf(m_model, QStringLiteral("A")));
    m_index->expand(indexOf(m_model, QStringLiteral("B")));
    m_index->expand(indexOf(m_model, QStringLiteral("B/b0")));
    QCOMPARE(m_index->expandedCount(), qsizetype(3));

    m_index->collapseAll();
    QCOMPARE(m_index->visibleRowCount(), qsizetype(3));
    QCOMPARE(m_index->expandedCount(), qsizetype(0));
}

void TestTreeVisibilityIndex::rowLookupRoundTrip()
{
    m_index->expand(indexOf(m_model, QStringLiteral("A")));
    for (qsizetype row = 0; row < m_index->visibleRowCount(); ++row) {
        const QModelIndex index = m_index->indexAtVisibleRow(row);
        QCOMPARE(m_index->visibleRowForIndex(index), row);
    }
    QCOMPARE(m_index->visibleRowForIndex(QModelIndex()), qsizetype(-1));

    // An index from another model is rejected.
    QStandardItemModel other;
    other.appendRow(new QStandardItem(QStringLiteral("x")));
    QCOMPARE(m_index->visibleRowForIndex(other.index(0, 0)), qsizetype(-1));
}

void TestTreeVisibilityIndex::depthFollowsHierarchy()
{
    QCOMPARE(m_index->depth(indexOf(m_model, QStringLiteral("A"))), 0);
    QCOMPARE(m_index->depth(indexOf(m_model, QStringLiteral("A/a1"))), 1);
    QCOMPARE(m_index->depth(indexOf(m_model, QStringLiteral("B/b0/b00"))), 2);
    QCOMPARE(m_index->depth(QModelIndex()), -1);
}

void TestTreeVisibilityIndex::indexAtVisibleRowOutOfRange()
{
    QVERIFY(!m_index->indexAtVisibleRow(-1).isValid());
    QVERIFY(!m_index->indexAtVisibleRow(3).isValid());
    QVERIFY(m_index->indexAtVisibleRow(2).isValid());
}

void TestTreeVisibilityIndex::rootIndexRestrictsTheFlattenedTree()
{
    const QModelIndex b = indexOf(m_model, QStringLiteral("B"));
    m_index->setRootIndex(b);

    QCOMPARE(m_index->visibleRowCount(), qsizetype(2)); // b0, b1
    QCOMPARE(m_index->indexAtVisibleRow(0).data(Qt::DisplayRole).toString(), QStringLiteral("b0"));
    QCOMPARE(m_index->depth(indexOf(m_model, QStringLiteral("B/b0"))), 0);
    QCOMPARE(m_index->visibleRowForIndex(indexOf(m_model, QStringLiteral("A"))), qsizetype(-1));

    m_index->expand(indexOf(m_model, QStringLiteral("B/b0")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(4));
}

void TestTreeVisibilityIndex::modelChangedKeepsExpansionState()
{
    m_index->expand(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(5));

    // Insert a new child into the expanded item and notify the index.
    QStandardItem *a = m_model->findItems(QStringLiteral("A"), Qt::MatchExactly).value(0);
    a->insertRow(0, new QStandardItem(QStringLiteral("a-new")));
    m_index->handleModelChanged();

    QCOMPARE(m_index->isExpanded(indexOf(m_model, QStringLiteral("A"))), true);
    QCOMPARE(m_index->visibleRowCount(), qsizetype(6));
    QCOMPARE(m_index->indexAtVisibleRow(1).data(Qt::DisplayRole).toString(), QStringLiteral("a-new"));

    // Remove the whole A sub-tree.
    m_model->removeRow(0, m_model->index(0, 0).parent());
    m_index->handleModelChanged();
    QCOMPARE(m_index->visibleRowCount(), qsizetype(2)); // B, C
}

void TestTreeVisibilityIndex::modelResetClearsExpansionState()
{
    m_index->expand(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_index->expandedCount(), qsizetype(1));

    m_index->handleModelReset();
    QCOMPARE(m_index->expandedCount(), qsizetype(0));
    QCOMPARE(m_index->visibleRowCount(), qsizetype(3));
}

void TestTreeVisibilityIndex::expandDoesNotWalkTheWholeTree()
{
    // A wide tree: 400 top level rows with 4 children each.
    QStandardItemModel model;
    for (int row = 0; row < 400; ++row) {
        auto *item = new QStandardItem(QStringLiteral("row-%1").arg(row));
        for (int child = 0; child < 4; ++child)
            item->appendRow(new QStandardItem(QStringLiteral("child-%1").arg(child)));
        model.appendRow(item);
    }
    TreeVisibilityIndex index(&model);
    QCOMPARE(index.visibleRowCount(), qsizetype(400));

    index.resetModelQueryCount();
    index.expand(model.index(0, 0));
    QCOMPARE(index.visibleRowCount(), qsizetype(404));

    // Expanding one item may only query that sub-tree, not the other 399 rows.
    QVERIFY2(index.modelQueryCount() < 30,
             qPrintable(QStringLiteral("model queries: %1").arg(index.modelQueryCount())));

    index.resetModelQueryCount();
    index.collapse(model.index(0, 0));
    QCOMPARE(index.visibleRowCount(), qsizetype(400));
    QCOMPARE(index.modelQueryCount(), quint64(0));
}

QTEST_MAIN(TestTreeVisibilityIndex)

#include "tst_treevisibilityindex.moc"
