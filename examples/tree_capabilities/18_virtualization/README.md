# 18 可见行映射与控件复用

复用：只有视口、overscan 和保留项有控件，折叠子树不创建行；可调整复用池。

源码相对路径：`examples/tree_capabilities/18_virtualization/example.cpp`。

树只有一个行物化轴，模型层级通过可见映射转换为扁平行。`setOverscan()` 控制额外物化行，池按 `WidgetType` 分开限额，`trim()` 和 `clear()` 只处理已解绑的池内控件。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 18` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_virtualization::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
