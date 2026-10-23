# 13 状态背景、范围与圆角

状态背景：hover 和 selected 由框架绘制，背景范围独立于分割线，可修改颜色、圆角和曲线。

源码相对路径：`examples/tree_capabilities/13_visual_states/example.cpp`。

`NodeOnly` 从内容开始，`NodeAndIcon` 包括本行图标格，`FullWidth` 包括所有祖先缩进。仅需配置函数，无需继承 view 或在行控件自绘。动画默认 OutCubic、180 ms；圆角默认 0，此页设为 6。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 13` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_visual_states::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
