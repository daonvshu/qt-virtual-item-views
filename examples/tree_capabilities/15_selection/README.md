# 15 选择、当前项与键盘导航

选择：节点身份和当前项独立；程序选择可跨父节点，扩展选择支持 Ctrl/Shift。

源码相对路径：`examples/tree_capabilities/15_selection/example.cpp`。

`SelectionMode` 约束鼠标和键盘选择手势，不限制应用直接操作 `QItemSelectionModel`。每个父节点内的 `QItemSelection` 范围分别构建。上下键按可见节点导航，右方向键展开或进入子节点，左方向键收起或回到父节点。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 15` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_selection::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
