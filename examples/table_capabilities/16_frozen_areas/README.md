# 16 冻结行列与分隔线

左右冻结列与上下冻结行组合，滚动内容在各自区域裁剪。

勾选“显示间距”同时启用 12px 行、列间距，取消后恢复为 0，便于观察冻结边界、表头与分隔线在有无间距时的表现。

本节启用横、纵表头拖动：拖动列头或行号换序，拖动边界调整列宽或行高。模型继承 `ReorderableTableModel`，通过 `sourceRow()` 读取数据，使行号拖动放下后保留新顺序。

源码相对路径：`examples/table_capabilities/16_frozen_areas/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 16`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_frozen_areas::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
