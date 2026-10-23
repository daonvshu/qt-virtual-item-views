# 25 平滑滚动（可选）

平滑滚动：切换控制器与原生滚轮，调整步长；仅检测到匹配的 SmoothScrollbar 时编译本章。

源码相对路径：`examples/tree_capabilities/25_smooth_scrolling/example.cpp`。

启用 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR` 并检测到与当前 Qt 主版本匹配的包时，才编译并显示第 25 章。直接复制此目录独立使用时，还需链接 `SmoothScrollbar::SmoothScrollbar`。控制器只改变滚轮运动，节点映射和复用仍由视图管理。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 25` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_smooth_scrolling::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
