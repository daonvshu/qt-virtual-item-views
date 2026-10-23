# 08 切换显示根与相对深度

显示根：切换整棵树或仅显示某个分组的子树；显示根本身不作为一行。

源码相对路径：`examples/tree_capabilities/08_root_index/example.cpp`。

`setRootIndex()` 改变显示范围，仍使用原模型索引。显示根的直接子节点深度为 0；同一个节点的模型 `row()`、可见映射行和深度不是一回事。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 8` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_root_index::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
