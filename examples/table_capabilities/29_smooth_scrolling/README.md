# 29 平滑滚动（可选）

检测到匹配的 SmoothScrollbar 后才编译和显示，开关对比原生双向滚动。

源码相对路径：`examples/table_capabilities/29_smooth_scrolling/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 29`。

通过 `SmoothScrollbar_DIR` 或 `CMAKE_PREFIX_PATH` 提供匹配 Qt 主版本的包；项目需保持 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR=ON`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews、Qt Widgets 和 `SmoothScrollbar::SmoothScrollbar`：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_smooth_scrolling::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
