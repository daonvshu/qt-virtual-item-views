# 10 Hover 与选中背景

背景跟随按行或按格选择，可配置颜色、圆角、时长和曲线。

本节仅使用默认表头和普通单元格 adapter，状态背景由框架绘制。`setSelectionBehavior()` 同时决定 hover 和 selected 的范围：`SelectRows` 高亮整行，`SelectItems` 高亮单元格。圆角按完整行或单元格的实际边界计算，横向滚动只裁剪背景。

先看 `Example::setupExample()` 中的 `setVisualStateCornerRadius()`、`setHoverBackgroundColor()`、`setSelectedBackgroundColor()`、`setVisualStateAnimationDuration()` 和 `setVisualStateAnimationEasingCurve()`，再操作页面上的选择方式、圆角、时长和曲线控件。

源码相对路径：`examples/table_capabilities/10_visual_states/example.cpp`。表头底色、透明背景、圆角扩展、列头选择、渐变和图标示例见 [第 19 节](../19_custom_header/README.md)。

在统一程序中选择本项，或使用 `table_capabilities --example 10`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_visual_states::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
