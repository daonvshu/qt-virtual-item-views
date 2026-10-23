# 24 统计与生命周期日志

运行诊断：滚动、展开或收起后查看物化统计与有界的 create/bind/unbind/recycle 日志。

源码相对路径：`examples/tree_capabilities/24_diagnostics/example.cpp`。

`stats()` 的物化、池、保留数是当前计数，create／bind／recycle 是累计数，不应期待滚动后归零。`lifecycleLog()` 是有界快照；`clearLifecycleLog()` 不销毁控件，也不重置累计统计。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 24` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_diagnostics::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
