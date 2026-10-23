#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QMimeData>
#include <QPersistentModelIndex>

namespace table_cell_drop {

// Deliberately single-cell, same-model data. Multi-cell offsets belong to the
// comprehensive drag_drop demo; they are not required for this minimal policy.
class CellMimeData : public QMimeData
{
public:
    QPersistentModelIndex source;
    QMap<int, QVariant> roles;
    Qt::ItemFlags itemFlags;
};

class Model : public QStandardItemModel
{
public:
    explicit Model(QObject *parent) : QStandardItemModel(80, 8, parent)
    {
        for (int c = 0; c < columnCount(); ++c)
            setHeaderData(c, Qt::Horizontal, QStringLiteral("列 %1").arg(c + 1));
        for (int r = 0; r < rowCount(); ++r)
            for (int c = 0; c < columnCount(); ++c)
                if ((r + c) % 3 != 2)
                    setData(index(r, c), QStringLiteral("格 %1 / %2").arg(r + 1).arg(c + 1));
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags result = QStandardItemModel::flags(index);
        if (index.isValid() && (itemData(index).isEmpty()
                               || hasChildren(index) || index.parent().isValid()))
            result &= ~Qt::ItemIsDragEnabled;
        return result;
    }

    QStringList mimeTypes() const override
    {
        return {QStringLiteral("application/x-viv-table-capability-cell")};
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        if (indexes.size() != 1 || indexes.first().model() != this
            || !(flags(indexes.first()) & Qt::ItemIsDragEnabled))
            return nullptr;
        auto *data = new CellMimeData;
        data->source = indexes.first();
        data->roles = itemData(indexes.first());
        data->itemFlags = flags(indexes.first());
        data->setData(mimeTypes().first(), QByteArrayLiteral("1"));
        return data;
    }

    bool canDropMimeData(const QMimeData *mime, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        const auto *data = dynamic_cast<const CellMimeData *>(mime);
        if (!data || !data->source.isValid() || data->source.model() != this
            || !(flags(data->source) & Qt::ItemIsDragEnabled)
            || itemData(data->source) != data->roles
            || (action != Qt::CopyAction && action != Qt::MoveAction))
            return false;
        if (parent.isValid()) {
            // Existing-cell drop: our policy accepts empty root cells only.
            return row == -1 && column == -1 && parent.model() == this
                && !parent.parent().isValid() && itemData(parent).isEmpty()
                && !hasChildren(parent);
        }
        // Between-row drop: insert one new row at the indicated column.
        return row >= 0 && row <= rowCount() && column >= 0 && column < columnCount();
    }

    bool dropMimeData(const QMimeData *mime, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(mime, action, row, column, parent))
            return false;
        const auto *data = static_cast<const CellMimeData *>(mime);
        const int destinationRow = parent.isValid() ? parent.row() : row;
        const int destinationColumn = parent.isValid() ? parent.column() : column;
        if (!parent.isValid() && !insertRows(row, 1))
            return false;
        auto *item = new QStandardItem;
        item->setFlags(data->itemFlags);
        for (auto it = data->roles.cbegin(); it != data->roles.cend(); ++it)
            item->setData(it.value(), it.key());
        setItem(destinationRow, destinationColumn, item);
        // The persistent source follows insertion. Remove its contents, never its row.
        if (action == Qt::MoveAction && data->source.isValid())
            setItem(data->source.row(), data->source.column(), nullptr);
        return true;
    }
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        // Replace old content on every bind; a widget can be reused for a different row or column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("26 单格插入与空白格投放：格子上下边缘插入新行，中间只接受空白格；Move 只清空源格"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_target = new QComboBox(this);
        m_target->addItems({QStringLiteral("行间插入 + 格内投放"), QStringLiteral("仅行间插入"), QStringLiteral("仅格内投放")});
        controls->addWidget(m_target);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new Model(m_view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // SelectItems uses a single-cell payload, with different data semantics from whole-row dragging.
        m_view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        m_view->setDragEnabled(true);
        m_view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        m_view->setDefaultDropAction(Qt::MoveAction);
        // The framework handles hit testing and cell indicators; the model checks empty cells, inserts and clears the source.
        m_view->setDropTargetMode(viv::VirtualTableView::DropTargetMode::BetweenRowsOrOnCell);
        m_view->setMoveRemovesSourceRows(false);

        connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const viv::VirtualTableView::DropTargetMode modes[] = {viv::VirtualTableView::DropTargetMode::BetweenRowsOrOnCell, viv::VirtualTableView::DropTargetMode::BetweenRows, viv::VirtualTableView::DropTargetMode::OnCell};
            m_view->setDropTargetMode(modes[value]);
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    Model *m_model = nullptr;
    QComboBox *m_target = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_cell_drop
