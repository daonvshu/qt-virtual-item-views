# 17 多区域与独立滚动组

同一张表组合冻结区域和两个横向独立滚动区域。

将鼠标放在目标区域，使用横向滚轮、触控板横向手势或 `Shift + 滚轮`，只滚动该区域所属的组。普通滚轮仍纵向滚动，底部水平滚动条控制主组。

源码相对路径：`examples/table_capabilities/17_panes/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 17`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_panes::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
