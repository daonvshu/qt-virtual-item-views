#include <virtualitemviews/accessibility.h>
#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/virtualtreetableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QAccessible>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <functional>

using namespace viv;
using namespace vivtest;

namespace {

QList<QStandardItem *> row(const QString &name)
{
    return {new QStandardItem(name), new QStandardItem(name + QStringLiteral(" type")),
            new QStandardItem(name + QStringLiteral(" state"))};
}

class RowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QWidget(parent); }
    void bindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }
};

class ColoredHost : public ColumnHost
{
public:
    explicit ColoredHost(int column, QWidget *parent) : ColumnHost(column, parent) {}

    void setFillColor(const QColor &color)
    {
        m_fillColor = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_fillColor.isValid())
            return;
        QPainter painter(this);
        painter.fillRect(rect(), m_fillColor);
    }

private:
    QColor m_fillColor;
};

class HostedRow : public QWidget
{
public:
    HostedRow(int columns, QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < columns; ++column)
            m_hosts.append(new ColoredHost(column, this));
    }

    ColoredHost *host(int column) const { return m_hosts.at(column); }

private:
    QVector<ColoredHost *> m_hosts;
};

class ParentTypedRowAdapter : public TableWidgetAdapter
{
public:
    explicit ParentTypedRowAdapter(int columns = 3) : m_columns(columns) {}

    WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.parent().data().toString() == QStringLiteral("wide") ? 1 : 0;
    }

    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        auto *widget = new HostedRow(m_columns, parent);
        widget->setProperty("rowType", type);
        return widget;
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }

private:
    int m_columns = 3;
};

class CellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

class RootVisibilityProxy : public QSortFilterProxyModel
{
public:
    void setHideFirstRoot(bool hidden)
    {
        m_hideFirstRoot = hidden;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        return sourceParent.isValid() || !m_hideFirstRoot || sourceRow != 0;
    }

private:
    bool m_hideFirstRoot = false;
};

class ParentTypedCellAdapter : public CellWidgetAdapter
{
public:
    WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() == 2 && index.parent().data().toString() == QStringLiteral("wide")
            ? 1 : 0;
    }

    QWidget *createCellWidget(WidgetType type, QWidget *parent) override
    {
        return type == 1 ? static_cast<QWidget *>(new QProgressBar(parent))
                         : static_cast<QWidget *>(new QLabel(parent));
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (auto *progress = qobject_cast<QProgressBar *>(widget)) {
            progress->setRange(0, 100);
            progress->setValue(75);
            progress->setFormat(index.data().toString());
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }
};

class InspectTreeTableView : public VirtualTreeTableView
{
public:
    using VirtualTreeTableView::selectionRange;
};

class RecordingDropModel : public QStandardItemModel
{
public:
    QStringList mimeTypes() const override
    {
        return {QStringLiteral("application/x-viv-tree-table-test")};
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::CopyAction | Qt::MoveAction;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int targetRow,
                         int column, const QModelIndex &parent) const override
    {
        const bool allowed = data && data->hasFormat(mimeTypes().first())
            && (action == Qt::CopyAction || action == Qt::MoveAction)
            && targetRow >= 0 && targetRow <= rowCount(parent) && column <= 0;
        if (onCanDrop)
            onCanDrop();
        return allowed;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int targetRow,
                      int column, const QModelIndex &parent) override
    {
        ++dropCalls;
        lastAction = action;
        lastRow = targetRow;
        lastColumn = column;
        lastParent = parent;
        if (onDrop) {
            onDrop();
            return acceptDrop;
        }
        if (!acceptDrop || !canDropMimeData(data, action, targetRow, column, parent))
            return false;
        const QList<QStandardItem *> items = row(QStringLiteral("dropped"));
        if (parent.isValid())
            itemFromIndex(parent)->insertRow(targetRow, items);
        else
            insertRow(targetRow, items);
        return true;
    }

    int dropCalls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    Qt::DropAction lastAction = Qt::IgnoreAction;
    QPersistentModelIndex lastParent;
    bool acceptDrop = true;
    std::function<void()> onCanDrop;
    std::function<void()> onDrop;
};

class RejectingMoveModel : public QStandardItemModel
{
public:
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        ++moveCalls;
        lastSourceParent = sourceParent;
        lastSourceRow = sourceRow;
        lastCount = count;
        lastDestinationParent = destinationParent;
        lastDestinationChild = destinationChild;
        return false;
    }

    int moveCalls = 0;
    int lastSourceRow = -1;
    int lastCount = 0;
    int lastDestinationChild = -1;
    QPersistentModelIndex lastSourceParent;
    QPersistentModelIndex lastDestinationParent;
};

class SiblingMoveModel : public QAbstractItemModel
{
public:
    SiblingMoveModel()
    {
        Node *parent = append(&m_root, QStringLiteral("parent"));
        append(parent, QStringLiteral("child 0"));
        append(parent, QStringLiteral("child 1"));
        append(parent, QStringLiteral("child 2"));
        append(&m_root, QStringLiteral("other parent"));
    }

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override
    {
        if (row < 0 || column < 0 || column >= 3 || (parent.isValid() && parent.column() != 0))
            return QModelIndex();
        Node *owner = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                       : const_cast<Node *>(&m_root);
        return row < owner->children.size()
            ? createIndex(row, column, owner->children.at(row)) : QModelIndex();
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid())
            return QModelIndex();
        Node *owner = static_cast<Node *>(child.internalPointer())->parent;
        if (!owner || owner == &m_root)
            return QModelIndex();
        return createIndex(owner->parent->children.indexOf(owner), 0, owner);
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
        return parent.isValid() && parent.column() != 0 ? 0 : 3;
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
        if (!sourceParent.isValid() || sourceParent != destinationParent || count != 1
            || sourceRow < 0 || sourceRow >= rowCount(sourceParent)
            || destinationChild < 0 || destinationChild > rowCount(destinationParent))
            return false;
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow,
                           destinationParent, destinationChild))
            return false;
        Node *owner = static_cast<Node *>(sourceParent.internalPointer());
        Node *moving = owner->children.takeAt(sourceRow);
        owner->children.insert(destinationChild > sourceRow ? destinationChild - 1
                                                            : destinationChild, moving);
        endMoveRows();
        ++moveCalls;
        return true;
    }

    int moveCalls = 0;

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
        Node *node = new Node;
        node->text = text;
        node->parent = parent;
        parent->children.append(node);
        return node;
    }

    Node m_root;
};

void sendHeaderMouse(QWidget *widget, QEvent::Type type, const QPoint &position,
                     Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

bool sendDrop(VirtualTreeTableView *view, const QPoint &position,
              const QMimeData *data, Qt::DropAction action)
{
    QDragEnterEvent enter(position, action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    QDragMoveEvent move(position, action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &move);
    QDropEvent drop(QPointF(position), action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
    return drop.isAccepted();
}

void configure(VirtualTreeTableView *view, QStandardItemModel *model, bool cells)
{
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    if (cells) {
        view->setCellAdapter(new CellAdapter, true);
        view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view->setTableAdapter(new RowAdapter, true);
    }
    view->setModel(model);
    showView(view, QSize(440, 280));
}

} // namespace

class TestTreeTableViewInteraction : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void branchClickFollowsMovedColumn();
    void frozenColumnAndSiblingSpanStayAligned();
    void expandedFrozenRowsKeepHeadersAndBranchAligned();
    void rowStripDragRejectsCrossParentAndRestoresPreview();
    void rowStripDragMovesSiblingIdentity();
    void proxySortKeepsNodeStateAndRestoresHeaderLayout();
    void filteringDoesNotTransferNodeStateToAnotherRow();
    void modelResetClearsRootCellAndNodeState();
    void parentSpecificCellWidgetsRespectMissingColumnsAndFolding();
    void parentSpecificRowHostsRespectMissingColumnsAndFolding();
    void rowHostsStayClippedInSeparateScrollGroups();
    void depthSpacingKeepsRowHeadersAndHitTestsAligned();
    void customSpacingWidgetsFollowVisibleNodesAndHeaders();
    void wideTreeMaterializesOnlyTheWindow();
    void accessibilityExposesHierarchyAndCells();
    void selectedBackgroundExtentKeepsGridLineVisible();
    void rootSchemaAndCrossParentSelection();
    void dropTargetsFollowVisibleTreeAndSpacing();
    void modelReceivesDropAndCanRejectIt();
    void modelSwitchDuringDropDoesNotSubmitStaleTarget();
};

void TestTreeTableViewInteraction::initTestCase()
{
    installAccessibilityFactory();
}

void TestTreeTableViewInteraction::cleanupTestCase()
{
    removeAccessibilityFactory();
}

void TestTreeTableViewInteraction::branchClickFollowsMovedColumn()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, false);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    view.horizontalHeaderGeometry()->moveSection(0, 2);
    view.flushPendingRelayout();
    const QModelIndex node = model.index(0, 0);
    const ColumnGeometry column = view.columnGeometry(0);
    QVERIFY(column.viewportX > 0);
    const QPoint indicator(column.viewportX + view.indentation() / 2,
                           view.visualRect(node).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(model.index(0, 1)).center()), model.index(0, 1));
}

void TestTreeTableViewInteraction::frozenColumnAndSiblingSpanStayAligned()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("first")));
    parent.first()->appendRow(row(QStringLiteral("second")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    view.setFrozenColumns({0});
    view.setFrozenRows(1);
    view.expand(model.index(0, 0));
    view.setSpan(1, 1, 2, 2);
    view.flushPendingRelayout();

    const QModelIndex first = model.index(0, 1, model.index(0, 0));
    const QModelIndex covered = model.index(1, 2, model.index(0, 0));
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QCOMPARE(view.anchorIndex(covered), first);
    QVERIFY(view.isSpanCovered(covered));
    QVERIFY(view.spanRect(first).height() > view.visualRect(first).height());
    QVERIFY(view.cellWidget(first));
    QVERIFY(!view.cellWidget(covered));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(model.index(0, 0)).center()), model.index(0, 0));
}

void TestTreeTableViewInteraction::expandedFrozenRowsKeepHeadersAndBranchAligned()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto first = row(QStringLiteral("first"));
    first.first()->appendRow(row(QStringLiteral("first child")));
    model.appendRow(first);
    model.appendRow(row(QStringLiteral("middle")));
    auto last = row(QStringLiteral("last"));
    last.first()->appendRow(row(QStringLiteral("last child")));
    model.appendRow(last);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({2});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.flushPendingRelayout();

    const QModelIndex firstRoot = model.index(0, 0);
    const QModelIndex lastRoot = model.index(2, 0);
    const QModelIndex lastChild = model.index(0, 0, lastRoot);
    const auto verifyFrozenRow = [&](const QModelIndex &node, qsizetype row,
                                     ItemPane::Type type) {
        QApplication::processEvents();
        const QVector<ItemPane> rowPanes = view.itemPanes();
        QCOMPARE(rowPanes.size(), 3);
        QRect paneRect;
        for (const ItemPane &pane : rowPanes) {
            if (pane.type == type) {
                paneRect = pane.viewportRect;
                QCOMPARE(pane.firstRow <= row && row <= pane.lastRow, true);
                break;
            }
        }
        QVERIFY(!paneRect.isEmpty());
        const QRect bodyRect = view.visualRect(node);
        QVERIFY(paneRect.contains(bodyRect.center()));
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     view.cellRect(node.siblingAtColumn(1)).center()),
                 node.siblingAtColumn(1));
        const QRect viewportRect = view.viewport()->geometry();
        bool foundHeader = false;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (header->orientation() != Qt::Vertical || !header->isVisible()
                || header->geometry().y() != viewportRect.y() + paneRect.y()
                || header->height() != paneRect.height())
                continue;
            QWidget *section = header->sectionWidget(int(row));
            QVERIFY(section);
            QCOMPARE(header->geometry().y() + section->y(),
                     viewportRect.y() + bodyRect.y());
            auto *label = qobject_cast<LabelHeaderSection *>(section);
            QVERIFY(label);
            QCOMPARE(label->text(), QString::number(row + 1));
            foundHeader = true;
            break;
        }
        QVERIFY(foundHeader);
    };

    verifyFrozenRow(firstRoot, 0, ItemPane::Type::FrozenTop);
    verifyFrozenRow(lastRoot, 2, ItemPane::Type::FrozenBottom);
    const ColumnGeometry firstColumn = view.columnGeometry(0);
    const QPoint firstBranch(firstColumn.viewportX + view.indentation() / 2,
                             view.visualRect(firstRoot).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstBranch);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    verifyFrozenRow(lastRoot, 3, ItemPane::Type::FrozenBottom);

    const QPoint lastBranch(firstColumn.viewportX + view.indentation() / 2,
                            view.visualRect(lastRoot).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, lastBranch);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(5));
    verifyFrozenRow(lastChild, 4, ItemPane::Type::FrozenBottom);

    view.collapse(lastRoot);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    verifyFrozenRow(lastRoot, 3, ItemPane::Type::FrozenBottom);
}

void TestTreeTableViewInteraction::rowStripDragRejectsCrossParentAndRestoresPreview()
{
    RejectingMoveModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child 0")));
    parent.first()->appendRow(row(QStringLiteral("child 1")));
    parent.first()->appendRow(row(QStringLiteral("child 2")));
    model.appendRow(parent);
    model.appendRow(row(QStringLiteral("other parent")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setVerticalHeaderDragEnabled(true);
    const QModelIndex firstParent = model.index(0, 0);
    view.expand(firstParent);
    view.flushPendingRelayout();
    QSignalSpy requestSpy(&view, &VirtualTableView::rowMoveRequested);
    QVERIFY(requestSpy.isValid());
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    QWidget *header = strip->headerWidget();

    const auto dragRow = [&](int from, int to) {
        const int fromY = view.visualRect(view.visibilityIndex()->indexAtVisibleRow(from)).center().y();
        const int toY = view.visualRect(view.visibilityIndex()->indexAtVisibleRow(to)).center().y();
        sendHeaderMouse(header, QEvent::MouseButtonPress, QPoint(4, fromY),
                        Qt::LeftButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, fromY + 2),
                        Qt::NoButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, toY + 10),
                        Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
        sendHeaderMouse(header, QEvent::MouseButtonRelease, QPoint(4, toY + 10),
                        Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    };

    dragRow(1, 3);
    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(model.moveCalls, 1);
    QCOMPARE(model.lastSourceParent, QPersistentModelIndex(firstParent));
    QCOMPARE(model.lastDestinationParent, QPersistentModelIndex(firstParent));
    QCOMPARE(model.lastSourceRow, 0);
    QCOMPARE(model.lastCount, 1);
    QCOMPARE(model.lastDestinationChild, 3);
    QCOMPARE(model.index(0, 0, firstParent).data().toString(), QStringLiteral("child 0"));

    dragRow(3, 4);
    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(model.moveCalls, 1);
    for (int rowNumber : {1, 2, 3, 4}) {
        QWidget *section = strip->sectionWidget(rowNumber);
        QVERIFY(section);
        const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(rowNumber);
        QCOMPARE(strip->geometry().y() + section->y(),
                 view.viewport()->geometry().y() + view.visualRect(node).top());
    }
}

void TestTreeTableViewInteraction::rowStripDragMovesSiblingIdentity()
{
    SiblingMoveModel model;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new RowAdapter, true);
    view.setModel(&model);
    view.setVerticalHeaderDragEnabled(true);
    showView(&view, QSize(440, 280));
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    view.flushPendingRelayout();
    const QPersistentModelIndex moving(model.index(0, 0, parent));
    const QPersistentModelIndex currentCell(model.index(0, 1, parent));
    view.setCurrentIndex(currentCell);
    QSignalSpy requestSpy(&view, &VirtualTableView::rowMoveRequested);
    QSignalSpy movedSpy(&model, &QAbstractItemModel::rowsMoved);
    QVERIFY(requestSpy.isValid());
    QVERIFY(movedSpy.isValid());
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    QWidget *header = strip->headerWidget();
    const int fromY = view.visualRect(moving).center().y();
    const int toY = view.visualRect(model.index(2, 0, parent)).center().y() + 10;
    sendHeaderMouse(header, QEvent::MouseButtonPress, QPoint(4, fromY),
                    Qt::LeftButton, Qt::LeftButton);
    sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, fromY + 2),
                    Qt::NoButton, Qt::LeftButton);
    sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, toY),
                    Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    sendHeaderMouse(header, QEvent::MouseButtonRelease, QPoint(4, toY),
                    Qt::LeftButton, Qt::NoButton);
    view.flushPendingRelayout();
    QApplication::processEvents();

    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(model.moveCalls, 1);
    QCOMPARE(moving.row(), 2);
    QCOMPARE(currentCell.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(currentCell));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(3), QModelIndex(moving));
    QCOMPARE(model.index(0, 0, parent).data().toString(), QStringLiteral("child 1"));
    QCOMPARE(model.index(1, 0, parent).data().toString(), QStringLiteral("child 2"));
    QWidget *section = strip->sectionWidget(3);
    QVERIFY(section);
    QCOMPARE(strip->geometry().y() + section->y(),
             view.viewport()->geometry().y() + view.visualRect(moving).top());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(currentCell).center()), QModelIndex(currentCell));
}

void TestTreeTableViewInteraction::proxySortKeepsNodeStateAndRestoresHeaderLayout()
{
    QStandardItemModel source;
    source.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                      QStringLiteral("State")});
    auto parent = row(QStringLiteral("Zulu"));
    parent.first()->appendRow(row(QStringLiteral("nested")));
    source.appendRow(parent);
    source.appendRow(row(QStringLiteral("Alpha")));
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setDynamicSortFilter(true);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new RowAdapter, true);
    view.setModel(&proxy);
    showView(&view, QSize(440, 280));
    const QPersistentModelIndex zulu(proxy.mapFromSource(source.index(0, 0)));
    const QPersistentModelIndex child(proxy.index(0, 1, zulu));
    view.expand(zulu);
    view.setCurrentIndex(child);
    view.selectionModel()->select(child, QItemSelectionModel::ClearAndSelect);
    QVERIFY(view.isExpanded(zulu));
    QVERIFY(view.selectionModel()->isSelected(child));

    view.sortByColumn(0, Qt::AscendingOrder);
    view.flushPendingRelayout();
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("Alpha"));
    QCOMPARE(zulu.row(), 1);
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(zulu), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(child), qsizetype(2));
    QCOMPARE(view.currentIndex(), QModelIndex(child));
    QVERIFY(view.selectionModel()->isSelected(child));
    QVERIFY(view.isExpanded(zulu));

    view.setColumnWidth(1, 135);
    view.horizontalHeaderGeometry()->moveSection(0, 2);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({2});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.flushPendingRelayout();
    const QByteArray state = view.saveHeaderState();
    QVERIFY(!state.isEmpty());

    VirtualTreeTableView restored;
    restored.setUniformItemHeight(28);
    restored.setDefaultColumnWidth(100);
    restored.setTableAdapter(new RowAdapter, true);
    restored.setModel(&proxy);
    showView(&restored, QSize(440, 280));
    QSignalSpy sortSpy(&restored, &VirtualTableView::sortIndicatorRequested);
    QVERIFY(sortSpy.isValid());
    QVERIFY(restored.restoreHeaderState(state));
    QCOMPARE(sortSpy.count(), 0);
    QCOMPARE(restored.columnWidth(1), 135);
    QCOMPARE(restored.horizontalHeaderGeometry()->visualIndex(0), 2);
    QCOMPARE(restored.horizontalHeaderGeometry()->sortIndicatorSection(), 0);
    QCOMPARE(restored.horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::AscendingOrder);
    QCOMPARE(restored.frozenColumns(), QVector<int>({0}));
    QCOMPARE(restored.frozenRightColumns(), QVector<int>({2}));
    QCOMPARE(restored.frozenRows(), 1);
    QCOMPARE(restored.frozenBottomRows(), 1);
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("Alpha"));
}

void TestTreeTableViewInteraction::filteringDoesNotTransferNodeStateToAnotherRow()
{
    QStandardItemModel source;
    auto first = row(QStringLiteral("first"));
    first.first()->appendRow(row(QStringLiteral("first child")));
    source.appendRow(first);
    auto second = row(QStringLiteral("second"));
    second.first()->appendRow(row(QStringLiteral("second child")));
    source.appendRow(second);

    RootVisibilityProxy proxy;
    proxy.setSourceModel(&source);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setTableAdapter(new RowAdapter, true);
    view.setModel(&proxy);
    showView(&view, QSize(440, 280));

    const QPersistentModelIndex firstNode(proxy.mapFromSource(source.index(0, 0)));
    const QPersistentModelIndex firstChild(proxy.index(0, 1, firstNode));
    const QPersistentModelIndex secondNode(proxy.mapFromSource(source.index(1, 0)));
    view.expand(firstNode);
    view.setCurrentIndex(firstChild);
    view.setItemPinned(firstChild);
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QVERIFY(view.isExpanded(firstNode));
    QVERIFY(view.isItemPinned(firstChild));

    proxy.setHideFirstRoot(true);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0), QModelIndex(secondNode));
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(!view.isItemPinned(firstChild));
    QVERIFY(!view.isExpanded(secondNode));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(secondNode).center()), QModelIndex(secondNode));

    proxy.setHideFirstRoot(false);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0),
             proxy.mapFromSource(source.index(0, 0)));
    QVERIFY(!view.isExpanded(proxy.mapFromSource(source.index(0, 0))));
    QVERIFY(!view.currentIndex().isValid());
}

void TestTreeTableViewInteraction::modelResetClearsRootCellAndNodeState()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto root = row(QStringLiteral("root"));
    auto branch = row(QStringLiteral("branch"));
    branch.first()->appendRow(row(QStringLiteral("old leaf")));
    root.first()->appendRow(branch);
    model.appendRow(root);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    const QModelIndex rootIndex = model.index(0, 0);
    const QModelIndex branchIndex = model.index(0, 0, rootIndex);
    const QPersistentModelIndex oldCell(model.index(0, 1, branchIndex));
    view.setRootIndex(rootIndex);
    view.expand(branchIndex);
    view.setCurrentIndex(oldCell);
    view.setItemPinned(oldCell);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(view.cellWidget(oldCell));
    QVERIFY(view.isItemPinned(oldCell));
    QVERIFY(view.selectionModel()->isSelected(oldCell));

    model.clear();
    view.flushPendingRelayout();
    QVERIFY(!oldCell.isValid());
    QVERIFY(!view.rootIndex().isValid());
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
    QCOMPARE(view.columnCount(), 0);
    QCOMPARE(view.materializedCellCount(), qsizetype(0));
    QCOMPARE(view.visibilityIndex()->expandedCount(), qsizetype(0));

    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    model.appendRow(row(QStringLiteral("new root")));
    view.flushPendingRelayout();
    const QModelIndex freshCell = model.index(0, 1);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0), model.index(0, 0));
    QCOMPARE(view.columnCount(), 3);
    QVERIFY(view.cellWidget(freshCell));
    QVERIFY(!view.isItemPinned(freshCell));
    QVERIFY(!view.selectionModel()->isSelected(freshCell));
    QVERIFY(!view.currentIndex().isValid());
}

void TestTreeTableViewInteraction::parentSpecificCellWidgetsRespectMissingColumnsAndFolding()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto narrow = row(QStringLiteral("narrow"));
    narrow.first()->appendRow(new QStandardItem(QStringLiteral("narrow child")));
    model.appendRow(narrow);
    auto wide = row(QStringLiteral("wide"));
    wide.first()->appendRow(row(QStringLiteral("wide child")));
    model.appendRow(wide);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(160);
    view.setColumnOverscan(0);
    view.setCellAdapter(new ParentTypedCellAdapter, true);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&model);
    showView(&view, QSize(320, 260));
    const QModelIndex narrowRoot = model.index(0, 0);
    const QModelIndex wideRoot = model.index(1, 0);
    const QModelIndex narrowChild = model.index(0, 0, narrowRoot);
    const QModelIndex wideCell = model.index(0, 2, wideRoot);
    view.expand(narrowRoot);
    view.expand(wideRoot);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    QVERIFY(!model.index(0, 2, narrowRoot).isValid());
    QVERIFY(!view.cellWidget(wideCell));

    view.setHorizontalOffset(220);
    view.flushPendingRelayout();
    QVERIFY(view.horizontalOffset() > 0);
    auto *progress = qobject_cast<QProgressBar *>(view.cellWidget(wideCell));
    QVERIFY(progress);
    QCOMPARE(progress->format(), QStringLiteral("wide child state"));
    const QPoint absentCellPoint(view.cellRect(wideCell).center().x(),
                                 view.visualRect(narrowChild).center().y());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(absentCellPoint), QModelIndex());

    view.collapse(wideRoot);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QVERIFY(!view.cellWidget(wideCell));
    view.expand(wideRoot);
    view.flushPendingRelayout();
    QVERIFY(qobject_cast<QProgressBar *>(view.cellWidget(wideCell)));
}

void TestTreeTableViewInteraction::parentSpecificRowHostsRespectMissingColumnsAndFolding()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto narrow = row(QStringLiteral("narrow"));
    narrow.first()->appendRow(new QStandardItem(QStringLiteral("narrow child")));
    model.appendRow(narrow);
    auto wide = row(QStringLiteral("wide"));
    wide.first()->appendRow(row(QStringLiteral("wide child")));
    model.appendRow(wide);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(160);
    view.setTableAdapter(new ParentTypedRowAdapter, true);
    view.setModel(&model);
    showView(&view, QSize(320, 260));
    const QModelIndex narrowRoot = model.index(0, 0);
    const QModelIndex wideRoot = model.index(1, 0);
    const QModelIndex narrowChild = model.index(0, 0, narrowRoot);
    const QModelIndex wideChild = model.index(0, 0, wideRoot);
    const QModelIndex wideCell = model.index(0, 2, wideRoot);
    view.expand(narrowRoot);
    view.expand(wideRoot);
    view.flushPendingRelayout();
    auto *narrowRow = static_cast<HostedRow *>(view.widgetForIndex(narrowChild));
    auto *wideRow = static_cast<HostedRow *>(view.widgetForIndex(wideChild));
    QVERIFY(narrowRow);
    QVERIFY(wideRow);
    QCOMPARE(narrowRow->property("rowType").toInt(), 0);
    QCOMPARE(wideRow->property("rowType").toInt(), 1);
    QVERIFY(narrowRow->host(2)->isHidden());
    QVERIFY(wideRow->host(2)->isHidden());
    QVERIFY(narrowRow->host(0)->mapTo(view.viewport(), QPoint()).x()
            > view.cellRect(narrowChild).x());

    view.setHorizontalOffset(220);
    view.flushPendingRelayout();
    QVERIFY(view.horizontalOffset() > 0);
    QVERIFY(narrowRow->host(2)->isHidden());
    QVERIFY(wideRow->host(2)->isVisible());
    QCOMPARE(wideRow->host(2)->mapTo(view.viewport(), QPoint()).x(),
             view.cellRect(wideCell).x());

    view.collapse(wideRoot);
    view.flushPendingRelayout();
    QVERIFY(!view.widgetForIndex(wideChild));
    view.expand(wideRoot);
    view.flushPendingRelayout();
    auto *rebound = static_cast<HostedRow *>(view.widgetForIndex(wideChild));
    QVERIFY(rebound);
    QCOMPARE(rebound->property("rowType").toInt(), 1);
    QVERIFY(rebound->host(2)->isVisible());
}

void TestTreeTableViewInteraction::rowHostsStayClippedInSeparateScrollGroups()
{
    QStandardItemModel model;
    QList<QStandardItem *> rootRow;
    QList<QStandardItem *> childRow;
    for (int column = 0; column < 10; ++column) {
        rootRow.append(new QStandardItem(QStringLiteral("root %1").arg(column)));
        childRow.append(new QStandardItem(QStringLiteral("child %1").arg(column)));
    }
    rootRow.first()->appendRow(childRow);
    model.appendRow(rootRow);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new ParentTypedRowAdapter(10), true);
    view.setModel(&model);
    showView(&view, QSize(900, 260));
    const auto pane = [](const QVector<int> &columns, PaneScroll scroll, int group) {
        TablePaneSpec spec;
        spec.logicalColumns = columns;
        spec.scroll = scroll;
        spec.scrollGroup = group;
        return spec;
    };
    view.setPanes({pane({0}, PaneScroll::Frozen, 0),
                   pane({1, 2, 3}, PaneScroll::Scrollable, 0),
                   pane({4}, PaneScroll::Frozen, 0),
                   pane({5, 6, 7, 8, 9}, PaneScroll::Scrollable, 1)});
    const QModelIndex root = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, root);
    view.expand(root);
    view.flushPendingRelayout();
    QCOMPARE(view.panes().size(), 4);
    QCOMPARE(view.scrollGroups(), QVector<int>({0, 1}));
    QVERIFY(view.maximumHorizontalOffset(1) > 0);

    auto *rowWidget = static_cast<HostedRow *>(view.widgetForIndex(child));
    QVERIFY(rowWidget);
    const QList<QWidget *> clipHosts = rowWidget->findChildren<QWidget *>(
        QStringLiteral("vivPaneClipHost"), Qt::FindDirectChildrenOnly);
    QCOMPARE(clipHosts.size(), 2);
    QVERIFY(rowWidget->host(1)->parentWidget() != rowWidget->host(5)->parentWidget());
    QCOMPARE(rowWidget->host(4)->parentWidget(), static_cast<QWidget *>(rowWidget));
    const int firstGroupX = rowWidget->host(1)->mapTo(view.viewport(), QPoint()).x();
    const int frozenX = rowWidget->host(4)->mapTo(view.viewport(), QPoint()).x();
    const int secondGroupX = rowWidget->host(5)->mapTo(view.viewport(), QPoint()).x();
    QCOMPARE(firstGroupX, view.cellRect(model.index(0, 1, root)).x());
    QCOMPARE(secondGroupX, view.cellRect(model.index(0, 5, root)).x());

    const qint64 step = qMin<qint64>(40, view.maximumHorizontalOffset(1));
    view.setHorizontalOffset(1, step);
    view.flushPendingRelayout();
    QCOMPARE(view.horizontalOffset(1), step);
    QCOMPARE(view.horizontalOffset(0), qint64(0));
    QCOMPARE(rowWidget->host(1)->mapTo(view.viewport(), QPoint()).x(), firstGroupX);
    QCOMPARE(rowWidget->host(4)->mapTo(view.viewport(), QPoint()).x(), frozenX);
    QCOMPARE(rowWidget->host(5)->mapTo(view.viewport(), QPoint()).x(),
             secondGroupX - int(step));
    QCOMPARE(rowWidget->host(5)->mapTo(view.viewport(), QPoint()).x(),
             view.cellRect(model.index(0, 5, root)).x());

    rowWidget->host(4)->hide();
    const int y = view.visualRect(child).center().y();
    QVERIFY(rowWidget->host(5)->isVisible());
    const QRect hostRect(rowWidget->host(5)->mapTo(view.viewport(), QPoint()),
                         rowWidget->host(5)->size());
    const QRect overFrozen = hostRect.intersected(view.panes().at(2).viewportRect);
    const QRect inScrollPane = hostRect.intersected(view.panes().at(3).viewportRect);
    QVERIFY(!overFrozen.isEmpty());
    QVERIFY(!inScrollPane.isEmpty());
    const QPoint frozenSample(overFrozen.center().x(), y);
    const QPoint scrollingSample(inScrollPane.center().x(), y);
    const QImage baseline = view.viewport()->grab().toImage();
    const QColor red(233, 40, 60);
    rowWidget->host(5)->setFillColor(red);
    const QImage painted = view.viewport()->grab().toImage();
    QCOMPARE(painted.pixelColor(scrollingSample), red);
    QCOMPARE(painted.pixelColor(frozenSample), baseline.pixelColor(frozenSample));
}

void TestTreeTableViewInteraction::depthSpacingKeepsRowHeadersAndHitTestsAligned()
{
    QStandardItemModel model;
    auto rootRow = row(QStringLiteral("root"));
    auto childRow = row(QStringLiteral("child"));
    childRow.first()->appendRow(row(QStringLiteral("grandchild")));
    rootRow.first()->appendRow(childRow);
    model.appendRow(rootRow);
    model.appendRow(row(QStringLiteral("sibling")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(4);
    view.setDepthRowSpacing(0, 8);
    view.setDepthRowSpacing(1, 13);
    view.setDepthRowSpacing(2, 21);
    const QModelIndex root = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, root);
    const QModelIndex grandchild = model.index(0, 0, child);
    const QModelIndex sibling = model.index(1, 0);
    view.expand(root);
    view.expand(child);
    view.flushPendingRelayout();

    auto *header = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(header);
    const auto verify = [&](const QVector<QModelIndex> &nodes,
                            const QVector<int> &gaps) {
        QCOMPARE(view.visibleRowCount(), qsizetype(nodes.size()));
        QCOMPARE(view.verticalHeaderGeometry()->sectionCount(), nodes.size());
        for (int rowNumber = 0; rowNumber < nodes.size(); ++rowNumber) {
            const QRect body = view.visualRect(nodes.at(rowNumber));
            QVERIFY(body.isValid());
            QWidget *section = header->sectionWidget(rowNumber);
            QVERIFY(section);
            QCOMPARE(header->geometry().y() + section->y(),
                     view.viewport()->geometry().y() + body.y());
            QCOMPARE(section->height(), body.height());
            if (rowNumber + 1 < nodes.size()) {
                const int gap = gaps.at(rowNumber);
                QCOMPARE(view.verticalHeaderGeometry()->sectionSpacingAfter(rowNumber), gap);
                QCOMPARE(view.visualRect(nodes.at(rowNumber + 1)).y() - body.bottom() - 1, gap);
                if (gap > 0) {
                    const QPoint gapPoint(view.cellRect(nodes.at(rowNumber)).center().x(),
                                          body.bottom() + 1);
                    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(gapPoint),
                             QModelIndex());
                }
            }
        }
    };
    verify({root, child, grandchild, sibling}, {8, 13, 21});

    view.setDepthRowSpacing(1, 0);
    view.flushPendingRelayout();
    verify({root, child, grandchild, sibling}, {8, 0, 21});

    view.collapse(root);
    view.flushPendingRelayout();
    verify({root, sibling}, {8});
}

void TestTreeTableViewInteraction::customSpacingWidgetsFollowVisibleNodesAndHeaders()
{
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentRow);
    model.appendRow(row(QStringLiteral("sibling")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(8);
    view.setDepthRowSpacing(1, 12);
    view.setColumnSpacing(9);
    view.setRowSpacingFactory([](const QModelIndex &, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("rowGapContent"));
        return label;
    }, [](QWidget *widget, const QModelIndex &index) {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    });
    view.setColumnSpacingFactory([](int column, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("columnGapContent"));
        label->setProperty("logicalColumn", column);
        return label;
    });
    view.setHeaderColumnSpacingFactory([](int column, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("headerGapContent"));
        label->setProperty("logicalColumn", column);
        return label;
    });
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    view.flushPendingRelayout();

    const auto rowGaps = view.findChildren<QLabel *>(QStringLiteral("rowGapContent"));
    bool foundParent = false;
    bool foundChild = false;
    for (QLabel *label : rowGaps) {
        if (!label->isVisible())
            continue;
        const QModelIndex node = label->text() == QStringLiteral("parent") ? parent : child;
        if (label->text() != QStringLiteral("parent")
            && label->text() != QStringLiteral("child"))
            continue;
        const QRect body = view.visualRect(node);
        const QPoint gapTop = view.viewport()->mapFromGlobal(
            label->parentWidget()->mapToGlobal(QPoint()));
        QCOMPARE(gapTop.y(), body.bottom() + 1);
        QCOMPARE(label->parentWidget()->height(),
                 node == parent ? 8 : 12);
        foundParent |= node == parent;
        foundChild |= node == child;
    }
    QVERIFY(foundParent);
    QVERIFY(foundChild);

    for (int column = 0; column < 2; ++column) {
        QLabel *bodyGap = nullptr;
        QLabel *headerGap = nullptr;
        for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("columnGapContent"))) {
            if (label->isVisible() && label->property("logicalColumn").toInt() == column)
                bodyGap = label;
        }
        for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("headerGapContent"))) {
            if (label->isVisible() && label->property("logicalColumn").toInt() == column)
                headerGap = label;
        }
        QVERIFY(bodyGap);
        QVERIFY(headerGap);
        const int end = view.columnGeometry(column).viewportX + view.columnWidth(column);
        const QRect bodyRect(view.viewport()->mapFromGlobal(bodyGap->mapToGlobal(QPoint())),
                             bodyGap->size());
        const QRect headerRect(view.viewport()->mapFromGlobal(headerGap->mapToGlobal(QPoint())),
                               headerGap->size());
        QVERIFY(bodyRect.left() >= end - view.verticalGridLineWidth());
        QVERIFY(bodyRect.right() < end + view.columnSpacing());
        QVERIFY(bodyRect.top() >= 0);
        QVERIFY(headerRect.bottom() < 0);
    }

    view.collapse(parent);
    view.flushPendingRelayout();
    for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("rowGapContent")))
        QVERIFY(!label->isVisible() || label->text() != QStringLiteral("child"));
}

void TestTreeTableViewInteraction::wideTreeMaterializesOnlyTheWindow()
{
    QStandardItemModel model;
    for (int rowNumber = 0; rowNumber < 600; ++rowNumber) {
        QList<QStandardItem *> items;
        for (int column = 0; column < 12; ++column)
            items.append(new QStandardItem(QStringLiteral("%1:%2").arg(rowNumber).arg(column)));
        if (rowNumber == 0) {
            for (int child = 0; child < 300; ++child) {
                QList<QStandardItem *> children;
                for (int column = 0; column < 12; ++column)
                    children.append(new QStandardItem(QStringLiteral("child %1:%2").arg(child).arg(column)));
                items.first()->appendRow(children);
            }
        }
        model.appendRow(items);
    }

    VirtualTreeTableView view;
    configure(&view, &model, true);
    QCOMPARE(view.visibleRowCount(), qsizetype(600));
    QVERIFY(view.materializedCellCount() > 0);
    QVERIFY(view.materializedCellCount() < 300);

    view.expand(model.index(0, 0));
    view.setVerticalOffset(2000);
    view.setHorizontalOffset(400);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(900));
    QVERIFY(view.materializedCellCount() > 0);
    QVERIFY(view.materializedCellCount() < 300);
}

void TestTreeTableViewInteraction::accessibilityExposesHierarchyAndCells()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();

    QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(interface);
    QCOMPARE(interface->role(), QAccessible::Tree);
    auto *table = static_cast<QAccessibleTableInterface *>(
        interface->interface_cast(QAccessible::TableInterface));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->columnCount(), 3);
    QAccessibleInterface *childCell = table->cellAt(1, 1);
    QVERIFY(childCell);
    QCOMPARE(childCell->text(QAccessible::Name), QStringLiteral("child type"));
    QAccessibleInterface *parentNode = interface->child(0);
    QVERIFY(parentNode);
    QCOMPARE(parentNode->role(), QAccessible::TreeItem);
    QVERIFY(parentNode->childCount() > 0);
}

void TestTreeTableViewInteraction::selectedBackgroundExtentKeepsGridLineVisible()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("first")));
    parent.first()->appendRow(row(QStringLiteral("second")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setVisualStateAnimationDuration(0);
    view.setVisualStateBackgroundVisible(true);
    view.setSelectedBackgroundColor(QColor(230, 30, 70));
    view.setHorizontalGridLineColor(QColor(15, 125, 40));
    view.setVerticalGridLinesVisible(false);
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();
    const QModelIndex child = model.index(0, 0, model.index(0, 0));
    const QRect rowRect = view.visualRect(child);
    QVERIFY(rowRect.height() > 2);
    const int y = rowRect.center().y();
    const QImage baseline = view.viewport()->grab().toImage();

    view.setCurrentIndex(child);
    QVERIFY(view.visualState(child).selected);
    const QColor selected(230, 30, 70);
    const QColor line(15, 125, 40);
    using Extent = VirtualTreeTableView::VisualStateBackgroundExtent;
    for (const Extent extent : {Extent::NodeOnly, Extent::NodeAndIcon, Extent::FullWidth}) {
        view.setVisualStateBackgroundExtent(extent);
        const QImage image = view.viewport()->grab().toImage();
        QCOMPARE(image.pixelColor(45, y), selected);
        QCOMPARE(image.pixelColor(25, y), extent == Extent::NodeOnly
                     ? baseline.pixelColor(25, y) : selected);
        QCOMPARE(image.pixelColor(5, y), extent == Extent::FullWidth
                     ? selected : baseline.pixelColor(5, y));
        const QImage full = view.grab().toImage();
        QCOMPARE(full.pixelColor(view.viewport()->geometry().topLeft()
                                     + QPoint(70, rowRect.bottom())), line);
    }
}

void TestTreeTableViewInteraction::rootSchemaAndCrossParentSelection()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto firstRoot = row(QStringLiteral("first root"));
    firstRoot.first()->appendRow({new QStandardItem(QStringLiteral("narrow")),
                                  new QStandardItem(QStringLiteral("type"))});
    model.appendRow(firstRoot);
    auto secondRoot = row(QStringLiteral("second root"));
    secondRoot.first()->appendRow(row(QStringLiteral("wide")));
    model.appendRow(secondRoot);

    InspectTreeTableView view;
    configure(&view, &model, true);
    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    const QModelIndex narrow = model.index(0, 0, first);
    const QModelIndex wide = model.index(0, 0, second);
    view.expand(first);
    view.expand(second);
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    const QList<QModelIndex> cells = view.selectionRange(narrow.siblingAtColumn(1),
                                                         wide.siblingAtColumn(2)).indexes();
    QCOMPARE(cells.size(), 5);
    QVERIFY(cells.contains(narrow.siblingAtColumn(1)));
    QVERIFY(cells.contains(second.siblingAtColumn(2)));
    QVERIFY(cells.contains(wide.siblingAtColumn(2)));

    view.setRootIndex(first);
    view.flushPendingRelayout();
    QCOMPARE(view.columnCount(), 2);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    view.setColumnWidth(1, 135);
    const QByteArray narrowState = view.saveHeaderState();
    view.setRootIndex(QModelIndex());
    view.flushPendingRelayout();
    QCOMPARE(view.columnCount(), 3);
    QVERIFY(!view.restoreHeaderState(narrowState));
    view.setRootIndex(first);
    view.setColumnWidth(1, 90);
    QVERIFY(view.restoreHeaderState(narrowState));
    QCOMPARE(view.columnWidth(1), 135);
}

void TestTreeTableViewInteraction::dropTargetsFollowVisibleTreeAndSpacing()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto firstRoot = row(QStringLiteral("first root"));
    firstRoot.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(firstRoot);
    model.appendRow(row(QStringLiteral("second root")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(8);
    const QModelIndex root = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, root);
    view.expand(root);
    view.flushPendingRelayout();
    const QRect rootRect = view.visualRect(root);
    const QRect childRect = view.visualRect(child);
    const auto beforeRoot = view.dropTargetAt(QPoint(50, rootRect.top() + 1));
    QVERIFY(!beforeRoot.parent.isValid());
    QCOMPARE(beforeRoot.row, 0);
    QVERIFY(!beforeRoot.ontoItem);

    const auto intoRoot = view.dropTargetAt(QPoint(50, rootRect.center().y()));
    QCOMPARE(intoRoot.parent, root);
    QCOMPARE(intoRoot.row, 1);
    QVERIFY(intoRoot.ontoItem);
    QVERIFY(!view.dropIndicatorRect(intoRoot).isEmpty());

    const auto beforeChild = view.dropTargetAt(QPoint(50, rootRect.bottom() + 2));
    QCOMPARE(beforeChild.parent, root);
    QCOMPARE(beforeChild.row, 0);
    QVERIFY(!beforeChild.ontoItem);
    const auto afterChild = view.dropTargetAt(QPoint(50, childRect.bottom() - 1));
    QCOMPARE(afterChild.parent, root);
    QCOMPARE(afterChild.row, 1);
    QVERIFY(!afterChild.ontoItem);

    const QRect last = view.visualRect(model.index(1, 0));
    const auto trailing = view.dropTargetAt(QPoint(50, last.bottom() + 5));
    QVERIFY(!trailing.parent.isValid());
    QCOMPARE(trailing.row, 2);
    QVERIFY(trailing.trailing);
}

void TestTreeTableViewInteraction::modelReceivesDropAndCanRejectIt()
{
    RecordingDropModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto firstRoot = row(QStringLiteral("first root"));
    firstRoot.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(firstRoot);
    model.appendRow(row(QStringLiteral("second root")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setDragEnabled(true);
    const QModelIndex root = model.index(0, 0);
    view.expand(root);
    view.flushPendingRelayout();
    int droppedSignals = 0;
    QObject::connect(&view, &VirtualItemView::itemDropped, &view,
                     [&](const QModelIndex &, int, int, Qt::DropAction) { ++droppedSignals; });
    QMimeData data;
    data.setData(model.mimeTypes().first(), QByteArrayLiteral("new child"));

    const QPoint intoRoot(50, view.visualRect(root).center().y());
    QVERIFY(sendDrop(&view, intoRoot, &data, Qt::CopyAction));
    QCOMPARE(model.dropCalls, 1);
    QCOMPARE(model.lastParent, QPersistentModelIndex(root));
    QCOMPARE(model.lastRow, 1);
    QCOMPARE(model.lastColumn, -1);
    QCOMPARE(model.lastAction, Qt::CopyAction);
    QCOMPARE(droppedSignals, 1);
    QCOMPARE(model.rowCount(root), 2);
    QCOMPARE(view.visibleRowCount(), qsizetype(4));

    model.acceptDrop = false;
    const QPoint intoSecond(50, view.visualRect(model.index(1, 0)).center().y());
    QVERIFY(!sendDrop(&view, intoSecond, &data, Qt::MoveAction));
    QCOMPARE(model.dropCalls, 2);
    QCOMPARE(model.lastParent, QPersistentModelIndex(model.index(1, 0)));
    QCOMPARE(model.lastAction, Qt::MoveAction);
    QCOMPARE(droppedSignals, 1);
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
}

void TestTreeTableViewInteraction::modelSwitchDuringDropDoesNotSubmitStaleTarget()
{
    RecordingDropModel oldModel;
    oldModel.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                        QStringLiteral("State")});
    oldModel.appendRow(row(QStringLiteral("old parent")));
    RecordingDropModel newModel;
    newModel.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                        QStringLiteral("State")});
    newModel.appendRow(row(QStringLiteral("new parent")));

    VirtualTreeTableView view;
    configure(&view, &oldModel, false);
    view.setDragEnabled(true);
    QMimeData data;
    data.setData(oldModel.mimeTypes().first(), QByteArrayLiteral("child"));
    const QPoint target(50, view.visualRect(oldModel.index(0, 0)).center().y());
    int droppedSignals = 0;
    QObject::connect(&view, &VirtualItemView::itemDropped, &view,
                     [&](const QModelIndex &, int, int, Qt::DropAction) { ++droppedSignals; });

    QDragEnterEvent firstEnter(target, Qt::CopyAction, &data, Qt::LeftButton,
                               Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &firstEnter);
    QVERIFY(firstEnter.isAccepted());
    oldModel.onCanDrop = [&] { view.setModel(&newModel); };
    QDragMoveEvent move(target, Qt::CopyAction, &data, Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(!move.isAccepted());
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&newModel));

    view.setModel(&oldModel);
    oldModel.onCanDrop = {};
    QDragEnterEvent secondEnter(target, Qt::CopyAction, &data, Qt::LeftButton,
                                Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &secondEnter);
    QVERIFY(secondEnter.isAccepted());
    QDragMoveEvent firstMove(target, Qt::CopyAction, &data, Qt::LeftButton,
                             Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &firstMove);
    QVERIFY(firstMove.isAccepted());
    oldModel.onCanDrop = [&] { view.setModel(&newModel); };
    QDropEvent firstDrop(QPointF(target), Qt::CopyAction, &data, Qt::LeftButton,
                         Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &firstDrop);
    QVERIFY(!firstDrop.isAccepted());
    QCOMPARE(oldModel.dropCalls, 0);
    QCOMPARE(newModel.dropCalls, 0);
    QCOMPARE(droppedSignals, 0);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&newModel));

    view.setModel(&oldModel);
    oldModel.onCanDrop = {};
    QDragEnterEvent thirdEnter(target, Qt::CopyAction, &data, Qt::LeftButton,
                               Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &thirdEnter);
    QVERIFY(thirdEnter.isAccepted());
    QDragMoveEvent secondMove(target, Qt::CopyAction, &data, Qt::LeftButton,
                              Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &secondMove);
    QVERIFY(secondMove.isAccepted());
    oldModel.onDrop = [&] { view.setModel(&newModel); };
    QDropEvent secondDrop(QPointF(target), Qt::CopyAction, &data, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &secondDrop);
    QVERIFY(!secondDrop.isAccepted());
    QCOMPARE(oldModel.dropCalls, 1);
    QCOMPARE(newModel.dropCalls, 0);
    QCOMPARE(droppedSignals, 0);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&newModel));
}

QTEST_MAIN(TestTreeTableViewInteraction)

#include "tst_treetableviewinteraction.moc"
