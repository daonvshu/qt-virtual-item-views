# 多列表格树

`VirtualTreeTableView` 使用树形 `QAbstractItemModel`，纵向只物化展开后的可见节点，横向沿用 `VirtualTableView` 的列布局、表头、控件模式和 pane。单列且不需要表格布局时使用 `VirtualTreeView`；没有父子节点时使用 `VirtualTableView`。

支持继承的 `setAutoHeightEnabled(true)`，按表头与当前展开内容计算建议高度；最大高度沿用 `setMaximumHeight()`，父布局可进一步缩小视口，超出时才滚动。具体约束见 [自适应高度](auto-height.md)，完整示例见 [第 36 章](../examples/treetable_capabilities/36_auto_height/example.cpp)。

## 按功能查找独立示例

运行与复制说明见 [treetable_capabilities/README.md](../examples/treetable_capabilities/README.md)。同一个进程切换章节，每章独立保存模型、adapter 与控件。下列路径相对于仓库根目录，关键接口调用旁有英文注释。

| 编号 | 功能 | 源码 |
| --- | --- | --- |
| 01 | 基础多列树 | [examples/treetable_capabilities/01_basic/example.cpp](../examples/treetable_capabilities/01_basic/example.cpp) |
| 02 | 整行物化与 ColumnHost | [examples/treetable_capabilities/02_row_widgets/example.cpp](../examples/treetable_capabilities/02_row_widgets/example.cpp) |
| 03 | 单元格物化 | [examples/treetable_capabilities/03_cell_widgets/example.cpp](../examples/treetable_capabilities/03_cell_widgets/example.cpp) |
| 04 | 层级与列控件类型 | [examples/treetable_capabilities/04_widget_types/example.cpp](../examples/treetable_capabilities/04_widget_types/example.cpp) |
| 05 | 节点结构体与绑定上下文 | [examples/treetable_capabilities/05_binding_context/example.cpp](../examples/treetable_capabilities/05_binding_context/example.cpp) |
| 06 | 层级模型更新 | [examples/treetable_capabilities/06_model_updates/example.cpp](../examples/treetable_capabilities/06_model_updates/example.cpp) |
| 07 | 固定与动态行高 | [examples/treetable_capabilities/07_row_heights/example.cpp](../examples/treetable_capabilities/07_row_heights/example.cpp) |
| 08 | 展开收起与淡入淡出 | [examples/treetable_capabilities/08_expansion/example.cpp](../examples/treetable_capabilities/08_expansion/example.cpp) |
| 09 | 显示根与相对深度 | [examples/treetable_capabilities/09_root_index/example.cpp](../examples/treetable_capabilities/09_root_index/example.cpp) |
| 10 | 缩进与分支配置 | [examples/treetable_capabilities/10_indentation/example.cpp](../examples/treetable_capabilities/10_indentation/example.cpp) |
| 11 | 自定义分支绘制 | [examples/treetable_capabilities/11_branch_renderer/example.cpp](../examples/treetable_capabilities/11_branch_renderer/example.cpp) |
| 12 | 列宽、隐藏与换序 | [examples/treetable_capabilities/12_columns/example.cpp](../examples/treetable_capabilities/12_columns/example.cpp) |
| 13 | 比例列宽与适应视口 | [examples/treetable_capabilities/13_stretch_columns/example.cpp](../examples/treetable_capabilities/13_stretch_columns/example.cpp) |
| 14 | 节点间距与分割线 | [examples/treetable_capabilities/14_spacing/example.cpp](../examples/treetable_capabilities/14_spacing/example.cpp) |
| 15 | 状态背景与圆角 | [examples/treetable_capabilities/15_visual_states/example.cpp](../examples/treetable_capabilities/15_visual_states/example.cpp) |
| 16 | 状态关联内容 | [examples/treetable_capabilities/16_state_content/example.cpp](../examples/treetable_capabilities/16_state_content/example.cpp) |
| 17 | 共享选择与键盘导航 | [examples/treetable_capabilities/17_selection/example.cpp](../examples/treetable_capabilities/17_selection/example.cpp) |
| 18 | 行内事件与几何查询 | [examples/treetable_capabilities/18_item_events/example.cpp](../examples/treetable_capabilities/18_item_events/example.cpp) |
| 19 | 大树定位与滚动 | [examples/treetable_capabilities/19_scrolling/example.cpp](../examples/treetable_capabilities/19_scrolling/example.cpp) |
| 20 | 二维虚拟化与复用 | [examples/treetable_capabilities/20_virtualization/example.cpp](../examples/treetable_capabilities/20_virtualization/example.cpp) |
| 21 | 左右冻结列 | [examples/treetable_capabilities/21_frozen_columns/example.cpp](../examples/treetable_capabilities/21_frozen_columns/example.cpp) |
| 22 | 独立横向滚动分组 | [examples/treetable_capabilities/22_panes/example.cpp](../examples/treetable_capabilities/22_panes/example.cpp) |
| 23 | 表头交互与调整 | [examples/treetable_capabilities/23_headers/example.cpp](../examples/treetable_capabilities/23_headers/example.cpp) |
| 24 | 表头样式、Adapter 与 Corner | [examples/treetable_capabilities/24_custom_header/example.cpp](../examples/treetable_capabilities/24_custom_header/example.cpp) |
| 25 | 保存与恢复布局 | [examples/treetable_capabilities/25_header_persistence/example.cpp](../examples/treetable_capabilities/25_header_persistence/example.cpp) |
| 26 | 同级排序与递归过滤 | [examples/treetable_capabilities/26_sorting_filtering/example.cpp](../examples/treetable_capabilities/26_sorting_filtering/example.cpp) |
| 27 | 同级行号拖动排序 | [examples/treetable_capabilities/27_row_reordering/example.cpp](../examples/treetable_capabilities/27_row_reordering/example.cpp) |
| 28 | 同级单元格合并 | [examples/treetable_capabilities/28_spans/example.cpp](../examples/treetable_capabilities/28_spans/example.cpp) |
| 29 | 节点拖放与指示器 | [examples/treetable_capabilities/29_drag_indicator/example.cpp](../examples/treetable_capabilities/29_drag_indicator/example.cpp) |
| 30 | 节点拖放与位移动画 | [examples/treetable_capabilities/30_drag_animation/example.cpp](../examples/treetable_capabilities/30_drag_animation/example.cpp) |
| 31 | 异步加载子节点 | [examples/treetable_capabilities/31_lazy_loading/example.cpp](../examples/treetable_capabilities/31_lazy_loading/example.cpp) |
| 32 | 屏外保留与释放 | [examples/treetable_capabilities/32_pinning/example.cpp](../examples/treetable_capabilities/32_pinning/example.cpp) |
| 33 | 统计与性能诊断 | [examples/treetable_capabilities/33_diagnostics/example.cpp](../examples/treetable_capabilities/33_diagnostics/example.cpp) |
| 34 | 可选平滑滚动（可选依赖） | [examples/treetable_capabilities/34_smooth_scrolling/example.cpp](../examples/treetable_capabilities/34_smooth_scrolling/example.cpp) |
| 35 | 圆角边框与透明视图 | [examples/treetable_capabilities/35_borders/example.cpp](../examples/treetable_capabilities/35_borders/example.cpp) |
| 36 | 自适应高度与滚动上限 | [examples/treetable_capabilities/36_auto_height/example.cpp](../examples/treetable_capabilities/36_auto_height/example.cpp) |

## 节点的整行数据

每个深度的节点都可以只保存一份完整结构体，放在该节点第 0 逻辑列的 `viv::kDefaultItemDataRole` 中。行控件及单元格控件分别重写接收 `const viv::ItemBindingContext &` 的 `bindWidget()` / `bindCellWidget()`，直接用 `context.rowData.value<NodeData>()` 获取数据；该自定义类型需在命名空间外声明 `Q_DECLARE_METATYPE(NodeData)`。可通过 `setItemDataRole()` 对接已有 role。

`context.index` 保留当前节点或单元格的身份，`context.logicalColumn` 是当前逻辑列，`context.depth` 是相对当前显示根的节点深度。其他列读取同一行第 0 列的 payload，不会读到父节点或相同可见行号的其他节点；列隐藏、换序不会改变来源。第 0 列 payload 改变并发出该 role 的 `dataChanged` 后，该节点所有已物化单元格都会重新绑定。

[第 05 章](../examples/treetable_capabilities/05_binding_context/example.cpp) 为每级节点设置一份结构体，两种控件模式均使用上下文回调，并在提示中显示深度及单元格逻辑列。不同层级的 payload 类型可以不同，由 adapter 的控件类型及业务规则区分。完整 API 说明、旧回调兼容方式和大对象传递建议见[表格的整行数据上下文](table.md#一行传递一个结构体)。

## 最小用法

每个节点由逻辑列 0 的索引标识。行控件用 `ColumnHost` 承载各列内容，视图负责列宽、滚动和换序；adapter 绑定时通过当前行索引的 `siblingAtColumn()` 读取其他列。

```cpp
#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < 3; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(6, 0, 6, 0);
            labels[column] = new QLabel(host);
            layout->addWidget(labels[column]);
        }
    }

    void bind(const QModelIndex &node)
    {
        for (int column = 0; column < 3; ++column)
            labels[column]->setText(node.siblingAtColumn(column).data().toString());
    }

private:
    QLabel *labels[3] = {nullptr, nullptr, nullptr};
};

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new RowWidget(parent);
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowWidget *>(widget)->bind(index);
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(0, 32); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({"Name", "Type", "State"});
    QList<QStandardItem *> parent{new QStandardItem("Device"),
                                 new QStandardItem("Group"),
                                 new QStandardItem("Online")};
    parent[0]->appendRow({new QStandardItem("Channel"),
                          new QStandardItem("Input"),
                          new QStandardItem("Ready")});
    model.appendRow(parent);

    viv::VirtualTreeTableView view;
    view.setModel(&model);
    view.setTableAdapter(new RowAdapter, true);
    view.setUniformItemHeight(32);
    view.setColumnWidth(0, 220);
    view.setColumnWidth(1, 120);
    view.setColumnWidth(2, 120);
    view.expand(model.index(0, 0));
    view.resize(560, 320);
    view.show();
    return app.exec();
}
```

行控件和单元格控件模式的选择、adapter 生命周期以及焦点保留规则与[表格文档](table.md)相同。单元格模式使用 `setMaterializationMode(MaterializationMode::CellWidgets)` 和 `setCellAdapter()`；第 0 列内容会自动给分支图标留出缩进。

若不同父节点或列需要不同控件，在单元格 adapter 中用模型索引分类；每种类型有自己的复用池：

```cpp
#include <QLabel>
#include <QProgressBar>

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        const bool leafState = index.column() == 2 && index.parent().isValid()
            && index.parent().parent().isValid();
        return leafState ? 1 : 0;
    }

    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1)
            return new QProgressBar(parent);
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (cellWidgetType(index) == 1) {
            auto *progress = static_cast<QProgressBar *>(widget);
            progress->setRange(0, 100);
            progress->setValue(index.data().toString() == QStringLiteral("正常") ? 100 : 0);
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }
};

view.setCellAdapter(new CellAdapter, true);
view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
view.setSelectionBehavior(viv::VirtualTableView::SelectionBehavior::SelectItems);
```

控件再次绑定时必须覆盖旧索引的数据；第 0 列所属节点仍由树视图负责展开与分支绘制。

`setItemPinned(index)` 在行控件模式固定该索引所属的节点，在单元格模式固定指定单元格；视口外的固定项也会按需创建控件。`pinWidget()` 和 `unpinWidget()` 可对当前已物化的行控件或单元格控件操作。折叠节点时，已固定的控件保留但移出可见区域，展开后恢复显示。

拖动期间另有临时固定状态，`isItemPinned()` 会将它计入。拖动结束只释放临时状态，不改变应用在拖动之前或期间设置的显式 pin；单元格模式保留实际拖动格的控件，合并格使用锚点控件。

`setMaxPinnedItems()` 是诊断软上限：超限时发出一次警告，不解除已有 pin 或回收固定控件；零或负值关闭检查。重新设置上限会重新检查当前显式 pin 数量。解除 pin 后，离屏或折叠的控件在下一次布局中恢复正常回收。

`setLifecycleLoggingEnabled(true)` 开启两种物化模式的诊断日志，记录创建、复用、绑定、重绑、解绑、回收及显式 pin/unpin 事件。单元格创建、复用、绑定、重绑和解绑事件带有 `cell` 标识和模型行列坐标，回收事件记录控件类型；坐标用于诊断，不是稳定节点 ID。日志最多保留最近 `kLifecycleLogCapacity`（400）条；`clearLifecycleLog()` 清空但不关闭记录，关闭记录会清空现有日志。

视图自动创建的选择模型由视图管理。切换模型、安装外部选择模型或销毁视图时，旧内置选择模型先脱离视图，再通过 `deleteLater()` 释放，允许正在执行的 Qt 选择通知返回；应用提供的选择模型仍由应用负责生命周期。`selectionModel()` 只返回当前安装的模型，应用不应继续操作已退役的内置模型。

## 树与列

- `setRootIndex()` 限定显示某个节点的子树；`expand()`、`collapse()`、`expandRecursively()`、`collapseAll()` 控制可见行。`visibleRowCount()` 返回展开后的节点数。第 0 逻辑列即使换到其他视觉位置，分支图标仍跟随该列。整行选择模式下 Left/Right 操作树节点；单元格选择模式下 Left/Right 在列间移动。两种模式都可用 `Alt+Left` / `Alt+Right` 操作树，隐藏第 0 列后也可用此快捷键或公开展开 API。
- 根索引下的列数决定全局表头。某个父节点缺少子列时，该格没有有效模型索引，不创建单元格控件；行控件模式下对应的 `ColumnHost` 会隐藏。
- 模型插入、删除或移动逻辑列 0 后，原第 0 列索引或其祖先若转到其他列，就不再代表当前树中的节点；相关展开、选择、当前项、显式行高和固定状态失效。限定根节点若失去第 0 列身份，视图恢复显示模型根。仅调整表头视觉顺序不会改变节点身份。
- 垂直表头显示当前可见节点的序号。表头拖动行只允许同一父节点的兄弟项，并交给模型的 `moveRows()`；模型拒绝时顺序不变。排序交给模型或代理模型，展开状态跟随节点索引。
- Shift 扩展选择按当前可见行顺序覆盖节点，因此跨父节点和已展开的子节点也在范围内。整行选择覆盖这些节点的现有列；单元格选择使用起点和终点的逻辑列范围，跳过某个父节点不存在的列。
- 视图发起的整行选择限定在当前根的列 schema 与节点实际列数的交集。外部代码直接调用原生 `QItemSelectionModel::select()` 并传入 `Rows` 标志时，Qt 会按所属父节点的列数扩展选择，可能选入当前根 schema 之外的列；需要相同约束时应自行构造有效列范围的 `QItemSelection`，不传 `Rows`。
- 树节点不支持冻结行。左右冻结列、多 pane、比例列宽、列状态保存与恢复沿用表格接口。跨进程恢复展开状态需要应用自己的稳定节点 ID。

`setExpanded()`、`toggleExpanded()` 和 `isExpanded()` 接受同节点的非零列索引，并归一到逻辑列 0；`hasChildren()` 查询模型是否有子节点。展开或折叠发生变化后，`expanded` / `collapsed` 的索引也是列 0 节点。`visibleRowsChanged` 通知可见行映射刷新；批量展开、折叠不保证为每个后代逐一发送展开、折叠信号。

`visibilityIndex()` 用于查询 `visibleRowForIndex()` / `indexAtVisibleRow()` 等映射，行号会随展开、排序及结构变化改变。应用应通过视图的展开和根索引 API 修改状态，直接修改返回的映射对象不会自动同步视图布局。`itemDepth()` 的深度从当前显示根的子节点开始计为 0。`setIndentation()` 的负值归零。

与普通树相同，展开/收起默认启用真实布局动画，支持 `RowWidgets` 和 `CellWidgets`。`setExpansionAnimationEnabled(false)` 可关闭，`setExpansionAnimationDuration(ms)` 设置时长，默认 300 ms，0 关闭过渡。真实控件、后续行、垂直表头、分支及分割线使用同一份动画几何，命中检测也随之更新；不截取画面、不使用截图覆盖层。部分露出的控件使用原生父控件矩形裁剪，保留业务控件正常高度。大子树仅物化当前露出的行，屏外尾部不逐行参与动画。

收起开始时 `isExpanded()` 返回 `false`，子节点映射和 `visibleRowCount()` 在动画结束后才移除，随后发送 `collapsed`；展开映射和 `expanded` 在开始时更新。连续反向操作从当前进度继续。模型结构变化取消过渡并重建布局；滚轮操作、窗口尺寸变化、列布局变化和切换根节点会结束当前过渡。`collapseAll()` 和已展开节点的递归展开即时完成，不使用截图回退。

子节点淡入淡出默认启用，`setExpansionFadeEnabled(false)` 可单独关闭渐变，`expansionFadeEnabled()` 查询配置。行控件、单元格控件、行号、状态背景和分支装饰使用同一进度，父节点及后续分组不渐变。临时框架容器保留业务控件已有的图形效果；Qt 的实时透明度合成可能使用离屏缓冲，因此该选项会增加绘制成本。使用写法见 [树表第 08 章](../examples/treetable_capabilities/08_expansion/example.cpp)。

`visibleRowsChanged` 发出时可查询刷新后的可见行映射和垂直表头。`rowHeightChanged(row, height)` 的 row 是通知当时的可见行号，height 为应用后的高度；应用需长期保存目标时应立即取得模型节点索引。显式行高仍跟随节点，在折叠/展开及切换显示根后保留。`selectionModelChanged` 发布当前安装的选择模型指针。横向偏移信号只携带应用后的偏移值，不携带滚动组 ID；使用多个独立组时，应通过带组号的查询读取各组状态。

`setBranchIndicatorsVisible(false)` 隐藏分支绘制，但保留节点缩进和键盘展开入口。默认 branch 的便捷配置接口与 `VirtualTreeView` 一致：`setBranchIconSize()` 设置逻辑像素大小（默认 10，最小 1），`setBranchIconColor()` 设置颜色（无效颜色恢复图标原色或内置三角的 `QPalette::WindowText`），`setBranchIndicatorAlignment()` 设置对齐（默认居中），`setBranchIndicatorPadding()` 设置四边内边距（默认零，负值归零）。先扣除 padding 再对齐，图标会缩小到剩余空间；这些设置不改变层级缩进。交互示例见 [树表第 10 章](../examples/treetable_capabilities/10_indentation/example.cpp)。

`setBranchIndicatorRenderer(renderer, takeOwnership)` 可替换分支绘制，默认由应用持有 renderer；传入 `true` 后由视图持有，传入 `nullptr` 恢复已配置图标或内置三角。便捷图标设置不影响自定义 renderer。renderer 的 `paintBranch()` 收到视口坐标的缩进格，`branchState(index, cellDepth)` 可查询当前节点或祖先格的深度、子节点、兄弟和展开状态；省略 `cellDepth` 时查询节点自身的格。

默认 10px 圆角等边三角形复用同一个朝下的展开图形，随真实行展开进度旋转 90°，收起方向为朝右；旋转共享动画的时长、曲线和反向进度，单独关闭淡入淡出不影响旋转。`setExpandedBranchIcon()`、`setCollapsedBranchIcon()` 只设置其中一个非空图标时，也复用该图标随展开进度旋转；同时设置两个非空图标时立即切换，清空两个图标则恢复内置三角。未执行展开过渡时直接应用最终方向。自定义 renderer 仍按 `branchState()` 自行切换图标，框架不自动旋转它绘制的内容。

`setBranchAreaVisible(false)` 隐藏全部分支装饰，并移除逻辑列 0 中节点自身的图标占位，保留 `depth × indentation()` 的层级缩进。`branchAreaVisible()` 默认返回 `true`；隐藏期间仍可双击或使用键盘展开，重新显示后恢复原有装饰配置。行控件和单元格控件使用相同的缩进规则；行分割线和状态背景始终包含逻辑列 0 的分支与缩进区域。

`saveHeaderState()` 保存当前根的列宽、视觉顺序、隐藏列、比例设置和左右冻结列。旧布局中的冻结行计数会被忽略。先设置模型及根索引，再调用 `restoreHeaderState()`；保存时与恢复时的根列数必须相同，否则返回 `false` 且不修改现有列状态。切换到列数更少的根会截去高位列的临时状态，切回后这些列恢复默认值。需要分别保留多个根的列布局时，应用应按稳定根 ID 分别保存状态，并在每次切换根后恢复对应状态。

安装非空 `setPanes()` 列表会清空默认左右冻结列集合。`clearFrozenColumns()` 只清理这两个默认集合，显式 pane 列表仍生效；调用 `setPanes({})` 返回默认布局，并使用当时的默认冻结集合。

## 间距与状态

```cpp
view.setRowSpacing(4);               // Default gap below each node.
node->setData(10, viv::VirtualTreeTableView::NodeRowSpacingRole); // Override this node's below gap.
view.setColumnSpacing(6);
view.setHorizontalGridLinesVisible(true);
view.setVerticalGridLinesVisible(true);
view.setHorizontalGridLineColor(QColor("#9aa8b2"));
view.setHorizontalGridLineWidth(1);
view.setVisualStateBackgroundVisible(true);
view.setHoverBackgroundColor(QColor("#e7f4ed"));
view.setSelectedBackgroundColor(QColor("#b9d9f1"));
```

树表行分割线固定覆盖实际列的整行范围，包含分支与缩进区域，不按节点层级裁剪。状态背景直接复用表格绘制：默认按整行高亮；切换为按单元格选择时高亮完整单元格，同样保留树装饰区域。没有实际列的空白区域不填充状态背景。分割线的颜色、宽度、显隐、间距中心或两侧绘制、是否穿过行列间距，以及自定义间距控件均沿用[表格设置](table.md)。状态背景默认开启；行控件需要保持透明，才能看到视图在其下方绘制的背景。也可以使用 adapter 的状态回调自行绘制。

hover 和 selected 默认启用。按行选择时，内容区的状态背景默认贯通列间距，可通过继承的 `setVisualStateThroughColumnSpacing(false)` 关闭；按单元格选择时仅高亮单元格。间距中心模式下，可通过 `setVisualStateThroughRowSpacing(true)` 延伸到相邻上下半行间距；表头间距保持自身外观。自定义间距控件仍可绘制自己的内容，保持背景透明时可显示框架状态背景。

与 `VirtualTreeView` 相同，创建节点时将根据深度或业务属性计算的间距写入第 0 列的 `NodeRowSpacingRole`（`Qt::UserRole + 0x100`）。间距属于上方节点，0 表示没有间距，缺失/无效/负值回退到 `rowSpacing()`。该 role 的 dataChanged 会更新布局；表头、单元格和间距命中使用同一几何，末行后不额外留白。模型移动或切换显示根不会自动修改节点保存的间距；旧的按深度、同级和层级设置接口已移除。

可用 `NodeRowSpacingBelowRole` 明确设置下方间距（原 role 的别名），以及 `NodeRowSpacingAboveRole`（`Qt::UserRole + 0x101`）设置上方间距。相邻可见节点的总间距为前者下方加后者上方；上方缺失/无效/负值为 0。第一可见节点上方与末节点下方不额外留白。仅在后续根节点上方留白时，将默认/下方间距设为 0，创建根节点时写入 `rootRow > 0 ? 12 : 0` 到上方 role，展开收起不会把留白移到根节点之后。

结构变化时，存活的滚动锚点按节点身份保留位置。锚点节点被删除后，视图保留原数值滚动偏移，并钳制到新内容的可滚动范围；不会将被删节点的选择、固定项、显式行高或跨度转给占据相同行号的新节点。

`WheelScrollMode::Items` 的完整滚轮刻度按当前可见节点序列步进，计入实际行高和深度间距；起点是视口顶部的可见节点。已有行内像素偏移随节点移动保留，超出范围时钳制到内容端点。高精度 `pixelDelta` 始终按像素应用；不足完整刻度的角度输入沿用参考行高换算。

横向滚轮及 Shift 滚轮驱动 `primaryScrollGroup()`，即显式 pane 列表中首个滚动 pane 的组，与鼠标所在 pane 无关。主组不要求编号为 0；`setHorizontalOffset(offset)` 操作主组，`setHorizontalOffset(group, offset)` 也能操作主组，并同步表头和滚动条。其他独立组由应用通过带组号的 setter 控制。

## 合并与拖放

单元格模式由锚点控件承载完整跨行内容；滚动到跨度中部时，即使锚点行位于 overscan 之外，也会物化锚点控件。行控件模式沿用原表格约束：`ColumnHost` 的跨度矩形裁剪在本行内，adapter 可读取布局上下文的跨度与覆盖信息，跨行内容需由业务绘制；需要框架直接显示完整跨行内容时使用单元格模式。

正在编辑的单元格成为跨度覆盖格时，焦点控件保留并移出可见区域，不覆盖锚点内容；清除跨度后恢复显示。两种控件模式均保留该编辑器的焦点和未提交草稿，释放焦点后恢复正常回收。

`TableSpanProvider` 与表格共用。行跨度仅在同一父节点的相邻行仍是连续可见节点时有效；不能跨折叠边界。越过同级行或全局列末端的跨度会裁剪到现有范围；跨列 pane 的跨度裁剪到锚点所在 pane，不能覆盖该父节点缺失的列。无效跨度按普通单元格处理。`TableSpanMap` 在排序或模型结构变化后若发现两个跨度重叠，保留当前同级行列顺序中靠前的锚点并移除另一个。拖放支持节点前、节点后和放入节点；实际移动、复制仍由模型的拖放接口决定。

provider 默认由应用管理生命周期。使用 `setSpanProvider(provider, true)` 后由视图持有；替换或撤销托管 provider 时，正在执行的查询持有它直到返回，再释放旧对象。`setSpanProvider(nullptr)` 撤销提供者，是否释放旧对象取决于安装旧对象时的所有权设置。

`canStartDrag()`、`dragDropActions()` 和 `startDrag()` 会查询模型的 flags、支持动作及 MIME。若这些回调同步更换模型、改变可见行映射或销毁视图，旧请求停止，查询返回不允许拖动或 `IgnoreAction`；`startDrag()` 在创建 QDrag 前完成动作查询和 MIME 准备。应用不能缓存一次查询结果作为后续模型状态的保证。

拖动反馈与 `VirtualTreeView` 一致，沿用表格的 `setDragFeedbackMode()`，默认 `DragFeedbackMode::RowMoveAnimation`，时长 180 ms、曲线 OutCubic。`SelectRows` 下两种物化模式及冻结列均支持本视图单节点 Move 的位移动画。Automatic 与 Manual 都在截图前立即收起源节点，不执行收起动画，取消或放下后保持收起；仅主节点的单行高度（含正常间距）占位，子树数据仍由模型一起移动。占位补齐上下分割线；同一可见末尾允许多个层级投放时，描边按目标层级缩进，仅在模型允许另一层级时显示。指示器的插入线同样按目标层级缩进，并定位在前一节点整棵子树的末尾。

`Indicator` 模式、单元格选择、复制、多节点、跨视图、放入节点及安装 span provider 时使用指示器；拖动中重新展开源节点也回退到指示器。动画只调整显示位置，不提前修改模型。[第 29 章](../examples/treetable_capabilities/29_drag_indicator/example.cpp) 演示指示器与手动起拖，[第 30 章](../examples/treetable_capabilities/30_drag_animation/example.cpp) 演示位移动画与冻结列。

## 辅助技术

安装组件可访问性工厂后，可见节点同时提供层级关系与表格坐标。第 0 列导出树项角色、展开状态和展开/折叠动作，保留表格单元格接口；其他列提供各自的模型值。焦点查询直接返回实际当前单元格。

Windows Qt >= 6.8 对视图聚焦时的当前节点展开/折叠发送 Announcement，供屏幕阅读器朗读状态。Qt5 和 Qt6.2 至 6.7 保留状态与动作接口，但没有该 Announcement API，不能保证同样的自动朗读表现。实际设备验收使用 Windows Qt6.11.2 讲述人，覆盖两种控件物化模式。
