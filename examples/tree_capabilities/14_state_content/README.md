# 14 状态关联内容

状态关联内容：仅 hover 或 selected 的节点显示操作按钮，背景仍由框架绘制。

源码相对路径：`examples/tree_capabilities/14_state_content/example.cpp`。

`visualStateChanged()` 在绑定后及状态变化时通知行控件，使用 `hovered || selected` 控制按钮显隐。`unbindWidget()` 清理辅助内容。按钮使用 `NoFocus`，点击后方向键继续用于树导航。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 14` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_state_content::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
