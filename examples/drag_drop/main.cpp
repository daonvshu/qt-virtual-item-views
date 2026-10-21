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

#include "dragmodels.h"
#include "dragwidgets.h"
#include <virtualitemviews/virtualtreeview.h>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QScopedPointer>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

using namespace dragdemo;

namespace {

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
    // This example paints state inside its row widget.
    list->setVisualStateBackgroundVisible(false);
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
