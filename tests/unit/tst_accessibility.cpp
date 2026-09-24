#include <virtualitemviews/accessibility.h>
#include <virtualitemviews/virtualitemview.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 24;
constexpr int kColumnWidth = 90;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 200;

/// List model with name/description/help roles and one disabled row, so the
/// accessible text and state have something to report (§37).
class AccessibleListModel : public QAbstractListModel
{
public:
    explicit AccessibleListModel(const QStringList &rows, QObject *parent = nullptr)
        : QAbstractListModel(parent)
        , m_rows(rows)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return QVariant();
        switch (role) {
        case Qt::DisplayRole:
            return m_rows.value(index.row());
        case Qt::ToolTipRole:
            return QStringLiteral("tip %1").arg(index.row());
        case Qt::StatusTipRole:
            return QStringLiteral("help %1").arg(index.row());
        default:
            return QVariant();
        }
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::ItemIsDropEnabled;
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.row() == disabledRow)
            flags.setFlag(Qt::ItemIsEnabled, false);
        return flags;
    }

    void insertRowAt(int row, const QString &text)
    {
        beginInsertRows(QModelIndex(), row, row);
        m_rows.insert(row, text);
        endInsertRows();
    }

    void removeRowAt(int row)
    {
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }

    void resetRows(const QStringList &rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }

    int disabledRow = -1;

private:
    QStringList m_rows;
};

/// Table body adapter: one row widget per row (the bridge never looks at the
/// widgets, it reads the model and the committed column geometry).
class AccessibleTableAdapter : public TableWidgetAdapter
{
public:
    explicit AccessibleTableAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new TestRowWidget(parent, kRowHeight);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<TestRowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<TestRowWidget *>(widget)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * m_columnCount, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

/// Index of a node by its path ("A/a0") in a QStandardItemModel tree.
QModelIndex treeIndex(QStandardItemModel *model, const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QModelIndex();
    QStandardItem *item = nullptr;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->item(row, 0)->text() == parts.first()) {
            item = model->item(row, 0);
            break;
        }
    }
    for (int i = 1; i < parts.size() && item; ++i) {
        QStandardItem *next = nullptr;
        for (int row = 0; row < item->rowCount(); ++row) {
            if (item->child(row, 0)->text() == parts.at(i)) {
                next = item->child(row, 0);
                break;
            }
        }
        item = next;
    }
    return item ? item->index() : QModelIndex();
}

/// Cell Widget Mode adapter: one QWidget per visible cell (§28), used to check
/// that the bridge also works when the table has no row widgets at all.
class AccessibleCellAdapter : public CellWidgetAdapter
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
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};
} // namespace

/// §37: the bridge exposes what is on screen, on demand, and never copies the
/// model. Every case goes through QAccessible::queryAccessibleInterface() after
/// installing the factory, which is exactly what a screen reader does.
class TestAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void factoryReportsTheRoleOfEveryViewType();
    void listExposesTheVisibleItemsOnDemand();
    void hugeModelStillExposesOnlyTheViewport();
    void itemTextStateAndRectComeFromTheModelAndView();
    void currentItemIsTheFocusChild();
    void childAtMapsAPointToTheItemUnderIt();
    void tableExposesRowsAndCells();
    void tableCellWidgetModeAlsoExposesRows();
    void mergedCellsAreOneAccessibleCell();
    void treeExposesHierarchyOfVisibleNodes();
    void pressingAnItemBehavesLikeAClick();
};

void TestAccessibility::initTestCase()
{
    installAccessibilityFactory();
}

void TestAccessibility::cleanupTestCase()
{
    removeAccessibilityFactory();
}

void TestAccessibility::factoryReportsTheRoleOfEveryViewType()
{
    AccessibleListModel model({QStringLiteral("a"), QStringLiteral("b")});
    TestAdapter adapter(kRowHeight);

    VirtualListView list;
    list.setAdapter(&adapter);
    list.setUniformItemHeight(kRowHeight);
    list.setModel(&model);
    showView(&list, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *listInterface = QAccessible::queryAccessibleInterface(&list);
    QVERIFY(listInterface);
    QVERIFY(listInterface->isValid());
    QCOMPARE(listInterface->object(), static_cast<QObject *>(&list));
    QCOMPARE(listInterface->role(), QAccessible::List);

    auto *tableModel = new QStandardItemModel(5, 3, this);
    AccessibleTableAdapter tableAdapter(3);
    VirtualTableView table;
    table.setTableAdapter(&tableAdapter);
    table.setUniformItemHeight(kRowHeight);
    table.setDefaultColumnWidth(kColumnWidth);
    table.setModel(tableModel);
    showView(&table, QSize(kViewWidth, kViewHeight));
    QAccessibleInterface *tableInterface = QAccessible::queryAccessibleInterface(&table);
    QVERIFY(tableInterface);
    QCOMPARE(tableInterface->role(), QAccessible::Table);

    auto *treeModel = new QStandardItemModel(this);
    auto *root = new QStandardItem(QStringLiteral("root"));
    root->appendRow(new QStandardItem(QStringLiteral("child")));
    treeModel->appendRow(root);
    VirtualTreeView tree;
    tree.setAdapter(&adapter);
    tree.setUniformItemHeight(kRowHeight);
    tree.setModel(treeModel);
    showView(&tree, QSize(kViewWidth, kViewHeight));
    QAccessibleInterface *treeInterface = QAccessible::queryAccessibleInterface(&tree);
    QVERIFY(treeInterface);
    QCOMPARE(treeInterface->role(), QAccessible::Tree);
}

void TestAccessibility::listExposesTheVisibleItemsOnDemand()
{
    AccessibleListModel model({QStringLiteral("row 0"), QStringLiteral("row 1"),
                               QStringLiteral("row 2")});
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);

    // Only the rows of the viewport window exist as nodes.
    QCOMPARE(viewInterface->childCount(), int(view.visibleItemRange().count()));
    QCOMPARE(viewInterface->childCount(), 3);
    QCOMPARE(viewInterface->text(QAccessible::Description),
             QStringLiteral("3 / 3 items visible"));

    QAccessibleInterface *first = viewInterface->child(0);
    QVERIFY(first);
    QCOMPARE(first->role(), QAccessible::ListItem);
    QCOMPARE(first->text(QAccessible::Name), QStringLiteral("row 0"));
    QCOMPARE(first->parent(), viewInterface);
    QVERIFY(first->rect().isValid());
    QCOMPARE(first->rect().height(), kRowHeight);
    QCOMPARE(viewInterface->indexOfChild(first), 0);
    QCOMPARE(viewInterface->indexOfChild(viewInterface->child(2)), 2);
}

void TestAccessibility::hugeModelStillExposesOnlyTheViewport()
{
    QStringList rows;
    rows.reserve(100000);
    for (int row = 0; row < 100000; ++row)
        rows << QStringLiteral("row %1").arg(row);
    AccessibleListModel model(rows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);
    // The node count follows the viewport, not the model: 100k logical rows and
    // still a handful of nodes.
    QCOMPARE(viewInterface->childCount(), int(view.visibleItemRange().count()));
    QVERIFY(viewInterface->childCount() < 100);
    QCOMPARE(viewInterface->text(QAccessible::Description),
             QStringLiteral("%1 / 100000 items visible").arg(view.visibleItemRange().count()));
    QCOMPARE(viewInterface->child(0)->text(QAccessible::Name), QStringLiteral("row 0"));
    QCOMPARE(viewInterface->child(0)->state().offscreen, false);
}

void TestAccessibility::itemTextStateAndRectComeFromTheModelAndView()
{
    QStringList rows;
    for (int row = 0; row < 20; ++row)
        rows << QStringLiteral("row %1").arg(row);
    AccessibleListModel model(rows);
    model.disabledRow = 1;
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *first = viewInterface->child(0);
    QAccessibleInterface *disabled = viewInterface->child(1);
    QVERIFY(first && disabled);

    // Text comes from the model roles, not from the (recyclable) row widget.
    QCOMPARE(first->text(QAccessible::Name), QStringLiteral("row 0"));
    QCOMPARE(first->text(QAccessible::Description), QStringLiteral("tip 0"));
    QCOMPARE(first->text(QAccessible::Help), QStringLiteral("help 0"));

    QVERIFY(!first->state().disabled);
    QVERIFY(first->state().selectable);
    QVERIFY(first->state().focusable);
    QVERIFY(!first->state().offscreen);
    QVERIFY(!first->state().invisible);
    QVERIFY(disabled->state().disabled);

    // The rect follows the committed layout: scrolling by one row moves the item
    // out of the window and the node follows.
    const QRect before = first->rect();
    QCOMPARE(before.height(), kRowHeight);
    view.scrollByPixels(kRowHeight);
    view.flushPendingRelayout();
    QAccessibleInterface *afterScroll = viewInterface->child(0);
    QVERIFY(afterScroll);
    QCOMPARE(afterScroll->text(QAccessible::Name), QStringLiteral("row 1"));
    QCOMPARE(afterScroll->rect(), before);
    QCOMPARE(first->rect().top(), before.top() - kRowHeight);
}

void TestAccessibility::currentItemIsTheFocusChild()
{
    QStringList rows;
    for (int row = 0; row < 20; ++row)
        rows << QStringLiteral("row %1").arg(row);
    AccessibleListModel model(rows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(!viewInterface->focusChild());

    // current/selection are reported per item, so a screen reader knows what the
    // application considers active (selection/current of §37).
    view.setCurrentIndex(model.index(1, 0));
    QAccessibleInterface *focusChild = viewInterface->focusChild();
    QVERIFY(focusChild);
    QCOMPARE(focusChild->text(QAccessible::Name), QStringLiteral("row 1"));
    QVERIFY(focusChild->state().selected);
    QVERIFY(focusChild->state().active);
    QCOMPARE(viewInterface->indexOfChild(focusChild), 1);

    // Scrolling the current item out of the window makes it offscreen and drops
    // it from the children of the view (the node itself stays valid).
    view.scrollByPixels(4 * kRowHeight);
    view.flushPendingRelayout();
    QVERIFY(!viewInterface->focusChild());
    QVERIFY(focusChild->state().offscreen);
    QCOMPARE(focusChild->text(QAccessible::Name), QStringLiteral("row 1"));
}

void TestAccessibility::childAtMapsAPointToTheItemUnderIt()
{
    AccessibleListModel model({QStringLiteral("row 0"), QStringLiteral("row 1"),
                               QStringLiteral("row 2")});
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    const QRect second = viewInterface->child(1)->rect();

    // childAt() takes screen coordinates, exactly like the platform does.
    QAccessibleInterface *hit = viewInterface->childAt(second.center().x(), second.center().y());
    QVERIFY(hit);
    QCOMPARE(hit->text(QAccessible::Name), QStringLiteral("row 1"));

    // Outside the viewport the bridge reports nothing instead of inventing a row.
    const QRect viewRect = viewInterface->rect();
    QVERIFY(!viewInterface->childAt(viewRect.center().x(), viewRect.top() - 5));
}

void TestAccessibility::tableExposesRowsAndCells()
{
    auto *model = new QStandardItemModel(6, 3, this);
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 3; ++column) {
            model->setItem(row, column,
                           new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
        }
    }
    AccessibleTableAdapter adapter(3);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);
    QVERIFY(viewInterface->childCount() > 0);

    // Row/column semantics: a row is a container whose children are the visible
    // cells, so the column text is reachable without inventing widgets.
    QAccessibleInterface *row = viewInterface->child(0);
    QVERIFY(row);
    QCOMPARE(row->role(), QAccessible::Row);
    QCOMPARE(row->childCount(), 3);
    QAccessibleInterface *cell = row->child(1);
    QVERIFY(cell);
    QCOMPARE(cell->role(), QAccessible::Cell);
    QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("r0c1"));
    QCOMPARE(cell->parent(), row);
    QCOMPARE(row->indexOfChild(cell), 1);

    // The cell rect follows the committed column geometry.
    QCOMPARE(cell->rect().width(), view.columnGeometry(1).width);
    QCOMPARE(cell->rect().height(), kRowHeight);

    // A hidden column disappears from the row (and is reported as invisible).
    view.setColumnHidden(1, true);
    QCOMPARE(row->childCount(), 2);
    QCOMPARE(row->child(0)->text(QAccessible::Name), QStringLiteral("r0c0"));
    QCOMPARE(row->child(1)->text(QAccessible::Name), QStringLiteral("r0c2"));
    QVERIFY(cell->state().invisible);
    view.setColumnHidden(1, false);
    QCOMPARE(row->childCount(), 3);
    QVERIFY(!cell->state().invisible);

    // The frozen panes keep their own x, so the cell rect stays put while the
    // scrollable columns move.
    view.setFrozenColumns({0});
    const QRect frozenCell = row->child(0)->rect();
    view.setHorizontalOffset(3 * kColumnWidth);
    QCOMPARE(row->child(0)->rect(), frozenCell);
}

void TestAccessibility::tableCellWidgetModeAlsoExposesRows()
{
    auto *model = new QStandardItemModel(6, 3, this);
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 3; ++column) {
            model->setItem(row, column,
                           new QStandardItem(QStringLiteral("c%1r%2").arg(column).arg(row)));
        }
    }
    AccessibleCellAdapter cellAdapter;
    VirtualTableView view;
    view.setCellAdapter(&cellAdapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    // Cell Widget Mode materializes cells, not rows: the bridge has to derive the
    // rows of the window from the cells it finds.
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(!view.materializedItemCount());
    QVERIFY(view.materializedCellCount() > 0);

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);
    QCOMPARE(viewInterface->childCount(), int(view.visibleItemRange().count()));
    QAccessibleInterface *row = viewInterface->child(0);
    QVERIFY(row);
    QCOMPARE(row->role(), QAccessible::Row);
    QVERIFY(row->childCount() > 0);
    QCOMPARE(row->child(0)->text(QAccessible::Name), QStringLiteral("c0r0"));
}

void TestAccessibility::mergedCellsAreOneAccessibleCell()
{
    auto *model = new QStandardItemModel(6, 4, this);
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 4; ++column) {
            model->setItem(row, column,
                           new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
        }
    }
    AccessibleTableAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setSpan(0, 1, 2, 2);
    view.flushPendingRelayout();

    // §43 spans: a merged area is a single accessible cell - the covered columns
    // and rows are not separate nodes, and the node reports the merged rect.
    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);
    QAccessibleInterface *row0 = viewInterface->child(0);
    QVERIFY(row0);
    QCOMPARE(row0->childCount(), 3);
    QAccessibleInterface *anchor = row0->child(1);
    QVERIFY(anchor);
    QCOMPARE(anchor->text(QAccessible::Name), QStringLiteral("r0c1"));
    QCOMPARE(anchor->rect().width(), 2 * kColumnWidth);
    QCOMPARE(anchor->rect().height(), 2 * kRowHeight);
    QCOMPARE(row0->child(2)->text(QAccessible::Name), QStringLiteral("r0c3"));
    // The childAt() hit test of the merged area resolves to the anchor cell (the
    // view node itself hands out rows, its row node hands out cells).
    const QRect merged = anchor->rect();
    QAccessibleInterface *rowHit
        = viewInterface->childAt(merged.center().x(), merged.center().y());
    QVERIFY(rowHit);
    QCOMPARE(rowHit->role(), QAccessible::Row);
    QCOMPARE(rowHit->text(QAccessible::Name), QStringLiteral("r0c0"));
    QCOMPARE(row0->childAt(merged.center().x(), merged.center().y())->text(QAccessible::Name),
             QStringLiteral("r0c1"));

    // Clearing the spans brings the covered cells back.
    view.clearSpans();
    view.flushPendingRelayout();
    QCOMPARE(viewInterface->child(0)->childCount(), 4);
    QCOMPARE(viewInterface->child(0)->child(2)->text(QAccessible::Name), QStringLiteral("r0c2"));
}

void TestAccessibility::treeExposesHierarchyOfVisibleNodes()
{
    auto *model = new QStandardItemModel(this);
    auto *a = new QStandardItem(QStringLiteral("A"));
    a->appendRow(new QStandardItem(QStringLiteral("a0")));
    a->appendRow(new QStandardItem(QStringLiteral("a1")));
    model->appendRow(a);
    model->appendRow(new QStandardItem(QStringLiteral("B")));
    TestAdapter adapter(kRowHeight);
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);
    // Collapsed: the view shows the two top level nodes, the hierarchy is not
    // flattened.
    QCOMPARE(viewInterface->childCount(), 2);
    QAccessibleInterface *nodeA = viewInterface->child(0);
    QAccessibleInterface *nodeB = viewInterface->child(1);
    QVERIFY(nodeA && nodeB);
    QCOMPARE(nodeA->role(), QAccessible::TreeItem);
    QCOMPARE(nodeA->text(QAccessible::Name), QStringLiteral("A"));
    QCOMPARE(nodeA->parent(), viewInterface);
    QVERIFY(nodeA->state().expandable);
    QVERIFY(nodeA->state().collapsed);
    QVERIFY(!nodeA->state().expanded);
    QCOMPARE(nodeA->childCount(), 0);

    // Expanding reports the visible children and their parent, so a screen reader
    // can walk the tree hierarchy.
    view.expand(treeIndex(model, QStringLiteral("A")));
    view.flushPendingRelayout();
    QCOMPARE(nodeA->childCount(), 2);
    QVERIFY(nodeA->state().expanded);
    QAccessibleInterface *child0 = nodeA->child(0);
    QAccessibleInterface *child1 = nodeA->child(1);
    QVERIFY(child0 && child1);
    QCOMPARE(child0->text(QAccessible::Name), QStringLiteral("a0"));
    QCOMPARE(child1->text(QAccessible::Name), QStringLiteral("a1"));
    QCOMPARE(child0->parent(), nodeA);
    QCOMPARE(nodeA->indexOfChild(child1), 1);
    QCOMPARE(viewInterface->childCount(), 2);

    // A leaf is not expandable and has no children of its own.
    QVERIFY(!child0->state().expandable);
    QCOMPARE(child1->childCount(), 0);
    QVERIFY(!child0->rect().isEmpty());
}

void TestAccessibility::pressingAnItemBehavesLikeAClick()
{
    QStringList rows;
    for (int row = 0; row < 20; ++row)
        rows << QStringLiteral("row %1").arg(row);
    AccessibleListModel model(rows);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QAccessibleInterface *viewInterface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewInterface);

    int clicked = 0;
    QObject::connect(&view, &VirtualItemView::clicked, [&clicked]() { ++clicked; });
    QObject::connect(&view, &VirtualItemView::activated, [&clicked]() { ++clicked; });

    // A "press" from a screen reader behaves exactly like a click, so business
    // code keeps a single code path.
    QAccessibleInterface *item = viewInterface->child(0);
    QAccessibleActionInterface *actions = item->actionInterface();
    QVERIFY(actions);
    QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::setFocusAction()));
    QVERIFY(!actions->keyBindingsForAction(QAccessibleActionInterface::pressAction()).isEmpty());
    actions->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(clicked, 2);
    QCOMPARE(view.currentIndex().data(Qt::DisplayRole).toString(), QStringLiteral("row 0"));

    // The view itself offers scrolling to the assistive tool.
    QAccessibleActionInterface *viewActions = viewInterface->actionInterface();
    QVERIFY(viewActions);
    QVERIFY(viewActions->actionNames().contains(QAccessibleActionInterface::scrollDownAction()));
    QVERIFY(!viewActions->keyBindingsForAction(QAccessibleActionInterface::scrollDownAction()).isEmpty());
    viewActions->doAction(QAccessibleActionInterface::scrollDownAction());
    QVERIFY(view.verticalOffset() > 0);
    // Back to the top: the window the assertions below rely on.
    view.setVerticalOffset(0);
    view.flushPendingRelayout();

    // Structural changes of the model are reported and the nodes follow.
    model.insertRowAt(1, QStringLiteral("inserted"));
    view.flushPendingRelayout();
    QAccessibleInterface *inserted = QAccessible::queryAccessibleInterface(&view)->child(1);
    QVERIFY(inserted);
    QCOMPARE(inserted->text(QAccessible::Name), QStringLiteral("inserted"));
    model.removeRowAt(1);
    model.resetRows({QStringLiteral("only")});
    view.flushPendingRelayout();
    QAccessibleInterface *afterReset = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(afterReset->childCount(), 1);
    QCOMPARE(afterReset->child(0)->text(QAccessible::Name), QStringLiteral("only"));
}

QTEST_MAIN(TestAccessibility)
#include "tst_accessibility.moc"
