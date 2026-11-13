# 12 节点间距与分割线

间距：节点下方间距可覆盖默认值，再加下一节点上方间距；分割线可从节点、图标或整行开始。

源码相对路径：`examples/tree_capabilities/12_row_spacing/example.cpp`。

`NodeRowSpacingBelowRole` 及 `NodeRowSpacingAboveRole` 写在节点第零列，移动节点后随节点保留。首节点无前置留白，末节点无后置留白；hover／selected 不覆盖行间距。分割线范围与状态背景范围独立。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 12` 进入本章。复制本目录即可使用，不依赖其他能力目录。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_row_spacing::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)

## 分组边界与整线边距

`setRowGridLineDepths({0})` 只显示主节点分组之间的分割线。展开后，线位于整组最后一个可见后代之后；不会在主节点与第一个子节点之间画线。深度相对于当前显示根，主节点为 0，子节点为 1。空集合恢复全部深度；负深度被忽略。末尾边线仍受末行自动隐藏设置控制。

`setGridLineMargins(QMargins(left, top, right, bottom))` 默认四边为 0。横线使用左右值，竖线使用上下值；正数缩短、负数伸长，绘制最终仍受视图裁剪。它修改整条线的端点，不改变项目大小、间距、命中或每个 cell 的局部分割线。外边框、冻结分隔线和拖放指示器保持各自的样式设置。

树的 NodeOnly／NodeAndIcon 先计算起点，再应用左边距。例如 NodeOnly 配合左边距 -12，会向缩进区域多画 12 个逻辑像素。

## 分割线边距参照

`setGridLineMarginReference(VirtualItemView::GridLineMarginReference::Viewport)` 为默认值，按可见区域设置整线边距。切到 `Content` 后使用完整内容端点；未滚动到右侧或底部时，屏幕边缘不会提前留出对应边距。左右控制横线，上下控制竖线；正数缩短、负数延伸，超过视图的部分正常裁剪。表头与内容使用同一组整体端点。

`setRowVisualStateMargins(left, right)` 单独调整 hover 和选中行背景的左右边距，默认均为 0；正数缩短、负数延伸。在原有状态范围计算后应用，保持控件尺寸、命中和选择范围不变。表格仅在 `SelectRows` 模式下生效；垂直表头显示且启用状态背景时，与内容合并为整行范围，边距只计算一次，圆角跟随整体端点。表头隐藏或关闭状态背景时从内容端点计算；水平表头和单元格选择模式不受影响。List／Tree 原有的 `setVisualStateBackgroundMargins` 仍可单独配置状态范围及命中。
