# 09 间距与分割线

行列间距、分割线颜色和粗细，以及留白里的自定义控件。

“Hover / 选中背景穿过列间距”默认勾选，对应 `setVisualStateThroughColumnSpacing(true)`。按行选择时，hover 和 selected 背景贯通内容区的横向列间距，使用同一动画进度与圆角；取消勾选后，列间距保留自身背景。按单元格选择时只高亮单元格，纵向行间距和表头间距始终不扩展状态背景。自定义间距控件若绘制不透明底色，会遮住其下方的状态背景。

源码相对路径：`examples/table_capabilities/09_spacing/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 9`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_spacing::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
