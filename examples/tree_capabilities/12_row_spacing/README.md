# 12 节点间距与分割线

间距：节点下方间距可覆盖默认值，再加下一节点上方间距；分割线可从节点、图标或整行开始。

源码相对路径：`examples/tree_capabilities/12_row_spacing/example.cpp`。

`NodeRowSpacingBelowRole` 及 `NodeRowSpacingAboveRole` 写在节点第零列，移动节点后随节点保留。首节点无前置留白，末节点无后置留白；hover／selected 不覆盖行间距。分割线范围与状态背景范围独立。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 12` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

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
