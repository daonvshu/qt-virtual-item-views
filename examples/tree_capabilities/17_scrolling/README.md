# 17 大树滚动、定位与可见映射

滚动：模型按需提供百万节点，每组 20 万叶子；展开路径后再定位到目标节点。

源码相对路径：`examples/tree_capabilities/17_scrolling/example.cpp`。

仅大数据模型继承 `QAbstractItemModel`，不为每条记录分配对象。`visibleRowCount()` 是展开后参与布局的节点数，不是屏幕行数；`visibleItemRange()` 才是当前窗口。纵向像素偏移使用 64 位值，先展开祖先再 `scrollTo()`。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 17` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_scrolling::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
