#pragma once

#include <QItemSelectionModel>

namespace viv {
namespace detail {

// Check every matching parent range; selections can span several tree levels.
inline bool rowIntersectsSelection(const QItemSelectionModel *selectionModel,
                                   int row, const QModelIndex &parent)
{
    const QAbstractItemModel *model = selectionModel ? selectionModel->model() : nullptr;
    if (!model || row < 0 || (parent.isValid() && parent.model() != model)
        || row >= model->rowCount(parent))
        return false;

    const QItemSelection selection = selectionModel->selection();
    for (const QItemSelectionRange &range : selection) {
        if (range.parent() != parent || row < range.top() || row > range.bottom())
            continue;
        for (int column = range.left(); column <= range.right(); ++column) {
            const QModelIndex index = model->index(row, column, parent);
            const Qt::ItemFlags flags = index.flags();
            if (index.isValid() && flags.testFlag(Qt::ItemIsSelectable)
                && flags.testFlag(Qt::ItemIsEnabled))
                return true;
        }
    }
    return false;
}

} // namespace detail
} // namespace viv
