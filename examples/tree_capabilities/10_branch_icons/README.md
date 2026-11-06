# 10 内置分支图标配置

分支图标：替换展开/收起图标，调整大小、颜色与分支格宽；隐藏图标时保留缩进。

默认图标为圆角等边三角形，边长为 10 个逻辑像素，绘制时适配当前 DPI。

默认复用一个朝下的展开三角形，收起时逆时针旋转 90° 得到朝右图标，展开时沿同一条布局动画旋转回来。只传入 `setExpandedBranchIcon(icon)` 时使用相同规则；只传入 `setCollapsedBranchIcon(icon)` 时，展开方向为该图标顺时针旋转 90°。同时设置两个非空图标则按展开状态立即切换；清空两个图标恢复内置旋转。工具栏提供这四种模式的对比。

图标旋转复用展开动画的时长、曲线和反向进度，不受 `setExpansionFadeEnabled(false)` 影响。关闭展开动画、时长设为 0 或执行不带过渡的批量操作时，图标直接到达最终角度。自定义 `BranchIndicatorRenderer` 继续自行决定各状态的绘制，框架不自动旋转。

源码相对路径：`examples/tree_capabilities/10_branch_icons/example.cpp`。

这些设置只作用于默认 renderer，图标实际大小受分支格大小限制。`setIndentation()` 设置每级分支格宽；`setBranchIndicatorsVisible(false)` 隐藏图标及图标点击区域，不移除缩进；仍可双击节点或用键盘展开。

“隐藏 branch 区域”默认不勾选；勾选时调用 `setBranchAreaVisible(false)`，去掉图标和节点自身的图标占位，保留 `depth × indentation()` 的层级缩进。取消后恢复占位和图标，已有格宽、颜色、对齐和 padding 配置保留。只需要隐藏图标、保留图标占位时，取消“显示图标”即可；若连同层级缩进一起移除，则使用 `setIndentation(0)`。

`setBranchIndicatorAlignment(Qt::Alignment)` 与 `setBranchIndicatorPadding(QMargins)`（或四参数重载）控制图标在分支格中的位置和四向留白；默认居中、padding 全为 0。点击区域仍是整个分支格。第 10 节工具栏提供分支格宽、九宫格对齐和四向 padding 输入，可实时观察；“重置对齐/留白”恢复默认布局。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 10` 进入本章。复制本目录即可使用，不依赖其他能力目录。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_branch_icons::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
