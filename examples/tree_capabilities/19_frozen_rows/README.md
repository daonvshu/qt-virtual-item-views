# 19 顶部底部冻结与区域分隔线

冻结：冻结展开后最前/最后几行；它们是当前可见映射行，不是固定的模型节点身份。

源码相对路径：`examples/tree_capabilities/19_frozen_rows/example.cpp`。

冻结行始终物化，普通滚动行裁剪到自己的 pane 内。查询有效计数用 `frozenRows()`／`frozenBottomRows()`。启用冻结行时，展开动画使用淡入淡出，避免视觉内容跨冻结边界。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 19` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_frozen_rows::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
