#include <virtualitemviews/tablespan.h>
#include <virtualitemviews/treevisibilityindex.h>

#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QtTest>

#include <limits>

using namespace viv;

namespace {

class RootFilterProxy : public QSortFilterProxyModel
{
public:
    void setHideSecondRoot(bool hidden)
    {
        m_hideSecondRoot = hidden;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        return sourceParent.isValid() || !m_hideSecondRoot || sourceRow != 1;
    }

private:
    bool m_hideSecondRoot = false;
};

class MovableTreeModel : public QAbstractItemModel
{
public:
    MovableTreeModel()
    {
        auto *first = append(&m_root, QStringLiteral("first"));
        auto *moving = append(first, QStringLiteral("moving"));
        append(moving, QStringLiteral("leaf"));
        append(&m_root, QStringLiteral("second"));
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (column < 0 || column >= 2 || row < 0 || (parent.isValid() && parent.column() != 0))
            return QModelIndex();
        Node *owner = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                       : const_cast<Node *>(&m_root);
        return row < owner->children.size() ? createIndex(row, column, owner->children.at(row))
                                             : QModelIndex();
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid())
            return QModelIndex();
        Node *owner = static_cast<Node *>(child.internalPointer())->parent;
        if (!owner || owner == &m_root)
            return QModelIndex();
        Node *grandparent = owner->parent;
        return createIndex(grandparent->children.indexOf(owner), 0, owner);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const Node *owner = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                             : &m_root;
        return owner->children.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() && parent.column() != 0 ? 0 : 2;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return static_cast<Node *>(index.internalPointer())->text;
    }

    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if (!sourceParent.isValid() || !destinationParent.isValid() || count != 1
            || sourceParent == destinationParent || sourceRow < 0
            || sourceRow >= rowCount(sourceParent) || destinationChild < 0
            || destinationChild > rowCount(destinationParent))
            return false;
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow,
                           destinationParent, destinationChild))
            return false;
        auto *source = static_cast<Node *>(sourceParent.internalPointer());
        auto *destination = static_cast<Node *>(destinationParent.internalPointer());
        Node *moving = source->children.takeAt(sourceRow);
        moving->parent = destination;
        destination->children.insert(destinationChild, moving);
        endMoveRows();
        return true;
    }

private:
    struct Node
    {
        QString text;
        Node *parent = nullptr;
        QVector<Node *> children;
        ~Node()
        {
            for (Node *child : children)
                delete child;
        }
    };

    Node *append(Node *parent, const QString &text)
    {
        auto *child = new Node;
        child->text = text;
        child->parent = parent;
        parent->children.append(child);
        return child;
    }

    Node m_root;
};

} // namespace

class TestTreeTableModelContracts : public QObject
{
    Q_OBJECT

private slots:
    void expansionFollowsNodeAfterSiblingInsertion();
    void rootDepthAndColumnsVaryByParent();
    void expansionFollowsProxySorting();
    void filteringDropsOnlyHiddenExpansion();
    void expansionFollowsCrossParentMove();
    void foreignIndexesDoNotChangeVisibility();
    void nonCanonicalTreeIndexesDoNotChangeVisibility();
    void resetClearsRootAndExpansion();
    void columnZeroReplacementInvalidatesExpansion();
    void spansAreScopedToTheirParent();
    void spanAnchorFollowsInsertedRows();
    void sortingDoesNotLeaveOverlappingSpans();
    void largeSpanExtentDoesNotOverflowReverseLookup();
    void siblingSpanContinuityTracksExpandedDescendants();
};

void TestTreeTableModelContracts::expansionFollowsNodeAfterSiblingInsertion()
{
    QStandardItemModel model;
    auto *branch = new QStandardItem(QStringLiteral("branch"));
    branch->appendRow(new QStandardItem(QStringLiteral("child")));
    model.appendRow(branch);

    TreeVisibilityIndex visibility(&model);
    const QPersistentModelIndex anchor(model.index(0, 0));
    visibility.expand(anchor);
    QCOMPARE(visibility.visibleRowCount(), qsizetype(2));

    model.insertRow(0, new QStandardItem(QStringLiteral("inserted")));
    visibility.handleModelChanged();
    QVERIFY(visibility.isExpanded(anchor));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(3));
    QCOMPARE(visibility.visibleRowForIndex(anchor), qsizetype(1));
    QCOMPARE(visibility.visibleRowForIndex(model.index(0, 0, anchor)), qsizetype(2));
}

void TestTreeTableModelContracts::rootDepthAndColumnsVaryByParent()
{
    QStandardItemModel model;
    auto *root = new QStandardItem(QStringLiteral("root"));
    auto *narrow = new QStandardItem(QStringLiteral("narrow"));
    narrow->appendRow(new QStandardItem(QStringLiteral("one column")));
    auto *wide = new QStandardItem(QStringLiteral("wide"));
    wide->appendRow({new QStandardItem(QStringLiteral("first")),
                     new QStandardItem(QStringLiteral("second")),
                     new QStandardItem(QStringLiteral("third"))});
    root->appendRow(narrow);
    root->appendRow(wide);
    model.appendRow(root);

    const QModelIndex rootIndex = model.index(0, 0);
    const QModelIndex narrowIndex = model.index(0, 0, rootIndex);
    const QModelIndex wideIndex = model.index(1, 0, rootIndex);
    TreeVisibilityIndex visibility(&model);
    visibility.setRootIndex(rootIndex);
    visibility.expand(narrowIndex);
    visibility.expand(wideIndex);

    QCOMPARE(visibility.visibleRowCount(), qsizetype(4));
    QCOMPARE(visibility.indexAtVisibleRow(0), narrowIndex);
    QCOMPARE(visibility.indexAtVisibleRow(2), wideIndex);
    QCOMPARE(visibility.depth(model.index(0, 0, narrowIndex)), 1);
    QCOMPARE(visibility.visibleRowForIndex(model.index(0, 2, wideIndex)), qsizetype(3));
    QCOMPARE(model.columnCount(narrowIndex), 1);
    QCOMPARE(model.columnCount(wideIndex), 3);
    QVERIFY(!model.index(0, 2, narrowIndex).isValid());
    QVERIFY(model.index(0, 2, wideIndex).isValid());
    QCOMPARE(visibility.visibleRowForIndex(rootIndex), qsizetype(-1));
}

void TestTreeTableModelContracts::expansionFollowsProxySorting()
{
    QStandardItemModel source;
    auto *branch = new QStandardItem(QStringLiteral("alpha"));
    branch->appendRow(new QStandardItem(QStringLiteral("child")));
    source.appendRow(branch);
    source.appendRow(new QStandardItem(QStringLiteral("beta")));

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setDynamicSortFilter(true);
    proxy.sort(0, Qt::AscendingOrder);
    TreeVisibilityIndex visibility(&proxy);
    const QPersistentModelIndex expanded(proxy.mapFromSource(source.index(0, 0)));
    visibility.expand(expanded);

    source.setData(source.index(0, 0), QStringLiteral("zeta"));
    visibility.handleModelChanged();
    QVERIFY(expanded.isValid());
    QVERIFY(visibility.isExpanded(expanded));
    QCOMPARE(visibility.visibleRowForIndex(expanded), qsizetype(1));
    QCOMPARE(visibility.visibleRowForIndex(proxy.index(0, 0, expanded)), qsizetype(2));
}

void TestTreeTableModelContracts::filteringDropsOnlyHiddenExpansion()
{
    QStandardItemModel source;
    for (const QString &name : {QStringLiteral("first"), QStringLiteral("second")}) {
        auto *branch = new QStandardItem(name);
        branch->appendRow(new QStandardItem(name + QStringLiteral(" child")));
        source.appendRow(branch);
    }
    RootFilterProxy proxy;
    proxy.setSourceModel(&source);
    TreeVisibilityIndex visibility(&proxy);
    const QPersistentModelIndex first(proxy.index(0, 0));
    const QPersistentModelIndex second(proxy.index(1, 0));
    visibility.expand(first);
    visibility.expand(second);
    QCOMPARE(visibility.visibleRowCount(), qsizetype(4));

    proxy.setHideSecondRoot(true);
    visibility.handleModelChanged();
    QVERIFY(visibility.isExpanded(first));
    QVERIFY(!second.isValid());
    QCOMPARE(visibility.expandedCount(), qsizetype(1));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(2));

    proxy.setHideSecondRoot(false);
    visibility.handleModelChanged();
    QCOMPARE(visibility.visibleRowCount(), qsizetype(3));
    QVERIFY(!visibility.isExpanded(proxy.index(1, 0)));
}

void TestTreeTableModelContracts::expansionFollowsCrossParentMove()
{
    MovableTreeModel model;
    TreeVisibilityIndex visibility(&model);
    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    const QPersistentModelIndex moving(model.index(0, 0, first));
    visibility.expand(first);
    visibility.expand(moving);
    QCOMPARE(visibility.visibleRowCount(), qsizetype(4));

    QVERIFY(model.moveRows(first, 0, 1, second, 0));
    visibility.handleModelChanged();
    QVERIFY(moving.isValid());
    QCOMPARE(moving.parent(), second);
    QVERIFY(visibility.isExpanded(moving));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(2));

    visibility.expand(second);
    QCOMPARE(visibility.visibleRowCount(), qsizetype(4));
    QCOMPARE(visibility.indexAtVisibleRow(2), QModelIndex(moving));
    const QModelIndex leaf = model.index(0, 0, moving);
    QVERIFY(leaf.isValid());
    QCOMPARE(leaf.parent(), QModelIndex(moving));
    QCOMPARE(visibility.indexAtVisibleRow(3), leaf);
    QCOMPARE(visibility.visibleRowForIndex(leaf), qsizetype(3));
}

void TestTreeTableModelContracts::foreignIndexesDoNotChangeVisibility()
{
    QStandardItemModel source;
    auto *branch = new QStandardItem(QStringLiteral("branch"));
    branch->appendRow(new QStandardItem(QStringLiteral("child")));
    source.appendRow(branch);
    QStandardItemModel foreign;
    foreign.appendRow(new QStandardItem(QStringLiteral("foreign")));

    TreeVisibilityIndex visibility(&source);
    visibility.resetModelQueryCount();
    visibility.setRootIndex(foreign.index(0, 0));
    visibility.expandRecursively(foreign.index(0, 0));
    QVERIFY(!visibility.rootIndex().isValid());
    QCOMPARE(visibility.visibleRowCount(), qsizetype(1));
    QCOMPARE(visibility.expandedCount(), qsizetype(0));
    QCOMPARE(visibility.modelQueryCount(), quint64(0));

    visibility.expandRecursively(source.index(0, 0));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(2));
}

void TestTreeTableModelContracts::nonCanonicalTreeIndexesDoNotChangeVisibility()
{
    QStandardItemModel model;
    auto *node = new QStandardItem(QStringLiteral("node"));
    node->appendRow(new QStandardItem(QStringLiteral("canonical child")));
    auto *otherColumn = new QStandardItem(QStringLiteral("other column"));
    otherColumn->appendRow(new QStandardItem(QStringLiteral("wrong child")));
    model.appendRow({node, otherColumn});

    TreeVisibilityIndex visibility(&model);
    const QModelIndex nonCanonical = model.index(0, 1);
    visibility.setRootIndex(nonCanonical);
    visibility.expand(nonCanonical);
    visibility.expandRecursively(nonCanonical);
    QVERIFY(!visibility.rootIndex().isValid());
    QCOMPARE(visibility.visibleRowCount(), qsizetype(1));
    QCOMPARE(visibility.expandedCount(), qsizetype(0));
    QVERIFY(!visibility.isExpanded(nonCanonical));

    visibility.expand(model.index(0, 0));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(2));
    QCOMPARE(visibility.indexAtVisibleRow(1).data(Qt::DisplayRole).toString(),
             QStringLiteral("canonical child"));
}

void TestTreeTableModelContracts::resetClearsRootAndExpansion()
{
    QStandardItemModel model;
    auto *root = new QStandardItem(QStringLiteral("root"));
    auto *branch = new QStandardItem(QStringLiteral("branch"));
    branch->appendRow(new QStandardItem(QStringLiteral("child")));
    root->appendRow(branch);
    model.appendRow(root);
    TreeVisibilityIndex visibility(&model);
    visibility.setRootIndex(model.index(0, 0));
    visibility.expand(model.index(0, 0, visibility.rootIndex()));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(2));

    model.clear();
    visibility.handleModelReset();
    QVERIFY(!visibility.rootIndex().isValid());
    QCOMPARE(visibility.expandedCount(), qsizetype(0));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(0));
}

void TestTreeTableModelContracts::columnZeroReplacementInvalidatesExpansion()
{
    QStandardItemModel model;
    auto *branch = new QStandardItem(QStringLiteral("branch"));
    auto *child = new QStandardItem(QStringLiteral("child"));
    child->appendRow(new QStandardItem(QStringLiteral("grandchild")));
    branch->appendRow(child);
    model.appendRow({branch, new QStandardItem(QStringLiteral("value"))});

    TreeVisibilityIndex visibility(&model);
    const QPersistentModelIndex oldNode(model.index(0, 0));
    visibility.expand(oldNode);
    const QPersistentModelIndex oldChild(model.index(0, 0, oldNode));
    visibility.expand(oldChild);
    QCOMPARE(visibility.visibleRowCount(), qsizetype(3));
    TreeVisibilityIndex rooted(&model);
    rooted.setRootIndex(oldNode);
    QCOMPARE(rooted.visibleRowCount(), qsizetype(1));

    QVERIFY(model.insertColumn(0));
    QCOMPARE(oldNode.column(), 1);
    QCOMPARE(oldChild.column(), 0);
    QCOMPARE(oldChild.parent().column(), 1);
    visibility.handleModelChanged();
    QCOMPARE(visibility.expandedCount(), qsizetype(0));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(1));
    rooted.handleModelChanged();
    QVERIFY(!rooted.rootIndex().isValid());
    QCOMPARE(rooted.visibleRowCount(), qsizetype(1));

    QVERIFY(model.removeColumn(0));
    QCOMPARE(oldNode.column(), 0);
    visibility.handleModelChanged();
    QCOMPARE(visibility.expandedCount(), qsizetype(0));
    QCOMPARE(visibility.visibleRowCount(), qsizetype(1));
}

void TestTreeTableModelContracts::spansAreScopedToTheirParent()
{
    QStandardItemModel model;
    for (int group = 0; group < 2; ++group) {
        auto *parent = new QStandardItem(QStringLiteral("group %1").arg(group));
        for (int row = 0; row < 2; ++row)
            parent->appendRow({new QStandardItem(QString::number(row)),
                               new QStandardItem(QStringLiteral("value"))});
        model.appendRow(parent);
    }

    TableSpanMap spans;
    const QModelIndex first = model.index(0, 0, model.index(0, 0));
    const QModelIndex second = model.index(0, 0, model.index(1, 0));
    spans.setSpan(first, 2, 2);
    spans.setSpan(second, 2, 2);
    QCOMPARE(spans.count(), 2);
    spans.modelStructureChanged();
    QCOMPARE(spans.count(), 2);
    QCOMPARE(spans.anchorOf(model.index(1, 1, first.parent())), first);
    QCOMPARE(spans.anchorOf(model.index(1, 1, second.parent())), second);
}

void TestTreeTableModelContracts::spanAnchorFollowsInsertedRows()
{
    QStandardItemModel model(3, 2);
    for (int row = 0; row < 3; ++row) {
        model.setItem(row, 0, new QStandardItem(QString::number(row)));
        model.setItem(row, 1, new QStandardItem(QStringLiteral("value")));
    }
    TableSpanMap spans;
    const QPersistentModelIndex anchor(model.index(1, 0));
    spans.setSpan(anchor, 2, 2);

    model.insertRow(0, {new QStandardItem(QStringLiteral("new")),
                        new QStandardItem(QStringLiteral("value"))});
    spans.modelStructureChanged();
    QCOMPARE(anchor.row(), 2);
    QCOMPARE(spans.spanAt(anchor), TableSpan({2, 2}));
    QCOMPARE(spans.anchorOf(model.index(3, 1)), QModelIndex(anchor));
}

void TestTreeTableModelContracts::sortingDoesNotLeaveOverlappingSpans()
{
    QStandardItemModel source;
    for (const QString &name : {QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("C"), QStringLiteral("D")}) {
        source.appendRow({new QStandardItem(name), new QStandardItem(name)});
    }
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setDynamicSortFilter(true);
    proxy.sort(0);

    const QPersistentModelIndex first(proxy.index(0, 0));
    const QPersistentModelIndex last(proxy.index(2, 0));
    const QPersistentModelIndex far(proxy.index(3, 0));
    TableSpanMap spans;
    spans.setSpan(first, 2, 2);
    spans.setSpan(last, 1, 2);
    spans.setSpan(far, 1, 2);
    QCOMPARE(spans.count(), 3);

    source.setData(source.index(2, 0), QStringLiteral("AA"));
    QCOMPARE(last.row(), 1);
    spans.modelStructureChanged();
    QCOMPARE(spans.count(), 2);
    QCOMPARE(spans.spanAt(first), TableSpan({2, 2}));
    QCOMPARE(spans.spanAt(last), TableSpan());
    QCOMPARE(spans.spanAt(far), TableSpan({1, 2}));
    QCOMPARE(spans.maximumSpan(), TableSpan({2, 2}));
    QCOMPARE(spans.anchorOf(proxy.index(1, 1)), QModelIndex(first));
}

void TestTreeTableModelContracts::largeSpanExtentDoesNotOverflowReverseLookup()
{
    QStandardItemModel model(3, 3);
    TableSpanMap spans;
    const QModelIndex anchor = model.index(2, 2);
    spans.setSpan(anchor, std::numeric_limits<int>::max(),
                  std::numeric_limits<int>::max());
    QCOMPARE(spans.anchorOf(anchor), anchor);
    QCOMPARE(spans.maximumSpan().rowSpan, std::numeric_limits<int>::max());
}

void TestTreeTableModelContracts::siblingSpanContinuityTracksExpandedDescendants()
{
    QStandardItemModel model;
    for (const QString &name : {QStringLiteral("first"), QStringLiteral("second"),
                                QStringLiteral("third")}) {
        auto *item = new QStandardItem(name);
        item->appendRow(new QStandardItem(name + QStringLiteral(" child")));
        model.appendRow(item);
    }
    TreeVisibilityIndex visibility(&model);
    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    const QModelIndex third = model.index(2, 0);

    QCOMPARE(visibility.visibleRowForIndex(third) - visibility.visibleRowForIndex(first),
             qsizetype(2));
    visibility.expand(second);
    QCOMPARE(visibility.visibleRowForIndex(second) - visibility.visibleRowForIndex(first),
             qsizetype(1));
    QCOMPARE(visibility.visibleRowForIndex(third) - visibility.visibleRowForIndex(first),
             qsizetype(3));
    visibility.expand(first);
    QCOMPARE(visibility.visibleRowForIndex(second) - visibility.visibleRowForIndex(first),
             qsizetype(2));
    visibility.collapse(first);
    visibility.collapse(second);
    QCOMPARE(visibility.visibleRowForIndex(third) - visibility.visibleRowForIndex(first),
             qsizetype(2));
}

QTEST_APPLESS_MAIN(TestTreeTableModelContracts)

#include "tst_treetablemodelcontracts.moc"
