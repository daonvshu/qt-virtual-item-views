# 21 层级排序与递归过滤

代理模型：过滤后保留匹配节点的祖先，排序只改变兄弟节点顺序。

源码相对路径：`examples/tree_capabilities/21_sorting_filtering/example.cpp`。

所有 view API 及选择模型都使用 proxy index；业务修改源模型前通过 `mapToSource()` 转换。`setRecursiveFilteringEnabled(true)` 保留匹配后代的祖先；过滤后展开结果路径，避免匹配项藏在折叠分支中。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 21` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_sorting_filtering::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
