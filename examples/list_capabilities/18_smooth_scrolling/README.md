# 18 平滑滚动（可选）

独立演示 `VirtualListView` 接入 `sscroll::SmoothScrollController`，包括开关、滚轮步长和停止当前动画。关闭控制器后由列表处理原生滚轮事件；两种模式使用相同的像素步长。

## 编译和显示条件

项目启用 `VIRTUALITEMVIEWS_BUILD_EXAMPLES`、保持 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR=ON`，并检测到与当前 Qt 主版本匹配的 `SmoothScrollbar::SmoothScrollbar` 时，才编译本目录并在左侧显示第 18 项。通过 `SmoothScrollbar_DIR` 或 `CMAKE_PREFIX_PATH` 指向已安装的包。

缺少依赖、Qt 主版本不匹配或显式关闭可选集成时，启动器只保留原来的 17 项，不编译本目录，也不会显示占位页面。

## 阅读入口

- [example.cpp](example.cpp)：本地 adapter、model 装配、控制器和参数连接。
- [example.h](example.h)：只导出本例的 `createExample()`。
- `setupUi()` 创建页面布局，`setupExample()` 接入列表和控制器，`applyStep()` 同步原生与平滑滚轮步长。

控制器由 view 持有。每个列表只需创建一个控制器，不需要改动 model、adapter 或 VirtualItemViews 框架。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 18`。

## 复制本例独立运行

只复制当前目录，将下列 `main.cpp` 与 `example.cpp` 一起编译，链接 `VirtualItemViews::VirtualItemViews`、`Qt5::Widgets`（或 `Qt6::Widgets`）以及 `SmoothScrollbar::SmoothScrollbar`，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_smooth_scrolling::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
