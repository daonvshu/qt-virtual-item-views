# 09 间距与分割线

行列间距、分割线颜色和粗细，以及留白里的自定义控件。

默认使用 `setGridLinePlacement(viv::GridLinePlacement::SpacingCenter)`，在非零间距中心绘制一条分割线，并始终贯通交叉方向的间距。切换为 `SpacingEdges` 后，在间距两侧绘制分割线，才可以用 `setVerticalSpacingLineThroughRowSpacing()` 和 `setHorizontalSpacingLineThroughColumnSpacing()` 设置是否贯通；中心模式忽略这两个设置，查询始终返回 `true`。任一方向间距为 0 时，两种模式都回退为相邻项共享的一条边界线。

表格和树表格在中心模式下，自动在内容区四周各补半个对应方向的间距，让首尾行、首尾列的控件与内部单元格一样位于分割线之间。留白计入内容尺寸、滚动范围和列宽铺满计算，表头同步对齐；相邻冻结分区之间的间距拆成两半，分别放在各自分区内，总间距保持不变。奇数间距由前侧取整、后侧补齐剩余像素。切回两侧模式或把对应方向间距设为 0 时，外侧留白随之取消。

未指定间距颜色时，内容区与表头间距统一使用 `QPalette::Base`，四周半间距与内部间距保持一致。可分别调用 `horizontalHeader()->setSpacingBackgroundColor()`、`verticalHeader()->setSpacingBackgroundColor()` 设置表头间距颜色，或传入 `QColor()` 恢复默认；显式配置表头底色后，未指定颜色的表头间距跟随该底色。

`setVerticalGridLinesThroughHeader()` 控制竖线是否绘制到水平表头，`setHorizontalGridLinesThroughHeader()` 控制横线是否绘制到垂直表头，两者默认启用，适用于两种模式及冻结表头区域。关闭后仍保留表头与内容区的边界线，只隐藏表头内部的网格线；自定义表头绘制仍由业务负责。

`setGridLinePlacement()` 同样适用于列表、树、树表格、流式网格和瀑布流视图；列表和树只绘制行分割线。

“Hover / 选中背景穿过列间距”默认勾选，对应 `setVisualStateThroughColumnSpacing(true)`。按行选择时，hover 和 selected 背景贯通内容区的横向列间距，使用同一动画进度与圆角；取消勾选后，列间距保留自身背景。按单元格选择时只高亮对应单元格的宽度。

“状态延伸到上下半行间距”默认勾选，对应 `setVisualStateThroughRowSpacing(true)`，仅在间距中心模式生效。每行状态向上、向下延伸到相邻间距的中心，默认行号表头同步扩展，保留动画、圆角和自定义状态画刷；关闭后仅覆盖项目本身。合并单元格只延伸其外侧行间距，冻结行之间保持各自区域。自定义间距控件若绘制不透明底色，会遮住其下方的状态背景。

本节启用列宽和行高拖动。中心模式下，调整尺寸的鼠标提示与命中区域跟随间距中心线；两侧模式及零间距保留项目边界上的命中方式。

鼠标位于间距或表头区域时，未被自定义控件处理的滚轮事件继续交给视图滚动；横向滚轮和 Shift + 滚轮沿用鼠标所在分区的横向滚动规则。业务无需为间距单独安装滚轮事件过滤器。

“冻结左右列（各一列）”默认关闭，勾选后使用 `setFrozenColumns({0})` 和 `setFrozenRightColumns({model->columnCount() - 1})` 固定首尾列，方便检查两侧半间距、冻结分隔线和表头对齐。

“冻结上下行（各一行）”使用 `setFrozenRows(1)` 和 `setFrozenBottomRows(1)` 固定首尾行。两种模式下，行、列冻结边界两侧均各保留半个间距，中间绘制冻结分隔线；两侧模式还在各半间距靠近单元格的一端绘制普通网格线，中心模式不增加这些线。零间距不补普通线，表头同步对齐，普通网格线是否贯通交叉间距、是否穿过表头仍遵循对应开关。行、列冻结可以同时启用，也可以与少量数据组合观察中间留白。

本节通过 `setPaneSeparatorStyle()` 将行、列冻结分隔线统一设置为 2 物理像素宽的红色虚线，与普通网格线区分。修改普通网格线颜色和线宽不会改变冻结分隔线的显式样式。

少量数据使可滚动内容提前结束时，在末行下方、末列右侧各保留半个间距并绘制收边线，表头同步收边；内容末端到冻结区域之间的空白不绘制网格线。内容末端与冻结边界重合时不再额外绘制收边线。

“少量数据（4 行 × 3 列）”默认关闭，勾选后从 120 行、8 列切换为 4 行、3 列，取消后重新补齐数据。默认窗口尺寸和列宽下，少量数据不会出现滚动条，可观察内容尾端留白及最后一行、最后一列的边界；仍使用正常的按需滚动条策略，缩小窗口或增大行高、列宽后可自然出现滚动条。两项可以同时勾选，右侧冻结列会随列数变化更新为当前最后一列。

源码相对路径：`examples/table_capabilities/09_spacing/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 9`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_spacing::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
