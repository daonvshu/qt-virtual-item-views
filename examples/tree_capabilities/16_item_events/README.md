# 16 行事件、正确索引与几何查询

行事件：按钮使用每次绑定更新的持久索引；查询屏外项的索引、控件和几何各不相同。

源码相对路径：`examples/tree_capabilities/16_item_events/example.cpp`。

`QPersistentModelIndex` 跟随模型行移动，不会自动跟随控件复用；必须每次 bind 替换、unbind 清空。另一种写法是第 14 章的 `indexForWidget()`。`widgetForIndex()` 仅查询，未物化时返回空；几何可能位于屏外，命中也可能为空，选择身份仍从选择模型读取。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 16` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_item_events::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
