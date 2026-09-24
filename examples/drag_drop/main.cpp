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
//   默认（条目语义 SelectItems）按单元格解析目标，插入线只跨目标列
//   --row-drop（行语义 SelectRows）时整行是拖放单位，插入线跨整个视口
//   冻结列不参与横向滚动，插入线依然落在冻结 pane 内
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
#include <QMimeData>
#include <QScopedPointer>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QResizeEvent>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kTreeRowHeight = 30;
constexpr int kTableRowHeight = 28;
constexpr int kTableColumnWidth = 120;

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

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<NodeRowWidget *>(widget);
        row->setTexts(index.data(Qt::DisplayRole).toString(),
                      index.data(Qt::UserRole + 1).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<NodeRowWidget *>(widget)->setTexts(QString(), QString());
    }

    QSize estimatedSize(const QModelIndex &) const override
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

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column) {
            row->label(column)->setText(
                index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column)
            row->label(column)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &) const override
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

QStandardItemModel *buildTableModel(int rows, int columns, QObject *parent)
{
    auto *model = new QStandardItemModel(rows, columns, parent);
    QStringList labels;
    for (int column = 0; column < columns; ++column)
        labels << QStringLiteral("列 %1").arg(column + 1);
    model->setHorizontalHeaderLabels(labels);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
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
    const QModelIndex hovered = view->indexAt(viewportPos);
    const QModelIndex source = hovered.isValid() ? hovered : view->model()->index(0, 0);
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
    auto *tableModel = buildTableModel(tableRows, tableColumns, &app);
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
    tree->setDragEnabled(true);
    splitter->addWidget(tree);

    auto *table = new viv::VirtualTableView(splitter);
    table->setObjectName(QStringLiteral("table"));
    table->setTableAdapter(&tableAdapter);
    table->setUniformItemHeight(kTableRowHeight);
    table->setDefaultColumnWidth(kTableColumnWidth);
    table->setModel(tableModel);
    table->setDragEnabled(true);
    if (frozenColumns > 0) {
        QVector<int> frozen;
        for (int column = 0; column < frozenColumns; ++column)
            frozen.append(column);
        table->setFrozenColumns(frozen);
    }
    // 表格的拖放粒度跟着选择语义走：SelectRows（默认）整行拖放，SelectItems
    // 按单元格拖放 —— 与 QTableView 的 dropOn() 一致。（枚举写属主全名：VC 14.50
    // 在 Qt 5 配置下出现过用派生类别名限定枚举常量时整条调用被丢掉的情况。）
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
    // 列表是 Move 语义：拖动 = 把行移到目标位置（model 决定怎么移）。
    list->setDefaultDropAction(Qt::MoveAction);
    vertical->addWidget(list);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);

    auto *dragCheck = new QCheckBox(QStringLiteral("允许拖拽"), central);
    dragCheck->setChecked(true);
    auto *indicatorCheck = new QCheckBox(QStringLiteral("显示插入指示器"), central);
    indicatorCheck->setChecked(true);
    auto *status = new QLabel(central);
    status->setObjectName(QStringLiteral("statusLabel"));
    toolbar->addWidget(dragCheck);
    toolbar->addWidget(indicatorCheck);
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
                            .arg(rowDrop ? QStringLiteral("行语义") : QStringLiteral("单元格语义"))
                            .arg(frozenColumns > 0
                                     ? QStringLiteral(" · 冻结 %1 列").arg(frozenColumns)
                                     : QString()));
    };
    QObject::connect(tree, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(table, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(list, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
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
