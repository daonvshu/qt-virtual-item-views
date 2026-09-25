#pragma once

#include <virtualitemviews/types.h>

#include <QAccessible>
#include <QList>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QString>
#include <QStringList>

class QObject;

#if QT_CONFIG(accessibility)

namespace viv {

class VirtualItemView;
class AccessibleVirtualItemView;

/// Accessibility node of one *visible* item (architecture document §37).
///
/// A virtualized view has no QWidget per logical row, so a screen reader cannot
/// walk real child widgets. The bridge therefore exposes virtual nodes on
/// demand: only the rows of the current viewport window exist as interfaces, and
/// every node reads its text, state and geometry from the model/view when it is
/// asked. A million-row model still costs zero nodes.
///
/// Mapping (see docs/accessibility.md):
///  - a list item is a ListItem, a table row is a Row, a tree node is a TreeItem
///    (its children are its *visible* children, its parent is its parent node),
///  - a table cell is a Cell whose parent is the node of its row.
///
/// The node owns no copy of the data: text comes from the model roles
/// (DisplayRole/ToolTipRole/StatusTipRole), the geometry from the view's
/// committed layout, the state from the item flags, the selection model and the
/// current index.
class AccessibleVirtualItem : public QAccessibleInterface,
                              public QAccessibleActionInterface,
                              public QAccessibleTableCellInterface
{
public:
    /// Row/item node: \a index is the model index of the row (column 0).
    AccessibleVirtualItem(VirtualItemView *view, const QModelIndex &index,
                          AccessibleVirtualItemView *viewNode);
    /// Cell node of a table: \a rowNode is the node of its row.
    AccessibleVirtualItem(VirtualItemView *view, const QModelIndex &cellIndex, int column,
                          AccessibleVirtualItem *rowNode, AccessibleVirtualItemView *viewNode);

    // -- QAccessibleInterface ------------------------------------------------
    bool isValid() const override;
    QObject *object() const override;
    QRect rect() const override;
    QAccessible::Role role() const override;
    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text t, const QString &text) override;
    QAccessible::State state() const override;
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int index) const override;
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QAccessibleInterface *childAt(int x, int y) const override;
    QAccessibleInterface *focusChild() const override;
    void *interface_cast(QAccessible::InterfaceType type) override;

    // -- QAccessibleActionInterface ------------------------------------------
    QStringList actionNames() const override;
    void doAction(const QString &actionName) override;
    QStringList keyBindingsForAction(const QString &actionName) const override;

    // -- QAccessibleTableCellInterface (cell nodes only) ---------------------
    // A screen reader asks a cell where it sits and whether it is selected; the
    // table interface above answers for the table as a whole. A merged area is
    // one cell, so its extent is the span, not 1x1 (§43 "spans").
    bool isSelected() const override;
    /// Headers are real widgets of the view (a separate strip), so a cell does not
    /// own header cell interfaces; the table reports the header text through
    /// columnDescription()/rowDescription() instead.
    QList<QAccessibleInterface *> columnHeaderCells() const override;
    QList<QAccessibleInterface *> rowHeaderCells() const override;
    int columnIndex() const override;
    int rowIndex() const override;
    int columnExtent() const override;
    int rowExtent() const override;
    QAccessibleInterface *table() const override;

    /// Model index the node stands for (invalid once the row was removed).
    QModelIndex index() const { return QModelIndex(m_index); }
    /// Column of a cell node; -1 for a row/item node.
    int column() const { return m_column; }
    /// Model index of the row the node belongs to (a table cell reports its row).
    /// Renamed from rowIndex() when the nodes started implementing
    /// QAccessibleTableCellInterface, whose rowIndex() returns the row *number*.
    QModelIndex rowModelIndex() const;

private:
    friend class AccessibleVirtualItemView;

    /// True while the row of this node is inside the viewport window.
    bool isInViewport() const;
    /// Table: the visible columns of a row node, left to right.
    QList<int> visibleColumns() const;
    /// Tree: \a position of the visible child of this node.
    QModelIndex treeChild(int position) const;
    int treeChildCount() const;
    /// Parent *item* node (tree parent, or the row of a cell); null for a row
    /// that sits directly under the view.
    AccessibleVirtualItem *itemParent() const;

    VirtualItemView *m_view = nullptr;
    QPersistentModelIndex m_index;
    int m_column = -1;
    AccessibleVirtualItem *m_rowNode = nullptr;
    AccessibleVirtualItemView *m_viewNode = nullptr;
};

/// Accessibility node of the view itself (architecture document §37).
///
/// Children are the visible rows of the current window; the interface is created
/// by the factory installed with installAccessibilityFactory(), so
/// QAccessible::queryAccessibleInterface(view) returns it.
class AccessibleVirtualItemView : public QAccessibleInterface,
                                  public QAccessibleActionInterface,
                                  public QAccessibleTableInterface
{
public:
    explicit AccessibleVirtualItemView(VirtualItemView *view);
    ~AccessibleVirtualItemView() override;

    // -- QAccessibleInterface ------------------------------------------------
    bool isValid() const override;
    QObject *object() const override;
    QRect rect() const override;
    QAccessible::Role role() const override;
    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text t, const QString &text) override;
    QAccessible::State state() const override;
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int index) const override;
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QAccessibleInterface *childAt(int x, int y) const override;
    QAccessibleInterface *focusChild() const override;
    void *interface_cast(QAccessible::InterfaceType type) override;

    // -- QAccessibleActionInterface ------------------------------------------
    QStringList actionNames() const override;
    void doAction(const QString &actionName) override;
    QStringList keyBindingsForAction(const QString &actionName) const override;

    // -- QAccessibleTableInterface (table views only) ------------------------
    // Rows and columns are addressed by *model* coordinates, so a screen reader can
    // read a table cell by cell even though only the visible rows exist as widgets.
    // selectRow()/selectColumn() go through the view's selection model and fail when
    // the view does not allow a selection at all.
    QAccessibleInterface *caption() const override;
    QAccessibleInterface *summary() const override;
    QAccessibleInterface *cellAt(int row, int column) const override;
    int selectedCellCount() const override;
    /// Selected cells of the current window: the count is exact, the list stays
    /// bounded by what is on screen (the library never materializes one node per
    /// logical row - see docs/accessibility.md).
    QList<QAccessibleInterface *> selectedCells() const override;
    QString columnDescription(int column) const override;
    QString rowDescription(int row) const override;
    int selectedColumnCount() const override;
    int selectedRowCount() const override;
    int columnCount() const override;
    int rowCount() const override;
    QList<int> selectedColumns() const override;
    QList<int> selectedRows() const override;
    bool isColumnSelected(int column) const override;
    bool isRowSelected(int row) const override;
    bool selectRow(int row) override;
    bool selectColumn(int column) override;
    bool unselectRow(int row) override;
    bool unselectColumn(int column) override;
    void modelChange(QAccessibleTableModelChangeEvent *event) override;

    VirtualItemView *view() const { return m_view; }
    /// Node of \a index (created on demand, owned by the view node). \a column
    /// selects a cell node; -1 (the default) asks for the row/item node.
    AccessibleVirtualItem *itemFor(const QModelIndex &index, int column = -1);
    /// Model indexes of the visible rows, top to bottom.
    QList<QModelIndex> visibleIndexes() const;
    /// Position of \a index in visibleIndexes(), or -1 when it is not visible.
    int visiblePosition(const QModelIndex &index) const;
    /// Shared body of selectRow()/unselectRow()/selectColumn()/unselectColumn():
    /// whole rows (row >= 0) or whole columns (column >= 0) through the selection
    /// model. False when the view has no selection model / no selection allowed.
    bool applySelection(int row, int column, bool selected);

private:
    friend class AccessibleVirtualItem;

    AccessibleVirtualItem *cachedNode(const QModelIndex &index, int column) const;
    AccessibleVirtualItem *createNode(const QModelIndex &index, int column);

    VirtualItemView *m_view = nullptr;
    /// Nodes handed out so far, owned by this interface (freed in the
    /// destructor). A node whose row disappears reports isValid() == false
    /// instead of vanishing under a screen reader.
    QList<AccessibleVirtualItem *> m_nodes;
    /// Interface of the parent widget, resolved once (not owned).
    QAccessibleInterface *m_parentNode = nullptr;
    /// QAccessibleEvent source of this view (owned; see accessibility.cpp).
    QObject *m_notifier = nullptr;
};

/// Interface of \a object when it is a VirtualItemView, otherwise nullptr.
/// This is the QAccessible::InterfaceFactory signature, so it can be installed
/// directly (§37).
QAccessibleInterface *createAccessibleItemViewInterface(const QString &className, QObject *object);

/// Installs (or removes) the factory above. Idempotent; call it once after
/// QApplication was created. Without it the view still works, it just reports
/// the generic widget interface of QWidget.
void installAccessibilityFactory();
void removeAccessibilityFactory();

} // namespace viv

#endif // QT_CONFIG(accessibility)
