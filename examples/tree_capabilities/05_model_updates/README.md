# 05 层级模型更新

模型更新：选中节点后修改、增加子节点或删除；重置后展开状态重新建立。

源码相对路径：`examples/tree_capabilities/05_model_updates/example.cpp`。

普通数据和结构变更通过 Qt model 信号驱动，无需重新 `setModel()` 或手动刷新。节点移动及排序见第 20、21 章。删除与重置会使对应的持久索引失效。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 5` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_model_updates::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
