#include <virtualitemviews/virtualmasonryview.h>
#include "vivtestfixtures.h"
#include <virtualitemviews/accessibility.h>

#include <QAccessible>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFrame>
#include <QHash>
#include <QMimeData>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QtTest>
#include <functional>
#include <limits>
#include <utility>

namespace {

class MasonryAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(120, 80); }
};

class EstimatedAdapter : public MasonryAdapter
{
public:
    mutable int estimateCalls = 0;
    int sizeMultiplier = 1;
    mutable std::function<void()> onEstimate;

    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        ++estimateCalls;
        const QSize sourceSize = index.data(Qt::UserRole).toSize();
        const QSize size(sourceSize.width(), sourceSize.height() * sizeMultiplier);
        if (onEstimate) {
            const auto callback = std::exchange(onEstimate, {});
            callback();
        }
        return size;
    }
};

class RootAdapter : public EstimatedAdapter
{
public:
    int childUnbinds = 0;
    bool invalidUnbind = false;

    void unbindWidget(viv::WidgetType, QWidget *, const QModelIndex &index) override
    {
        invalidUnbind = invalidUnbind || !index.isValid();
        if (index.data().toString().startsWith(QStringLiteral("child-")))
            ++childUnbinds;
    }
};

class ColumnAdapter : public EstimatedAdapter
{
public:
    QHash<QWidget *, QPersistentModelIndex> bindings;
    int unbinds = 0;
    bool badBinding = false;

    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        const QString text = index.data().toString();
        return text.endsWith(QStringLiteral("-second")) ? 2
            : text.endsWith(QStringLiteral("-new")) ? 3 : 1;
    }
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        QWidget *widget = MasonryAdapter::createWidget(type, parent);
        widget->setProperty("columnType", type);
        return widget;
    }
    void bindWidget(viv::WidgetType type, QWidget *widget, const QModelIndex &index) override
    {
        badBinding = badBinding || !index.isValid() || index.column() != 0
            || widget->property("columnType").toInt() != widgetType(index);
        bindings.insert(widget, QPersistentModelIndex(index));
        MasonryAdapter::bindWidget(type, widget, index);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        ++unbinds;
        badBinding = badBinding || !index.isValid() || !bindings.contains(widget)
            || bindings.take(widget) != index;
    }
};

class MasonryMovingModel : public QAbstractItemModel
{
public:
    MasonryMovingModel()
    {
        for (int root = 0; root < 2; ++root) {
            Node *parent = append(&m_root, QStringLiteral("root-%1").arg(root));
            for (int row = 0; row < 4; ++row)
                append(parent, QStringLiteral("child-%1-%2").arg(root).arg(row), 40 + row * 10);
        }
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        const Node *owner = node(parent);
        return column >= 0 && column < owner->columns && row >= 0 && row < owner->children.size()
            ? createIndex(row, column, owner->children.at(row)) : QModelIndex();
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return {};
        Node *owner = node(index)->parent;
        return owner == &m_root ? QModelIndex()
            : createIndex(owner->parent->children.indexOf(owner), 0, owner);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return node(parent)->children.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return node(parent)->columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return {};
        if (role == Qt::DisplayRole)
            return node(index)->text.at(index.column());
        if (role == Qt::UserRole) {
            const QString text = node(index)->text.at(index.column());
            const int extra = text.endsWith(QStringLiteral("-second")) ? 80
                : text.endsWith(QStringLiteral("-new")) ? 120 : 0;
            return QSize(260, node(index)->height + extra);
        }
        return {};
    }

    bool insertColumns(int first, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (count != 1 || first < 0 || first > columnCount(parent))
            return false;
        beginInsertColumns(parent, first, first);
        ++node(parent)->columns;
        for (Node *child : node(parent)->children)
            child->text.insert(first, QStringLiteral("inserted-new"));
        endInsertColumns();
        return true;
    }

    bool removeColumns(int first, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (count <= 0 || first < 0 || first + count > columnCount(parent))
            return false;
        beginRemoveColumns(parent, first, first + count - 1);
        node(parent)->columns -= count;
        for (Node *child : node(parent)->children) {
            for (int i = 0; i < count; ++i)
                child->text.removeAt(first);
        }
        endRemoveColumns();
        return true;
    }

    bool moveColumns(const QModelIndex &source, int first, int count,
                     const QModelIndex &destination, int column) override
    {
        if (source != destination || first != 0 || count != 1 || column != 2
            || columnCount(source) < 2 || !beginMoveColumns(source, first, first, destination, column))
            return false;
        for (Node *child : node(source)->children)
            child->text.move(0, 1);
        endMoveColumns();
        return true;
    }

    bool moveRows(const QModelIndex &source, int first, int count,
                  const QModelIndex &destination, int row) override
    {
        if (count != 1 || source == destination || first < 0 || first >= rowCount(source)
            || row < 0 || row > rowCount(destination)
            || !beginMoveRows(source, first, first, destination, row))
            return false;
        Node *moving = node(source)->children.takeAt(first);
        moving->parent = node(destination);
        node(destination)->children.insert(row, moving);
        endMoveRows();
        return true;
    }

private:
    struct Node {
        QStringList text;
        int height = 40;
        int columns = 2;
        Node *parent = nullptr;
        QVector<Node *> children;
        ~Node() { qDeleteAll(children); }
    };

    Node *node(const QModelIndex &index) const
    {
        return index.isValid() ? static_cast<Node *>(index.internalPointer())
                               : const_cast<Node *>(&m_root);
    }

    Node *append(Node *parent, const QString &text, int height = 40)
    {
        auto *child = new Node;
        child->text = QStringList{text, text + QStringLiteral("-second")};
        child->height = height;
        child->parent = parent;
        parent->children.append(child);
        return child;
    }

    Node m_root;
};

class MasonryColumnMovingModel : public QAbstractItemModel
{
public:
    MasonryColumnMovingModel()
    {
        populate(&m_top, QStringLiteral("top"));
        populate(m_top.rows.at(0).at(1), QStringLiteral("child"));
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        const Cell *owner = cell(parent);
        return row >= 0 && row < owner->rows.size() && column >= 0 && column < owner->columns
            ? createIndex(row, column, owner->rows.at(row).at(column)) : QModelIndex();
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        return index.isValid() ? indexForCell(cell(index)->parent) : QModelIndex();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return cell(parent)->rows.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return cell(parent)->columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return {};
        if (role == Qt::DisplayRole)
            return cell(index)->text;
        if (role == Qt::UserRole)
            return QSize(260, cell(index)->height);
        return {};
    }

    bool moveColumns(const QModelIndex &source, int first, int count,
                     const QModelIndex &destination, int column) override
    {
        if (source == destination || count != 1 || first < 0 || first >= columnCount(source)
            || column < 0 || column > columnCount(destination) || rowCount(source) != rowCount(destination))
            return false;
        Cell *from = cell(source);
        Cell *to = cell(destination);
        for (const auto &row : from->rows) {
            for (Cell *ancestor = to; ancestor; ancestor = ancestor->parent) {
                if (ancestor == row.at(first))
                    return false;
            }
        }
        if (!beginMoveColumns(source, first, first, destination, column))
            return false;
        for (int row = 0; row < from->rows.size(); ++row) {
            Cell *moving = from->rows[row].takeAt(first);
            moving->parent = to;
            to->rows[row].insert(column, moving);
        }
        --from->columns;
        ++to->columns;
        endMoveColumns();
        return true;
    }

private:
    struct Cell {
        QString text;
        int height = 40;
        int columns = 0;
        Cell *parent = nullptr;
        QVector<QVector<Cell *>> rows;
        ~Cell()
        {
            for (const auto &row : rows)
                qDeleteAll(row);
        }
    };

    Cell *cell(const QModelIndex &index) const
    {
        return index.isValid() ? static_cast<Cell *>(index.internalPointer()) : const_cast<Cell *>(&m_top);
    }

    QModelIndex indexForCell(Cell *target) const
    {
        if (!target || target == &m_top)
            return {};
        const Cell *owner = target->parent;
        for (int row = 0; row < owner->rows.size(); ++row) {
            const int column = owner->rows.at(row).indexOf(target);
            if (column >= 0)
                return createIndex(row, column, target);
        }
        return {};
    }

    void populate(Cell *owner, const QString &prefix)
    {
        owner->columns = 3;
        for (int row = 0; row < 4; ++row) {
            QVector<Cell *> cells;
            for (int column = 0; column < 3; ++column) {
                auto *item = new Cell;
                item->parent = owner;
                item->text = QStringLiteral("%1-%2-%3%4").arg(prefix).arg(row).arg(column)
                    .arg(column == 1 ? QStringLiteral("-second")
                        : prefix == QStringLiteral("child") ? QStringLiteral("-new") : QString());
                item->height = 40 + row * 10 + column * 80;
                cells.append(item);
            }
            owner->rows.append(cells);
        }
    }

    Cell m_top;
};

class MasonryDropModel : public QStandardItemModel
{
public:
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        return data && data->hasText() && action == Qt::CopyAction && column == -1
            && !parent.isValid() && row >= 0 && row <= rowCount();
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        ++calls;
        lastRow = row;
        lastColumn = column;
        if (!canDropMimeData(data, action, row, column, parent) || !submitSucceeds)
            return false;
        insertRow(row, new QStandardItem(data->text()));
        return true;
    }

    int calls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    bool submitSucceeds = true;
};

class PaintableMasonryView : public viv::VirtualMasonryView
{
public:
    using VirtualMasonryView::paintVisualStateBackgrounds;
    using VirtualMasonryView::paintGridLines;
    QModelIndex selected;

    VisualState visualState(const QModelIndex &index) const override
    {
        VisualState state;
        state.selected = index == selected;
        state.selectedProgress = state.selected ? 1.0 : 0.0;
        return state;
    }
};

} // namespace

class VirtualMasonryViewTest : public QObject
{
    Q_OBJECT
private slots:
    void materializesVisibleItemsAcrossColumns();
    void overscanUsesPerColumnNeighbors();
    void rootIndexBindsChildrenOnly();
    void rootRemovalRebuildsTopLevel_data();
    void rootRemovalRebuildsTopLevel();
    void modelResetDiscardsRootGeometry();
    void scrollsAndHitsAcrossColumns();
    void scrollsBeyondIntOffsetsAndKeepsAnchor();
    void publicGeometryTracksSourceIdentity();
    void adapterEstimatesAreLoadedAfterModel();
    void invalidAdapterEstimatesKeepFallbackAndIdentity();
    void lateAdapterAndReplacementRefreshEstimates();
    void sizeInvalidationsCoalesceAndKeepAnchor();
    void pendingSizesDiscardOutgoingModelAndAdapter();
    void estimateCallbackCanReplaceRoot();
    void sizeInvalidationSurvivesReentrantReflow();
    void modelMutationsPreserveSourceIdentityAndAnchor();
    void modelMutationsRekeyLookupAcrossColumns();
    void proxySortPreservesAuthoritativeHeight_data();
    void proxySortPreservesAuthoritativeHeight();
    void sortPreservesManualEstimates();
    void crossParentMovesPreserveChildSizing_data();
    void crossParentMovesPreserveChildSizing();
    void canonicalColumnChangesRebind_data();
    void canonicalColumnChangesRebind();
    void crossParentColumnMovesKeepCanonicalIdentity_data();
    void crossParentColumnMovesKeepCanonicalIdentity();
    void noncanonicalColumnsPreserveSizing_data();
    void noncanonicalColumnsPreserveSizing();
    void emptyColumnsDiscardItems_data();
    void emptyColumnsDiscardItems();
    void rootColumnRemovalRebuildsTopLevel_data();
    void rootColumnRemovalRebuildsTopLevel();
    void modelDestructionClearsMasonryState();
    void resizeReflowsColumnsWithoutLosingIdentity();
    void heightUpdatePreservesScrolledAnchor();
    void keyboardUsesGeometricNeighbors();
    void keyboardSupportsPageAndEnds();
    void selectionGesturesPreserveSourceOrder_data();
    void selectionGesturesPreserveSourceOrder();
    void selectionModesAndBlankClicks_data();
    void selectionModesAndBlankClicks();
    void dropUsesSourceOrder();
    void dropGapUsesSourceOrder();
    void dropSubmitsSourceOrderToModel();
    void heightAnimationCommitsTargetWhenDisabledByDuration();
    void heightAnimationMovesFollowingColumnItemsContinuously();
    void heightAnimationFinishesBeforeModelMutation();
    void stateBackgroundsRespectSpacingAndDpr();
    void gridLinesUseSinglePhysicalPixelBands();
    void accessibilityExposesCellsWithLiveColumn();
};

void VirtualMasonryViewTest::materializesVisibleItemsAcrossColumns()
{
    QStandardItemModel model;
    for (int row = 0; row < 80; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.resize(440, 240);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QVERIFY(view.columnCount() >= 2);
    QVERIFY(view.materializedItemCount() < model.rowCount());
    QSet<QPersistentModelIndex> identities;
    for (const auto &item : view.materializedItems())
        identities.insert(item.index);
    QCOMPARE(identities.size(), view.materializedItemCount());
    QVERIFY(view.contentMargins().left() >= 0);
}

void VirtualMasonryViewTest::overscanUsesPerColumnNeighbors()
{
    QStandardItemModel model(100, 1);
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(40);
    view.setRowSpacing(12);
    view.setModel(&model);
    vivtest::showView(&view, QSize(460, 170));
    view.setItemHeight(model.index(0, 0), 900);
    view.setVerticalOffset(300);

    const auto expected = [&](int before, int after) {
        QSet<QPersistentModelIndex> result;
        for (int column = 0; column < view.columnCount(); ++column) {
            QVector<QModelIndex> items;
            for (int row = 0; row < model.rowCount(); ++row) {
                const QModelIndex index = model.index(row, 0);
                if (view.itemColumn(index) == column)
                    items.append(index);
            }
            int first = 0;
            while (first < items.size() && view.visualRect(items.at(first)).bottom() < 0)
                ++first;
            int end = first;
            while (end < items.size()
                   && view.visualRect(items.at(end)).top() < view.viewport()->height())
                ++end;
            for (int position = qMax(0, first - before);
                 position < qMin(items.size(), end + after); ++position)
                result.insert(QPersistentModelIndex(items.at(position)));
        }
        return result;
    };
    const auto actual = [&]() {
        QSet<QPersistentModelIndex> result;
        for (const auto &item : view.materializedItems())
            result.insert(item.index);
        return result;
    };

    view.setOverscan(0, 0);
    view.flushPendingRelayout();
    QVERIFY(actual() == expected(0, 0));
    QVERIFY(view.widgetForIndex(model.index(0, 0)));

    view.setOverscan(3, 1);
    view.flushPendingRelayout();
    QVERIFY(actual() == expected(3, 1));

    const QPersistentModelIndex pin(model.index(99, 0));
    view.setItemPinned(pin, true);
    view.flushPendingRelayout();
    auto withPin = expected(3, 1);
    withPin.insert(pin);
    QVERIFY(actual() == withPin);
    view.setItemPinned(pin, false);
    view.flushPendingRelayout();
    QVERIFY(actual() == expected(3, 1));
}

void VirtualMasonryViewTest::rootIndexBindsChildrenOnly()
{
    QStandardItemModel model;
    auto *parentItem = new QStandardItem(QStringLiteral("parent"));
    for (int row = 0; row < 3; ++row)
        parentItem->appendRow(new QStandardItem(QStringLiteral("child-%1").arg(row)));
    model.appendRow(parentItem);

    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setFixedItemHeight(48);
    view.setModel(&model);
    vivtest::showView(&view, QSize(280, 180));

    const QModelIndex parent = model.index(0, 0);
    view.setRootIndex(parent);
    view.flushPendingRelayout();

    QCOMPARE(view.rootIndex(), parent);
    QCOMPARE(view.stats().logicalItems, qsizetype(3));
    QVERIFY(view.visualRect(parent).isEmpty());
    for (int row = 0; row < 3; ++row) {
        const QModelIndex child = model.index(row, 0, parent);
        QVERIFY(!view.visualRect(child).isEmpty());
        QVERIFY(view.itemColumn(child) >= 0);
        QCOMPARE(view.indexAt(view.visualRect(child).center()), child);
    }
}

void VirtualMasonryViewTest::rootRemovalRebuildsTopLevel_data()
{
    QTest::addColumn<bool>("removeAncestor");
    QTest::newRow("root") << false;
    QTest::newRow("ancestor") << true;
}

void VirtualMasonryViewTest::rootRemovalRebuildsTopLevel()
{
    QFETCH(bool, removeAncestor);
    QStandardItemModel model;
    auto *root = new QStandardItem(QStringLiteral("root"));
    for (int row = 0; row < 3; ++row) {
        auto *child = new QStandardItem(QStringLiteral("child-%1").arg(row));
        child->setData(QSize(260, 40), Qt::UserRole);
        root->appendRow(child);
    }
    if (removeAncestor) {
        auto *ancestor = new QStandardItem(QStringLiteral("ancestor"));
        ancestor->appendRow(root);
        model.appendRow(ancestor);
    } else {
        model.appendRow(root);
    }
    for (int row = 0; row < 3; ++row) {
        auto *item = new QStandardItem(QStringLiteral("top-%1").arg(row));
        item->setData(QSize(260, 120 + row * 40), Qt::UserRole);
        model.appendRow(item);
    }
    RootAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    view.setRootIndex(root->index());
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex child(root->child(0)->index());
    view.setCurrentIndex(child);
    view.setItemPinned(child, true);
    view.setItemHeight(child, 333);
    view.invalidateItemSize(child);
    QVERIFY(view.widgetForIndex(child));
    const int materialized = int(view.materializedItemCount());

    QVERIFY(model.removeRow(0));
    vivtest::settle();
    QVERIFY(!child.isValid());
    QVERIFY(!view.rootIndex().isValid());
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.stats().logicalItems, qsizetype(3));
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    QCOMPARE(adapter.childUnbinds, materialized);
    QVERIFY(!adapter.invalidUnbind);
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        QCOMPARE(view.visualRect(index).height(), 120 + row * 40);
        QCOMPARE(view.indexAt(view.visualRect(index).center()), index);
    }
    for (const auto &item : view.materializedItems())
        QVERIFY(!item.index.parent().isValid());

    auto *inserted = new QStandardItem(QStringLiteral("new-top"));
    inserted->setData(QSize(260, 70), Qt::UserRole);
    model.insertRow(0, inserted);
    vivtest::settle();
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 70);
    QCOMPARE(view.stats().logicalItems, qsizetype(4));
}

void VirtualMasonryViewTest::modelResetDiscardsRootGeometry()
{
    QStandardItemModel model;
    auto *root = new QStandardItem(QStringLiteral("root"));
    auto *child = new QStandardItem(QStringLiteral("child-0"));
    child->setData(QSize(260, 40), Qt::UserRole);
    root->appendRow(child);
    model.appendRow(root);
    RootAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    view.setRootIndex(root->index());
    vivtest::showView(&view, QSize(260, 180));
    view.setItemHeight(child->index(), 333);
    view.invalidateItemSize(child->index());
    model.clear();
    auto *replacement = new QStandardItem(QStringLiteral("replacement"));
    replacement->setData(QSize(260, 90), Qt::UserRole);
    model.appendRow(replacement);
    vivtest::settle();

    QVERIFY(!view.rootIndex().isValid());
    QCOMPARE(view.stats().logicalItems, qsizetype(1));
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 90);
    QCOMPARE(adapter.childUnbinds, 1);
    QVERIFY(!adapter.invalidUnbind);
    QCOMPARE(view.indexForWidget(view.widgetForIndex(model.index(0, 0))), model.index(0, 0));
}

void VirtualMasonryViewTest::scrollsAndHitsAcrossColumns()
{
    QStandardItemModel model;
    for (int row = 0; row < 120; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));

    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(48);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(460, 160));

    const QModelIndex target = model.index(50, 0);
    view.setCurrentIndex(target);
    view.scrollTo(target, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();

    QCOMPARE(view.currentIndex(), target);
    QCOMPARE(view.visualRect(target).top(), 0);
    QCOMPARE(view.indexAt(view.visualRect(target).center()), target);
    QVERIFY(view.stats().materializedItems < view.stats().logicalItems);
}

void VirtualMasonryViewTest::scrollsBeyondIntOffsetsAndKeepsAnchor()
{
    vivtest::NumericListModel model(100000);
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(100000);
    view.setRowSpacing(17);
    view.setOverscan(0, 0);
    view.setModel(&model);
    vivtest::showView(&view, QSize(620, 180));
    const QPersistentModelIndex current(model.index(90000, 0));
    view.setCurrentIndex(current);
    view.setItemPinned(current, true);
    view.scrollTo(current, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    const qint64 top = qint64(90000 / 3) * 100017;
    QVERIFY(top > std::numeric_limits<int>::max());
    QCOMPARE(view.verticalOffset(), top);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.indexAt(QPoint(10, 10)), QModelIndex(current));
    QWidget *const widget = view.widgetForIndex(current);
    QVERIFY(widget);
    QCOMPARE(view.materializedItemCount(), qsizetype(3));
    for (const auto &item : view.materializedItems())
        QVERIFY(item.index.row() >= 90000 && item.index.row() <= 90002);

    view.setItemHeight(model.index(0, 0), 101000);
    view.flushPendingRelayout();
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.widgetForIndex(current), widget);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QCOMPARE(view.indexAt(view.visualRect(current).topLeft() + QPoint(10, 10)), QModelIndex(current));
    QTest::keyClick(&view, Qt::Key_Down);
    QVERIFY(view.currentIndex().isValid());
    QVERIFY(view.currentIndex().row() > current.row());
    view.scrollTo(view.currentIndex(), viv::VirtualItemView::PositionAtTop);
    QCOMPARE(view.visualRect(view.currentIndex()).top(), 0);
    QCOMPARE(view.indexAt(view.visualRect(view.currentIndex()).topLeft() + QPoint(10, 10)),
             view.currentIndex());

    view.setItemPinned(current, false);
    view.setVerticalOffset(view.maximumVerticalOffset());
    view.flushPendingRelayout();
    const QModelIndex last = model.index(model.rowCount() - 1, 0);
    QCOMPARE(view.visualRect(last).bottom(), view.viewport()->height() - 1);
    QCOMPARE(view.indexAt(view.visualRect(last).bottomLeft() + QPoint(10, -10)), last);
    QVERIFY(view.materializedItemCount() < 10);
}

void VirtualMasonryViewTest::publicGeometryTracksSourceIdentity()
{
    QStandardItemModel model;
    for (int row = 0; row < 8; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();
    view.setItemHeight(model.index(0, 0), 180);
    view.setItemHeight(model.index(1, 0), 60);
    view.setItemHeight(model.index(2, 0), 100);

    const QRect first = view.visualRect(model.index(0, 0));
    const QRect second = view.visualRect(model.index(1, 0));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QCOMPARE(view.indexAt(first.center()), model.index(0, 0));
    QCOMPARE(view.indexAt(second.center()), model.index(1, 0));
    QVERIFY(view.itemColumn(model.index(0, 0)) >= 0);
    QVERIFY(view.itemColumn(model.index(1, 0)) >= 0);
}

void VirtualMasonryViewTest::adapterEstimatesAreLoadedAfterModel()
{
    QStandardItemModel model;
    for (const QSize &size : {QSize(140, 240), QSize(140, 60), QSize(140, 120)}) {
        auto *item = new QStandardItem;
        item->setData(size, Qt::UserRole);
        model.appendRow(item);
    }
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 240);
    QCOMPARE(view.visualRect(model.index(1, 0)).height(), 60);
    QCOMPARE(view.visualRect(model.index(2, 0)).height(), 120);

    const int callsBeforeUpdate = adapter.estimateCalls;
    model.setData(model.index(1, 0), QSize(140, 180), Qt::UserRole);
    QCoreApplication::processEvents();
    QCOMPARE(view.visualRect(model.index(1, 0)).height(), 180);
    QCOMPARE(adapter.estimateCalls - callsBeforeUpdate, 1);
}

void VirtualMasonryViewTest::invalidAdapterEstimatesKeepFallbackAndIdentity()
{
    QStandardItemModel model(4, 1);
    model.setData(model.index(0, 0), QSize(260, 400), Qt::UserRole);
    model.setData(model.index(1, 0), QSize(0, 999), Qt::UserRole);
    model.setData(model.index(2, 0), QSize(260, 0), Qt::UserRole);
    EstimatedAdapter adapter;
    EstimatedAdapter replacement;
    replacement.sizeMultiplier = 2;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(73);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 400);
    for (int row = 1; row < 4; ++row)
        QCOMPARE(view.visualRect(model.index(row, 0)).height(), 73);
    const QPersistentModelIndex current(model.index(1, 0));
    view.setCurrentIndex(current);
    view.setItemPinned(current, true);
    view.scrollTo(current, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *const widget = view.widgetForIndex(current);
    QVERIFY(widget);
    QCOMPARE(view.visualRect(current).top(), 0);
    model.setData(model.index(0, 0), QSize(0, 888), Qt::UserRole);
    for (int i = 0; i < 20; ++i)
        view.invalidateItemSize(model.index(0, 0));
    vivtest::settle();
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 73);
    QCOMPARE(view.visualRect(current).height(), 73);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.widgetForIndex(current), widget);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QVERIFY(view.isItemPinned(current));
    view.setItemHeight(model.index(2, 0), 111);
    view.setFixedItemHeight(91);
    QCOMPARE(view.visualRect(current).height(), 91);
    QCOMPARE(view.visualRect(model.index(2, 0)).height(), 111);
    view.setAdapter(&replacement);
    view.flushPendingRelayout();
    QCOMPARE(view.visualRect(current).height(), 91);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.visualRect(model.index(2, 0)).height(), 111);
    auto *inserted = new QStandardItem(QStringLiteral("invalid-new"));
    model.insertRow(0, inserted);
    vivtest::settle();
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 91);
    QCOMPARE(view.visualRect(current).height(), 91);
    QCOMPARE(view.visualRect(current).top(), 0);
    QVERIFY(view.isItemPinned(current));
    QVERIFY(model.removeRow(0));
    vivtest::settle();
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.visualRect(current).height(), 91);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QVERIFY(view.isItemPinned(current));
    QStandardItemModel nextModel(2, 1);
    view.setModel(&nextModel);
    vivtest::settle();
    QCOMPARE(view.visualRect(nextModel.index(0, 0)).height(), 91);
    QCOMPARE(view.visualRect(nextModel.index(1, 0)).height(), 91);
}

void VirtualMasonryViewTest::lateAdapterAndReplacementRefreshEstimates()
{
    QStandardItemModel model(30, 1);
    for (int row = 0; row < model.rowCount(); ++row)
        model.setData(model.index(row, 0), QSize(120, 60 + row), Qt::UserRole);
    EstimatedAdapter first;
    EstimatedAdapter replacement;
    replacement.sizeMultiplier = 2;
    viv::VirtualMasonryView view;
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    vivtest::showView(&view, QSize(420, 180));
    viv::VirtualItemView *base = &view;
    base->setAdapter(&first);
    view.flushPendingRelayout();
    const QModelIndex index = model.index(1, 0);
    const QRect original = view.visualRect(index);
    QCOMPARE(original.height(), qRound(original.width() * 61.0 / 120));
    QVERIFY(view.widgetForIndex(index));

    base->setAdapter(&replacement);
    view.flushPendingRelayout();
    const QRect updated = view.visualRect(index);
    QCOMPARE(updated.height(), qRound(updated.width() * 122.0 / 120));
    QCOMPARE(replacement.estimateCalls, model.rowCount());
    QCOMPARE(view.indexForWidget(view.widgetForIndex(index)), index);
}

void VirtualMasonryViewTest::sizeInvalidationsCoalesceAndKeepAnchor()
{
    QStandardItemModel model(100, 1);
    for (int row = 0; row < model.rowCount(); ++row)
        model.setData(model.index(row, 0), QSize(260, 40), Qt::UserRole);
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setColumnCount(1);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex anchor(model.index(40, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    vivtest::settle();
    QWidget *identity = view.widgetForIndex(anchor);
    QVERIFY(identity);
    const qint64 offset = view.verticalOffset();
    const int calls = adapter.estimateCalls;
    QSignalSpy layoutSpy(&view, &viv::VirtualMasonryView::masonryLayoutChanged);

    {
        const QSignalBlocker silent(&model);
        model.setData(model.index(0, 0), QSize(260, 100), Qt::UserRole);
        model.setData(model.index(2, 0), QSize(260, 80), Qt::UserRole);
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        view.invalidateItemSize(model.index(0, 0));
        view.invalidateItemSize(model.index(2, 0));
    }
    QCOMPARE(adapter.estimateCalls, calls);
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 40);
    QCOMPARE(layoutSpy.count(), 0);
    vivtest::settle();
    QCOMPARE(adapter.estimateCalls - calls, 2);
    QCOMPARE(layoutSpy.count(), 1);
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 100);
    QCOMPARE(view.visualRect(model.index(2, 0)).height(), 80);
    QCOMPARE(view.visualRect(anchor).top(), 0);
    QCOMPARE(view.verticalOffset(), offset + 100);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identity);

    layoutSpy.clear();
    const int callsBeforeDataChanges = adapter.estimateCalls;
    model.setData(model.index(2, 0), QSize(260, 90), Qt::UserRole);
    model.setData(model.index(2, 0), QSize(260, 110), Qt::UserRole);
    model.setData(model.index(3, 0), QSize(260, 70), Qt::UserRole);
    vivtest::settle();
    QCOMPARE(adapter.estimateCalls - callsBeforeDataChanges, 2);
    QCOMPARE(layoutSpy.count(), 1);
    QCOMPARE(view.visualRect(model.index(2, 0)).height(), 110);
    QCOMPARE(view.visualRect(model.index(3, 0)).height(), 70);
    QCOMPARE(view.visualRect(anchor).top(), 0);
}

void VirtualMasonryViewTest::pendingSizesDiscardOutgoingModelAndAdapter()
{
    QStandardItemModel original(20, 1);
    QStandardItemModel replacement(6, 1);
    for (QStandardItemModel *model : {&original, &replacement}) {
        for (int row = 0; row < model->rowCount(); ++row)
            model->setData(model->index(row, 0), QSize(140, 60), Qt::UserRole);
    }
    EstimatedAdapter adapter;
    EstimatedAdapter newAdapter;
    newAdapter.sizeMultiplier = 2;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&original);
    vivtest::showView(&view, QSize(420, 300));
    view.invalidateItemSize(original.index(0, 0));
    view.setModel(&replacement);
    const int callsAfterModel = adapter.estimateCalls;
    vivtest::settle();
    QCOMPARE(adapter.estimateCalls, callsAfterModel);
    QCOMPARE(view.stats().logicalItems, qsizetype(6));

    view.invalidateItemSize(replacement.index(0, 0));
    view.setAdapter(&newAdapter);
    const int callsAfterAdapter = newAdapter.estimateCalls;
    vivtest::settle();
    QCOMPARE(adapter.estimateCalls, callsAfterModel);
    QCOMPARE(newAdapter.estimateCalls, callsAfterAdapter);
    QCOMPARE(view.visualRect(replacement.index(0, 0)).height(), 120);
}

void VirtualMasonryViewTest::estimateCallbackCanReplaceRoot()
{
    QStandardItemModel model;
    for (const QString &name : {QStringLiteral("a"), QStringLiteral("b")}) {
        auto *parent = new QStandardItem(name);
        for (int row = 0; row < 5; ++row) {
            auto *child = new QStandardItem(name + QString::number(row));
            child->setData(QSize(140, name == QStringLiteral("a") ? 60 : 120), Qt::UserRole);
            parent->appendRow(child);
        }
        model.appendRow(parent);
    }
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    view.setRootIndex(model.index(0, 0));
    vivtest::showView(&view, QSize(420, 300));
    const QModelIndex outgoing = model.index(0, 0, view.rootIndex());
    view.invalidateItemSize(outgoing);
    adapter.onEstimate = [&]() { view.setRootIndex(model.index(1, 0)); };
    vivtest::settle();
    QCOMPARE(view.rootIndex(), model.index(1, 0));
    QVERIFY(!view.widgetForIndex(outgoing));
    for (const auto &item : view.materializedItems()) {
        QCOMPARE(item.index.parent(), view.rootIndex());
        const QRect rect = view.visualRect(item.index);
        QCOMPARE(rect.height(), qRound(rect.width() * 120.0 / 140));
    }
    QVERIFY(view.materializedItemCount() > 0);
}

void VirtualMasonryViewTest::sizeInvalidationSurvivesReentrantReflow()
{
    QStandardItemModel model(8, 1);
    for (int row = 0; row < model.rowCount(); ++row)
        model.setData(model.index(row, 0), QSize(140, 60), Qt::UserRole);
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    vivtest::showView(&view, QSize(420, 300));
    {
        const QSignalBlocker silent(&model);
        model.setData(model.index(0, 0), QSize(140, 180), Qt::UserRole);
    }
    adapter.onEstimate = [&]() { view.setColumnCount(1); };
    view.invalidateItemSize(model.index(0, 0));
    vivtest::settle();
    QCOMPARE(view.columnCount(), 1);
    const QRect rect = view.visualRect(model.index(0, 0));
    QCOMPARE(rect.height(), qRound(rect.width() * 180.0 / 140));
}

void VirtualMasonryViewTest::modelMutationsPreserveSourceIdentityAndAnchor()
{
    vivtest::StringListModel model;
    for (int row = 0; row < 80; ++row)
        model.appendRow(QStringLiteral("row-%1").arg(row));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 140));

    QPersistentModelIndex anchor(model.index(20, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    model.insertRowsAt(0, {QStringLiteral("inserted")});
    view.flushPendingRelayout();
    QVERIFY(anchor.isValid());
    QCOMPARE(anchor.row(), 21);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    QVERIFY(model.moveRow(anchor.row(), 2));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    QVERIFY(model.removeRowsAt(0, 1));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 1);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    model.replaceAll({QStringLiteral("fresh-0"), QStringLiteral("fresh-1")});
    view.flushPendingRelayout();
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.verticalOffset(), qint64(0));
}

void VirtualMasonryViewTest::modelMutationsRekeyLookupAcrossColumns()
{
    vivtest::StringListModel model;
    for (int row = 0; row < 80; ++row)
        model.appendRow(QStringLiteral("row-%1").arg(row));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(420, 180));

    QPersistentModelIndex anchor(model.index(20, 0));
    view.setItemPinned(anchor, true);
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);

    model.insertRowsAt(0, {QStringLiteral("inserted")});
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 21);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());

    QVERIFY(model.moveRow(anchor.row(), 2));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());
    QVERIFY(!view.visualRect(anchor).intersects(view.viewport()->rect()));

    QVERIFY(model.removeRowsAt(0, 1));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 1);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());
    view.setItemPinned(anchor, false);
    view.flushPendingRelayout();
    QVERIFY(!view.widgetForIndex(anchor));
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
}

void VirtualMasonryViewTest::proxySortPreservesAuthoritativeHeight_data()
{
    QTest::addColumn<bool>("rooted");
    QTest::addColumn<bool>("estimated");
    QTest::addColumn<bool>("animated");
    QTest::newRow("top-fixed") << false << false << false;
    QTest::newRow("root-fixed") << true << false << false;
    QTest::newRow("top-estimated") << false << true << false;
    QTest::newRow("root-estimated") << true << true << false;
    QTest::newRow("top-animated") << false << true << true;
    QTest::newRow("root-animated") << true << true << true;
}

void VirtualMasonryViewTest::proxySortPreservesAuthoritativeHeight()
{
    QFETCH(bool, rooted);
    QFETCH(bool, estimated);
    QFETCH(bool, animated);
    QStandardItemModel source;
    auto *owner = source.invisibleRootItem();
    if (rooted) {
        owner = new QStandardItem(QStringLiteral("root"));
        source.appendRow(owner);
    }
    for (int row = 0; row < 12; ++row) {
        auto *item = new QStandardItem(QStringLiteral("item-%1").arg(row, 2, 10, QLatin1Char('0')));
        item->setData(QSize(260, 40 + row * 10), Qt::UserRole);
        owner->appendRow(item);
    }
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    if (estimated)
        view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&proxy);
    if (rooted)
        view.setRootIndex(proxy.mapFromSource(owner->index()));
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex current(proxy.index(5, 0, view.rootIndex()));
    const QPersistentModelIndex other(proxy.index(2, 0, view.rootIndex()));
    view.setItemHeight(other, 177);
    view.setItemPinned(current, true);
    view.setCurrentIndex(current);
    view.scrollTo(current, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *const widget = view.widgetForIndex(current);
    QVERIFY(widget);
    view.setItemHeightAnimationDuration(500);
    view.setItemHeightAnimationEnabled(animated);
    view.setItemHeight(current, 333);

    proxy.sort(0, Qt::DescendingOrder);
    vivtest::settle();
    QCOMPARE(current.row(), 6);
    QCOMPARE(other.row(), 9);
    QCOMPARE(view.visualRect(current).height(), 333);
    QCOMPARE(view.visualRect(other).height(), 177);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QVERIFY(view.selectionModel()->isSelected(current));
    QCOMPARE(view.widgetForIndex(current), widget);
    QCOMPARE(view.indexForWidget(widget), QModelIndex(current));
    QCOMPARE(view.pinnedItemCount(), qsizetype(1));
    for (int row = 0; row < proxy.rowCount(view.rootIndex()); ++row) {
        const QModelIndex index = proxy.index(row, 0, view.rootIndex());
        if (index != current && index != other)
            QCOMPARE(view.visualRect(index).height(), estimated ? index.data(Qt::UserRole).toSize().height() : 40);
    }
    proxy.sort(0, Qt::AscendingOrder);
    vivtest::settle();
    QCOMPARE(current.row(), 5);
    QCOMPARE(view.visualRect(current).height(), 333);
    QCOMPARE(view.visualRect(other).height(), 177);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.widgetForIndex(current), widget);
    view.setItemPinned(current, false);
}

void VirtualMasonryViewTest::sortPreservesManualEstimates()
{
    QStandardItemModel source;
    source.appendRow(new QStandardItem(QStringLiteral("a")));
    source.appendRow(new QStandardItem(QStringLiteral("b")));
    source.appendRow(new QStandardItem(QStringLiteral("c")));
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setColumnCount(1);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&proxy);
    vivtest::showView(&view, QSize(260, 500));
    view.setItemEstimatedSizes({QSize(260, 70), QSize(260, 110), QSize(260, 150)});
    const QPersistentModelIndex first(proxy.index(0, 0));
    const QPersistentModelIndex second(proxy.index(1, 0));
    const QPersistentModelIndex third(proxy.index(2, 0));
    proxy.sort(0, Qt::DescendingOrder);
    vivtest::settle();
    QCOMPARE(first.row(), 2);
    QCOMPARE(third.row(), 0);
    QCOMPARE(view.visualRect(first).height(), 70);
    QCOMPARE(view.visualRect(second).height(), 110);
    QCOMPARE(view.visualRect(third).height(), 150);
    QCOMPARE(view.indexAt(view.visualRect(second).center()), QModelIndex(second));
}

void VirtualMasonryViewTest::crossParentMovesPreserveChildSizing_data()
{
    QTest::addColumn<bool>("estimated");
    QTest::newRow("fixed") << false;
    QTest::newRow("estimated") << true;
}

void VirtualMasonryViewTest::crossParentMovesPreserveChildSizing()
{
    QFETCH(bool, estimated);
    MasonryMovingModel model;
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    if (estimated)
        view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    const QPersistentModelIndex root(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex current(model.index(2, 0, root));
    const QPersistentModelIndex leaving(model.index(0, 0, root));
    const QPersistentModelIndex remaining(model.index(1, 0, root));
    const QPersistentModelIndex incoming(model.index(0, 0, destination));
    view.setItemHeight(leaving, 111);
    view.setItemHeight(current, 333);
    view.setCurrentIndex(current);
    view.setItemPinned(current, true);
    view.scrollTo(current, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *const widget = view.widgetForIndex(current);
    QVERIFY(widget);

    QVERIFY(model.moveRows(root, leaving.row(), 1, destination, 0));
    vivtest::settle();
    QCOMPARE(view.stats().logicalItems, qsizetype(3));
    QVERIFY(view.visualRect(leaving).isEmpty());
    QCOMPARE(view.visualRect(current).height(), 333);
    QCOMPARE(view.visualRect(remaining).height(), estimated ? 50 : 40);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.widgetForIndex(current), widget);

    QVERIFY(model.moveRows(destination, incoming.row(), 1, root, 0));
    vivtest::settle();
    QCOMPARE(view.stats().logicalItems, qsizetype(4));
    QCOMPARE(view.visualRect(incoming).height(), 40);
    QCOMPARE(view.visualRect(current).height(), 333);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.widgetForIndex(current), widget);

    QVERIFY(model.moveRows(QModelIndex(), root.row(), 1, destination, model.rowCount(destination)));
    vivtest::settle();
    QCOMPARE(root.parent(), QModelIndex(destination));
    QCOMPARE(view.rootIndex(), QModelIndex(root));
    QCOMPARE(view.stats().logicalItems, qsizetype(4));
    QCOMPARE(view.visualRect(current).height(), 333);
    QCOMPARE(view.visualRect(remaining).height(), estimated ? 50 : 40);
    QCOMPARE(view.visualRect(current).top(), 0);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QCOMPARE(view.widgetForIndex(current), widget);
    QCOMPARE(view.indexForWidget(widget), QModelIndex(current));
    QCOMPARE(view.pinnedItemCount(), qsizetype(1));
    view.setItemPinned(current, false);
}

void VirtualMasonryViewTest::canonicalColumnChangesRebind_data()
{
    QTest::addColumn<bool>("rooted");
    QTest::addColumn<int>("operation");
    for (bool rooted : {false, true}) {
        for (int operation = 0; operation < 3; ++operation)
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(rooted ? "root" : "top").arg(operation)))
                << rooted << operation;
    }
}

void VirtualMasonryViewTest::canonicalColumnChangesRebind()
{
    QFETCH(bool, rooted);
    QFETCH(int, operation);
    MasonryMovingModel model;
    ColumnAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    const QPersistentModelIndex root(rooted ? model.index(0, 0) : QModelIndex());
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(260, 40));
    const QPersistentModelIndex old(model.index(0, 0, root));
    view.setCurrentIndex(old);
    view.setItemPinned(old, true);
    view.setItemHeight(old, 333);
    view.scrollTo(model.index(1, 0, root), viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QCOMPARE(view.visualRect(model.index(1, 0, root)).top(), 0);
    const int materialized = int(view.materializedItemCount());
    adapter.unbinds = 0;

    if (operation == 0)
        QVERIFY(model.insertColumn(0, root));
    else if (operation == 1)
        QVERIFY(model.removeColumn(0, root));
    else
        QVERIFY(model.moveColumns(root, 0, 1, root, 2));
    vivtest::settle();

    QVERIFY(adapter.unbinds >= materialized);
    QVERIFY(!adapter.badBinding);
    const QModelIndex canonical = model.index(0, 0, root);
    QVERIFY(view.isItemPinned(canonical));
    QVERIFY(view.widgetForIndex(canonical));
    QCOMPARE(view.indexForWidget(view.widgetForIndex(canonical)), canonical);
    QCOMPARE(static_cast<QLabel *>(view.widgetForIndex(canonical))->text(), canonical.data().toString());
    QCOMPARE(view.visualRect(canonical).height(), canonical.data(Qt::UserRole).toSize().height());
    QCOMPARE(view.visualRect(model.index(1, 0, root)).top(), 0);
    QVERIFY(!view.currentIndex().isValid() || view.currentIndex() == canonical);
    for (const auto &item : view.materializedItems()) {
        QCOMPARE(item.index.column(), 0);
        QCOMPARE(item.index.parent(), QModelIndex(root));
        if (view.viewport()->rect().contains(view.visualRect(item.index).center()))
            QCOMPARE(view.indexAt(view.visualRect(item.index).center()), QModelIndex(item.index));
    }
    if (old.isValid())
        QVERIFY(view.visualRect(old).isEmpty());
}

void VirtualMasonryViewTest::crossParentColumnMovesKeepCanonicalIdentity_data()
{
    QTest::addColumn<bool>("rooted");
    QTest::addColumn<bool>("toTop");
    QTest::addColumn<bool>("canonical");
    QTest::addColumn<bool>("estimated");
    for (bool rooted : {false, true}) {
        for (bool toTop : {false, true}) {
            for (bool canonical : {false, true}) {
                for (bool estimated : {false, true})
                    QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3-%4")
                        .arg(rooted ? "root" : "top").arg(toTop ? "to-top" : "to-root")
                        .arg(canonical ? "canonical" : "other").arg(estimated ? "estimate" : "fixed")))
                        << rooted << toTop << canonical << estimated;
            }
        }
    }
}

void VirtualMasonryViewTest::crossParentColumnMovesKeepCanonicalIdentity()
{
    QFETCH(bool, rooted);
    QFETCH(bool, toTop);
    QFETCH(bool, canonical);
    QFETCH(bool, estimated);
    MasonryColumnMovingModel model;
    ColumnAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    if (estimated)
        view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    const QPersistentModelIndex branch(model.index(0, 1));
    const QPersistentModelIndex root(rooted ? QModelIndex(branch) : QModelIndex());
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(260, 40));
    const QPersistentModelIndex old(model.index(0, 0, root));
    view.setCurrentIndex(old);
    view.setItemPinned(old, true);
    view.setItemHeight(old, 333);
    view.scrollTo(model.index(1, 0, root), viv::VirtualItemView::PositionAtTop);
    view.setVerticalOffset(view.verticalOffset() + 7);
    view.flushPendingRelayout();
    QCOMPARE(view.visualRect(model.index(1, 0, root)).top(), -7);
    QWidget *const oldWidget = view.widgetForIndex(old);
    QVERIFY(oldWidget);
    const int materialized = int(view.materializedItemCount());
    adapter.unbinds = 0;

    const QPersistentModelIndex source(toTop ? QModelIndex(branch) : QModelIndex());
    const QPersistentModelIndex destination(toTop ? QModelIndex() : QModelIndex(branch));
    const int first = canonical ? 0 : 2;
    const int column = canonical ? 0 : model.columnCount(destination);
    const QPersistentModelIndex moving(model.index(0, first, source));
    const QString movingText = moving.data().toString();
    QVERIFY(model.moveColumns(source, first, 1, destination, column));
    vivtest::settle();

    QVERIFY(moving.isValid());
    QCOMPARE(moving.data().toString(), movingText);
    QCOMPARE(moving.parent(), QModelIndex(destination));
    QCOMPARE(moving.column(), column);
    QCOMPARE(model.index(moving.row(), moving.column(), moving.parent()), QModelIndex(moving));
    QCOMPARE(view.rootIndex(), QModelIndex(root));
    QCOMPARE(view.stats().logicalItems, qsizetype(4));
    QVERIFY(!adapter.badBinding);
    const QModelIndex currentCanonical = model.index(0, 0, root);
    QVERIFY(view.isItemPinned(currentCanonical));
    QWidget *const currentWidget = view.widgetForIndex(currentCanonical);
    QVERIFY(currentWidget);
    QCOMPARE(view.indexForWidget(currentWidget), currentCanonical);
    QCOMPARE(static_cast<QLabel *>(currentWidget)->text(), currentCanonical.data().toString());
    QCOMPARE(view.visualRect(model.index(1, 0, root)).top(), -7);
    if (canonical) {
        QVERIFY(adapter.unbinds >= materialized);
        QCOMPARE(view.visualRect(currentCanonical).height(),
                 estimated ? currentCanonical.data(Qt::UserRole).toSize().height() : 40);
        QVERIFY(!view.currentIndex().isValid() || view.currentIndex() == currentCanonical);
        QVERIFY(view.visualRect(old).isEmpty());
    } else {
        QCOMPARE(adapter.unbinds, 0);
        QCOMPARE(currentWidget, oldWidget);
        QCOMPARE(view.visualRect(old).height(), 333);
        QCOMPARE(view.currentIndex(), QModelIndex(old));
    }
    for (const auto &item : view.materializedItems()) {
        QCOMPARE(item.index.column(), 0);
        QCOMPARE(item.index.parent(), QModelIndex(root));
        const QPoint center = view.visualRect(item.index).center();
        if (view.viewport()->rect().contains(center))
            QCOMPARE(view.indexAt(center), QModelIndex(item.index));
    }
}

void VirtualMasonryViewTest::noncanonicalColumnsPreserveSizing_data()
{
    QTest::addColumn<bool>("rooted");
    QTest::newRow("top") << false;
    QTest::newRow("root") << true;
}

void VirtualMasonryViewTest::noncanonicalColumnsPreserveSizing()
{
    QFETCH(bool, rooted);
    MasonryMovingModel model;
    ColumnAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setModel(&model);
    const QPersistentModelIndex root(rooted ? model.index(0, 0) : QModelIndex());
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex current(model.index(0, 0, root));
    view.setItemHeight(current, 333);
    view.setCurrentIndex(current);
    view.setItemPinned(current, true);
    view.flushPendingRelayout();
    QWidget *const widget = view.widgetForIndex(current);
    QVERIFY(widget);
    adapter.unbinds = 0;
    QVERIFY(model.insertColumn(1, root));
    vivtest::settle();
    QVERIFY(model.removeColumn(1, root));
    vivtest::settle();
    QCOMPARE(view.widgetForIndex(current), widget);
    QCOMPARE(view.visualRect(current).height(), 333);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QVERIFY(view.isItemPinned(current));
    QCOMPARE(adapter.unbinds, 0);
    QVERIFY(!adapter.badBinding);
}

void VirtualMasonryViewTest::emptyColumnsDiscardItems_data()
{
    noncanonicalColumnsPreserveSizing_data();
}

void VirtualMasonryViewTest::emptyColumnsDiscardItems()
{
    QFETCH(bool, rooted);
    MasonryMovingModel model;
    ColumnAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    const QPersistentModelIndex root(rooted ? model.index(0, 0) : QModelIndex());
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex current(model.index(0, 0, root));
    view.setCurrentIndex(current);
    view.setItemPinned(current, true);
    view.setItemHeight(current, 333);
    view.invalidateItemSize(current);
    QVERIFY(model.removeColumns(0, 2, root));
    vivtest::settle();
    QVERIFY(model.rowCount(root) > 0);
    QCOMPARE(view.stats().logicalItems, qsizetype(0));
    QCOMPARE(view.materializedItemCount(), qsizetype(0));
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(!view.indexAt(QPoint(10, 10)).isValid());
    QVERIFY(!adapter.badBinding);
    QVERIFY(model.insertColumn(0, root));
    vivtest::settle();
    QCOMPARE(view.stats().logicalItems, qsizetype(model.rowCount(root)));
    const QModelIndex restored = model.index(0, 0, root);
    QVERIFY(view.widgetForIndex(restored));
    QCOMPARE(view.visualRect(restored).height(), restored.data(Qt::UserRole).toSize().height());
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    QVERIFY(!adapter.badBinding);
}

void VirtualMasonryViewTest::rootColumnRemovalRebuildsTopLevel_data()
{
    QTest::addColumn<bool>("removeAncestor");
    QTest::newRow("root") << false;
    QTest::newRow("ancestor") << true;
}

void VirtualMasonryViewTest::rootColumnRemovalRebuildsTopLevel()
{
    QFETCH(bool, removeAncestor);
    QStandardItemModel model;
    auto *root = new QStandardItem(QStringLiteral("root"));
    for (int row = 0; row < 3; ++row) {
        auto *child = new QStandardItem(QStringLiteral("child-%1").arg(row));
        child->setData(QSize(260, 40), Qt::UserRole);
        root->appendRow(child);
    }
    auto *other = new QStandardItem(QStringLiteral("top"));
    other->setData(QSize(260, 120), Qt::UserRole);
    if (removeAncestor) {
        auto *ancestor = new QStandardItem(QStringLiteral("ancestor"));
        ancestor->appendRow(root);
        model.appendRow(QList<QStandardItem *>{ancestor, other});
    } else {
        model.appendRow(QList<QStandardItem *>{root, other});
    }
    RootAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    view.setRootIndex(root->index());
    vivtest::showView(&view, QSize(260, 180));
    const QPersistentModelIndex child(root->child(0)->index());
    view.setCurrentIndex(child);
    view.setItemPinned(child, true);
    view.setItemHeight(child, 333);
    const int materialized = int(view.materializedItemCount());
    QVERIFY(model.removeColumn(0));
    vivtest::settle();
    QVERIFY(!child.isValid());
    QVERIFY(!view.rootIndex().isValid());
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.stats().logicalItems, qsizetype(1));
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    QCOMPARE(adapter.childUnbinds, materialized);
    QVERIFY(!adapter.invalidUnbind);
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 120);
    QCOMPARE(view.indexAt(view.visualRect(model.index(0, 0)).center()), model.index(0, 0));
}

void VirtualMasonryViewTest::modelDestructionClearsMasonryState()
{
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(40);
    auto *doomed = new QStandardItemModel(80, 1);
    view.setModel(doomed);
    vivtest::showView(&view, QSize(420, 180));

    QPersistentModelIndex current(doomed->index(20, 0));
    view.setCurrentIndex(current);
    view.scrollTo(current, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QVERIFY(view.widgetForIndex(current));
    QVERIFY(view.maximumVerticalOffset() > 0);

    delete doomed;
    vivtest::settle();
    QVERIFY(!view.model());
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.stats().logicalItems, qsizetype(0));
    QCOMPARE(view.stats().materializedItems, qsizetype(0));
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    QCOMPARE(view.maximumVerticalOffset(), qint64(0));

    QStandardItemModel replacement(7, 1);
    view.setModel(&replacement);
    vivtest::settle();
    QCOMPARE(view.stats().logicalItems, qsizetype(7));
    for (int row = 0; row < replacement.rowCount(); ++row) {
        const QModelIndex index = replacement.index(row, 0);
        QVERIFY(view.widgetForIndex(index));
    }
}

void VirtualMasonryViewTest::resizeReflowsColumnsWithoutLosingIdentity()
{
    QStandardItemModel model;
    for (int row = 0; row < 120; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("row-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setFixedItemHeight(56);
    view.resize(760, 180);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QPersistentModelIndex anchor(model.index(30, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    const int wideColumns = view.columnCount();
    QVERIFY(wideColumns >= 3);

    view.resize(420, 180);
    view.flushPendingRelayout();
    const int narrowColumns = view.columnCount();
    QVERIFY(narrowColumns < wideColumns);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());

    view.resize(900, 180);
    view.flushPendingRelayout();
    QVERIFY(view.columnCount() > narrowColumns);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());
}

void VirtualMasonryViewTest::heightUpdatePreservesScrolledAnchor()
{
    QStandardItemModel model;
    for (int row = 0; row < 100; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("row-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    QPersistentModelIndex anchor(model.index(40, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);
    const qint64 offsetBefore = view.verticalOffset();
    QVERIFY(offsetBefore > 0);

    view.setItemHeight(model.index(0, 0), 120);
    view.flushPendingRelayout();
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);
    QCOMPARE(view.verticalOffset(), offsetBefore + 80);
}

void VirtualMasonryViewTest::keyboardUsesGeometricNeighbors()
{
    QStandardItemModel model;
    for (int row = 0; row < 12; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();
    view.setItemHeight(model.index(0, 0), 180);
    view.setItemHeight(model.index(1, 0), 60);
    view.setItemHeight(model.index(2, 0), 100);
    view.setCurrentIndex(model.index(0, 0));
    view.setFocus();

    QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(view.currentIndex(), model.index(3, 0));
    view.setCurrentIndex(model.index(0, 0));
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(3, 0));
}

void VirtualMasonryViewTest::keyboardSupportsPageAndEnds()
{
    QStandardItemModel model;
    for (int row = 0; row < 12; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();
    view.setCurrentIndex(model.index(0, 0));
    view.setFocus();

    QTest::keyClick(&view, Qt::Key_PageDown);
    QVERIFY(view.currentIndex().isValid());
    QVERIFY(view.currentIndex() != model.index(0, 0));
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.currentIndex(), model.index(0, 0));
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.currentIndex(), model.index(11, 0));
}

void VirtualMasonryViewTest::selectionGesturesPreserveSourceOrder_data()
{
    QTest::addColumn<bool>("rooted");
    QTest::addColumn<bool>("estimated");
    QTest::newRow("top-fixed") << false << false;
    QTest::newRow("top-estimated") << false << true;
    QTest::newRow("root-fixed") << true << false;
    QTest::newRow("root-estimated") << true << true;
}

void VirtualMasonryViewTest::selectionGesturesPreserveSourceOrder()
{
    QFETCH(bool, rooted);
    QFETCH(bool, estimated);
    QStandardItemModel model;
    QStandardItem *owner = model.invisibleRootItem();
    if (rooted) {
        owner = new QStandardItem(QStringLiteral("root"));
        model.appendRow(owner);
    }
    for (int row = 0; row < 20; ++row) {
        auto *item = new QStandardItem(QStringLiteral("item-%1").arg(row));
        item->setData(QSize(135, 100), Qt::UserRole);
        owner->appendRow(QList<QStandardItem *>{item, new QStandardItem(QStringLiteral("hidden-%1").arg(row))});
    }
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setFixedItemHeight(100);
    if (estimated)
        view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setRowSpacing(8);
    view.setColumnSpacing(8);
    view.setModel(&model);
    const QPersistentModelIndex root(owner->index());
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(420, 600));
    QCOMPARE(view.columnCount(), 3);
    view.setItemHeight(model.index(0, 0, root), 180);
    view.setItemHeight(model.index(1, 0, root), 60);
    view.setItemHeight(model.index(2, 0, root), 100);
    view.flushPendingRelayout();
    const auto click = [&](int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QModelIndex index = model.index(row, 0, root);
        view.scrollTo(index);
        view.flushPendingRelayout();
        const QPoint center = view.visualRect(index).center();
        QVERIFY(view.viewport()->rect().contains(center));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, center);
        vivtest::settle();
    };
    const auto selectedRows = [&]() {
        QSet<int> rows;
        for (const QModelIndex &index : view.selectionModel()->selectedIndexes()) {
            if (index.parent() != root || index.column() != 0)
                return QSet<int>{-1};
            rows.insert(index.row());
        }
        return rows;
    };
    const auto selectedNames = [&]() {
        QSet<QString> names;
        for (const QModelIndex &index : view.selectionModel()->selectedIndexes()) {
            if (index.parent() != root || index.column() != 0)
                return QSet<QString>{QStringLiteral("wrong-source")};
            names.insert(index.data().toString());
        }
        return names;
    };

    click(0);
    QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(3, 0, root));
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3}));
    QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(view.currentIndex(), model.index(4, 0, root));
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3}));
    QTest::keyClick(&view, Qt::Key_Space, Qt::ControlModifier);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3, 4}));
    QTest::keyClick(&view, Qt::Key_Space, Qt::ControlModifier);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3}));
    click(0);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(3, 0, root));
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3}));

    click(0);
    click(7, Qt::ShiftModifier);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    click(9, Qt::ControlModifier);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3, 4, 5, 6, 7, 9}));
    click(7, Qt::ControlModifier);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1, 2, 3, 4, 5, 6, 9}));
    click(2, Qt::ShiftModifier);
    QCOMPARE(selectedRows(), (QSet<int>{2, 3, 4, 5, 6, 7}));
    const QPersistentModelIndex current(view.currentIndex());
    const QSet<QString> names = selectedNames();

    const QRect first = view.visualRect(model.index(0, 0, root));
    for (const QPoint &gap : {QPoint(first.right() + 3, first.top() + 10),
                              QPoint(first.center().x(), first.bottom() + 3)}) {
        QVERIFY(view.viewport()->rect().contains(gap));
        QVERIFY(!view.indexAt(gap).isValid());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, gap);
        QCOMPARE(selectedNames(), names);
        QCOMPARE(view.currentIndex(), QModelIndex(current));
    }
    view.resize(260, 600);
    vivtest::settle();
    QCOMPARE(view.columnCount(), 2);
    QCOMPARE(selectedNames(), names);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    view.setItemHeight(model.index(0, 0, root), 260);
    QCOMPARE(selectedNames(), names);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    owner->insertRow(0, QList<QStandardItem *>{new QStandardItem(QStringLiteral("new")), new QStandardItem});
    vivtest::settle();
    QCOMPARE(selectedNames(), names);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    QVERIFY(model.removeRow(0, root));
    vivtest::settle();
    QCOMPARE(selectedNames(), names);
    owner->sortChildren(0, Qt::DescendingOrder);
    vivtest::settle();
    QCOMPARE(selectedNames(), names);
    QCOMPARE(view.currentIndex(), QModelIndex(current));
    for (const QModelIndex &index : view.selectionModel()->selectedIndexes()) {
        QVERIFY(view.visualState(index).selected);
        const QRect rect = view.visualRect(index);
        QVERIFY(!rect.isEmpty());
        if (view.viewport()->rect().contains(rect.center()))
            QCOMPARE(view.indexAt(rect.center()), index);
    }
}

void VirtualMasonryViewTest::selectionModesAndBlankClicks_data()
{
    QTest::addColumn<bool>("rooted");
    QTest::newRow("top") << false;
    QTest::newRow("root") << true;
}

void VirtualMasonryViewTest::selectionModesAndBlankClicks()
{
    QFETCH(bool, rooted);
    QStandardItemModel model;
    QStandardItem *owner = model.invisibleRootItem();
    if (rooted) {
        owner = new QStandardItem(QStringLiteral("root"));
        model.appendRow(owner);
    }
    for (int row = 0; row < 4; ++row)
        owner->appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setFixedItemHeight(100);
    view.setModel(&model);
    const QPersistentModelIndex root(owner->index());
    view.setRootIndex(root);
    vivtest::showView(&view, QSize(420, 300));
    QCOMPARE(view.columnCount(), 3);
    const auto click = [&](int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint center = view.visualRect(model.index(row, 0, root)).center();
        QVERIFY(view.viewport()->rect().contains(center));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, center);
    };
    const auto selectedRows = [&]() {
        QSet<int> rows;
        for (const QModelIndex &index : view.selectionModel()->selectedIndexes()) {
            if (index.parent() != root || index.column() != 0)
                return QSet<int>{-1};
            rows.insert(index.row());
        }
        return rows;
    };

    view.setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
    click(0);
    click(1, Qt::ControlModifier);
    QCOMPARE(selectedRows(), (QSet<int>{1}));
    QTest::keyClick(&view, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(0, 0, root));
    QCOMPARE(selectedRows(), (QSet<int>{0}));
    view.setSelectionMode(viv::VirtualItemView::SelectionMode::MultiSelection);
    view.selectionModel()->clearSelection();
    click(0);
    click(1);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1}));
    click(0);
    QCOMPARE(selectedRows(), (QSet<int>{1}));
    QTest::keyClick(&view, Qt::Key_Space);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1}));

    const QModelIndex current(view.currentIndex());
    const QRect shortColumn = view.visualRect(model.index(1, 0, root));
    const QPoint absent(shortColumn.center().x(), shortColumn.bottom() + 20);
    QVERIFY(view.viewport()->rect().contains(absent));
    QVERIFY(!view.indexAt(absent).isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, absent);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1}));
    QCOMPARE(view.currentIndex(), current);
    const QPoint below(shortColumn.center().x(), view.viewport()->height() - 1);
    QVERIFY(!view.indexAt(below).isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, below);
    QCOMPARE(selectedRows(), (QSet<int>{0, 1}));
    QCOMPARE(view.currentIndex(), current);

    view.setSelectionMode(viv::VirtualItemView::SelectionMode::NoSelection);
    click(0);
    QVERIFY(selectedRows().isEmpty());
    QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(1, 0, root));
    QTest::keyClick(&view, Qt::Key_Space);
    QVERIFY(selectedRows().isEmpty());
}

void VirtualMasonryViewTest::dropUsesSourceOrder()
{
    QStandardItemModel model;
    for (int row = 0; row < 8; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    const QRect first = view.visualRect(model.index(0, 0));
    const viv::VirtualItemView::DropTarget target =
        view.dropTargetAt(first.center() + QPoint(0, first.height() / 3));
    QVERIFY(target.isValid());
    QCOMPARE(target.parent, QModelIndex());
    QCOMPARE(target.row, 1);
    QVERIFY(!view.dropIndicatorRect(target).isEmpty());
}

void VirtualMasonryViewTest::dropGapUsesSourceOrder()
{
    QStandardItemModel model;
    for (int row = 0; row < 6; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(60);
    view.setRowSpacing(12);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    const QRect first = view.visualRect(model.index(0, 0));
    const QPoint gap(first.center().x(), first.bottom() + 1);
    const viv::VirtualItemView::DropTarget target = view.dropTargetAt(gap);
    QVERIFY(target.isValid());
    QCOMPARE(target.parent, QModelIndex());
    QCOMPARE(target.row, 1);
    QVERIFY(!view.dropIndicatorRect(target).isEmpty());
}

void VirtualMasonryViewTest::dropSubmitsSourceOrderToModel()
{
    MasonryDropModel model;
    for (int row = 0; row < 4; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(60);
    view.setDragEnabled(true);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    const QRect first = view.visualRect(model.index(0, 0));
    QVERIFY(!first.isEmpty());
    const QPoint position = first.center() + QPoint(0, first.height() / 3);
    QMimeData data;
    data.setText(QStringLiteral("inserted"));

    QDragEnterEvent enter(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDragMoveEvent move(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(move.isAccepted());

    QSignalSpy submitted(&view, &viv::VirtualItemView::itemDropped);
    QDropEvent drop(QPointF(position), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &drop);
    QVERIFY(drop.isAccepted());
    vivtest::settle();
    QCOMPARE(model.calls, 1);
    QCOMPARE(model.lastRow, 1);
    QCOMPARE(model.lastColumn, -1);
    QCOMPARE(model.index(1, 0).data().toString(), QStringLiteral("inserted"));
    QCOMPARE(submitted.count(), 1);
}

void VirtualMasonryViewTest::heightAnimationCommitsTargetWhenDisabledByDuration()
{
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("item")));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(320, 240);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    view.setItemHeightAnimationDuration(0);
    view.setItemHeightAnimationEnabled(true);
    view.setItemHeight(model.index(0, 0), 180);
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 180);
}

void VirtualMasonryViewTest::heightAnimationMovesFollowingColumnItemsContinuously()
{
    QStandardItemModel model;
    for (int row = 0; row < 6; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setColumnSpacing(10);
    view.setRowSpacing(10);
    view.setColumnCount(1);
    view.setFixedItemHeight(100);
    view.resize(420, 700);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    const QModelIndex source = model.index(0, 0);
    qsizetype followingItem = -1;
    const int sourceColumn = view.itemColumn(source);
    const int sourceTop = view.visualRect(source).top();
    for (int row = 1; row < model.rowCount(); ++row) {
        const QModelIndex candidate = model.index(row, 0);
        if (view.itemColumn(candidate) == sourceColumn
            && view.visualRect(candidate).top() > sourceTop) {
            followingItem = row;
            break;
        }
    }
    QVERIFY(followingItem >= 0);
    const QModelIndex following = model.index(int(followingItem), 0);
    const int initialTop = view.visualRect(following).top();
    view.setItemHeightAnimationDuration(80);
    view.setItemHeightAnimationEnabled(true);
    view.setItemHeight(source, 220);
    QCoreApplication::processEvents();
    QTest::qWait(30);
    QCoreApplication::processEvents();
    const int intermediateTop = view.visualRect(following).top();
    QVERIFY(intermediateTop > initialTop);
    QVERIFY(intermediateTop < initialTop + 120);
    QTRY_VERIFY_WITH_TIMEOUT(view.visualRect(source).height() == 220, 500);
    QCOMPARE(view.visualRect(following).top(), initialTop + 120);
}

void VirtualMasonryViewTest::heightAnimationFinishesBeforeModelMutation()
{
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("original")));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setItemHeightAnimationDuration(500);
    view.setItemHeightAnimationEnabled(true);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    view.setItemHeight(model.index(0, 0), 100);
    model.insertRow(0, new QStandardItem(QStringLiteral("inserted")));
    view.flushPendingRelayout();

    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 40);
    QCOMPARE(view.visualRect(model.index(1, 0)).height(), 100);
}

void VirtualMasonryViewTest::stateBackgroundsRespectSpacingAndDpr()
{
    QStandardItemModel model;
    for (int row = 0; row < 6; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));

    PaintableMasonryView view;
    MasonryAdapter adapter;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setFixedItemHeight(40);
    view.setColumnSpacing(8);
    view.setRowSpacing(8);
    view.setSelectedBackgroundColor(Qt::red);
    view.setStateBackgroundCornerRadius(0);
    view.setModel(&model);
    vivtest::showView(&view, QSize(320, 220));

    const auto paint = [&](qreal dpr) {
        QImage image(qCeil(view.viewport()->width() * dpr),
                     qCeil(view.viewport()->height() * dpr),
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::white);
        QPainter painter(&image);
        view.paintVisualStateBackgrounds(&painter, view.viewport()->rect());
        painter.end();
        return image;
    };

    view.setColumnCount(2);
    view.flushPendingRelayout();
    view.selected = model.index(0, 0);
    const QRect columnItem = view.visualRect(view.selected);
    QVERIFY(!columnItem.isEmpty());
    const QPoint columnGap(columnItem.right() + 1, columnItem.center().y());
    QVERIFY(columnGap.x() < view.viewport()->width());
    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
        const auto withoutGap = paint(dpr);
        QCOMPARE(withoutGap.pixelColor(qRound(columnGap.x() * dpr),
                                       qRound(columnGap.y() * dpr)), QColor(Qt::white));
    }

    view.setColumnCount(1);
    view.flushPendingRelayout();
    const QRect rowItem = view.visualRect(view.selected);
    const QPoint rowGap(rowItem.center().x(), rowItem.bottom() + 1);
    QVERIFY(rowGap.y() < view.viewport()->height());
    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
        const auto withoutGap = paint(dpr);
        QCOMPARE(withoutGap.pixelColor(qRound(rowGap.x() * dpr),
                                       qRound(rowGap.y() * dpr)), QColor(Qt::white));
    }
}

void VirtualMasonryViewTest::gridLinesUseSinglePhysicalPixelBands()
{
    QStandardItemModel model;
    for (int row = 0; row < 12; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    PaintableMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setColumnSpacing(12);
    view.setRowSpacing(12);
    view.setVerticalGridLineColor(QColor(220, 30, 40));
    view.setHorizontalGridLineColor(QColor(30, 80, 220));
    view.setVerticalGridLineWidth(1);
    view.setHorizontalGridLineWidth(1);
    view.resize(420, 500);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QVector<QModelIndex> firstItems;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        bool hasEarlierInColumn = false;
        for (const QModelIndex &earlier : firstItems) {
            if (view.itemColumn(earlier) == view.itemColumn(index)) {
                hasEarlierInColumn = true;
                break;
            }
        }
        if (!hasEarlierInColumn)
            firstItems.append(index);
    }
    QVERIFY(firstItems.size() >= 2);
    const QModelIndex left = firstItems.at(0);
    const QModelIndex right = firstItems.at(1);
    const QRect leftRect = view.visualRect(left);
    const QRect rightRect = view.visualRect(right);
    QVERIFY(leftRect.right() < rightRect.left());

    QModelIndex below;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex candidate = model.index(row, 0);
        if (view.itemColumn(candidate) == view.itemColumn(left)
            && view.visualRect(candidate).top() > leftRect.bottom()) {
            below = candidate;
            break;
        }
    }
    QVERIFY(below.isValid());
    const QRect belowRect = view.visualRect(below);

    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
        QImage image(900, 900, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::white);
        QPainter painter(&image);
        view.paintGridLines(&painter);
        painter.end();

        const auto countNearVertical = [&](int logicalX, int logicalY, const QColor &color) {
            const int x = qRound(logicalX * dpr);
            const int y = qRound(logicalY * dpr);
            int count = 0;
            for (int px = qMax(0, x - 3); px <= qMin(image.width() - 1, x + 3); ++px)
                count += image.pixelColor(px, y) == color;
            return count;
        };
        const auto countNearHorizontal = [&](int logicalX, int logicalY, const QColor &color) {
            const int x = qRound(logicalX * dpr);
            const int y = qRound(logicalY * dpr);
            int count = 0;
            for (int py = qMax(0, y - 3); py <= qMin(image.height() - 1, y + 3); ++py)
                count += image.pixelColor(x, py) == color;
            return count;
        };

        QCOMPARE(countNearVertical(leftRect.right() + 1, leftRect.center().y(),
                                   QColor(220, 30, 40)), 1);
        QCOMPARE(countNearVertical(rightRect.left(), rightRect.center().y(),
                                   QColor(220, 30, 40)), 1);
        QCOMPARE(countNearHorizontal(leftRect.center().x(), leftRect.bottom() + 1,
                                     QColor(30, 80, 220)), 1);
        QCOMPARE(countNearHorizontal(belowRect.center().x(), belowRect.top(),
                                     QColor(30, 80, 220)), 1);
    }
}

void VirtualMasonryViewTest::accessibilityExposesCellsWithLiveColumn()
{
    QStandardItemModel model;
    for (int row = 0; row < 8; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    viv::installAccessibilityFactory();
    QAccessibleInterface *viewNode = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewNode);
    QVERIFY(viewNode->childCount() > 0);
    QAccessibleInterface *cell = viewNode->child(0);
    QVERIFY(cell);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("item-0"));
    QVERIFY(!cell->state().disabled);
    QVERIFY(cell->state().selectable);
    QVERIFY(!cell->state().selected);
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5 exposes the Cell role through the generic Client role.
    QVERIFY(cell->role() == QAccessible::Cell || cell->role() == QAccessible::Client);
#else
    QCOMPARE(cell->role(), QAccessible::Cell);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto *cellInterface = static_cast<QAccessibleTableCellInterface *>(
        cell->interface_cast(QAccessible::TableCellInterface));
    QVERIFY(cellInterface);
    QCOMPARE(cellInterface->columnIndex(), view.itemColumn(model.index(0, 0)));
    QCOMPARE(cellInterface->rowIndex(), 0);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QCOMPARE(cell->rect().size(), view.visualRect(model.index(0, 0)).size());
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    view.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect);
    QCoreApplication::processEvents();
    QVERIFY(cell->state().selected);

    QVERIFY(viewNode->childCount() > 3);
    QAccessibleInterface *movingCell = viewNode->child(3);
    QVERIFY(movingCell);
    auto *movingCellInterface = static_cast<QAccessibleTableCellInterface *>(
        movingCell->interface_cast(QAccessible::TableCellInterface));
    QVERIFY(movingCellInterface);
    const QModelIndex movingIndex = model.index(3, 0);
    const int oldColumn = movingCellInterface->columnIndex();
    const int oldColumnCount = view.columnCount();
    view.resize(900, 300);
    view.flushPendingRelayout();
    QVERIFY(view.columnCount() != oldColumnCount);
    QCOMPARE(movingCellInterface->columnIndex(), view.itemColumn(movingIndex));
    QCOMPARE(movingCell->rect().size(), view.visualRect(movingIndex).size());
    QVERIFY(movingCellInterface->columnIndex() != oldColumn);
#endif
    viv::removeAccessibilityFactory();
}

QTEST_MAIN(VirtualMasonryViewTest)
#include "tst_virtualmasonryview.moc"
