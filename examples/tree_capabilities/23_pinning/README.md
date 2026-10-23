# 23 屏外保留与编辑生命周期

屏外保留：选择节点后保留控件，再滚出屏幕查询实例编号；数据仍应写回 model。

源码相对路径：`examples/tree_capabilities/23_pinning/example.cpp`。

保留针对节点身份，供临时编辑、弹窗或异步活动使用，不是数据持久化。每次保留都应有对应释放。pin 不会替应用展开祖先，折叠隐藏与屏外滚动是两种情况。实例编号显示在查询结果中，便于观察复用。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 23` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_pinning::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
