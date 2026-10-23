# 04 节点结构体与绑定上下文

整节点数据：不同层级分别存结构体，bind 直接读取 rowData 和 depth。

源码相对路径：`examples/tree_capabilities/04_binding_context/example.cpp`。

每个节点自身保存一份结构体，`Q_DECLARE_METATYPE` 放在命名空间外。`rowData` 来自配置的 role，`index` 仍是节点身份，`depth` 相对于当前显示根计算，`logicalColumn` 为 -1。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 4` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_binding_context::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
