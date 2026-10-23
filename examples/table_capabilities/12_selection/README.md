# 12 选择与当前项

整行、单格选择与选择模式独立配置，程序选择不受鼠标选择模式限制。

行号默认支持点击选整行，列头点击选整列默认关闭。页面提供两个开关，对上下两个共享选择模型的视图同时生效。Ctrl 追加或取消，Shift 连续选择；无选择模式不响应表头选择，单选模式只保留当前行或列。

本页默认按行选择，hover 和 selected 都显示整行背景，行号条同步行 hover。切换到“按格”后，两种状态都只高亮对应单元格，便于查看列选择。只需配置 `setSelectionBehavior()`，背景范围自动跟随。完整行选中才点亮行号条，完整列选中才点亮列头；仅选中一列不会点亮所有行号。开启列头选择时点击不再排序，关闭后恢复既有排序配置。

“行号状态背景”和“列头状态背景”默认开启，对上下两个视图同时生效。通过 `setHeaderVisualStateBackgroundVisible(Qt::Vertical, visible)` 和 `setHeaderVisualStateBackgroundVisible(Qt::Horizontal, visible)` 分别控制；关闭两项后，只有主体 item 显示 hover/selected 背景，表头仍保留原样式和已启用的点击选择能力。

源码相对路径：`examples/table_capabilities/12_selection/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 12`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_selection::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
