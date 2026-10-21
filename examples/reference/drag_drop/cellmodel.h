#pragma once

#include <QMap>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QStandardItemModel>

namespace reference {

// Deliberately single-cell, same-model data. Multi-cell offsets belong to the
// comprehensive drag_drop demo; they are not required for this minimal policy.
class CellMimeData : public QMimeData
{
public:
    QPersistentModelIndex source;
    QMap<int, QVariant> roles;
    Qt::ItemFlags itemFlags;
};

class CellModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;

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
        return {QStringLiteral("application/x-viv-reference-cell")};
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

} // namespace reference
