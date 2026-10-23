# 22 业务驱动的延迟加载

延迟加载：展开尚未加载的分组后模拟异步请求；请求结束通过 model 插入子节点。

源码相对路径：`examples/tree_capabilities/22_lazy_loading/example.cpp`。

这是业务通过 `expanded` 信号发起请求的示例，框架不会自动调用 `fetchMore()`。占位子项让分组可展开，防重复请求由 model role 控制；持久索引和 QObject 上下文保护延迟回调，结果必须在 GUI 线程写回模型。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 22` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_lazy_loading::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
