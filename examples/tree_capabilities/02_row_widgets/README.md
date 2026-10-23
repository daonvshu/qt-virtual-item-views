# 02 组合行控件与数据回写

组合控件：编辑名称或勾选复选框，滚动复用后数据仍来自 model。

源码相对路径：`examples/tree_capabilities/02_row_widgets/example.cpp`。

构造时只连接一次控件事件；事件发生时使用 `indexForWidget()` 查当前节点，每次绑定完整替换内容，并用 `QSignalBlocker` 防止绑定触发业务回写。编辑状态保存在模型里。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 2` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_row_widgets::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
