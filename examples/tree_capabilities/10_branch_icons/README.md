# 10 内置分支图标配置

分支图标：替换展开/收起图标，调整大小与颜色；隐藏图标时保留缩进。

源码相对路径：`examples/tree_capabilities/10_branch_icons/example.cpp`。

这些设置只作用于默认 renderer，图标实际大小受分支格大小限制。`setBranchIndicatorsVisible(false)` 隐藏图标及图标点击区域，不移除缩进；仍可双击节点或用键盘展开。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 10` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

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
