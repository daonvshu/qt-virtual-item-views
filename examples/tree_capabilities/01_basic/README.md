# 01 基础接入

基础树：模型保存层级，adapter 只处理行控件；点击三角展开子节点。

源码相对路径：`examples/tree_capabilities/01_basic/example.cpp`。

`setAdapter(..., true)` 交给视图管理 adapter，模型通过 QObject 父对象管理。`setModel()` 接入后只创建当前展开路径中的可见行与 overscan 行，折叠的后代不创建控件。

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 1` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_basic::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
