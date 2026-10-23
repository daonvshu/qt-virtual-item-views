# 25 整行移动动画拖动

整行 Move 与 Ctrl 复制，支持自动起拖或手柄手动起拖。

默认预览跟随选择行为：`SelectRows` 截取整行可见内容，`SelectItems` 截取起拖单元格，与行或单元格物化方式无关。自动模式与手动模式均适用；手动只传按下位置即可使用默认预览。

源码相对路径：`examples/table_capabilities/25_drag_animation/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 25`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_drag_animation::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
