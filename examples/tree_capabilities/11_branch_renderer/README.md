# 11 自定义分支渲染器

自定义分支：绘制祖先连接线和加减框，普通行控件无需绘制树装饰。

源码相对路径：`examples/tree_capabilities/11_branch_renderer/example.cpp`。

`paintBranch()` 接收 viewport 坐标和每格状态，`adjoinsItem` 区分本行格与祖先格；只有本行有子项时绘制加减图标。`setBranchIndicatorRenderer(nullptr)` 恢复默认图标。图标大小、颜色配置不改变自定义 renderer 的绘制。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 11` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_branch_renderer::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
