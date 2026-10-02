# 多列表格树

`VirtualTreeTableView` 使用树形 `QAbstractItemModel`，纵向只物化展开后的可见节点，横向沿用 `VirtualTableView` 的列布局、表头、控件模式和 pane。单列且不需要表格布局时使用 `VirtualTreeView`；没有父子节点时使用 `VirtualTableView`。

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
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowWidget *>(widget)->bind(index);
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 32); }
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
- 首/尾冻结行按当前可见节点序列计算，展开或折叠后重新定位。左右冻结列、多 pane、比例列宽、列状态保存与恢复沿用表格接口。跨进程恢复展开状态需要应用自己的稳定节点 ID。

`setExpanded()`、`toggleExpanded()` 和 `isExpanded()` 接受同节点的非零列索引，并归一到逻辑列 0；`hasChildren()` 查询模型是否有子节点。展开或折叠发生变化后，`expanded` / `collapsed` 的索引也是列 0 节点。`visibleRowsChanged` 通知可见行映射刷新；批量展开、折叠不保证为每个后代逐一发送展开、折叠信号。

`visibilityIndex()` 用于查询 `visibleRowForIndex()` / `indexAtVisibleRow()` 等映射，行号会随展开、排序及结构变化改变。应用应通过视图的展开和根索引 API 修改状态，直接修改返回的映射对象不会自动同步视图布局。`itemDepth()` 的深度从当前显示根的子节点开始计为 0。`setIndentation()` 的负值归零；`setDepthRowSpacing(depth, -1)` 移除该深度覆盖并恢复默认 `rowSpacing()`。

`visibleRowsChanged` 发出时可查询刷新后的可见行映射和垂直表头。`rowHeightChanged(row, height)` 的 row 是通知当时的可见行号，height 为应用后的高度；应用需长期保存目标时应立即取得模型节点索引。显式行高仍跟随节点，在折叠/展开及切换显示根后保留。`selectionModelChanged` 发布当前安装的选择模型指针。横向偏移信号只携带应用后的偏移值，不携带滚动组 ID；使用多个独立组时，应通过带组号的查询读取各组状态。

`setBranchIndicatorsVisible(false)` 隐藏分支绘制，但保留节点缩进和键盘展开入口。`setBranchIndicatorRenderer(renderer, takeOwnership)` 可替换分支绘制，默认由应用持有 renderer；传入 `true` 后由视图持有，传入 `nullptr` 恢复内置分支。renderer 的 `paintBranch()` 收到视口坐标的缩进格，`branchState(index, cellDepth)` 可查询当前节点或祖先格的深度、子节点、兄弟和展开状态；省略 `cellDepth` 时查询节点自身的格。

`saveHeaderState()` 保存当前根的列宽、视觉顺序、隐藏列、比例设置和冻结行列。先设置模型及根索引，再调用 `restoreHeaderState()`；保存时与恢复时的根列数必须相同，否则返回 `false` 且不修改现有列状态。切换到列数更少的根会截去高位列的临时状态，切回后这些列恢复默认值。需要分别保留多个根的列布局时，应用应按稳定根 ID 分别保存状态，并在每次切换根后恢复对应状态。

安装非空 `setPanes()` 列表会清空默认左右冻结列集合。`clearFrozenColumns()` 只清理这两个默认集合，显式 pane 列表仍生效；调用 `setPanes({})` 返回默认布局，并使用当时的默认冻结集合。

## 间距与状态

```cpp
view.setRowSpacing(4);               // 所有节点后的默认间距
view.setDepthRowSpacing(1, 10);      // 深度 1 节点后的间距
view.setColumnSpacing(6);
view.setHorizontalGridLinesVisible(true);
view.setVerticalGridLinesVisible(true);
view.setRowGridLineExtent(viv::VirtualTreeTableView::RowGridLineExtent::NodeAndIcon);
view.setHorizontalGridLineColor(QColor("#9aa8b2"));
view.setHorizontalGridLineWidth(1);
view.setVisualStateBackgroundVisible(true);
view.setVisualStateBackgroundExtent(
    viv::VirtualTreeTableView::VisualStateBackgroundExtent::NodeOnly);
view.setHoverBackgroundColor(QColor("#e7f4ed"));
view.setSelectedBackgroundColor(QColor("#b9d9f1"));
```

`RowGridLineExtent` 和 `VisualStateBackgroundExtent` 分别选择只覆盖节点内容（`NodeOnly`）、图标加内容（`NodeAndIcon`）或整行（`FullWidth`）。范围只扣除第 0 逻辑列中的树装饰区域，其他列仍正常绘制。分割线的颜色、宽度、显隐、是否穿过行列间距，以及自定义间距控件均沿用[表格设置](table.md)。状态背景默认关闭；行控件需要保持透明，才能看到视图在其下方绘制的背景。也可以使用 adapter 的状态回调自行绘制。

深度行间距跟随其上方节点，垂直表头的行号位置和间隙随可见节点同步更新。

结构变化时，存活的滚动锚点按节点身份保留位置。锚点节点被删除后，视图保留原数值滚动偏移，并钳制到新内容的可滚动范围；不会将被删节点的选择、固定项、显式行高或跨度转给占据相同行号的新节点。

`WheelScrollMode::Items` 的完整滚轮刻度按当前可见节点序列步进，计入实际行高和深度间距；起点是冻结顶部区域下方的滚动行。已有行内像素偏移随节点移动保留，超出范围时钳制到内容端点。高精度 `pixelDelta` 始终按像素应用；不足完整刻度的角度输入沿用参考行高换算。

横向滚轮及 Shift 滚轮驱动 `primaryScrollGroup()`，即显式 pane 列表中首个滚动 pane 的组，与鼠标所在 pane 无关。主组不要求编号为 0；`setHorizontalOffset(offset)` 操作主组，`setHorizontalOffset(group, offset)` 也能操作主组，并同步表头和滚动条。其他独立组由应用通过带组号的 setter 控制。

## 合并与拖放

单元格模式由锚点控件承载完整跨行内容；滚动到跨度中部时，即使锚点行位于 overscan 之外，也会物化锚点控件。行控件模式沿用原表格约束：`ColumnHost` 的跨度矩形裁剪在本行内，adapter 可读取布局上下文的跨度与覆盖信息，跨行内容需由业务绘制；需要框架直接显示完整跨行内容时使用单元格模式。

正在编辑的单元格成为跨度覆盖格时，焦点控件保留并移出可见区域，不覆盖锚点内容；清除跨度后恢复显示。两种控件模式均保留该编辑器的焦点和未提交草稿，释放焦点后恢复正常回收。

`TableSpanProvider` 与表格共用。行跨度仅在同一父节点的相邻行仍是连续可见节点时有效；不能跨折叠边界或上下冻结 pane。越过同级行或全局列末端的跨度会裁剪到现有范围；跨列 pane 的跨度裁剪到锚点所在 pane，不能覆盖该父节点缺失的列。无效跨度按普通单元格处理。`TableSpanMap` 在排序或模型结构变化后若发现两个跨度重叠，保留当前同级行列顺序中靠前的锚点并移除另一个。拖放支持节点前、节点后和放入节点；实际移动、复制仍由模型的拖放接口决定。

provider 默认由应用管理生命周期。使用 `setSpanProvider(provider, true)` 后由视图持有；替换或撤销托管 provider 时，正在执行的查询持有它直到返回，再释放旧对象。`setSpanProvider(nullptr)` 撤销提供者，是否释放旧对象取决于安装旧对象时的所有权设置。

`canStartDrag()`、`dragDropActions()` 和 `startDrag()` 会查询模型的 flags、支持动作及 MIME。若这些回调同步更换模型、改变可见行映射或销毁视图，旧请求停止，查询返回不允许拖动或 `IgnoreAction`；`startDrag()` 在创建 QDrag 前完成动作查询和 MIME 准备。应用不能缓存一次查询结果作为后续模型状态的保证。

## 辅助技术

安装组件可访问性工厂后，可见节点同时提供层级关系与表格坐标。第 0 列导出树项角色、展开状态和展开/折叠动作，保留表格单元格接口；其他列提供各自的模型值。焦点查询直接返回实际当前单元格。

Windows Qt >= 6.8 对视图聚焦时的当前节点展开/折叠发送 Announcement，供屏幕阅读器朗读状态。Qt5 和 Qt6.2 至 6.7 保留状态与动作接口，但没有该 Announcement API，不能保证同样的自动朗读表现。实际设备验收使用 Windows Qt6.11.2 讲述人，覆盖两种控件物化模式。
