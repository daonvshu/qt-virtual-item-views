#include <virtualitemviews/accessibility.h>

#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualitemview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>

#include <QAbstractItemModel>
#include <QAccessibleEvent>
#include <QItemSelectionModel>
#include <QPointer>
#include <QWidget>

#include <algorithm>

namespace viv {

#if QT_CONFIG(accessibility)

namespace {
/// Pixels one "scroll up/down/left/right" action moves the view.
constexpr int kScrollStepPixels = 32;

bool isTable(const VirtualItemView *view)
{
    return view && qobject_cast<const VirtualTableView *>(view) != nullptr;
}

bool isTree(const VirtualItemView *view)
{
    return view && qobject_cast<const VirtualTreeView *>(view) != nullptr;
}

QAccessible::Role viewRole(const VirtualItemView *view)
{
    if (isTable(view))
        return QAccessible::Table;
    if (isTree(view))
        return QAccessible::Tree;
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
    for (int logical = 0; logical < table->columnCount(); ++logical) {
        const ColumnGeometry geometry = table->columnGeometry(logical);
        if (!geometry.isValid() || geometry.hidden || geometry.width <= 0)
            continue;
        if (geometry.viewportX + geometry.width <= 0 || geometry.viewportX >= viewportWidth)
            continue;
        columns.append(logical);
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
                sendModelChange(QAccessibleTableModelChangeEvent::ModelReset);
            });
            connect(model, &QAbstractItemModel::rowsInserted, this,
                    [this](const QModelIndex &, int first, int last) {
                        sendModelChange(QAccessibleTableModelChangeEvent::RowsInserted, first, last);
                    });
            connect(model, &QAbstractItemModel::rowsRemoved, this,
                    [this](const QModelIndex &, int first, int last) {
                        sendModelChange(QAccessibleTableModelChangeEvent::RowsRemoved, first, last);
                    });
            connect(model, &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                        sendModelChange(QAccessibleTableModelChangeEvent::DataChanged,
                                        topLeft.row(), bottomRight.row());
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

QModelIndex AccessibleVirtualItem::rowIndex() const
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
                if (column.isValid() && !column.hidden && column.width > 0)
                    itemRect = QRect(column.viewportX, rowRect.y(), column.width, rowRect.height());
            }
        }
    }
    return toGlobal(m_view, itemRect);
}

QAccessible::Role AccessibleVirtualItem::role() const
{
    if (m_column >= 0)
        return QAccessible::Cell;
    if (isTable(m_view))
        return QAccessible::Row;
    if (isTree(m_view))
        return QAccessible::TreeItem;
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
    if (!isValid())
        return false;
    const int viewportHeight = m_view->viewport()->height();
    const QRect rowRect = m_view->visualRect(m_index);
    if (!rowRect.isValid() || rowRect.bottom() < 0 || rowRect.top() >= viewportHeight)
        return false;
    if (m_column < 0)
        return true;
    const auto *table = qobject_cast<const VirtualTableView *>(m_view);
    if (!table)
        return false;
    const ColumnGeometry column = table->columnGeometry(m_column);
    if (!column.isValid() || column.hidden || column.width <= 0)
        return false;
    return column.viewportX + column.width > 0
        && column.viewportX < m_view->viewport()->width();
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

    if (isTree(m_view)) {
        const auto *tree = qobject_cast<const VirtualTreeView *>(m_view);
        if (tree && m_index.isValid()) {
            state.expandable = tree->hasChildren(m_index);
            state.expanded = state.expandable && tree->isExpanded(m_index);
            state.collapsed = state.expandable && !tree->isExpanded(m_index);
        }
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
        if (table->isSpanCovered(row.siblingAtColumn(logical)))
            continue;
        anchors.append(logical);
    }
    return anchors;
}

int AccessibleVirtualItem::treeChildCount() const
{
    const auto *tree = qobject_cast<const VirtualTreeView *>(m_view);
    TreeVisibilityIndex *visibility = tree ? tree->visibilityIndex() : nullptr;
    if (!tree || !visibility || !m_index.isValid())
        return 0;
    const qsizetype row = visibility->visibleRowForIndex(m_index);
    if (row < 0)
        return 0;
    const int depth = tree->itemDepth(m_index);
    int count = 0;
    for (qsizetype candidate = row + 1; candidate < tree->visibleRowCount(); ++candidate) {
        const QModelIndex index = visibility->indexAtVisibleRow(candidate);
        if (!index.isValid() || tree->itemDepth(index) <= depth)
            break;
        if (tree->itemDepth(index) == depth + 1)
            ++count;
    }
    return count;
}

QModelIndex AccessibleVirtualItem::treeChild(int position) const
{
    if (position < 0)
        return QModelIndex();
    const auto *tree = qobject_cast<const VirtualTreeView *>(m_view);
    TreeVisibilityIndex *visibility = tree ? tree->visibilityIndex() : nullptr;
    if (!tree || !visibility || !m_index.isValid())
        return QModelIndex();
    const qsizetype row = visibility->visibleRowForIndex(m_index);
    if (row < 0)
        return QModelIndex();
    const int depth = tree->itemDepth(m_index);
    int seen = -1;
    for (qsizetype candidate = row + 1; candidate < tree->visibleRowCount(); ++candidate) {
        const QModelIndex index = visibility->indexAtVisibleRow(candidate);
        if (!index.isValid() || tree->itemDepth(index) <= depth)
            break;
        if (tree->itemDepth(index) != depth + 1)
            continue;
        if (++seen == position)
            return index;
    }
    return QModelIndex();
}

int AccessibleVirtualItem::childCount() const
{
    if (!isValid() || m_column >= 0)
        return 0;
    if (isTree(m_view))
        return treeChildCount();
    if (isTable(m_view))
        return int(visibleColumns().size());
    return 0;
}

QAccessibleInterface *AccessibleVirtualItem::child(int index) const
{
    if (!m_viewNode || index < 0 || !isValid())
        return nullptr;
    if (isTree(m_view)) {
        const QModelIndex childIndex = treeChild(index);
        if (!childIndex.isValid())
            return nullptr;
        return m_viewNode->itemFor(childIndex);
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
    if (!childInterface)
        return -1;
    // Interfaces are cached per (index, column), so identity is stable; comparing
    // the coordinates instead keeps this correct even for a foreign interface.
    const int count = childCount();
    for (int i = 0; i < count; ++i) {
        if (child(i) == childInterface)
            return i;
    }
    return -1;
}

QAccessibleInterface *AccessibleVirtualItem::childAt(int x, int y) const
{
    if (!isValid())
        return nullptr;
    const int count = childCount();
    for (int i = 0; i < count; ++i) {
        QAccessibleInterface *candidate = child(i);
        if (candidate && candidate->rect().contains(x, y))
            return candidate;
    }
    return nullptr;
}

QAccessibleInterface *AccessibleVirtualItem::focusChild() const
{
    return nullptr;
}

void *AccessibleVirtualItem::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::ActionInterface)
        return static_cast<QAccessibleActionInterface *>(this);
    return nullptr;
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
    const auto *tree = qobject_cast<const VirtualTreeView *>(m_view);
    if (tree && tree->rootIndex().isValid() && parentIndex == tree->rootIndex())
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
        state.expandable = model && model->rowCount() > 0;
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

    const auto *tree = qobject_cast<const VirtualTreeView *>(m_view);
    const int viewportHeight = m_view->viewport()->height();
    for (const QModelIndex &index : candidates) {
        // The window of the viewport: materialized rows also cover the overscan
        // and pins that scrolled away.
        const QRect geometry = m_view->visualRect(index);
        if (!geometry.isValid() || geometry.bottom() < 0 || geometry.top() >= viewportHeight)
            continue;
        // A tree is a hierarchy: the view node exposes the top level rows, the
        // items expose their own visible children.
        if (tree && tree->itemDepth(index) != 0)
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
    const QModelIndex parent = row.parent();
    if (!parent.isValid()) {
        const QList<QModelIndex> indexes = visibleIndexes();
        for (int i = 0; i < indexes.size(); ++i) {
            if (indexes.at(i) == row)
                return i;
        }
        return -1;
    }
    // Visible siblings of a tree node: the rows at the same depth below the same
    // parent, in visible order.
    const auto *tree = qobject_cast<const VirtualTreeView *>(m_view);
    TreeVisibilityIndex *visibility = tree ? tree->visibilityIndex() : nullptr;
    const qsizetype rowOfIndex = visibility ? visibility->visibleRowForIndex(row) : -1;
    if (rowOfIndex < 0)
        return -1;
    const int depth = tree->itemDepth(row);
    int position = -1;
    for (qsizetype candidate = 0; candidate <= rowOfIndex; ++candidate) {
        const QModelIndex candidateIndex = visibility->indexAtVisibleRow(candidate);
        if (!candidateIndex.isValid() || tree->itemDepth(candidateIndex) != depth)
            continue;
        if (candidateIndex.parent() == parent)
            ++position;
    }
    return position;
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
    const int count = childCount();
    for (int i = 0; i < count; ++i) {
        if (child(i) == childInterface)
            return i;
    }
    return -1;
}

QAccessibleInterface *AccessibleVirtualItemView::childAt(int x, int y) const
{
    if (!m_view)
        return nullptr;
    const QPoint local = m_view->viewport()->mapFromGlobal(QPoint(x, y));
    const QModelIndex index = m_view->indexAt(local);
    if (!index.isValid())
        return nullptr;
    return const_cast<AccessibleVirtualItemView *>(this)->itemFor(index);
}

QAccessibleInterface *AccessibleVirtualItemView::focusChild() const
{
    if (!m_view)
        return nullptr;
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid() || visiblePosition(current) < 0)
        return nullptr;
    return const_cast<AccessibleVirtualItemView *>(this)->itemFor(current);
}

void *AccessibleVirtualItemView::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::ActionInterface)
        return static_cast<QAccessibleActionInterface *>(this);
    return nullptr;
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

#endif // QT_CONFIG(accessibility)

} // namespace viv
