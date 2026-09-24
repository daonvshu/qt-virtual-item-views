#include <virtualitemviews/virtualitemview.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 20;
constexpr int kColumnWidth = 100;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;
constexpr int kIndentation = 20;
const char *const kMimeType = "application/x-viv-dnd-test";

/// Row widget of the tests: a label, so a drag source has something to grab.
class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("rowLabel"));
        m_label->setGeometry(2, 0, 200, kRowHeight);
    }

    void setText(const QString &text) { m_label->setText(text); }
    QString text() const { return m_label->text(); }

private:
    QLabel *m_label = nullptr;
};

class RowAdapter : public WidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new RowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<RowWidget *>(widget)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kViewWidth, kRowHeight);
    }
};

/// Table body adapter of the tests: a table row is one widget with a label.
class TableRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new RowWidget(parent); }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<RowWidget *>(widget)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kViewWidth, kRowHeight);
    }
};

/// Model side of §38 for the flat case: flags, MIME payload and the
/// insert/reject decision live in the model, the view only reports the target.
class DropListModel : public QAbstractListModel
{
public:
    explicit DropListModel(const QStringList &rows, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_rows(rows)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return m_rows.value(index.row());
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::ItemIsDropEnabled;
        if (!dragEnabled)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled;
    }

    QStringList mimeTypes() const override { return {QString::fromLatin1(kMimeType)}; }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        ++mimeDataCalls;
        QStringList payload;
        for (const QModelIndex &index : indexes)
            payload << m_rows.value(index.row());
        auto *data = new QMimeData;
        data->setData(kMimeType, payload.join(QLatin1Char('\n')).toUtf8());
        return data;
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction | Qt::CopyAction;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction, int row, int column,
                         const QModelIndex &parent) const override
    {
        if (!data || !data->hasFormat(kMimeType) || parent.isValid() || !acceptDrops)
            return false;
        return row >= 0 && row <= int(m_rows.size()) && column <= 0;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction, int row, int column,
                      const QModelIndex &parent) override
    {
        ++dropCalls;
        lastRow = row;
        lastColumn = column;
        lastParent = parent;
        if (!canDropMimeData(data, Qt::MoveAction, row, column, parent))
            return false;
        const QStringList payload
            = QString::fromUtf8(data->data(kMimeType)).split(QLatin1Char('\n'));
        beginInsertRows(QModelIndex(), row, row + int(payload.size()) - 1);
        for (int i = 0; i < payload.size(); ++i)
            m_rows.insert(row + i, payload.at(i));
        endInsertRows();
        return true;
    }

    QStringList rows() const { return m_rows; }

    mutable int mimeDataCalls = 0;
    int dropCalls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    QModelIndex lastParent;
    bool dragEnabled = true;
    bool acceptDrops = true;

private:
    QStringList m_rows;
};

/// Hierarchical model of the tree cases: nodes are only appended, so the cached
/// row of a node stays valid. Drop handling only records the contract.
class RecordingTreeModel : public QAbstractItemModel
{
public:
    struct Node
    {
        QString text;
        Node *parent = nullptr;
        QVector<Node *> children;
        int row = 0;
    };

    explicit RecordingTreeModel(QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {
        m_root = new Node;
    }

    ~RecordingTreeModel() override { deleteSubtree(m_root); }

    Node *append(Node *parent, const QString &text)
    {
        Node *target = parent ? parent : m_root;
        const int row = int(target->children.size());
        beginInsertRows(indexForNode(target), row, row);
        auto *node = new Node;
        node->text = text;
        node->parent = target;
        node->row = row;
        target->children.append(node);
        endInsertRows();
        return node;
    }

    QModelIndex indexForNode(Node *node) const
    {
        if (!node || node == m_root)
            return QModelIndex();
        return createIndex(node->row, 0, node);
    }

    Node *nodeOf(const QModelIndex &index) const
    {
        return index.isValid() ? static_cast<Node *>(index.internalPointer()) : m_root;
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        Node *node = nodeOf(parent);
        if (!node || row < 0 || row >= node->children.size())
            return QModelIndex();
        return createIndex(row, column, node->children.at(row));
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        Node *node = nodeOf(child);
        if (!node || node == m_root || !node->parent || node->parent == m_root)
            return QModelIndex();
        return createIndex(node->parent->row, 0, node->parent);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Node *node = nodeOf(parent);
        return node ? int(node->children.size()) : 0;
    }

    int columnCount(const QModelIndex & = QModelIndex()) const override { return 1; }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        Node *node = nodeOf(index);
        return node ? QVariant(node->text) : QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::ItemIsDropEnabled;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled;
    }

    QStringList mimeTypes() const override { return {QString::fromLatin1(kMimeType)}; }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        ++mimeDataCalls;
        QStringList payload;
        for (const QModelIndex &index : indexes)
            payload << index.data(Qt::DisplayRole).toString();
        auto *data = new QMimeData;
        data->setData(kMimeType, payload.join(QLatin1Char('\n')).toUtf8());
        return data;
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction | Qt::CopyAction;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        ++canDropCalls;
        lastAction = action;
        lastRow = row;
        lastColumn = column;
        lastParent = QPersistentModelIndex(parent);
        return data && data->hasFormat(kMimeType);
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        ++dropCalls;
        lastAction = action;
        lastRow = row;
        lastColumn = column;
        lastParent = QPersistentModelIndex(parent);
        return canDropMimeData(data, action, row, column, parent);
    }

    mutable int mimeDataCalls = 0;
    mutable int canDropCalls = 0;
    int dropCalls = 0;
    /// canDropMimeData() is const, so the recorded contract of the last query
    /// has to be mutable.
    mutable Qt::DropAction lastAction = Qt::IgnoreAction;
    mutable int lastRow = -1;
    mutable int lastColumn = -2;
    mutable QPersistentModelIndex lastParent;

    Node *m_root = nullptr;

private:
    void deleteSubtree(Node *node)
    {
        if (!node)
            return;
        for (Node *child : node->children)
            deleteSubtree(child);
        delete node;
    }
};

/// The kernel paints the drop indicator with a child widget of the viewport.
QWidget *dropIndicatorOf(VirtualItemView *view)
{
    return view->viewport()->findChild<QWidget *>(QStringLiteral("vivDropIndicator"));
}

/// Selection unit of a table. Called through the kernel reference and named
/// through the enum's owner on purpose: VC 14.50 (Qt 5 configuration) was
/// observed to drop the whole call - the object code contained no `call` at all
/// - for the plain member call on the table (and for a call to this helper with
/// the table as the argument). The table tests therefore go through this helper
/// and verify that the write landed.
void setSelectionUnit(VirtualTableView &view, VirtualItemView::SelectionBehavior behavior)
{
    VirtualItemView &asKernel = view;
    asKernel.setSelectionBehavior(behavior);
}

QMimeData *payload(const QString &text)
{
    auto *data = new QMimeData;
    data->setData(kMimeType, text.toUtf8());
    return data;
}

/// Sends the two events of a hover to the viewport, which is where Qt delivers
/// drag & drop (QAbstractScrollArea forwards them to the view).
void sendDragMove(VirtualItemView *view, const QPoint &pos, const QMimeData *data,
                  Qt::DropAction action = Qt::CopyAction)
{
    QDragEnterEvent enter(pos, action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    QDragMoveEvent move(pos, action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &move);
}

void sendDrop(VirtualItemView *view, const QPoint &pos, const QMimeData *data,
              Qt::DropAction action = Qt::CopyAction)
{
    QDropEvent drop(QPointF(pos), action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
}
} // namespace

/// §38: the kernel resolves the drop target, paints the indicator, autoscrolls
/// and hands the target to the model (canDropMimeData/dropMimeData); a tree adds
/// "drop into this item" and a table adds cell targets.
///
/// Not covered here: the guard against dropping a dragged item onto itself needs
/// a real platform drag (QDrag::exec), which has no offscreen behaviour.
class TestDnd : public QObject
{
    Q_OBJECT

private slots:
    void listTargetSplitsTheRowAtItsMidpoint();
    void listTargetIsInvalidOutsideTheContent();
    void listIndicatorIsALine();
    void dropAsksTheModelAndInserts();
    void dropIsRefusedWhenTheModelRefuses();
    void dragEnterIgnoresAnUnsupportedAction();
    void dropIndicatorFollowsTheScrollAndModelChange();
    void canStartDragFollowsTheModelFlags();

    void treeTargetDropsBetweenSiblings();
    void treeTargetDropsIntoAnItem();
    void treeIndicatorIsAFrameForAnItemAndALineBetweenRows();
    void treeTargetAppendsBelowTheLastRow();
    void treeAnchorsAnInsertionOfACollapsedBranch();
    void treeDropPassesTheNewParentToTheModel();

    void tableCellDropResolvesTheColumnAndItsIndicator();
    void tableRowDropKeepsTheWholeRow();
    void tableFrozenCellDropResolvesTheFrozenColumn();
};

void TestDnd::listTargetSplitsTheRowAtItsMidpoint()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Top half of a row inserts before it, the bottom half after it; a list
    // inserts inside the root.
    const VirtualItemView::DropTarget top = view.dropTargetAt(QPoint(20, 2));
    QCOMPARE(top.row, 0);
    QVERIFY(!top.parent.isValid());
    QCOMPARE(top.column, -1);
    QVERIFY(!top.ontoItem);

    QCOMPARE(view.dropTargetAt(QPoint(20, kRowHeight - 2)).row, 1);
    QCOMPARE(view.dropTargetAt(QPoint(20, kRowHeight + 2)).row, 1);
    QCOMPARE(view.dropTargetAt(QPoint(20, 2 * kRowHeight - 2)).row, 2);
}

void TestDnd::listTargetIsInvalidOutsideTheContent()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // The content is 40 px high: the empty area below it is not a drop target.
    const VirtualItemView::DropTarget target = view.dropTargetAt(QPoint(20, 200));
    QVERIFY(!target.isValid());
    QVERIFY(view.dropIndicatorRect(target).isEmpty());
}

void TestDnd::listIndicatorIsALine()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const VirtualItemView::DropTarget target = view.dropTargetAt(QPoint(20, kRowHeight - 2));
    QCOMPARE(view.dropIndicatorStyle(target), VirtualItemView::DropIndicatorStyle::Line);
    const QRect rect = view.dropIndicatorRect(target);
    QCOMPARE(rect.height(), 2);
    QCOMPARE(rect.width(), view.viewport()->width());
    // The line sits on the boundary between row 0 and row 1.
    QCOMPARE(rect.y(), kRowHeight - 1);
}

void TestDnd::dropAsksTheModelAndInserts()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    view.setDragEnabled(true);
    showView(&view, QSize(kViewWidth, kViewHeight));

    int droppedCount = 0;
    int droppedRow = -99;
    int droppedColumn = -99;
    QObject::connect(&view, &VirtualItemView::itemDropped,
                     [&](const QModelIndex &, int row, int column, Qt::DropAction) {
                         ++droppedCount;
                         droppedRow = row;
                         droppedColumn = column;
                     });

    QScopedPointer<QMimeData> data(payload(QStringLiteral("x")));
    sendDragMove(&view, QPoint(20, kRowHeight - 2), data.data());

    QWidget *indicator = dropIndicatorOf(&view);
    QVERIFY(indicator);
    QVERIFY(indicator->isVisible());
    QCOMPARE(indicator->geometry().y(), kRowHeight - 1);
    QCOMPARE(indicator->geometry().height(), 2);

    sendDrop(&view, QPoint(20, kRowHeight - 2), data.data());
    QCOMPARE(model.dropCalls, 1);
    QCOMPARE(model.lastRow, 1);
    QCOMPARE(model.lastColumn, -1);
    QVERIFY(!model.lastParent.isValid());
    QCOMPARE(model.rows(), QStringList({QStringLiteral("a"), QStringLiteral("x"),
                                        QStringLiteral("b"), QStringLiteral("c")}));
    // The application can observe the accepted drop (parent/row/column/action).
    QCOMPARE(droppedCount, 1);
    QCOMPARE(droppedRow, 1);
    QCOMPARE(droppedColumn, -1);
    // The gesture is over: the indicator is gone and the new row is materialized.
    view.flushPendingRelayout();
    QVERIFY(!dropIndicatorOf(&view)->isVisible());
    QCOMPARE(view.materializedItemCount(), qsizetype(4));
}

void TestDnd::dropIsRefusedWhenTheModelRefuses()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b")});
    model.acceptDrops = false;
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    view.setDragEnabled(true);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QScopedPointer<QMimeData> data(payload(QStringLiteral("x")));
    sendDragMove(&view, QPoint(20, 2), data.data());
    if (QWidget *indicator = dropIndicatorOf(&view))
        QVERIFY(!indicator->isVisible());

    sendDrop(&view, QPoint(20, 2), data.data());
    QCOMPARE(model.dropCalls, 0);
    QCOMPARE(model.rowCount(), 2);
}

void TestDnd::dragEnterIgnoresAnUnsupportedAction()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    view.setDragEnabled(true);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QScopedPointer<QMimeData> data(payload(QStringLiteral("x")));
    // The model only supports Move|Copy: a Link-only drag is refused.
    QDragEnterEvent link(QPoint(20, 2), Qt::LinkAction, data.data(), Qt::LeftButton,
                         Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &link);
    QVERIFY(!link.isAccepted());

    QDragEnterEvent copy(QPoint(20, 2), Qt::CopyAction, data.data(), Qt::LeftButton,
                         Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &copy);
    QVERIFY(copy.isAccepted());
    QCOMPARE(copy.dropAction(), Qt::CopyAction);
}

void TestDnd::dropIndicatorFollowsTheScrollAndModelChange()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setOverscan(1, 1);
    view.setModel(&model);
    view.setDragEnabled(true);
    showView(&view, QSize(kViewWidth, 3 * kRowHeight));

    QScopedPointer<QMimeData> data(payload(QStringLiteral("x")));
    sendDragMove(&view, QPoint(20, 3 * kRowHeight - 2), data.data());
    QWidget *indicator = dropIndicatorOf(&view);
    QVERIFY(indicator);
    QVERIFY(indicator->isVisible());
    QCOMPARE(indicator->geometry().y(), 3 * kRowHeight - 1);

    // Scrolling moves the rows under the cursor; the next hover re-resolves the
    // target instead of reporting a stale boundary.
    view.scrollByPixels(10);
    sendDragMove(&view, QPoint(20, 3 * kRowHeight - 2), data.data());
    QVERIFY(dropIndicatorOf(&view)->isVisible());

    // A model change releases the state of the gesture (the old rows are gone).
    QVERIFY(view.isDropIndicatorShown());
    view.setModel(nullptr);
    if (QWidget *widget = dropIndicatorOf(&view))
        QVERIFY(!widget->isVisible());
}

void TestDnd::canStartDragFollowsTheModelFlags()
{
    DropListModel model({QStringLiteral("a"), QStringLiteral("b")});
    RowAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Drag & drop is off until the application asks for it.
    QVERIFY(!view.isDragEnabled());
    QVERIFY(!view.canStartDrag(model.index(0, 0)));
    // Qt only delivers drop events to a widget that accepts drops: the viewport
    // opts in with the source side.
    QVERIFY(!view.viewport()->acceptDrops());
    view.setDragEnabled(true);
    QVERIFY(view.viewport()->acceptDrops());
    QVERIFY(view.canStartDrag(model.index(0, 0)));

    // The model owns the flags: without ItemIsDragEnabled there is no source.
    model.dragEnabled = false;
    QVERIFY(!view.canStartDrag(model.index(0, 0)));
    model.dragEnabled = true;
    QVERIFY(!view.canStartDrag(QModelIndex()));

    // The actions offered by the view come from the model (§38).
    QCOMPARE(view.dragDropActions(), Qt::MoveAction | Qt::CopyAction);
    view.setDragDropActions(Qt::LinkAction);
    QCOMPARE(view.dragDropActions(), Qt::LinkAction);
}

void TestDnd::treeTargetDropsBetweenSiblings()
{
    auto *model = new RecordingTreeModel(this);
    RecordingTreeModel::Node *a = model->append(nullptr, QStringLiteral("A"));
    model->append(a, QStringLiteral("a0"));
    model->append(a, QStringLiteral("a1"));
    model->append(nullptr, QStringLiteral("B"));

    RowAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.expand(model->indexForNode(a));
    view.flushPendingRelayout();

    const QRect aRect = view.visualRect(model->indexForNode(a));
    QCOMPARE(aRect.height(), kRowHeight);

    // Top band: before A, at A's level (the root).
    const VirtualItemView::DropTarget above = view.dropTargetAt(QPoint(20, aRect.top() + 1));
    QVERIFY(!above.parent.isValid());
    QCOMPARE(above.row, 0);
    QVERIFY(!above.ontoItem);

    // Bottom band: after A and still a sibling of A - its children stay below it.
    const VirtualItemView::DropTarget below
        = view.dropTargetAt(QPoint(20, aRect.bottom() - 1));
    QVERIFY(!below.parent.isValid());
    QCOMPARE(below.row, 1);
    QVERIFY(!below.ontoItem);
}

void TestDnd::treeTargetDropsIntoAnItem()
{
    auto *model = new RecordingTreeModel(this);
    RecordingTreeModel::Node *a = model->append(nullptr, QStringLiteral("A"));
    model->append(a, QStringLiteral("a0"));
    model->append(a, QStringLiteral("a1"));

    RowAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.expand(model->indexForNode(a));
    view.flushPendingRelayout();

    // The middle of a drop-enabled row means "become a child of this item": the
    // target is (item, item.rowCount()), the convention dropMimeData() uses.
    const QRect aRect = view.visualRect(model->indexForNode(a));
    const VirtualItemView::DropTarget target
        = view.dropTargetAt(QPoint(20, aRect.center().y()));
    QVERIFY(target.ontoItem);
    QCOMPARE(target.parent, model->indexForNode(a));
    QCOMPARE(target.row, 2);
    QCOMPARE(target.column, -1);
}

void TestDnd::treeIndicatorIsAFrameForAnItemAndALineBetweenRows()
{
    auto *model = new RecordingTreeModel(this);
    RecordingTreeModel::Node *a = model->append(nullptr, QStringLiteral("A"));
    model->append(a, QStringLiteral("a0"));
    model->append(a, QStringLiteral("a1"));
    model->append(nullptr, QStringLiteral("B"));

    RowAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.expand(model->indexForNode(a));
    view.flushPendingRelayout();

    const QRect aRect = view.visualRect(model->indexForNode(a));
    const VirtualItemView::DropTarget into
        = view.dropTargetAt(QPoint(20, aRect.center().y()));
    QCOMPARE(view.dropIndicatorStyle(into), VirtualItemView::DropIndicatorStyle::Frame);
    // The frame covers the row the drop lands into, inset by the item depth.
    const QRect frame = view.dropIndicatorRect(into);
    QCOMPARE(frame.y(), aRect.y());
    QCOMPARE(frame.height(), aRect.height());
    QCOMPARE(frame.x(), kIndentation);
    QCOMPARE(frame.right(), aRect.right());

    // An insertion between rows is a line on the boundary of the hovered row,
    // like QTreeView's AboveItem/BelowItem indicator.
    const VirtualItemView::DropTarget between
        = view.dropTargetAt(QPoint(20, aRect.bottom() - 1));
    QCOMPARE(between.row, 1);
    QCOMPARE(view.dropIndicatorStyle(between), VirtualItemView::DropIndicatorStyle::Line);
    const QRect line = view.dropIndicatorRect(between);
    QCOMPARE(line.height(), 2);
    QCOMPARE(line.y(), aRect.bottom());
}

void TestDnd::treeTargetAppendsBelowTheLastRow()
{
    auto *model = new RecordingTreeModel(this);
    RecordingTreeModel::Node *a = model->append(nullptr, QStringLiteral("A"));
    model->append(a, QStringLiteral("a0"));
    model->append(nullptr, QStringLiteral("B"));

    RowAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.expand(model->indexForNode(a));
    view.flushPendingRelayout();

    // Three visible rows (A, a0, B) in a 300 px viewport: the area below them
    // extends the root instead of being outside the content.
    const VirtualItemView::DropTarget target = view.dropTargetAt(QPoint(20, 200));
    QVERIFY(target.isValid());
    QVERIFY(!target.parent.isValid());
    QCOMPARE(target.row, 2);
    const QRect line = view.dropIndicatorRect(target);
    QCOMPARE(line.height(), 2);
    // The 2 px bar straddles the boundary at the end of the content.
    QCOMPARE(line.y(), 3 * kRowHeight - 1);
}

void TestDnd::treeAnchorsAnInsertionOfACollapsedBranch()
{
    auto *model = new RecordingTreeModel(this);
    RecordingTreeModel::Node *a = model->append(nullptr, QStringLiteral("A"));
    model->append(a, QStringLiteral("a0"));
    model->append(a, QStringLiteral("a1"));
    model->append(nullptr, QStringLiteral("B"));

    RowAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // A is collapsed, so its children are not visible rows: an insertion into it
    // has to be anchored on the branch itself.
    const QModelIndex aIndex = model->indexForNode(a);
    QVERIFY(!view.isExpanded(aIndex));
    VirtualItemView::DropTarget target;
    target.parent = aIndex;
    target.row = 2;
    const QRect line = view.dropIndicatorRect(target);
    QCOMPARE(line.height(), 2);
    QCOMPARE(line.y(), view.visualRect(aIndex).bottom());
}

void TestDnd::treeDropPassesTheNewParentToTheModel()
{
    auto *model = new RecordingTreeModel(this);
    RecordingTreeModel::Node *b = model->append(nullptr, QStringLiteral("B"));
    model->append(b, QStringLiteral("b0"));
    model->append(nullptr, QStringLiteral("C"));

    RowAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    view.setDragEnabled(true);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.expand(model->indexForNode(b));
    view.flushPendingRelayout();

    const QModelIndex bIndex = model->indexForNode(b);
    const QRect bRect = view.visualRect(bIndex);
    QScopedPointer<QMimeData> data(payload(QStringLiteral("x")));
    // The payload comes from another view, so the drop is the model's decision
    // (the guard against dropping onto itself only applies to internal moves).
    sendDragMove(&view, QPoint(20, bRect.center().y()), data.data(), Qt::MoveAction);
    QVERIFY(model->canDropCalls > 0);
    QCOMPARE(model->lastAction, Qt::MoveAction);
    QCOMPARE(model->lastParent, bIndex);
    QCOMPARE(model->lastRow, 1);
    QCOMPARE(model->lastColumn, -1);

    sendDrop(&view, QPoint(20, bRect.center().y()), data.data(), Qt::MoveAction);
    QCOMPARE(model->dropCalls, 1);
    QCOMPARE(model->lastParent, bIndex);
    QCOMPARE(model->lastRow, 1);
}

void TestDnd::tableRowDropKeepsTheWholeRow()
{
    auto *model = new QStandardItemModel(20, 4, this);
    TableRowAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    // Row semantics: the drag unit is the whole row, so the model gets no column.
    setSelectionUnit(view, VirtualItemView::SelectionBehavior::SelectRows);
    QCOMPARE(view.selectionBehavior(), VirtualItemView::SelectionBehavior::SelectRows);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const VirtualItemView::DropTarget target
        = view.dropTargetAt(QPoint(2 * kColumnWidth + 10, kRowHeight + 2));
    QCOMPARE(target.row, 1);
    QCOMPARE(target.column, -1);
}

void TestDnd::tableCellDropResolvesTheColumnAndItsIndicator()
{
    auto *model = new QStandardItemModel(20, 4, this);
    TableRowAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    setSelectionUnit(view, VirtualItemView::SelectionBehavior::SelectItems);
    QCOMPARE(view.selectionBehavior(), VirtualItemView::SelectionBehavior::SelectItems);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Item semantics resolve the cell: the row from the y, the column from the x.
    const int column = 2;
    const VirtualItemView::DropTarget target
        = view.dropTargetAt(QPoint(column * kColumnWidth + 10, kRowHeight + 2));
    QCOMPARE(target.row, 1);
    QCOMPARE(target.column, column);
    QVERIFY(!target.ontoItem);

    // The insertion line is narrowed to that column instead of spanning the row.
    const QRect line = view.dropIndicatorRect(target);
    const ColumnGeometry geometry = view.columnGeometry(column);
    QCOMPARE(line.height(), 2);
    QCOMPARE(line.x(), geometry.viewportX);
    QCOMPARE(line.width(), kColumnWidth);

    // Without a column (row semantics, or a list) the line stays full width.
    VirtualItemView::DropTarget rowTarget;
    rowTarget.row = 1;
    rowTarget.column = -1;
    QCOMPARE(view.dropIndicatorRect(rowTarget).width(), view.viewport()->width());
}

void TestDnd::tableFrozenCellDropResolvesTheFrozenColumn()
{
    auto *model = new QStandardItemModel(20, 10, this);
    TableRowAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setFrozenColumns({0, 1});
    view.setModel(model);
    setSelectionUnit(view, VirtualItemView::SelectionBehavior::SelectItems);
    QCOMPARE(view.selectionBehavior(), VirtualItemView::SelectionBehavior::SelectItems);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setHorizontalOffset(3 * kColumnWidth);

    // Frozen columns keep their x, so a drop inside them resolves to the frozen
    // column and its indicator line stays inside the frozen pane.
    const VirtualItemView::DropTarget frozen
        = view.dropTargetAt(QPoint(kColumnWidth + 10, kRowHeight + 2));
    QCOMPARE(frozen.column, 1);
    QVERIFY(view.isColumnFrozen(frozen.column));
    const QRect line = view.dropIndicatorRect(frozen);
    QCOMPARE(line.x(), kColumnWidth);
    QCOMPARE(line.width(), kColumnWidth);

    // The same pane aware hit test drives indexAt: a frozen cell is hit by its
    // own viewport x, not by the shifted content position. (indexAt is public
    // through the kernel interface and virtual, so the table's override runs.)
    const VirtualItemView &asView = view;
    const QModelIndex frozenIndex = asView.indexAt(QPoint(kColumnWidth + 10, kRowHeight + 2));
    QCOMPARE(frozenIndex.row(), 1);
    QCOMPARE(frozenIndex.column(), 1);

    // A drop into the scrollable pane resolves the column under the cursor.
    const VirtualItemView::DropTarget scrollable
        = view.dropTargetAt(QPoint(2 * kColumnWidth + 10, kRowHeight + 2));
    QVERIFY(scrollable.column >= 0);
    QVERIFY(!view.isColumnFrozen(scrollable.column));
}

QTEST_MAIN(TestDnd)
#include "tst_dnd.moc"
