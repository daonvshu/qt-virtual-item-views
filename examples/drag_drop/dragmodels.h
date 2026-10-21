#pragma once

#include <QHash>
#include <QMap>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QStandardItemModel>
#include <QStringListModel>
#include <algorithm>
#include <limits>

namespace dragdemo {

/// 统一的 item flags：可选中、可拖动、可接收拖放。
inline Qt::ItemFlags dndFlags()
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
        | Qt::ItemIsDropEnabled;
}

const QString kRowFormat = QStringLiteral("application/x-viv-drag-drop-row");
/// 表格自己加的载荷：每行 "源行|显式高度"（-1 = 该行没被调过高度），用来让行高跟着行走。
const QString kTableRowFormat = QStringLiteral("application/x-viv-drag-drop-table-row");
const QString kTableCellFormat = QStringLiteral("application/x-viv-drag-drop-table-cell");

/// 整行载荷保留源模型与行身份，插入后也能准确删除原行。
class TableRowMimeData : public QMimeData
{
public:
    QPointer<const QStandardItemModel> sourceModel;
    QList<QPersistentModelIndex> sources;
};

/// 本表内部的单元格载荷：保存角色数据、权限和可跟随行变化的源索引。
class TableCellMimeData : public QMimeData
{
public:
    struct Cell {
        QPersistentModelIndex source;
        QMap<int, QVariant> roles;
        Qt::ItemFlags flags;
        int rowOffset = 0;
        int columnOffset = 0;
    };
    QPointer<const QStandardItemModel> sourceModel;
    QList<Cell> cells;
};

/// 列表模型：§38 的 model 侧由应用实现（MIME 载荷 + 插入/移动/拒绝），视图只把
/// (row, column, parent) 交过来。用 removeRows() + insertRows() 组合，Qt 5 与
/// Qt 6 的行为一致。
class DraggableListModel : public QStringListModel
{
public:
    explicit DraggableListModel(const QStringList &rows, QObject *parent = nullptr)
        : QStringListModel(rows, parent)
    {
    }

    QStringList mimeTypes() const override { return {QStringLiteral("text/plain"), kRowFormat}; }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        for (const QModelIndex &index : indexes) {
            if (!index.isValid() || index.column() != 0)
                continue;
            auto *data = new QMimeData;
            data->setText(index.data(Qt::DisplayRole).toString());
            data->setData(kRowFormat, QByteArray::number(index.row()));
            return data;
        }
        return nullptr;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction, int row, int column,
                         const QModelIndex &parent) const override
    {
        if (!data || parent.isValid() || column > 0)
            return false;
        if (!data->hasFormat(kRowFormat))
            return false;
        return row >= 0 && row <= rowCount();
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent))
            return false;
        const int source = data->data(kRowFormat).toInt();
        const bool move = action == Qt::MoveAction && source >= 0 && source < rowCount();
        // 原地放下不是有效的移动（视图对内部移动也会先拦掉这种目标）。
        if (move && (row == source || row == source + 1))
            return false;
        const QString text = data->text();
        int destination = row;
        if (move) {
            removeRows(source, 1);
            if (row > source)
                --destination;
        }
        if (!insertRows(destination, 1))
            return false;
        return setData(index(destination, 0), text, Qt::EditRole);
    }
};

/// 表格模型：单元格支持行间插入和填空格，整行使用"移动 + 行高跟随"（§38 §6 方法 1）。
///
/// 整行拖放时，库只负责"落到哪里、插什么"；**行高是视图状态**，库不认识，所以"拖动一行后它还是原来那么高"
/// 这件事必须由应用决定：拖放本身是"插入一份副本 + 删掉源行"，新行是另一行、按默认高度。
/// 这里演示做法 —— 模型把"源行 + 该行高度"写进自己的载荷，移动时自己删源行（同一个模型内部才
/// 做得到，像列表示例那样），再用信号告诉视图：新行应该用什么高度。
class DraggableTableModel : public QStandardItemModel
{
    Q_OBJECT

public:
    explicit DraggableTableModel(int rows, int columns, QObject *parent = nullptr)
        : QStandardItemModel(rows, columns, parent)
    {
    }

    /// 与视图的整行拖动开关同步，决定载荷和目标接收语义。
    void setRowDropEnabled(bool enabled) { m_rowDropEnabled = enabled; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags result = QStandardItemModel::flags(index);
        if (!m_rowDropEnabled && index.isValid()) {
            const QStandardItem *cell = item(index.row(), index.column());
            if (isEmptyCell(index.row(), index.column()) || (cell && cell->hasChildren()))
                result &= ~Qt::ItemIsDragEnabled;
        }
        return result;
    }

    /// 视图的行高变化镜像进来：视图是行高的事实来源，模型只是为了把它写进载荷。
    void rememberRowHeight(qsizetype row, int height)
    {
        const QModelIndex index = this->index(int(row), 0);
        if (index.isValid())
            m_heights.insert(QPersistentModelIndex(index), height);
    }

    QStringList mimeTypes() const override
    {
        QStringList types = QStandardItemModel::mimeTypes();
        types << kTableRowFormat << kTableCellFormat;
        return types;
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        if (!m_rowDropEnabled) {
            auto *data = new TableCellMimeData;
            data->sourceModel = this;
            int top = rowCount();
            int left = columnCount();
            QSet<QPersistentModelIndex> seen;
            for (const QModelIndex &index : indexes) {
                if (!index.isValid() || index.model() != this || index.parent().isValid()
                    || seen.contains(index) || !(flags(index) & Qt::ItemIsDragEnabled))
                    continue;
                seen.insert(index);
                data->cells.append({QPersistentModelIndex(index), itemData(index), flags(index)});
                top = qMin(top, index.row());
                left = qMin(left, index.column());
            }
            if (data->cells.isEmpty()) {
                delete data;
                return nullptr;
            }
            for (TableCellMimeData::Cell &cell : data->cells) {
                cell.rowOffset = cell.source.row() - top;
                cell.columnOffset = cell.source.column() - left;
            }
            // 专用格式仅供本表接收，避免其他视图把单元格误当整行移动。
            data->setData(kTableCellFormat, QByteArrayLiteral("1"));
            return data;
        }
        QMimeData *encoded = QStandardItemModel::mimeData(indexes);
        if (!encoded)
            return nullptr;
        auto *data = new TableRowMimeData;
        data->sourceModel = this;
        for (const QString &format : encoded->formats())
            data->setData(format, encoded->data(format));
        delete encoded;
        QList<int> rows;
        for (const QModelIndex &index : indexes) {
            if (index.isValid() && !rows.contains(index.row()))
                rows.append(index.row());
        }
        std::sort(rows.begin(), rows.end());
        QStringList entries;
        for (int row : rows) {
            data->sources.append(QPersistentModelIndex(index(row, 0)));
            const auto height = m_heights.constFind(QPersistentModelIndex(index(row, 0)));
            entries << QStringLiteral("%1|%2")
                           .arg(row)
                           .arg(height == m_heights.constEnd() ? -1 : height.value());
        }
        data->setData(kTableRowFormat, entries.join(QLatin1Char(';')).toUtf8());
        return data;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        if (!m_rowDropEnabled) {
            const auto *cells = dynamic_cast<const TableCellMimeData *>(data);
            if (!cells || cells->sourceModel != this || cells->cells.isEmpty()
                || (action != Qt::MoveAction && action != Qt::CopyAction))
                return false;
            const bool fillCell = parent.isValid();
            if (fillCell) {
                if (parent.model() != this || parent.parent().isValid() || row != -1 || column != -1)
                    return false;
            } else if (row < 0 || row > rowCount() || column < 0 || column >= columnCount()) {
                return false;
            }
            const int targetRow = fillCell ? parent.row() : row;
            const int targetColumn = fillCell ? parent.column() : column;
            for (const TableCellMimeData::Cell &cell : cells->cells) {
                if (!cell.source.isValid() || cell.source.model() != this
                    || cell.source.parent().isValid()
                    || !(flags(cell.source) & Qt::ItemIsDragEnabled)
                    || itemData(cell.source) != cell.roles)
                    return false;
                const int rowOffset = cell.rowOffset;
                const int columnOffset = cell.columnOffset;
                if (rowOffset < 0 || columnOffset < 0
                    || columnOffset >= columnCount() - targetColumn)
                    return false;
                if (fillCell) {
                    if (rowOffset >= rowCount() - targetRow
                        || !isEmptyCell(targetRow + rowOffset, targetColumn + columnOffset))
                        return false;
                } else if (rowOffset >= std::numeric_limits<int>::max() - rowCount()) {
                    return false;
                }
            }
            return true;
        }
        return data && !data->hasFormat(kTableCellFormat) && !parent.isValid()
            && QStandardItemModel::canDropMimeData(data, action, row, column, parent);
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent))
            return false;
        if (!m_rowDropEnabled) {
            const auto *cells = static_cast<const TableCellMimeData *>(data);
            const int targetRow = parent.isValid() ? parent.row() : row;
            const int targetColumn = parent.isValid() ? parent.column() : column;
            if (!parent.isValid()) {
                int rowsToInsert = 0;
                for (const TableCellMimeData::Cell &cell : cells->cells)
                    rowsToInsert = qMax(rowsToInsert, cell.rowOffset + 1);
                if (!insertRows(row, rowsToInsert))
                    return false;
            }
            for (const TableCellMimeData::Cell &cell : cells->cells) {
                auto *copy = new QStandardItem;
                copy->setFlags(cell.flags);
                for (auto role = cell.roles.cbegin(); role != cell.roles.cend(); ++role)
                    copy->setData(role.value(), role.key());
                setItem(targetRow + cell.rowOffset, targetColumn + cell.columnOffset, copy);
            }
            if (action == Qt::MoveAction) {
                // 插入后用持续索引读取源位置，避免清空行号已经变化的其他格。
                for (const TableCellMimeData::Cell &cell : cells->cells)
                    setItem(cell.source.row(), cell.source.column(), nullptr);
            }
            return true;
        }
        // 数字行号用于计算最终位置；持续索引用于插入之后准确删除源行。
        QList<QPair<int, int>> sources;   // (源行, 该行显式高度；-1 = 没调过)
        const auto *rowData = dynamic_cast<const TableRowMimeData *>(data);
        if (action == Qt::MoveAction && rowData && rowData->sourceModel == this) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            const auto skipEmptyParts = Qt::SkipEmptyParts;
#else
            const auto skipEmptyParts = QString::SkipEmptyParts;
#endif
            const QStringList entries
                = QString::fromUtf8(data->data(kTableRowFormat)).split(QLatin1Char(';'),
                                                                       skipEmptyParts);
            for (const QString &entry : entries) {
                const QStringList parts = entry.split(QLatin1Char('|'));
                if (parts.size() == 2)
                    sources.append({parts.at(0).toInt(), parts.at(1).toInt()});
            }
            std::sort(sources.begin(), sources.end(),
                      [](const QPair<int, int> &lhs, const QPair<int, int> &rhs) {
                          return lhs.first < rhs.first;
                      });
        }

        const int rowsBefore = rowCount(parent);
        if (!QStandardItemModel::dropMimeData(data, action, row, column, parent))
            return false;
        if (sources.isEmpty())
            return true;   // 跨视图的移动：删源行是源视图的事（它才知道 drop 被接受了）

        // 同一个模型内部的移动：源行由模型自己删（§6 方法 1）。落点会在删除时上移，所以先算出
        // 新行最终的行号，再把"这一行的显式高度"告诉视图。
        const int inserted = rowCount(parent) - rowsBefore;
        QList<QPair<int, int>> heights;
        if (inserted == sources.size()) {
            int destination = row >= 0 ? row : rowsBefore;
            int removedBefore = 0;
            for (const QPair<int, int> &source : sources) {
                if (source.first < destination)
                    ++removedBefore;
            }
            destination -= removedBefore;
            for (int i = 0; i < sources.size(); ++i) {
                if (sources.at(i).second > 0)
                    heights.append({destination + i, sources.at(i).second});
            }
        }
        for (int i = rowData->sources.size() - 1; i >= 0; --i) {
            const QPersistentModelIndex source = rowData->sources.at(i);
            if (source.isValid() && source.model() == this)
                removeRow(source.row(), parent);
        }
        if (!heights.isEmpty())
            emit insertedRowHeights(heights);
        return true;
    }

signals:
    /// 模型完成一次移动后告诉视图：这些新行应该用这些高度（应用自己的约定，库不参与）。
    void insertedRowHeights(const QList<QPair<int, int>> &rowsAndHeights);

private:
    /// 没有角色数据和子项的格子可接收投放，包含 Qt 按需创建的空 item。
    bool isEmptyCell(int row, int column) const
    {
        const QStandardItem *cell = item(row, column);
        return itemData(index(row, column)).isEmpty() && (!cell || !cell->hasChildren());
    }

    bool m_rowDropEnabled = false;
    /// 行 -> 用户显式设置的高度（按行身份，插入/删除后自动跟着走）。
    QHash<QPersistentModelIndex, int> m_heights;
};

QStandardItemModel *buildTreeModel(QObject *parent);
DraggableTableModel *buildTableModel(int rows, int columns, QObject *parent);

} // namespace dragdemo
