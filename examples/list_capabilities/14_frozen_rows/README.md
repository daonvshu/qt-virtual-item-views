# 14 冻结行

setFrozenRows 固定前 N 行，setFrozenBottomRows 固定后 N 行。PaneSeparatorStyle 配置宽度、颜色、线型；宽度为 0 隐藏区域分隔线。冻结行同时保留普通 hover / selection。有效冻结数受视口高度限制；visibleItemRanges 可查询冻结区与滚动区，单个 visibleItemRange 仅描述滚动窗口。

分隔线宽度同时控制实线、虚线和点线的实际粗细，单位为物理像素。普通行分割线与冻结分隔线重叠时，由冻结分隔线统一呈现，避免虚线下方露出额外灰线；宽度设为 0 后恢复普通行分割线。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 14`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_frozen_rows::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
