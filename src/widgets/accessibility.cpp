#include <virtualitemviews/accessibility.h>

#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualitemview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/virtualtreetableview.h>

#include <QAbstractItemModel>
#include <QAccessibleEvent>
#include <QItemSelectionModel>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <algorithm>
#include <climits>

namespace viv {


namespace {
/// Pixels one "scroll up/down/left/right" action moves the view.
constexpr int kScrollStepPixels = 32;

bool isTable(const VirtualItemView *view)
{
    return view && qobject_cast<const VirtualTableView *>(view) != nullptr;
}

bool isTree(const VirtualItemView *view)
{
    return view && (qobject_cast<const VirtualTreeView *>(view) != nullptr
                    || qobject_cast<const VirtualTreeTableView *>(view) != nullptr);
}

TreeVisibilityIndex *visibilityOf(const VirtualItemView *view)
{
    if (const auto *tree = qobject_cast<const VirtualTreeView *>(view))
        return tree->visibilityIndex();
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(view))
        return treeTable->visibilityIndex();
    return nullptr;
}

int depthOf(const VirtualItemView *view, const QModelIndex &index)
{
    if (const auto *tree = qobject_cast<const VirtualTreeView *>(view))
        return tree->itemDepth(index);
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(view))
        return treeTable->itemDepth(index);
    return 0;
}

qsizetype visibleTreeRows(const VirtualItemView *view)
{
    if (const auto *tree = qobject_cast<const VirtualTreeView *>(view))
        return tree->visibleRowCount();
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(view))
        return treeTable->visibleRowCount();
    return 0;
}

QRect rowPaneClip(const VirtualItemView *view, const QModelIndex &index)
{
    if (!view || !index.isValid())
        return QRect();
    const TreeVisibilityIndex *visibility = visibilityOf(view);
    const qsizetype row = visibility ? visibility->visibleRowForIndex(index) : index.row();
    if (row < 0)
        return QRect();
    for (const ItemPane &pane : view->itemPanes()) {
        if (pane.containsRow(row))
            return pane.viewportRect.intersected(view->viewport()->rect());
    }
    return QRect();
}

QModelIndex treeRoot(const VirtualItemView *view)
{
    if (const auto *tree = qobject_cast<const VirtualTreeView *>(view))
        return tree->rootIndex();
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(view))
        return treeTable->rootIndex();
    return QModelIndex();
}

QModelIndex topLevelTreeIndex(const VirtualItemView *view, const QModelIndex &index)
{
    QModelIndex top = index.siblingAtColumn(0);
    const QModelIndex root = treeRoot(view);
    while (top.isValid() && top.parent().isValid() && top.parent() != root)
        top = top.parent();
    return top;
}

bool treeHasChildren(const VirtualItemView *view, const QModelIndex &index)
{
    if (const auto *tree = qobject_cast<const VirtualTreeView *>(view))
        return tree->hasChildren(index);
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(view))
        return treeTable->hasChildren(index);
    return false;
}

bool treeIsExpanded(const VirtualItemView *view, const QModelIndex &index)
{
    if (const auto *tree = qobject_cast<const VirtualTreeView *>(view))
        return tree->isExpanded(index);
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(view))
        return treeTable->isExpanded(index);
    return false;
}

QAccessible::Role viewRole(const VirtualItemView *view)
{
    if (isTree(view))
        return QAccessible::Tree;
    if (isTable(view))
        return QAccessible::Table;
    return QAccessible::List;
}

/// Viewport rect -> screen rect (the coordinates QAccessibleInterface uses).
QRect toGlobal(const VirtualItemView *view, const QRect &viewportRect)
{
    if (!view || !viewportRect.isValid())
        return QRect();
    return viewportRect.translated(view->viewport()->mapToGlobal(QPoint(0, 0)));
}

/// Visible columns of a table row, left to right (frozen columns included).
QList<int> visibleColumnsOf(const VirtualTableView *table, int viewportWidth)
{
    QList<int> columns;
    if (!table)
        return columns;
    const QVector<TablePane> panes = table->panes();
    const QRect viewportClip(0, 0, viewportWidth, 1);
    const auto appendColumn = [&](int logical) {
        const ColumnGeometry geometry = table->columnGeometry(logical);
        if (!geometry.isValid() || geometry.hidden || geometry.width <= 0)
            return;
        QRect clip = viewportClip;
        if (!panes.isEmpty()) {
            const int paneIndex = table->paneIndexOfColumn(logical);
            if (paneIndex < 0 || paneIndex >= panes.size())
                return;
            clip = clip.intersected(panes.at(paneIndex).viewportRect);
        }
        if (!QRect(geometry.viewportX, 0, geometry.width, 1).intersects(clip))
            return;
        columns.append(logical);
    };
    if (panes.isEmpty()) {
        for (int logical = 0; logical < table->columnCount(); ++logical)
            appendColumn(logical);
    } else {
        for (int logical : table->visibleColumnLogicalIndexes())
            appendColumn(logical);
    }
    std::sort(columns.begin(), columns.end(), [table](int lhs, int rhs) {
        return table->columnGeometry(lhs).viewportX < table->columnGeometry(rhs).viewportX;
    });
    return columns;
}
} // namespace

namespace {
/// Bridges the runtime changes of a view to QAccessible events (§37).
///
/// QAccessibleInterface is not a QObject, so the connections live in this small
/// helper (lambdas only, no meta-object needed). It is created together with the
/// view interface, that is: the first time an assistive tool asks for it, and it
/// keeps its wiring in sync because the application may replace both the model
/// and the selection model.
class AccessibilityNotifier : public QObject
{
public:
    AccessibilityNotifier(VirtualItemView *view, AccessibleVirtualItemView *viewNode)
        : QObject(nullptr)
        , m_view(view)
        , m_viewNode(viewNode)
    {
        // Model and selection model can be replaced at any time; the
        // materialization pass of the view is the cheapest place to notice.
        connect(view, &VirtualItemView::virtualizationUpdated, this, [this]() { syncSources(); });
        connect(view, &VirtualItemView::selectionModelChanged, this,
                [this]() { syncSources(); });
        if (auto *treeTable = qobject_cast<VirtualTreeTableView *>(view)) {
            connect(treeTable, &VirtualTreeTableView::visibleRowsChanged, this, [this]() {
                sendModelChange(QAccessibleTableModelChangeEvent::ModelReset);
            });
        }
        syncSources();
    }

private:
    void syncSources();
    void sendEvent(QAccessible::Event type);
    void sendModelChange(QAccessibleTableModelChangeEvent::ModelChangeType type, int firstRow = -1,
                         int lastRow = -1);

    VirtualItemView *m_view = nullptr;
    AccessibleVirtualItemView *m_viewNode = nullptr;
    QPointer<QAbstractItemModel> m_model;
    QPointer<QItemSelectionModel> m_selection;
};

void AccessibilityNotifier::syncSources()
{
    QAbstractItemModel *model = m_view ? m_view->model() : nullptr;
    if (model != m_model) {
        if (m_model)
            m_model->disconnect(this);
        m_model = model;
        if (model) {
            connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
                if (!qobject_cast<VirtualTreeTableView *>(m_view))
                    sendModelChange(QAccessibleTableModelChangeEvent::ModelReset);
            });
            connect(model, &QAbstractItemModel::rowsInserted, this,
                    [this](const QModelIndex &, int first, int last) {
                        if (!qobject_cast<VirtualTreeTableView *>(m_view))
                            sendModelChange(QAccessibleTableModelChangeEvent::RowsInserted, first, last);
                    });
            connect(model, &QAbstractItemModel::rowsRemoved, this,
                    [this](const QModelIndex &, int first, int last) {
                        if (!qobject_cast<VirtualTreeTableView *>(m_view))
                            sendModelChange(QAccessibleTableModelChangeEvent::RowsRemoved, first, last);
                    });
            connect(model, &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                        if (TreeVisibilityIndex *visibility = visibilityOf(m_view)) {
                            const int first = int(visibility->visibleRowForIndex(topLeft));
                            const int last = int(visibility->visibleRowForIndex(bottomRight));
                            if (first >= 0 && last >= first)
                                sendModelChange(QAccessibleTableModelChangeEvent::DataChanged,
                                                first, last);
                        } else {
                            sendModelChange(QAccessibleTableModelChangeEvent::DataChanged,
                                            topLeft.row(), bottomRight.row());
                        }
                    });
        }
    }

    QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (selection != m_selection) {
        if (m_selection)
            m_selection->disconnect(this);
        m_selection = selection;
        if (selection) {
            connect(selection, &QItemSelectionModel::currentChanged, this, [this]() {
                sendEvent(QAccessible::Focus);
            });
            connect(selection, &QItemSelectionModel::selectionChanged, this, [this]() {
                sendEvent(QAccessible::Selection);
            });
        }
    }
    Q_UNUSED(m_viewNode);
}

void AccessibilityNotifier::sendEvent(QAccessible::Event type)
{
    if (!QAccessible::isActive() || !m_view)
        return;
    QAccessibleEvent event(m_view, type);
    QAccessible::updateAccessibility(&event);
}

void AccessibilityNotifier::sendModelChange(QAccessibleTableModelChangeEvent::ModelChangeType type,
                                            int firstRow, int lastRow)
{
    if (!QAccessible::isActive() || !m_view)
        return;
    QAccessibleTableModelChangeEvent event(m_view, type);
    if (firstRow >= 0)
        event.setFirstRow(firstRow);
    if (lastRow >= 0)
        event.setLastRow(lastRow);
    QAccessible::updateAccessibility(&event);
}
} // namespace

// ---------------------------------------------------------------------------
// AccessibleVirtualItem
// ---------------------------------------------------------------------------

AccessibleVirtualItem::AccessibleVirtualItem(VirtualItemView *view, const QModelIndex &index,
                                            AccessibleVirtualItemView *viewNode)
    : m_view(view)
    , m_index(index)
    , m_column(-1)
    , m_rowNode(nullptr)
    , m_viewNode(viewNode)
{
}

AccessibleVirtualItem::AccessibleVirtualItem(VirtualItemView *view, const QModelIndex &cellIndex,
                                            int column, AccessibleVirtualItem *rowNode,
                                            AccessibleVirtualItemView *viewNode)
    : m_view(view)
    , m_index(cellIndex)
    , m_column(column)
    , m_rowNode(rowNode)
    , m_viewNode(viewNode)
{
}

bool AccessibleVirtualItem::isValid() const
{
    return m_view != nullptr && m_index.isValid();
}

QObject *AccessibleVirtualItem::object() const
{
    return m_view;
}

QModelIndex AccessibleVirtualItem::rowModelIndex() const
{
    if (!m_index.isValid())
        return QModelIndex();
    return m_column >= 0 ? QModelIndex(m_index).siblingAtColumn(0) : QModelIndex(m_index);
}

QRect AccessibleVirtualItem::rect() const
{
    if (!isValid())
        return QRect();
    const QRect rowRect = m_view->visualRect(m_index);
    if (!rowRect.isValid())
        return QRect();
    QRect itemRect = rowRect;
    if (m_column >= 0) {
        if (const auto *table = qobject_cast<const VirtualTableView *>(m_view)) {
            // A merged cell is one node covering its whole merged rectangle
            // (§43 "spans"); a plain cell keeps its column rect.
            const QRect merged = table->spanRect(m_index);
            if (!merged.isEmpty()) {
                itemRect = merged;
            } else {
                const ColumnGeometry column = table->columnGeometry(m_column);
                if (!column.isValid() || column.hidden || column.width <= 0)
                    return QRect();
                itemRect = QRect(column.viewportX, rowRect.y(), column.width, rowRect.height());
            }
            const int paneIndex = table->paneIndexOfColumn(m_column);
            const QVector<TablePane> panes = table->panes();
            if (!panes.isEmpty() && (paneIndex < 0 || paneIndex >= panes.size()))
                return QRect();
            if (!panes.isEmpty())
                itemRect = itemRect.intersected(panes.at(paneIndex).viewportRect);
        }
    }
    return toGlobal(m_view, itemRect.intersected(rowPaneClip(m_view, m_index)));
}

QAccessible::Role AccessibleVirtualItem::role() const
{
    if (m_column >= 0)
        return QAccessible::Cell;
    if (isTree(m_view))
        return QAccessible::TreeItem;
    if (isTable(m_view))
        return QAccessible::Row;
    return QAccessible::ListItem;
}

QString AccessibleVirtualItem::text(QAccessible::Text t) const
{
    if (!isValid())
        return QString();
    switch (t) {
    case QAccessible::Name:
        return m_index.data(Qt::DisplayRole).toString();
    case QAccessible::Description: {
        const QString toolTip = m_index.data(Qt::ToolTipRole).toString();
        return toolTip.isEmpty() ? m_index.data(Qt::StatusTipRole).toString() : toolTip;
    }
    case QAccessible::Help:
        return m_index.data(Qt::StatusTipRole).toString();
    case QAccessible::Value:
        return m_index.data(Qt::EditRole).toString();
    default:
        return QString();
    }
}

void AccessibleVirtualItem::setText(QAccessible::Text, const QString &)
{
    // The data belongs to the model: an accessible node never writes back.
}

bool AccessibleVirtualItem::isInViewport() const
{
    return !rect().isEmpty();
}

QAccessible::State AccessibleVirtualItem::state() const
{
    QAccessible::State state;
    const QAbstractItemModel *model = m_view ? m_view->model() : nullptr;
    if (!isValid() || !model) {
        state.invalid = true;
        return state;
    }

    const Qt::ItemFlags flags = model->flags(m_index);
    state.disabled = !(flags & Qt::ItemIsEnabled);
    state.selectable = flags & Qt::ItemIsSelectable;
    const QItemSelectionModel *selection = m_view->selectionModel();
    state.selected = selection && selection->isSelected(m_index);
    const bool current = m_view->currentIndex() == m_index;
    state.active = current;
    state.focused = current && m_view->hasFocus();
    state.focusable = state.selectable || current;
    state.readOnly = true;

    bool hiddenColumn = false;
    if (m_column >= 0) {
        const auto *table = qobject_cast<const VirtualTableView *>(m_view);
        const ColumnGeometry geometry = table ? table->columnGeometry(m_column) : ColumnGeometry();
        hiddenColumn = !geometry.isValid() || geometry.hidden;
    }
    state.invisible = hiddenColumn;
    state.offscreen = !hiddenColumn && !isInViewport();

    if (isTree(m_view) && m_column < 0) {
        state.expandable = treeHasChildren(m_view, m_index);
        state.expanded = state.expandable && treeIsExpanded(m_view, m_index);
        state.collapsed = state.expandable && !treeIsExpanded(m_view, m_index);
    }
    return state;
}

QList<int> AccessibleVirtualItem::visibleColumns() const
{
    const auto *table = qobject_cast<const VirtualTableView *>(m_view);
    const QList<int> columns = visibleColumnsOf(table, m_view ? m_view->viewport()->width() : 0);
    if (!table || !m_index.isValid())
        return columns;
    // The columns a merged area covers do not have a cell of their own: the
    // anchor is the one and only accessible cell of that area.
    const QModelIndex row = QModelIndex(m_index).siblingAtColumn(0);
    QList<int> anchors;
    anchors.reserve(columns.size());
    for (int logical : columns) {
        const QModelIndex cell = row.siblingAtColumn(logical);
        if (!cell.isValid() || table->isSpanCovered(cell))
            continue;
        anchors.append(logical);
    }
    return anchors;
}

int AccessibleVirtualItem::treeChildCount() const
{
    TreeVisibilityIndex *visibility = visibilityOf(m_view);
    if (!visibility || !m_index.isValid() || !treeIsExpanded(m_view, m_index))
        return 0;
    return visibility->visibleRowForIndex(m_index) >= 0 && m_view->model()
        ? m_view->model()->rowCount(m_index) : 0;
}

QModelIndex AccessibleVirtualItem::treeChild(int position) const
{
    if (position < 0)
        return QModelIndex();
    TreeVisibilityIndex *visibility = visibilityOf(m_view);
    if (!visibility || !m_index.isValid() || !treeIsExpanded(m_view, m_index)
        || !m_view->model() || visibility->visibleRowForIndex(m_index) < 0)
        return QModelIndex();
    return m_view->model()->index(position, 0, m_index);
}

int AccessibleVirtualItem::childCount() const
{
    if (!isValid() || m_column >= 0)
        return 0;
    if (isTree(m_view))
        return treeChildCount() + (isTable(m_view) ? int(visibleColumns().size()) : 0);
    if (isTable(m_view))
        return int(visibleColumns().size());
    return 0;
}

QAccessibleInterface *AccessibleVirtualItem::child(int index) const
{
    if (!m_viewNode || index < 0 || !isValid())
        return nullptr;
    if (isTree(m_view)) {
        const QList<int> columns = isTable(m_view) ? visibleColumns() : QList<int>();
        if (index < columns.size()) {
            const int column = columns.at(index);
            return m_viewNode->itemFor(QModelIndex(m_index).siblingAtColumn(column), column);
        }
        const QModelIndex childIndex = treeChild(index - columns.size());
        return childIndex.isValid() ? m_viewNode->itemFor(childIndex) : nullptr;
    }
    if (isTable(m_view)) {
        const QList<int> columns = visibleColumns();
        if (index >= columns.size())
            return nullptr;
        const int column = columns.at(index);
        return m_viewNode->itemFor(QModelIndex(m_index).siblingAtColumn(column), column);
    }
    return nullptr;
}

int AccessibleVirtualItem::indexOfChild(const QAccessibleInterface *childInterface) const
{
    const auto *childNode = dynamic_cast<const AccessibleVirtualItem *>(childInterface);
    if (!childNode || !isValid() || !childNode->isValid() || m_column >= 0
        || childNode->m_viewNode != m_viewNode
        || m_index.model() != m_view->model()
        || childNode->m_index.model() != m_view->model())
        return -1;
    const QList<int> columns = isTable(m_view) ? visibleColumns() : QList<int>();
    if (childNode->m_column >= 0) {
        return childNode->m_rowNode == this
            && childNode->m_index.column() == childNode->m_column
            ? columns.indexOf(childNode->m_column) : -1;
    }
    const QModelIndex child = childNode->index();
    return isTree(m_view) && treeIsExpanded(m_view, m_index)
        && child.isValid() && child.column() == 0 && child.parent() == m_index
        && child.row() < treeChildCount()
        ? columns.size() + child.row() : -1;
}

QAccessibleInterface *AccessibleVirtualItem::childAt(int x, int y) const
{
    if (!isValid() || m_column >= 0 || !m_viewNode)
        return nullptr;
    const QPoint local = m_view->viewport()->mapFromGlobal(QPoint(x, y));
    if (!m_view->viewport()->rect().contains(local))
        return nullptr;
    const QModelIndex hit = m_view->indexAt(local);
    if (!hit.isValid())
        return nullptr;
    if (isTable(m_view) && hit.siblingAtColumn(0) == m_index
        && visibleColumns().contains(hit.column())) {
        QAccessibleInterface *cell = m_viewNode->itemFor(hit, hit.column());
        return cell && cell->rect().contains(x, y) ? cell : nullptr;
    }
    const QModelIndex row = hit.siblingAtColumn(0);
    if (isTree(m_view) && row.parent() == m_index && treeIsExpanded(m_view, m_index)) {
        QAccessibleInterface *child = m_viewNode->itemFor(row);
        return child && child->rect().contains(x, y) ? child : nullptr;
    }
    return nullptr;
}

QAccessibleInterface *AccessibleVirtualItem::focusChild() const
{
    if (!isValid() || m_column >= 0 || !m_viewNode)
        return nullptr;
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid() || current.model() != m_view->model()
        || !m_view->visualRect(current).intersects(rowPaneClip(m_view, current)))
        return nullptr;
    if (const auto *table = qobject_cast<const VirtualTableView *>(m_view)) {
        const QModelIndex cell = table->anchorIndex(current);
        if (cell.isValid() && cell.siblingAtColumn(0) == m_index
            && visibleColumns().contains(cell.column()))
            return m_viewNode->itemFor(cell, cell.column());
    }
    if (isTree(m_view) && treeIsExpanded(m_view, m_index)) {
        QModelIndex row = current.siblingAtColumn(0);
        while (row.isValid() && row.parent() != m_index)
            row = row.parent();
        if (row.isValid() && row.parent() == m_index)
            return m_viewNode->itemFor(row);
    }
    return nullptr;
}

void *AccessibleVirtualItem::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::ActionInterface)
        return static_cast<QAccessibleActionInterface *>(this);
    // Only a cell node is a table cell; the row node above it is the table's child.
    if (type == QAccessible::TableCellInterface && m_column >= 0)
        return static_cast<QAccessibleTableCellInterface *>(this);
    return nullptr;
}

// -- QAccessibleTableCellInterface (cell nodes) ------------------------------

bool AccessibleVirtualItem::isSelected() const
{
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    const QModelIndex cell = index();
    return selection && cell.isValid() && m_column >= 0 && selection->isSelected(cell);
}

QList<QAccessibleInterface *> AccessibleVirtualItem::columnHeaderCells() const
{
    return {};
}

QList<QAccessibleInterface *> AccessibleVirtualItem::rowHeaderCells() const
{
    return {};
}

int AccessibleVirtualItem::columnIndex() const
{
    return m_column;
}

int AccessibleVirtualItem::rowIndex() const
{
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view)) {
        const qsizetype row = treeTable->visibilityIndex()->visibleRowForIndex(index());
        return row >= 0 && row <= INT_MAX ? int(row) : -1;
    }
    return index().row();
}

int AccessibleVirtualItem::columnExtent() const
{
    // A merged area is one cell: report how many columns it covers (§43 "spans").
    const auto *table = qobject_cast<const VirtualTableView *>(m_view);
    const QModelIndex cell = index();
    if (!table || !cell.isValid())
        return 1;
    const TableSpan span = table->spanAt(cell);
    if (!span.isMerged() || table->spanRect(cell).isEmpty())
        return 1;
    const HeaderGeometry *columns = table->horizontalHeaderGeometry();
    const int firstVisual = columns->visualIndex(cell.column());
    if (firstVisual < 0)
        return 1;
    int extent = 0;
    for (int visual = firstVisual;
         visual < columns->sectionCount() && extent < span.columnSpan; ++visual) {
        const int logical = columns->logicalIndex(visual);
        const QModelIndex candidate = cell.siblingAtColumn(logical);
        if (!candidate.isValid()) {
            const ColumnGeometry geometry = table->columnGeometry(logical);
            if (geometry.isValid() && !geometry.hidden && geometry.width > 0)
                break;
        } else if (table->anchorIndex(candidate) != cell) {
            break;
        }
        ++extent;
    }
    return qMax(1, extent);
}

int AccessibleVirtualItem::rowExtent() const
{
    const auto *table = qobject_cast<const VirtualTableView *>(m_view);
    const QModelIndex cell = index();
    if (!table || !cell.isValid())
        return 1;
    const TableSpan span = table->spanAt(cell);
    if (!span.isMerged() || table->spanRect(cell).isEmpty())
        return 1;
    const int available = table->model()->rowCount(cell.parent()) - cell.row();
    return qMax(1, qMin(span.rowSpan, available));
}

QAccessibleInterface *AccessibleVirtualItem::table() const
{
    return m_viewNode;
}

QStringList AccessibleVirtualItem::actionNames() const
{
    QStringList names;
    if (!isValid())
        return names;
    names << QAccessibleActionInterface::setFocusAction();
    // A row/item node "presses" (a tree node toggles instead); a cell has no
    // action of its own.
    if (m_column < 0)
        names << QAccessibleActionInterface::pressAction();
    return names;
}

void AccessibleVirtualItem::doAction(const QString &actionName)
{
    if (!isValid())
        return;
    if (actionName == QAccessibleActionInterface::setFocusAction()) {
        m_view->setFocus(Qt::OtherFocusReason);
        m_view->setCurrentIndex(m_index);
        return;
    }
    if (actionName == QAccessibleActionInterface::pressAction() && m_column < 0) {
        if (auto *tree = qobject_cast<VirtualTreeView *>(m_view)) {
            if (tree->hasChildren(m_index)) {
                tree->toggleExpanded(m_index);
                return;
            }
        }
        if (auto *treeTable = qobject_cast<VirtualTreeTableView *>(m_view)) {
            if (treeTable->hasChildren(m_index)) {
                treeTable->toggleExpanded(m_index);
                return;
            }
        }
        // Everything else reports the same signals as a click, so business code
        // does not need an accessibility specific path.
        m_view->activateIndex(m_index);
    }
}

QStringList AccessibleVirtualItem::keyBindingsForAction(const QString &actionName) const
{
    if (actionName == QAccessibleActionInterface::setFocusAction())
        return {QStringLiteral(";")};
    if (actionName == QAccessibleActionInterface::pressAction())
        return {QStringLiteral("Space")};
    return QStringList();
}

AccessibleVirtualItem *AccessibleVirtualItem::itemParent() const
{
    if (!isTree(m_view) || !m_viewNode || !m_index.isValid())
        return nullptr;
    const QModelIndex parentIndex = m_index.parent();
    if (!parentIndex.isValid())
        return nullptr;
    // With a root index the root itself is not a row of the view.
    const QModelIndex root = treeRoot(m_view);
    if (root.isValid() && parentIndex == root)
        return nullptr;
    return m_viewNode->itemFor(parentIndex);
}

QAccessibleInterface *AccessibleVirtualItem::parent() const
{
    if (!m_view)
        return nullptr;
    if (m_column >= 0)
        return m_rowNode ? static_cast<QAccessibleInterface *>(m_rowNode) : m_viewNode;
    if (AccessibleVirtualItem *itemNode = itemParent())
        return itemNode;
    return m_viewNode;
}

// ---------------------------------------------------------------------------
// AccessibleVirtualItemView
// ---------------------------------------------------------------------------

AccessibleVirtualItemView::AccessibleVirtualItemView(VirtualItemView *view)
    : m_view(view)
{
    if (view && view->parentWidget())
        m_parentNode = QAccessible::queryAccessibleInterface(view->parentWidget());
    if (view)
        m_notifier = new AccessibilityNotifier(view, this);
}

AccessibleVirtualItemView::~AccessibleVirtualItemView()
{
    delete m_notifier;
    m_notifier = nullptr;
    qDeleteAll(m_nodes);
    m_nodes.clear();
}

bool AccessibleVirtualItemView::isValid() const
{
    return m_view != nullptr;
}

QObject *AccessibleVirtualItemView::object() const
{
    return m_view;
}

QRect AccessibleVirtualItemView::rect() const
{
    if (!m_view)
        return QRect();
    return toGlobal(m_view, m_view->viewport()->rect());
}

QAccessible::Role AccessibleVirtualItemView::role() const
{
    return viewRole(m_view);
}

QString AccessibleVirtualItemView::text(QAccessible::Text t) const
{
    if (!m_view)
        return QString();
    switch (t) {
    case QAccessible::Name: {
        QString name = m_view->accessibleName();
        if (name.isEmpty())
            name = m_view->windowTitle();
        return name;
    }
    case QAccessible::Description: {
        const QAbstractItemModel *model = m_view->model();
        const int logical = model ? model->rowCount() : 0;
        const int visible = int(visibleIndexes().size());
        return QStringLiteral("%1 / %2 items visible").arg(visible).arg(logical);
    }
    default:
        return QString();
    }
}

void AccessibleVirtualItemView::setText(QAccessible::Text, const QString &)
{
    // The view has no editable text of its own (item text comes from the model).
}

QAccessible::State AccessibleVirtualItemView::state() const
{
    QAccessible::State state;
    if (!m_view) {
        state.invalid = true;
        return state;
    }
    state.focusable = true;
    state.focused = m_view->hasFocus();
    state.active = state.focused;
    state.selectable = true;
    state.multiSelectable = m_view->selectionMode()
        == VirtualItemView::SelectionMode::MultiSelection
        || m_view->selectionMode() == VirtualItemView::SelectionMode::ExtendedSelection;
    state.disabled = !m_view->isEnabled();
    state.invisible = !m_view->isVisible();
    state.sizeable = true;
    state.readOnly = true;
    if (isTree(m_view)) {
        // The tree itself can be expanded/collapsed as a whole.
        const QAbstractItemModel *model = m_view->model();
        state.expandable = model && model->rowCount(treeRoot(m_view)) > 0;
        state.expanded = state.expandable;
    }
    return state;
}

QAccessibleInterface *AccessibleVirtualItemView::parent() const
{
    return m_parentNode;
}

QList<QModelIndex> AccessibleVirtualItemView::visibleIndexes() const
{
    QList<QModelIndex> indexes;
    if (!m_view)
        return indexes;

    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view)) {
        TreeVisibilityIndex *visibility = treeTable->visibilityIndex();
        QSet<QModelIndex> seen;
        for (const VisibleRange &range : treeTable->visibleItemRanges()) {
            for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                const QModelIndex index = visibility->indexAtVisibleRow(row);
                if (!index.isValid()
                    || !m_view->visualRect(index).intersects(rowPaneClip(m_view, index)))
                    continue;
                const QModelIndex top = topLevelTreeIndex(m_view, index);
                if (!seen.contains(top)) {
                    seen.insert(top);
                    indexes.append(top);
                }
            }
        }
        return indexes;
    }

    // Cell Widget Mode materializes cells instead of row widgets, so the rows of
    // the window come from the cells (deduplicated: one node per row).
    QList<QModelIndex> candidates;
    if (auto *table = qobject_cast<VirtualTableView *>(m_view)) {
        if (!table->usesItemWidgets()) {
            for (const QModelIndex &cell : table->materializedCellIndexes()) {
                const QModelIndex row = cell.siblingAtColumn(0);
                if (row.isValid() && !candidates.contains(row))
                    candidates.append(row);
            }
        }
    }
    if (candidates.isEmpty()) {
        for (const MaterializedItem &item : m_view->materializedItems()) {
            if (item.index.isValid())
                candidates.append(QModelIndex(item.index));
        }
    }
    // Top to bottom, whatever the container order was (Cell Widget Mode walks a
    // hash of cells, the row widgets come in row order).
    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](const QModelIndex &lhs, const QModelIndex &rhs) {
                         return m_view->visualRect(lhs).top() < m_view->visualRect(rhs).top();
                     });

    const bool hierarchical = isTree(m_view);
    for (const QModelIndex &index : candidates) {
        // The window of the viewport: materialized rows also cover the overscan
        // and pins that scrolled away.
        const QRect geometry = m_view->visualRect(index);
        if (!geometry.isValid() || !geometry.intersects(rowPaneClip(m_view, index)))
            continue;
        // A tree is a hierarchy: the view node exposes the top level rows, the
        // items expose their own visible children.
        if (hierarchical && depthOf(m_view, index) != 0)
            continue;
        indexes.append(index);
    }
    return indexes;
}

int AccessibleVirtualItemView::visiblePosition(const QModelIndex &index) const
{
    if (!index.isValid())
        return -1;
    const QModelIndex row = index.siblingAtColumn(0);
    if (!m_view || !m_view->visualRect(row).intersects(rowPaneClip(m_view, row)))
        return -1;
    const QModelIndex parent = row.parent();
    if (!parent.isValid() || parent == treeRoot(m_view)) {
        const QList<QModelIndex> indexes = visibleIndexes();
        for (int i = 0; i < indexes.size(); ++i) {
            if (indexes.at(i) == row)
                return i;
        }
        return -1;
    }
    // An expanded parent's direct children are contiguous in their model order.
    TreeVisibilityIndex *visibility = visibilityOf(m_view);
    return visibility && visibility->visibleRowForIndex(row) >= 0 ? row.row() : -1;
}

AccessibleVirtualItem *AccessibleVirtualItemView::cachedNode(const QModelIndex &index,
                                                            int column) const
{
    for (AccessibleVirtualItem *node : m_nodes) {
        if (node && node->column() == column && node->index() == index)
            return node;
    }
    return nullptr;
}

AccessibleVirtualItem *AccessibleVirtualItemView::createNode(const QModelIndex &index, int column)
{
    if (!index.isValid())
        return nullptr;
    if (column < 0) {
        auto *node = new AccessibleVirtualItem(m_view, index, this);
        m_nodes.append(node);
        return node;
    }
    AccessibleVirtualItem *rowNode = itemFor(index.siblingAtColumn(0), -1);
    auto *node = new AccessibleVirtualItem(m_view, index, column, rowNode, this);
    m_nodes.append(node);
    return node;
}

AccessibleVirtualItem *AccessibleVirtualItemView::itemFor(const QModelIndex &index, int column)
{
    if (!index.isValid())
        return nullptr;
    const QModelIndex normalized = column < 0 ? index.siblingAtColumn(0) : index;
    if (AccessibleVirtualItem *node = cachedNode(normalized, column))
        return node;
    return createNode(normalized, column);
}

int AccessibleVirtualItemView::childCount() const
{
    return int(visibleIndexes().size());
}

QAccessibleInterface *AccessibleVirtualItemView::child(int index) const
{
    const QList<QModelIndex> indexes = visibleIndexes();
    if (index < 0 || index >= indexes.size())
        return nullptr;
    return const_cast<AccessibleVirtualItemView *>(this)->itemFor(indexes.at(index));
}

int AccessibleVirtualItemView::indexOfChild(const QAccessibleInterface *childInterface) const
{
    const auto *childNode = dynamic_cast<const AccessibleVirtualItem *>(childInterface);
    if (!childNode || childNode->m_viewNode != this || childNode->m_column >= 0)
        return -1;
    return visibleIndexes().indexOf(childNode->index());
}

QAccessibleInterface *AccessibleVirtualItemView::childAt(int x, int y) const
{
    if (!m_view)
        return nullptr;
    const QPoint local = m_view->viewport()->mapFromGlobal(QPoint(x, y));
    const QModelIndex index = m_view->indexAt(local);
    if (!index.isValid())
        return nullptr;
    const QModelIndex child = isTree(m_view) ? topLevelTreeIndex(m_view, index) : index;
    return const_cast<AccessibleVirtualItemView *>(this)->itemFor(child);
}

QAccessibleInterface *AccessibleVirtualItemView::focusChild() const
{
    if (!m_view)
        return nullptr;
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid() || visiblePosition(current) < 0)
        return nullptr;
    const QModelIndex child = isTree(m_view) ? topLevelTreeIndex(m_view, current) : current;
    return const_cast<AccessibleVirtualItemView *>(this)->itemFor(child);
}

void *AccessibleVirtualItemView::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::ActionInterface)
        return static_cast<QAccessibleActionInterface *>(this);
    // A table gives a screen reader row/column coordinates; a list or a tree has no
    // such interface (their children are simply ordered).
    if (type == QAccessible::TableInterface && isTable(m_view))
        return static_cast<QAccessibleTableInterface *>(this);
    return nullptr;
}

// -- QAccessibleTableInterface (table views) --------------------------------

namespace {
/// Header text of one section, with the same fallback the header widgets use.
QString headerTextFor(QAbstractItemModel *model, int section, Qt::Orientation orientation)
{
    if (!model || section < 0)
        return QString();
    const QString text = model->headerData(section, orientation, Qt::DisplayRole).toString();
    return text.isEmpty() ? QString::number(section + 1) : text;
}
} // namespace

QAccessibleInterface *AccessibleVirtualItemView::caption() const
{
    return nullptr; // the view has no caption widget
}

QAccessibleInterface *AccessibleVirtualItemView::summary() const
{
    return nullptr; // ... and no summary widget
}

QAccessibleInterface *AccessibleVirtualItemView::cellAt(int row, int column) const
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    QAbstractItemModel *model = table ? table->model() : nullptr;
    if (!model || row < 0 || column < 0 || column >= table->columnCount())
        return nullptr;
    const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view);
    const QModelIndex node = treeTable
        ? treeTable->visibilityIndex()->indexAtVisibleRow(row) : QModelIndex();
    const QModelIndex index = treeTable ? node.siblingAtColumn(column)
                                       : model->index(row, column);
    if (!index.isValid())
        return nullptr;
    // A merged area is one cell: a covered index belongs to its anchor.
    const QModelIndex anchor = table->anchorIndex(index);
    return const_cast<AccessibleVirtualItemView *>(this)
        ->itemFor(anchor, anchor.column());
}

QString AccessibleVirtualItemView::columnDescription(int column) const
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    QAbstractItemModel *model = table ? table->model() : nullptr;
    if (!model || column < 0 || column >= table->columnCount())
        return QString();
    return headerTextFor(model, column, Qt::Horizontal);
}

QString AccessibleVirtualItemView::rowDescription(int row) const
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    QAbstractItemModel *model = table ? table->model() : nullptr;
    if (!model || row < 0 || row >= rowCount())
        return QString();
    if (qobject_cast<const VirtualTreeTableView *>(m_view))
        return QString::number(row + 1);
    return headerTextFor(model, row, Qt::Vertical);
}

int AccessibleVirtualItemView::rowCount() const
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    QAbstractItemModel *model = table ? table->model() : nullptr;
    if (qobject_cast<const VirtualTreeTableView *>(m_view))
        return int(qMin<qsizetype>(visibleTreeRows(m_view), INT_MAX));
    return model ? model->rowCount() : 0;
}

int AccessibleVirtualItemView::columnCount() const
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    QAbstractItemModel *model = table ? table->model() : nullptr;
    return model ? table->columnCount() : 0;
}

QList<int> AccessibleVirtualItemView::selectedRows() const
{
    QList<int> rows;
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (!selection)
        return rows;
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view)) {
        QSet<int> candidates;
        TreeVisibilityIndex *visibility = treeTable->visibilityIndex();
        const int count = rowCount();
        for (const QItemSelectionRange &range : selection->selection()) {
            if (!range.isValid())
                continue;
            const QModelIndex first = QModelIndex(range.topLeft()).siblingAtColumn(0);
            const QModelIndex last = QModelIndex(range.bottomRight()).siblingAtColumn(0);
            const qsizetype firstRow = visibility->visibleRowForIndex(first);
            const qsizetype lastRow = visibility->visibleRowForIndex(last);
            if (firstRow < 0 || lastRow < firstRow)
                continue;
            for (qsizetype visibleRow = firstRow; visibleRow <= lastRow && visibleRow < count;
                 ++visibleRow) {
                const QModelIndex node = visibility->indexAtVisibleRow(visibleRow);
                if (node.parent() == range.parent()
                    && node.row() >= range.top() && node.row() <= range.bottom())
                    candidates.insert(int(visibleRow));
            }
        }
        for (int row : candidates) {
            if (isRowSelected(row))
                rows.append(row);
        }
        std::sort(rows.begin(), rows.end());
        return rows;
    }
    // selectedRows(0) reports the rows whose *every* column is selected, which is the
    // same "the row is selected" a screen reader expects from isRowSelected().
    TreeVisibilityIndex *visibility = visibilityOf(m_view);
    for (const QModelIndex &index : selection->selectedRows(0)) {
        const int row = visibility ? int(visibility->visibleRowForIndex(index)) : index.row();
        if (row >= 0 && !rows.contains(row))
            rows.append(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

QList<int> AccessibleVirtualItemView::selectedColumns() const
{
    QList<int> columns;
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (!selection || !selection->hasSelection())
        return columns;
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view)) {
        QAbstractItemModel *model = treeTable->model();
        if (!model)
            return columns;
        const int count = treeTable->columnCount();
        QVector<qsizetype> availableDelta(count + 1, 0);
        QVector<qsizetype> selectedCount(count, 0);
        TreeVisibilityIndex *visibility = treeTable->visibilityIndex();
        for (qsizetype row = 0; row < treeTable->visibleRowCount(); ++row) {
            const QModelIndex node = visibility->indexAtVisibleRow(row);
            if (!node.isValid())
                continue;
            const int available = qBound(0, model->columnCount(node.parent()), count);
            if (available > 0) {
                ++availableDelta[0];
                --availableDelta[available];
            }
        }
        for (const QModelIndex &cell : selection->selectedIndexes()) {
            if (cell.column() >= 0 && cell.column() < count
                && visibility->visibleRowForIndex(cell) >= 0)
                ++selectedCount[cell.column()];
        }
        qsizetype available = 0;
        for (int column = 0; column < count; ++column) {
            available += availableDelta[column];
            if (available > 0 && selectedCount[column] == available)
                columns.append(column);
        }
        return columns;
    }
    for (const QModelIndex &index : selection->selectedColumns(0)) {
        if (!columns.contains(index.column()))
            columns.append(index.column());
    }
    std::sort(columns.begin(), columns.end());
    return columns;
}

bool AccessibleVirtualItemView::isRowSelected(int row) const
{
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view)) {
        const QModelIndex index = treeTable->visibilityIndex()->indexAtVisibleRow(row);
        if (!selection || !index.isValid())
            return false;
        const int count = qMin(treeTable->columnCount(),
                               treeTable->model()->columnCount(index.parent()));
        if (count <= 0)
            return false;
        if (selection->isRowSelected(index.row(), index.parent()))
            return true;
        for (int column = 0; column < count; ++column) {
            if (!selection->isSelected(index.siblingAtColumn(column)))
                return false;
        }
        return true;
    }
    return selection && selection->isRowSelected(row, QModelIndex());
}

bool AccessibleVirtualItemView::isColumnSelected(int column) const
{
    if (qobject_cast<const VirtualTreeTableView *>(m_view))
        return selectedColumns().contains(column);
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    return selection && selection->isColumnSelected(column, QModelIndex());
}

int AccessibleVirtualItemView::selectedRowCount() const
{
    return int(selectedRows().size());
}

int AccessibleVirtualItemView::selectedColumnCount() const
{
    return int(selectedColumns().size());
}

int AccessibleVirtualItemView::selectedCellCount() const
{
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (!selection)
        return 0;
    if (TreeVisibilityIndex *visibility = visibilityOf(m_view)) {
        const auto *treeTable = qobject_cast<const VirtualTreeTableView *>(m_view);
        int count = 0;
        for (const QModelIndex &index : selection->selectedIndexes()) {
            if (visibility->isVisible(index)
                && (!treeTable || index.column() < treeTable->columnCount()))
                ++count;
        }
        return count;
    }
    return int(selection->selectedIndexes().size());
}

QList<QAccessibleInterface *> AccessibleVirtualItemView::selectedCells() const
{
    QList<QAccessibleInterface *> cells;
    const QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (!selection)
        return cells;
    const auto *table = qobject_cast<const VirtualTableView *>(m_view);
    QSet<int> visibleColumns;
    if (table) {
        const QList<int> columns = visibleColumnsOf(table, m_view->viewport()->width());
        for (int column : columns)
            visibleColumns.insert(column);
    }
    TreeVisibilityIndex *visibility = visibilityOf(m_view);
    auto *self = const_cast<AccessibleVirtualItemView *>(this);
    for (const QModelIndex &cell : selection->selectedIndexes()) {
        if ((table && !visibleColumns.contains(cell.column()))
            || (visibility && !visibility->isVisible(cell))
            || !m_view->visualRect(cell).intersects(rowPaneClip(m_view, cell)))
            continue;
        cells.append(self->itemFor(cell, cell.column()));
    }
    return cells;
}

bool AccessibleVirtualItemView::selectRow(int row)
{
    return applySelection(row, -1, true);
}

bool AccessibleVirtualItemView::unselectRow(int row)
{
    return applySelection(row, -1, false);
}

bool AccessibleVirtualItemView::selectColumn(int column)
{
    return applySelection(-1, column, true);
}

bool AccessibleVirtualItemView::unselectColumn(int column)
{
    return applySelection(-1, column, false);
}

bool AccessibleVirtualItemView::applySelection(int row, int column, bool selected)
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    QAbstractItemModel *model = table ? table->model() : nullptr;
    QItemSelectionModel *selection = m_view ? m_view->selectionModel() : nullptr;
    if (!model || !selection || m_view->selectionMode() == VirtualItemView::SelectionMode::NoSelection)
        return false;
    if (auto *treeTable = qobject_cast<VirtualTreeTableView *>(m_view)) {
        if (row >= 0) {
            const QModelIndex node = treeTable->visibilityIndex()->indexAtVisibleRow(row);
            if (!node.isValid())
                return false;
            const int lastColumn = qMin(treeTable->columnCount(),
                                        model->columnCount(node.parent())) - 1;
            if (lastColumn < 0)
                return false;
            selection->select(QItemSelection(node, node.siblingAtColumn(lastColumn)),
                              selected ? QItemSelectionModel::Select
                                       : QItemSelectionModel::Deselect);
            return true;
        }
        if (column < 0 || column >= treeTable->columnCount())
            return false;
        QItemSelection cells;
        QModelIndex first;
        QModelIndex last;
        const auto flush = [&]() {
            if (first.isValid())
                cells.select(first, last);
            first = QModelIndex();
        };
        for (qsizetype visibleRow = 0; visibleRow < treeTable->visibleRowCount(); ++visibleRow) {
            const QModelIndex node = treeTable->visibilityIndex()->indexAtVisibleRow(visibleRow);
            const QModelIndex cell = node.siblingAtColumn(column);
            if (!cell.isValid()) {
                flush();
                continue;
            }
            if (first.isValid() && last.parent() == cell.parent()
                && last.row() + 1 == cell.row()) {
                last = cell;
                continue;
            }
            flush();
            first = cell;
            last = cell;
        }
        flush();
        if (cells.isEmpty())
            return false;
        selection->select(cells, selected ? QItemSelectionModel::Select
                                          : QItemSelectionModel::Deselect);
        return true;
    }
    if (row >= 0 && (row >= model->rowCount() || model->columnCount() <= 0))
        return false;
    if (column >= 0 && (column >= model->columnCount() || model->rowCount() <= 0))
        return false;

    const QModelIndex first = row >= 0 ? model->index(row, 0) : model->index(0, column);
    const QModelIndex last = row >= 0 ? model->index(row, model->columnCount() - 1)
                                      : model->index(model->rowCount() - 1, column);
    const QItemSelectionModel::SelectionFlags direction =
        row >= 0 ? QItemSelectionModel::Rows : QItemSelectionModel::Columns;
    selection->select(QItemSelection(first, last),
                      (selected ? QItemSelectionModel::Select : QItemSelectionModel::Deselect)
                          | direction);
    return true;
}

void AccessibleVirtualItemView::modelChange(QAccessibleTableModelChangeEvent *event)
{
    // Nothing to do: the nodes are created on demand and read the model live, and the
    // framework's own notifier already sends the table model change events (§37).
    Q_UNUSED(event);
}

QStringList AccessibleVirtualItemView::actionNames() const
{
    QStringList names;
    if (!m_view)
        return names;
    names << QAccessibleActionInterface::setFocusAction();
    names << QAccessibleActionInterface::scrollUpAction();
    names << QAccessibleActionInterface::scrollDownAction();
    if (isTable(m_view)) {
        names << QAccessibleActionInterface::scrollLeftAction();
        names << QAccessibleActionInterface::scrollRightAction();
    }
    return names;
}

void AccessibleVirtualItemView::doAction(const QString &actionName)
{
    auto *table = qobject_cast<VirtualTableView *>(m_view);
    if (actionName == QAccessibleActionInterface::setFocusAction()) {
        m_view->setFocus(Qt::OtherFocusReason);
    } else if (actionName == QAccessibleActionInterface::scrollUpAction()) {
        m_view->scrollByPixels(-kScrollStepPixels);
    } else if (actionName == QAccessibleActionInterface::scrollDownAction()) {
        m_view->scrollByPixels(kScrollStepPixels);
    } else if (table && actionName == QAccessibleActionInterface::scrollLeftAction()) {
        table->scrollByHorizontalPixels(-kScrollStepPixels);
    } else if (table && actionName == QAccessibleActionInterface::scrollRightAction()) {
        table->scrollByHorizontalPixels(kScrollStepPixels);
    }
}

QStringList AccessibleVirtualItemView::keyBindingsForAction(const QString &actionName) const
{
    if (actionName == QAccessibleActionInterface::setFocusAction())
        return {QStringLiteral(";")};
    if (actionName == QAccessibleActionInterface::scrollUpAction())
        return {QStringLiteral("Up")};
    if (actionName == QAccessibleActionInterface::scrollDownAction())
        return {QStringLiteral("Down")};
    if (actionName == QAccessibleActionInterface::scrollLeftAction())
        return {QStringLiteral("Left")};
    if (actionName == QAccessibleActionInterface::scrollRightAction())
        return {QStringLiteral("Right")};
    return QStringList();
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

QAccessibleInterface *createAccessibleItemViewInterface(const QString &className, QObject *object)
{
    Q_UNUSED(className);
    if (auto *view = qobject_cast<VirtualItemView *>(object))
        return new AccessibleVirtualItemView(view);
    return nullptr;
}

namespace {
bool g_accessibilityFactoryInstalled = false;
} // namespace

void installAccessibilityFactory()
{
    if (g_accessibilityFactoryInstalled)
        return;
    QAccessible::installFactory(&createAccessibleItemViewInterface);
    g_accessibilityFactoryInstalled = true;
}

void removeAccessibilityFactory()
{
    if (!g_accessibilityFactoryInstalled)
        return;
    QAccessible::removeFactory(&createAccessibleItemViewInterface);
    g_accessibilityFactoryInstalled = false;
}

} // namespace viv
