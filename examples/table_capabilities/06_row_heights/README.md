# 06 固定与动态行高

内容换行后自动测量，显式行高可优先于测量，支持清除覆盖。

选择“动态高度”后，拖动列头边界缩窄或加宽列，行高会随文本换行自动增减。`RowWidget::sizeHint()` 根据当前 `ColumnHost` 宽度计算内容高度，框架在列几何变化后自动重新测量已物化行；屏外行在物化时测量，无需业务额外连接列宽变化信号。使用“显式优先”时，首行设为 100px 后保持该高度，清除显式高度后恢复自适应。

表头的列宽、行高拖动默认关闭。本节通过 `setColumnResizeEnabled(true)` 开启列宽拖动，行高拖动保持关闭，显式高度由页面按钮演示。

源码相对路径：`examples/table_capabilities/06_row_heights/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 6`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_row_heights::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
