// §38 示例：拖放的"视图侧"语义。
//
// 视图负责：谁能作为拖拽源（model flags 的 ItemIsDragEnabled）、载荷从哪来
// （model->mimeData()）、落到哪里（resolveDropTarget）、指示器怎么画（线 / 框 /
// 单元格）、拖到边缘自动滚动。插入、移动、拒绝全部由 model 决定
// （canDropMimeData / dropMimeData），视图不做业务判断。
//
// 列表（下）：
//   默认插入线语义：行上半 = 插到该行之前，下半 = 插到该行之后
// 树（左）：
//   行上 1/4  —— 插到该节点之前（同级）
//   行中 1/2  —— 成为该节点的子节点（指示器是框住该行，对应 QTreeView 的 OnItem）
//   行下 1/4  —— 插到该节点之后（同级）
//   最后一行之下 —— 追加到根
// 表格（右）：
//   工具条上的"整行拖动"复选框（等价于启动参数 --row-drop）切换粒度：
//     不勾 = 条目语义 SelectItems：行的上下 1/4 区域插入，空白格中间区域直接填格；
//            单格插入线或边框标记目标，移动只清空源格，Ctrl 复制保留源格
//     勾选 = 行语义 SelectRows：按整行拖动 —— 载荷是整行的每一列（落下去是完整一行、
//            列一一对应），插入线跨整个视口，拖动预览是整行
//   冻结列不参与横向滚动，目标标记依然落在冻结 pane 内
//
// 三个视图都是 Move 语义（按住 Ctrl 拖 = 复制）：
//   行拖动结束后，源视图删掉自己拖动的行（setMoveRemovesSourceRows(true)），
//   因为跨视图的移动模型自己做不到 —— 这正是 QAbstractItemView 的做法。
//   表格还演示了应用层怎么让**行高跟着行走**：载荷里带上"源行|显式高度"，模型自己删源行
//   （§6 方法 1），再通知视图把高度给新行 —— 库不认识行高，这是应用状态。
//
// 自检：--hover tree:120 / --hover table:40:150 合成一次悬停，把插入指示器
// 画出来，配合 --snapshot 即可截图核对（不需要真的拖动鼠标）。

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QScopedPointer>
#include <QSet>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QResizeEvent>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>

#include <limits>

namespace {

constexpr int kTreeRowHeight = 30;
constexpr int kTableRowHeight = 28;
constexpr int kTableColumnWidth = 120;

/// 表格使用框架计算的行状态区域绘制背景，覆盖冻结列和滚动列。
class DragDropTableView : public viv::VirtualTableView
{
public:
    using viv::VirtualTableView::VirtualTableView;

protected:
    void paintStateBackgroundLayer(QPainter *painter, const QRect &extended,
                                  const QRegion &clip, const QRegion &,
                                  const QColor &color) const override
    {
        paintRoundedStateBackgroundLayer(painter, extended, clip, color, 0);
    }
};

/// 树的行控件：标题 + 副标题。
class NodeRowWidget : public QWidget
{
public:
    explicit NodeRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_title = new QLabel(this);
        m_title->setObjectName(QStringLiteral("rowTitle"));
        m_detail = new QLabel(this);
        m_detail->setObjectName(QStringLiteral("rowDetail"));
    }

    void setTexts(const QString &title, const QString &detail)
    {
        m_title->setText(title);
        m_detail->setText(detail);
    }

    void relayout(int width)
    {
        const int titleWidth = qMin(220, qMax(80, width / 2));
        m_title->setGeometry(0, 0, titleWidth, height());
        m_detail->setGeometry(titleWidth + 8, 0, qMax(0, width - titleWidth - 12), height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        // 树由视图统一绘制（包括 branch 区域）；列表由行控件绘制。
        for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            auto *view = qobject_cast<viv::VirtualListView *>(ancestor);
            if (!view)
                continue;
            const auto state = view->visualState(view->indexForWidget(this));
            QPainter painter(this);
            painter.setOpacity(state.hoverProgress);
            painter.fillRect(rect(), view->hoverBackgroundColor());
            painter.setOpacity(state.selectedProgress);
            painter.fillRect(rect(), view->selectedBackgroundColor());
            break;
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout(width());
    }

private:
    QLabel *m_title = nullptr;
    QLabel *m_detail = nullptr;
};

class NodeAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new NodeRowWidget(parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<NodeRowWidget *>(widget);
        row->setTexts(index.data(Qt::DisplayRole).toString(),
                      index.data(Qt::UserRole + 1).toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<NodeRowWidget *>(widget)->setTexts(QString(), QString());
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(320, kTreeRowHeight);
    }

    int created = 0;
};

/// 表格行控件：每列一个 ColumnHost，框架负责把 host 对齐到列几何。
class TableRowWidget : public QWidget
{
public:
    TableRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        m_labels.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(6, 0, kTableColumnWidth - 12, kTableRowHeight);
            m_labels.append(label);
        }
    }

    QLabel *label(int column) const { return m_labels.value(column); }

private:
    QVector<QLabel *> m_labels;
};

class TableRowAdapter : public viv::TableWidgetAdapter
{
public:
    explicit TableRowAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new TableRowWidget(parent, m_columnCount);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column) {
            row->label(column)->setText(
                index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column)
            row->label(column)->setText(QString());
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(kTableColumnWidth * m_columnCount, kTableRowHeight);
    }

    int created = 0;

private:
    int m_columnCount = 0;
};

/// 统一的 item flags：可选中、可拖动、可接收拖放。
Qt::ItemFlags dndFlags()
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
        | Qt::ItemIsDropEnabled;
}

const QString kRowFormat = QStringLiteral("application/x-viv-drag-drop-row");
/// 表格自己加的载荷：每行 "源行|显式高度"（-1 = 该行没被调过高度），用来让行高跟着行走。
const QString kTableRowFormat = QStringLiteral("application/x-viv-drag-drop-table-row");
const QString kTableCellFormat = QStringLiteral("application/x-viv-drag-drop-table-cell");

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

QStandardItemModel *buildTreeModel(QObject *parent)
{
    auto *model = new QStandardItemModel(parent);
    const QStringList lines{QStringLiteral("SMT-1"), QStringLiteral("SMT-2"),
                            QStringLiteral("AOI-1"), QStringLiteral("PACK-1")};
    for (const QString &line : lines) {
        auto *node = new QStandardItem(line);
        node->setFlags(dndFlags());
        node->setData(QStringLiteral("产线"), Qt::UserRole + 1);
        for (int station = 0; station < 3; ++station) {
            auto *child = new QStandardItem(QStringLiteral("%1-工位%2").arg(line).arg(station + 1));
            child->setFlags(dndFlags());
            child->setData(QStringLiteral("工位"), Qt::UserRole + 1);
            if (station == 0) {
                for (int device = 0; device < 2; ++device) {
                    auto *leaf = new QStandardItem(
                        QStringLiteral("%1-设备%2").arg(line).arg(device + 1));
                    leaf->setFlags(dndFlags());
                    leaf->setData(QStringLiteral("设备"), Qt::UserRole + 1);
                    child->appendRow(leaf);
                }
            }
            node->appendRow(child);
        }
        model->appendRow(node);
    }
    return model;
}

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
        QMimeData *data = QStandardItemModel::mimeData(indexes);
        if (!data)
            return nullptr;
        QList<int> rows;
        for (const QModelIndex &index : indexes) {
            if (index.isValid() && !rows.contains(index.row()))
                rows.append(index.row());
        }
        std::sort(rows.begin(), rows.end());
        QStringList entries;
        for (int row : rows) {
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
        // 源行必须在插入之前读出来（插入之后行号就变了）。
        QList<QPair<int, int>> sources;   // (源行, 该行显式高度；-1 = 没调过)
        if (action == Qt::MoveAction && data && data->hasFormat(kTableRowFormat)) {
            const QStringList entries
                = QString::fromUtf8(data->data(kTableRowFormat)).split(QLatin1Char(';'),
                                                                       Qt::SkipEmptyParts);
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
            for (const QPair<int, int> &source : sources) {
                if (source.first < destination)
                    --destination;
            }
            for (int i = 0; i < sources.size(); ++i) {
                if (sources.at(i).second > 0)
                    heights.append({destination + i, sources.at(i).second});
            }
        }
        for (int i = sources.size() - 1; i >= 0; --i)
            removeRow(sources.at(i).first, parent);
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

DraggableTableModel *buildTableModel(int rows, int columns, QObject *parent)
{
    auto *model = new DraggableTableModel(rows, columns, parent);
    QStringList labels;
    for (int column = 0; column < columns; ++column)
        labels << QStringLiteral("列 %1").arg(column + 1);
    model->setHorizontalHeaderLabels(labels);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            // 留出空白格，直接演示单元格移动和 Ctrl 复制。
            if ((row + column) % 5 == 4)
                continue;
            auto *item = new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column + 1));
            item->setFlags(dndFlags());
            model->setItem(row, column, item);
        }
    }
    return model;
}

/// 合成一次"悬停"：把与真实拖拽相同的 dragEnter/dragMove 送进视口，指示器就会
/// 画出来。载荷直接用 model 自己的编码，因此 canDropMimeData() 走的是真实分支。
bool hoverDrop(viv::VirtualItemView *view, const QPoint &viewportPos)
{
    view->setDragEnabled(true);
    QModelIndex source;
    for (int row = 0; row < view->model()->rowCount() && !source.isValid(); ++row) {
        for (int column = 0; column < view->model()->columnCount(); ++column) {
            const QModelIndex candidate = view->model()->index(row, column);
            if (view->model()->flags(candidate) & Qt::ItemIsDragEnabled) {
                source = candidate;
                break;
            }
        }
    }
    if (!source.isValid())
        return false;
    QScopedPointer<QMimeData> data(view->model()->mimeData({source}));
    if (!data)
        return false;
    const viv::VirtualItemView::DropTarget target = view->dropTargetAt(viewportPos);
    const QRect indicator = view->dropIndicatorRect(target);

    QDragEnterEvent enter(viewportPos, Qt::CopyAction, data.data(), Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    QDragMoveEvent move(viewportPos, Qt::CopyAction, data.data(), Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &move);
    if (!move.isAccepted())
        return false;
    qInfo().noquote() << QStringLiteral("hover(%1, %2) -> parent=%3 row=%4 column=%5 %6 rect=%7")
                             .arg(view->objectName())
                             .arg(viewportPos.y())
                             .arg(target.parent.isValid()
                                      ? target.parent.data(Qt::DisplayRole).toString()
                                      : QStringLiteral("<root>"))
                             .arg(target.row)
                             .arg(target.column)
                             .arg(target.ontoItem ? QStringLiteral("frame")
                                                  : QStringLiteral("line"))
                             .arg(QStringLiteral("%1,%2 %3x%4")
                                      .arg(indicator.x())
                                      .arg(indicator.y())
                                      .arg(indicator.width())
                                      .arg(indicator.height()));
    return true;
}

/// --hover 的取值：view:y 或 view:y:x（view = tree | table）。
bool parseHover(const QString &spec, QString *view, QPoint *pos)
{
    const QStringList parts = spec.split(QLatin1Char(':'));
    if (parts.size() < 2)
        return false;
    bool ok = false;
    const int y = parts.at(1).toInt(&ok);
    if (!ok)
        return false;
    int x = 20;
    if (parts.size() > 2) {
        x = parts.at(2).toInt(&ok);
        if (!ok)
            return false;
    }
    *view = parts.at(0);
    *pos = QPoint(x, y);
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: drag & drop (§38)"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("table-rows"), QStringLiteral("表格行数"),
                                  QStringLiteral("count"), QStringLiteral("500"));
    QCommandLineOption columnsOption(QStringLiteral("table-columns"),
                                     QStringLiteral("表格列数"), QStringLiteral("count"),
                                     QStringLiteral("10"));
    QCommandLineOption frozenOption(QStringLiteral("frozen"),
                                    QStringLiteral("冻结左侧列数"), QStringLiteral("count"),
                                    QStringLiteral("2"));
    QCommandLineOption rowSnapOption(
        QStringLiteral("row-drop"),
        QStringLiteral("表格按行语义拖放（默认按单元格语义）"));
    QCommandLineOption hoverOption(
        QStringLiteral("hover"),
        QStringLiteral("合成一次悬停并显示插入指示器：tree|table|list:<y>[:<x>]"),
        QStringLiteral("spec"));
    QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                      QStringLiteral("把窗口渲染到 PNG 后退出"),
                                      QStringLiteral("path"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(rowsOption);
    parser.addOption(columnsOption);
    parser.addOption(frozenOption);
    parser.addOption(rowSnapOption);
    parser.addOption(hoverOption);
    parser.addOption(snapshotOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int tableRows = qMax(1, parser.value(rowsOption).toInt());
    const int tableColumns = qMax(2, parser.value(columnsOption).toInt());
    const int frozenColumns = qBound(0, parser.value(frozenOption).toInt(), tableColumns);
    const bool rowDrop = parser.isSet(rowSnapOption);

    // 模型/适配器先于窗口构造，析构顺序上晚于视图：视图析构时仍能安全 unbind。
    auto *treeModel = buildTreeModel(&app);
    DraggableTableModel *tableModel = buildTableModel(tableRows, tableColumns, &app);
    QStringList listRows;
    const int listRowCount = 200;
    listRows.reserve(listRowCount);
    for (int row = 0; row < listRowCount; ++row)
        listRows << QStringLiteral("工单 %1 —— 拖动本行可重新排序").arg(row + 1);
    auto *listModel = new DraggableListModel(listRows, &app);
    NodeAdapter nodeAdapter;
    TableRowAdapter tableAdapter(tableColumns);

    QMainWindow window;
    auto *central = new QWidget(&window);
    auto *layout = new QVBoxLayout(central);
    auto *toolbar = new QHBoxLayout;
    layout->addLayout(toolbar);

    auto *vertical = new QSplitter(Qt::Vertical, central);
    layout->addWidget(vertical, 1);
    auto *splitter = new QSplitter(Qt::Horizontal, vertical);
    vertical->addWidget(splitter);
    window.setCentralWidget(central);

    auto *tree = new viv::VirtualTreeView(splitter);
    tree->setObjectName(QStringLiteral("tree"));
    tree->setAdapter(&nodeAdapter);
    tree->setUniformItemHeight(kTreeRowHeight);
    tree->setModel(treeModel);
    tree->setVisualStateBackgroundVisible(true);
    tree->setDragEnabled(true);
    // 三个视图统一是 Move 语义：拖动 = 把项移到目标位置。跨视图的移动没法由模型自己完成
    // （源模型不知道"外部的 drop 被接受了"），所以这里按 QAbstractItemView 的做法让**源视图**
    // 在 Move 拖放结束后删掉自己拖动的行（按住 Ctrl 拖仍然是复制）。
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setMoveRemovesSourceRows(true);
    splitter->addWidget(tree);

    auto *table = new DragDropTableView(splitter);
    table->setObjectName(QStringLiteral("table"));
    table->setTableAdapter(&tableAdapter);
    table->setUniformItemHeight(kTableRowHeight);
    table->setDefaultColumnWidth(kTableColumnWidth);
    table->setModel(tableModel);
    table->setVisualStateScope(viv::VirtualTableView::VisualStateScope::Row);
    table->setDragEnabled(true);
    table->setDropTargetMode(viv::VirtualTableView::DropTargetMode::BetweenRowsOrOnCell);
    table->setDefaultDropAction(Qt::MoveAction);
    table->setMoveRemovesSourceRows(rowDrop);
    tableModel->setRowDropEnabled(rowDrop);
    // 应用层接线（§6 方法 1）：视图的行高变化镜像进模型；模型完成一次"内部移动"后告诉视图
    // 新行要用什么高度。于是被拖高的行移动之后还是那么高（库本身不参与这件事）。
    QObject::connect(table, &viv::VirtualTableView::rowHeightChanged, tableModel,
                     &DraggableTableModel::rememberRowHeight);
    QObject::connect(tableModel, &DraggableTableModel::insertedRowHeights, table,
                     [table](const QList<QPair<int, int>> &entries) {
                         for (const QPair<int, int> &entry : entries)
                             table->setRowHeight(entry.first, entry.second);
                     });
    if (frozenColumns > 0) {
        QVector<int> frozen;
        for (int column = 0; column < frozenColumns; ++column)
            frozen.append(column);
        table->setFrozenColumns(frozen);
    }
    // 整行使用默认的插入落点；单元格按鼠标位置区分行间插入和空白格投放。
    table->setSelectionBehavior(rowDrop ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                        : viv::VirtualItemView::SelectionBehavior::SelectItems);
    splitter->addWidget(table);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto *list = new viv::VirtualListView(vertical);
    list->setObjectName(QStringLiteral("list"));
    list->setAdapter(&nodeAdapter);
    list->setUniformItemHeight(kTableRowHeight);
    list->setModel(listModel);
    list->setDragEnabled(true);
    // 列表的模型自己实现"移动"（removeRows + insertRows）：被删掉的源索引已经失效，
    // 所以视图的 Move 清理会跳过它们，不会删第二次。
    list->setDefaultDropAction(Qt::MoveAction);
    list->setMoveRemovesSourceRows(true);
    vertical->addWidget(list);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);

    auto *dragCheck = new QCheckBox(QStringLiteral("允许拖拽"), central);
    dragCheck->setChecked(true);
    auto *indicatorCheck = new QCheckBox(QStringLiteral("显示插入指示器"), central);
    indicatorCheck->setChecked(true);
    auto *rowDropCheck = new QCheckBox(QStringLiteral("整行拖动"), central);
    rowDropCheck->setChecked(rowDrop);
    rowDropCheck->setToolTip(QStringLiteral(
        "勾选 = 行语义（SelectionBehavior::SelectRows）：载荷是整行每一列、插入线跨整个视口、"
        "预览是整行；不勾 = 本表内上下 1/4 区域插入，中间区域填空白格，Ctrl 复制"));
    auto *status = new QLabel(central);
    status->setObjectName(QStringLiteral("statusLabel"));
    toolbar->addWidget(dragCheck);
    toolbar->addWidget(indicatorCheck);
    toolbar->addWidget(rowDropCheck);
    window.statusBar()->addWidget(status);

    QObject::connect(dragCheck, &QCheckBox::toggled, &window, [tree, table, list](bool enabled) {
        tree->setDragEnabled(enabled);
        table->setDragEnabled(enabled);
        list->setDragEnabled(enabled);
    });
    QObject::connect(indicatorCheck, &QCheckBox::toggled, &window, [tree, table, list](bool shown) {
        tree->setDropIndicatorShown(shown);
        table->setDropIndicatorShown(shown);
        list->setDropIndicatorShown(shown);
    });
    // 展开整棵树，让"插入线 / 成为子节点"两种情况都能演示。
    for (int row = 0; row < treeModel->rowCount(); ++row)
        tree->expandRecursively(treeModel->index(row, 0));

    window.setWindowTitle(QStringLiteral("VirtualItemViews · drag & drop (§38)"));
    window.resize(1100, 620);
    window.show();
    tree->flushPendingRelayout();
    table->flushPendingRelayout();
    list->flushPendingRelayout();

    const auto updateStatus = [&]() {
        status->setText(QStringLiteral("树 %1 行 / 物化 %2   ·   表格 %3x%4 / 物化 %5"
                                       "   ·   列表 %6 行 / 物化 %7   ·   拖放：%8%9")
                            .arg(tree->visibleRowCount())
                            .arg(tree->materializedItemCount())
                            .arg(tableModel->rowCount())
                            .arg(tableModel->columnCount())
                            .arg(table->materializedItemCount())
                            .arg(listModel->rowCount())
                            .arg(list->materializedItemCount())
                            .arg(table->selectionBehavior() == viv::VirtualItemView::SelectionBehavior::SelectRows
                                     ? QStringLiteral("行语义")
                                     : QStringLiteral("单元格语义"))
                            .arg(frozenColumns > 0
                                     ? QStringLiteral(" · 冻结 %1 列").arg(frozenColumns)
                                     : QString()));
    };
    QObject::connect(tree, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(table, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(list, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    // 单元格移动由模型清空源格，不能启用框架的整行删除。
    QObject::connect(rowDropCheck, &QCheckBox::toggled, &window,
                     [table, tableModel, updateStatus](bool rows) {
                         tableModel->setRowDropEnabled(rows);
                         table->setMoveRemovesSourceRows(rows);
                         table->setSelectionBehavior(
                             rows ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                  : viv::VirtualItemView::SelectionBehavior::SelectItems);
                         updateStatus();
                     });
    QObject::connect(tree, &viv::VirtualItemView::itemDropped, &window,
                     [status](const QModelIndex &, int row, int column, Qt::DropAction action) {
                         status->setText(QStringLiteral("drop: row=%1 column=%2 action=%3")
                                             .arg(row)
                                             .arg(column)
                                             .arg(int(action)));
                     });
    updateStatus();

    if (parser.isSet(hoverOption)) {
        QString which;
        QPoint pos;
        if (parseHover(parser.value(hoverOption), &which, &pos)) {
            viv::VirtualItemView *target = nullptr;
            if (which == QLatin1String("table"))
                target = table;
            else if (which == QLatin1String("list"))
                target = list;
            else
                target = tree;
            QTimer::singleShot(0, &window, [target, pos]() { hoverDrop(target, pos); });
        } else {
            qWarning("--hover 需要 tree|table|list:<y>[:<x>]");
            return 2;
        }
    }

    if (parser.isSet(snapshotOption)) {
        const QString path = parser.value(snapshotOption);
        QTimer::singleShot(120, &window, [&window, path]() {
            const bool ok = window.grab().save(path);
            qInfo().noquote() << QStringLiteral("snapshot %1: %2")
                                     .arg(path, ok ? QStringLiteral("ok") : QStringLiteral("failed"));
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);

    return app.exec();
}

// DraggableTableModel 是定义在本文件里的 QObject（带信号），AUTOMOC 需要这一行。
#include "main.moc"
