# 21 自适应高度

运行 `list_capabilities --example 21`。本章演示 `VirtualListView` 在垂直布局中随内容增高，达到控件最大高度或父布局的可用空间后显示竖向滚动条。

先看 `example.cpp` 的 `Example::setupExample()` 和 `applyOptions()`：

```cpp
view->setAutoHeightEnabled(true);
view->setMaximumHeight(260);
view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
layout->addWidget(view);
layout->addStretch();
```

自适应默认关闭，开启后由框架更新高度建议与垂直尺寸策略，业务无需设置固定高度、监听数据变化后手动 resize 或遍历所有项。最大高度沿用 QWidget 接口，`setMaximumHeight(QWIDGETSIZE_MAX)` 取消显式上限；父布局仍可按实际剩余空间缩小视图。

页面的“控件最大高度”设为“不限”可测试没有显式上限的情况。“父区域高度上限”限制承载垂直布局的容器；蓝色区域展示其他控件占用的剩余空间。修改行数、行高和行间距，观察增删数据后的实际高度。行数设为 0 可观察空模型。状态栏显示实际高度、建议高度、内容高度、视口高度和竖向滚动范围。

保留默认 `ScrollBarAsNeeded`，内容能放下时滚动范围为 0；内容超出实际视口后才滚动。不要同时设置固定高度或不可收缩的最小高度，否则这些显式约束仍优先。动态行高沿用框架的估计与物化测量反馈，不会为了计算总高度创建所有行控件。

“自动隐藏末行分割线”默认勾选。取消后调用 `setTrailingRowGridLineAutoHideEnabled(false)`，在行分割线启用时始终保留可见的末行线，表格的默认垂直表头和列间距也同步。恢复勾选后，自适应高度或存在实际竖向滚动范围时隐藏末行线；强制显示滚动条但范围为 0 不单独触发隐藏。

本目录自带 model、adapter 和 Designer 布局，不引用其他示例代码。四种视图的共同约束见 [自适应高度](../../../docs/auto-height.md)。

## 独立运行

将本目录的 `example.cpp`、`example.h` 和 `auto_height.ui` 加入目标，链接 VirtualItemViews 与 Qt Widgets，启用 `AUTOUIC`。MSVC 使用 `/utf-8`。

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_auto_height::createExample();
    page->resize(1000, 760);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部功能](../README.md)

## 嵌入外层滚动容器

`auto_height.ui` 将视图放在 `QScrollArea` 的内容布局中，并启用 `widgetResizable`。下方留有外层内容和底部标记，可以直接观察滚轮实际滚动的是哪一层；无需添加事件过滤器或手动转发滚轮。

- 保持自适应高度开启，减少数据使内部竖向滚动范围为 0，在项目、空白区域上滚动，应移动外层内容。
- 增加数据，使内容超过最大高度，再把鼠标放在视图上滚动，应滚动内部项目。
- 存在“父区域高度上限”时，它限制 `previewHost`；值为 0 表示不额外限制。取消显式上限后仍由内容布局分配高度，外层视口高度不会直接作为视图高度上限。

外层预留内容用于演示嵌套滚动，业务中替换为自己的其他控件即可。本章节保持独立，不依赖其他示例代码。

外层内容容器不设置固定最小高度；额外的 500px 留白由布局 spacer 提供，用来演示其他表单内容。视图位于可调整大小的外层滚动区且没有中间高度上限时，框架让内容建议高度参与布局最小高度计算，避免被压到只剩表头；内部最大高度设置仍然有效。
