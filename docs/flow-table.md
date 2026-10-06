# 流式表格：随宽度动态换列

`VirtualFlowTableView` 将根索引下的一维项目按从左到右、从上到下排列。显示列数由视口宽度决定；每个真实项目对应一个 `WidgetAdapter` 创建的 QWidget。它继承 `VirtualItemView`，独立于固定业务列的 `VirtualTableView`。

## 最小用法

下面的 adapter 用标签显示模型第 0 列。模型、adapter 的生命周期须覆盖视图的使用期；完整生命周期约定见 [入门](getting-started.md)。

```cpp
#include <virtualitemviews/virtualflowtableview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>

class CardAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setAlignment(Qt::AlignCenter);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        return label;
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(150, 100);
    }
};

// 按此顺序声明，确保视图先于 adapter 和模型销毁。
QStandardItemModel model;
for (int i = 0; i < 1000; ++i)
    model.appendRow(new QStandardItem(QStringLiteral("项目 %1").arg(i)));
CardAdapter adapter;
viv::VirtualFlowTableView view;
view.setAdapter(&adapter);
view.setModel(&model);
view.setCellWidthRange(100, 200);
view.setFixedCellHeight(100);
view.resize(800, 600);
view.show();
```

`estimatedSize()` 仍是 adapter 的必需接口，但 Flow 的布局高度取下面的统一配置，不使用业务控件的 sizeHint 测高。业务控件可以包含编辑器、按钮等子控件；只有纯展示控件才应设为鼠标透明。内置背景显示在业务控件背后，不透明控件会遮住背景。

## 宽度分配与末行

默认目标范围为 100～200 个逻辑像素。扣除内容边距和垂直滚动条后的宽度记为 `W`，列间距记为 `g`。通常列数为 `ceil((W + g) / (maximum + g))`，至少一列；扣除列间距后剩余宽度均分，整数余数从左列开始分配，列宽最多相差一个逻辑像素。

无间距时，400px 分为两列各 200px；401px 分为三列 134、134、133px。加列后全部重新分配，不能保证精确回到 100px。最小宽度是目标下限，不是强制产生水平滚动的限制；窄视口或不兼容的范围允许低于最小值。极大列间距使期望列数无法容纳正宽度单元格时，列数退回可容纳的数量，此时允许超过最大宽度。优先保证正宽度和横向精确分配。

所有显示行共用列位置和列宽，列数不受项目数量限制。10 项、4 列时，最后一行保留两个空缺位置，已有项目沿用相同列宽；空缺位置不创建控件、不返回模型索引。宽度为零时没有可绘制项目区域。

```cpp
view.setContentMargins(QMargins(8, 8, 8, 8));
view.setColumnSpacing(8);
view.setRowSpacing(8);
```

宽度、尺寸、边距和间距使用逻辑像素。宽度下限和固定高度至少为 1，最大宽度至少等于下限；负边距、负间距按 0 处理。

## 高度：固定或按比例

```cpp
view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::Fixed);
view.setFixedCellHeight(120);

// 切换为宽 / 高 = 1.5；高度由每列实际分配宽度计算。
view.setCellAspectRatio(1.5);
view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
```

默认固定高度 100，默认宽高比 1。比例必须为有限正数，否则保留原值。比例高度舍入到整数，至少 1，并对极大值设上限。每个显示行顶部对齐，以最宽列的格子高度推进；余数导致的个别高度差不会改变下一行位置。末行之后不加行间距。

**铺满仅约束横向列分配。** 窗口高度不参与格子拉伸，内容不足一屏时允许底部留白；不做纵向均分或瀑布流。

## 模型身份、选择与编辑器

默认显示模型顶层项目，也可以调用 `setRootIndex(index)` 显示某个根的直接子项；每项身份始终使用源模型第 0 列。其他业务字段可以由 adapter 查询，不会自动将模型的二维格子展平。

resize、排序、插删和移动可能改变项目的显示行列。业务回调应保存 `QPersistentModelIndex` 并在提交前检查有效性，不能保存显示行列号作为业务身份。每次 `bindWidget()` 完整刷新控件；`unbindWidget()` 停止旧项目的定时器、订阅及异步活动。模型 reset 会使旧持久索引失效，需要按业务 ID 另行恢复状态。

选择按真实项目处理。左右键移动相邻项目，上下键保持显示列换行，末行缺项钳制到最后真实项目；PageUp/PageDown 按显示行推进。Home/End、Shift/Ctrl 选择沿用共享交互。首版没有独立的视觉整行/整列选择能力。

`scrollTo(index)`、当前项、选择和 `setItemPinned(index)` 都使用源模型索引。重排保存顶部项目及偏移，恢复后钳制滚动位置；锚点删除时使用相邻有效项目。可见区与预加载区之外的普通控件会回收，焦点、输入法、弹窗和拖放的临时保护以及显式 pin 使用共享内核。真实系统输入法、弹窗和拖放在重排中的平台表现仍需原生人工验收。

拖放启用和模型实现见 [交互](interaction.md)。Flow 将目标转为根下的源模型插入位置：格子左/右半分别插入前/后，列间隙插入下一个项目之前，行间隙插入下一显示行之前，末行空缺或内容下方追加。边距外不接受目标。最终由模型的 `canDropMimeData()` / `dropMimeData()` 决定接受与否。

## 背景、圆角和分割线

```cpp
view.setVisualStateBackgroundVisible(true);
view.setStateBackgroundCornerRadius(8);
view.setHoverBackgroundThroughRowSpacing(false);
view.setSelectedBackgroundThroughRowSpacing(false);
view.setHoverBackgroundThroughColumnSpacing(true);
view.setSelectedBackgroundThroughColumnSpacing(true);
view.setVisualStateAnimationDuration(180);

view.setVerticalGridLinesVisible(true);
view.setHorizontalGridLinesVisible(true);
view.setVerticalGridLineWidth(1);
view.setHorizontalGridLineWidth(1);
view.setVerticalGridLineColor(QColor("#808080"));
view.setHorizontalGridLineColor(QColor()); // 恢复 palette Mid
view.setVerticalSpacingLineThroughRowSpacing(true);
view.setHorizontalSpacingLineThroughColumnSpacing(true);
```

默认显示内置背景，圆角为 0；hover/select 各自默认不覆盖行间距、覆盖列间距。行间距归属前一显示行，列间距归属前一个真实项目；缺项位置不继承状态。圆角应用于含已覆盖间距的完整形状。

横竖分割线默认显示、宽度为 1 个物理像素，最小为 1，默认穿过另一方向的间距。非零行间距上下、列间距左右均有边线；零间距共享一条分割线。分割线在业务控件上方绘制，不添加外框或空缺格边框。线宽与格子几何单位不同：例如 125% 缩放时仍画指定物理像素线宽，不扩大格子。

## 查询布局与可访问性

| 接口 | 含义 |
| --- | --- |
| `columnCount()` / `displayRowCount()` | 当前显示列数、显示行数 |
| `columnWidth(column)` / `columnViewportX(column)` | 当前列宽与视口横坐标 |
| `displayRowForIndex(index)` / `displayColumnForIndex(index)` | 源项目在当前布局的位置，非法项目返回 -1 |
| `visualRect(index)` / `indexAt(point)` | 项目视口矩形与命中；间距、缺项不命中 |
| `flowLayoutChanged()` | 已提交的宽度、列几何、行高度或数量发生变化 |

程序开始时调用 `viv::installAccessibilityFactory()`，头文件为 `<virtualitemviews/accessibility.h>`。视图暴露 Table、真实项目暴露 Cell；辅助节点身份绑定源项目，显示行列随重排更新。辅助接口不创建末行空缺节点，整显示行/列选择动作不支持。Qt5/Qt6 均已有用户确认的讲述人名称播报和键盘导航结果；Qt5 离屏只验证接口映射，原生操作由人工结果补充，不表示所有 UIA 事件类型均已单独验收。

Windows 讲述人在手动调整窗口宽度后，导航框可能保留原位置，按方向键后才更新。此项保留为已知限制；当前源项目、选择和查询到的辅助矩形保持正确。重排只发送位置更新，不通过切换焦点、重复选择或更换辅助节点身份强制移动讲述人的框。

## 边界与示例

使用专用 `setFixedCellHeight()` / `setCellAspectRatio()`，继承的 `setItemHeightMode()`、`setUniformItemHeight()`、`setEstimatedItemHeight()` 和自动测高启用不适用于 Flow，会给出警告。冻结行和自定义行间距控件同样不支持。固定业务列表头、隐藏/冻结/拖动列、合并格、树展开不属于 Flow API。

布局使用常量存储，不缓存全部项目矩形；重排只更新布局算式与可见控件，不遍历全模型。物化数量主要随可见范围、预加载和 pin 增长，业务控件自身的耗时仍由应用承担。

示例目标 `flow_table_view` 支持 `--items N`（默认 10000），提供宽度范围、高度模式、行列间距、背景覆盖和分割线控制；`--accessibility-trace` 输出当前项目与辅助几何诊断。当前 Qt5/Qt6 定向验证与原生人工验收分开记录，原生未测项目不视为通过。
