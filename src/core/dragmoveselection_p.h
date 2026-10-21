#pragma once

#include <QAbstractItemModel>
#include <QItemSelectionModel>
#include <QPointer>
#include <QVector>
#include <algorithm>
#include <functional>

namespace viv {

// Track a conventional row-insertion drop without guessing identities from item data.
// The source rows must be siblings, packed in row order into one inserted block.
class DragMoveSelection
{
public:
    DragMoveSelection(QItemSelectionModel *sourceSelection,
                      const QList<QPersistentModelIndex> &sources,
                      QItemSelectionModel *targetSelection, const QModelIndex &parent,
                      int row, int column)
        : m_sourceSelection(sourceSelection), m_targetSelection(targetSelection),
          m_sourceModel(sourceSelection ? sourceSelection->model() : nullptr),
          m_targetModel(targetSelection ? targetSelection->model() : nullptr),
          m_parent(parent), m_hadParent(parent.isValid()), m_row(row),
          m_column(qMax(0, column))
    {
        if (!m_sourceModel || !m_targetModel || sources.isEmpty() || row < 0
            || (parent.isValid() && parent.model() != m_targetModel))
            return;
        const QModelIndex sourceParent = sources.first().parent();
        m_firstColumn = sources.first().column();
        for (const QPersistentModelIndex &source : sources) {
            if (!source.isValid() || source.model() != m_sourceModel
                || source.parent() != sourceParent)
                return;
            m_rows.append(source.row());
            m_firstColumn = qMin(m_firstColumn, source.column());
            if (sourceSelection->isSelected(source))
                m_selected.append({source, source.row(), source.column(),
                                   sourceSelection->currentIndex() == source});
        }
        if (m_selected.isEmpty())
            return;
        std::sort(m_rows.begin(), m_rows.end());
        m_rows.erase(std::unique(m_rows.begin(), m_rows.end()), m_rows.end());
        m_valid = true;
        m_insertConnection = QObject::connect(m_targetModel.data(), &QAbstractItemModel::rowsInserted,
            m_targetModel.data(), [this](const QModelIndex &insertParent, int first, int last) {
                if (!m_valid)
                    return;
                if (!m_inserted.isEmpty() || insertParent != m_parent || first != m_row
                    || last - first + 1 != m_rows.size()) {
                    m_valid = false;
                    return;
                }
                for (int row = first; row <= last; ++row)
                    m_inserted.append(m_targetModel->index(row, 0, insertParent));
            });
        // A reset is not source deletion, even though both invalidate persistent indexes.
        for (QAbstractItemModel *model : {m_sourceModel.data(), m_targetModel.data()}) {
            m_resetConnections.append(QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset,
                model, [this] { m_valid = false; }));
        }
    }

    ~DragMoveSelection()
    {
        QObject::disconnect(m_insertConnection);
        for (const QMetaObject::Connection &connection : m_resetConnections)
            QObject::disconnect(connection);
    }

    // Refuse stale or replaced selection models before applying a delayed transfer.
    bool matches(QItemSelectionModel *source, QItemSelectionModel *target) const
    {
        return m_valid && m_sourceModel && m_targetModel && source && target && source == m_sourceSelection
            && target == m_targetSelection && source->model() == m_sourceModel
            && target->model() == m_targetModel;
    }

    // Resolve only the block observed during this accepted drop, before source cleanup.
    void finishDrop(bool accepted)
    {
        QObject::disconnect(m_insertConnection);
        if (!accepted || !m_valid || !m_targetModel || m_inserted.size() != m_rows.size()
            || (m_hadParent && !m_parent.isValid())) {
            m_valid = false;
            return;
        }
        for (const SourceCell &cell : m_selected) {
            const int row = int(std::lower_bound(m_rows.cbegin(), m_rows.cend(), cell.row) - m_rows.cbegin());
            const QModelIndex target = QModelIndex(m_inserted.at(row)).siblingAtColumn(
                m_column + cell.column - m_firstColumn);
            if (!target.isValid()) {
                m_valid = false;
                return;
            }
            m_cells.append(qMakePair(cell.index, QPersistentModelIndex(target)));
            if (cell.current) {
                m_currentSource = cell.index;
                m_currentTarget = target;
            }
        }
    }

    // Called after source cleanup. Failed removals keep their original selection.
    QModelIndex restore(const std::function<bool()> &isCurrent)
    {
        const auto ready = [&] {
            return matches(m_sourceSelection.data(), m_targetSelection.data()) && isCurrent();
        };
        if (!ready())
            return {};
        QItemSelection selection;
        for (const auto &cell : m_cells) {
            if (cell.first.isValid() || !cell.second.isValid())
                continue;
            const Qt::ItemFlags flags = m_targetModel->flags(cell.second);
            if (!ready())
                return {};
            if (cell.second.isValid() && (flags & Qt::ItemIsSelectable))
                selection.select(cell.second, cell.second);
        }
        if (selection.isEmpty())
            return {};
        // Preserve unrelated selections; do not use the view's ClearAndSelect helper.
        m_targetSelection->select(selection, QItemSelectionModel::Select);
        if (!ready() || m_currentSource.isValid() || !m_currentTarget.isValid()
            || !m_targetSelection->isSelected(m_currentTarget))
            return {};
        m_targetSelection->setCurrentIndex(m_currentTarget, QItemSelectionModel::NoUpdate);
        return ready() && m_currentTarget.isValid() ? QModelIndex(m_currentTarget) : QModelIndex();
    }

private:
    Q_DISABLE_COPY(DragMoveSelection)
    struct SourceCell {
        QPersistentModelIndex index;
        int row;
        int column;
        bool current;
    };
    QPointer<QItemSelectionModel> m_sourceSelection;
    QPointer<QItemSelectionModel> m_targetSelection;
    QPointer<QAbstractItemModel> m_sourceModel;
    QPointer<QAbstractItemModel> m_targetModel;
    QPersistentModelIndex m_parent;
    bool m_hadParent;
    int m_row;
    int m_column;
    int m_firstColumn = 0;
    bool m_valid = false;
    QVector<int> m_rows;
    QVector<SourceCell> m_selected;
    QList<QPersistentModelIndex> m_inserted;
    QVector<QPair<QPersistentModelIndex, QPersistentModelIndex>> m_cells;
    QPersistentModelIndex m_currentSource;
    QPersistentModelIndex m_currentTarget;
    QMetaObject::Connection m_insertConnection;
    QVector<QMetaObject::Connection> m_resetConnections;
};

} // namespace viv
