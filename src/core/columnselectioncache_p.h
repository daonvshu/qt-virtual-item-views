#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QItemSelectionModel>
#include <QPointer>

#include <functional>

namespace viv {
namespace detail {

// Scrolling and hover do not change column identities or selection coverage.
class ColumnSelectionCache
{
public:
    // Watch before the view connects its refresh handlers, including flag changes.
    explicit ColumnSelectionCache(QAbstractItemModel *model = nullptr)
        : m_model(model)
    {
        if (!model)
            return;
        const auto changed = [this]() {
            m_columns.clear();
            m_columnIndexes.clear();
            ++m_generation;
            ++m_modelRevision;
        };
        QObject::connect(model, &QAbstractItemModel::dataChanged, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::rowsAboutToBeInserted, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::rowsInserted, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::rowsRemoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::rowsAboutToBeMoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::rowsMoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::columnsAboutToBeInserted, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::columnsInserted, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::columnsRemoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::columnsAboutToBeMoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::columnsMoved, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::layoutAboutToBeChanged, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::layoutChanged, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, &m_context, changed);
        QObject::connect(model, &QAbstractItemModel::modelReset, &m_context, changed);
        QObject::connect(model, &QObject::destroyed, &m_context, changed);
    }

    // Detect model mutations made by callbacks while a column query is running.
    quint64 modelRevision() const { return m_modelRevision; }

    // Queries during model replacement must belong to the watched model.
    QAbstractItemModel *model() const { return m_model.data(); }

    // Cache missing columns too; some tree parents expose fewer columns.
    QModelIndex indexForColumn(int column, qsizetype rowCount, quint64 modelSerial,
                              quint64 mappingSerial, const std::function<QModelIndex()> &query)
    {
        ensureContext(rowCount, modelSerial, mappingSerial);
        const auto cached = m_columnIndexes.constFind(column);
        if (cached != m_columnIndexes.constEnd())
            return cached.value();
        const quint64 generation = m_generation;
        const QModelIndex index = query();
        if (generation == m_generation)
            m_columnIndexes.insert(column, index);
        return index;
    }

    // Recheck only after selection, model data or visible-node identities change.
    bool isSelected(int column, const QItemSelection &selection, qsizetype rowCount,
                    quint64 modelSerial, quint64 mappingSerial,
                    const std::function<bool()> &query)
    {
        ensureContext(rowCount, modelSerial, mappingSerial);
        if (m_selection != selection) {
            m_selection = selection;
            m_columns.clear();
            ++m_generation;
        }
        const auto cached = m_columns.constFind(column);
        if (cached != m_columns.constEnd())
            return cached.value();

        // An empty or unrelated selection cannot select this column.
        bool intersects = false;
        for (const QItemSelectionRange &range : selection) {
            if (range.isValid() && range.left() <= column && column <= range.right()) {
                intersects = true;
                break;
            }
        }
        const quint64 generation = m_generation;
        const bool selected = rowCount > 0 && intersects && query();
        // Model callbacks may have requested another selection while querying.
        if (generation == m_generation)
            m_columns.insert(column, selected);
        return selected;
    }

private:
    // Row counts alone cannot distinguish a root change or a reordered tree.
    void ensureContext(qsizetype rowCount, quint64 modelSerial, quint64 mappingSerial)
    {
        if (m_rowCount == rowCount && m_modelSerial == modelSerial
            && m_mappingSerial == mappingSerial)
            return;
        m_rowCount = rowCount;
        m_modelSerial = modelSerial;
        m_mappingSerial = mappingSerial;
        m_columns.clear();
        m_columnIndexes.clear();
        ++m_generation;
    }

    QItemSelection m_selection;
    QHash<int, bool> m_columns;
    QHash<int, QModelIndex> m_columnIndexes;
    qsizetype m_rowCount = -1;
    quint64 m_modelSerial = 0;
    quint64 m_mappingSerial = 0;
    quint64 m_generation = 0;
    quint64 m_modelRevision = 0;
    QPointer<QAbstractItemModel> m_model;
    QObject m_context;
};

} // namespace detail
} // namespace viv
