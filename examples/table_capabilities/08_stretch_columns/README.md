# 08 比例列宽与填满视图

首列固定，其余列按 1:2:1 比例分配剩余宽度。拖动任意列的边界，或调用 `setColumnWidth()` 设置任意列的具体宽度，都会立即清除全部比例因子、关闭末列拉伸，回退到普通固定列宽模式；其他列保持退出时的宽度，不再随窗口变化重新分配。原本固定的首列也适用，即使设置的宽度与当前相同，也会退出比例模式。

页面提供“设置列 3 宽度为 200”按钮演示接口设宽，以及“恢复 1:2:1 比例”按钮显式恢复比例分配。同时设置固定列宽和比例时，先设置列宽，再设置比例因子。

“拉伸末列”单独演示 `setStretchLastColumn(true)`。手动调整任意列宽度同样退出拉伸；复选框同步实际状态。

“填满视图（两侧比例调宽）”演示独立的 `setColumnResizeMode(ColumnResizeMode::FitViewport)` 模式：当前列宽作为初始比例，可见列和间距始终填满视图，无需水平滚动。在目标列内部靠近左边界拖动，由左侧可见列按按下时的宽度比例分摊；靠近右边界拖动则由右侧分摊，另一侧保持不变，最外侧边界固定。反向拖回时不会累计舍入误差。

此模式下点击“设置列 3 宽度为 200”仍保持填满，由其右侧列补偿。窗口缩放会按保留的比例重新分配；过窄、过宽时优先填满，必要时放宽最小、最大列宽限制并压缩间距。取消复选框后保留当前宽度，点击恢复比例或切换末列拉伸则回到普通模式。

源码相对路径：`examples/table_capabilities/08_stretch_columns/example.cpp`。

先看 `Example::setupExample()` 中带注释的视图配置，再看本文件的 model、adapter 和行／单元格控件。`setupUi()` 只创建页面布局。Hover 和 selected 背景由框架绘制，内容控件保持透明。

在统一程序中选择本项，或使用 `table_capabilities --example 8`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_stretch_columns::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
