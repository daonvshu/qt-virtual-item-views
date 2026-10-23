# 07 展开收起与过渡动画

展开：当前节点可展开、收起、切换或递归展开。方向键右/左和 * 也可操作。

源码相对路径：`examples/tree_capabilities/07_expansion/example.cpp`。

组件默认关闭展开动画，此页显式开启。`expandRecursively()` 展开当前节点及其后代，`collapseAll()` 收起全部分支。`expanded`／`collapsed` 信号反映逻辑状态立即改变；时长为 0 时没有过渡。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 7` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_expansion::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
