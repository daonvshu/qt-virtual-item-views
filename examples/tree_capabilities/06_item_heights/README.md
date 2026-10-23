# 06 固定、首项测量与动态行高

行高：点击节点切换详情高度；切换固定、首项测量、独立动态高度。

源码相对路径：`examples/tree_capabilities/06_item_heights/example.cpp`。

动态高度必须同时设置 `Variable` 和 `setAutoMeasureItemHeight(true)`。初始节点已有不同的详情高度；详情改变只更新 model，测量与锚点补偿由视图处理。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 6` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_item_heights::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
