# 31 自适应高度

运行 `table_capabilities --example 31`。本章演示 `VirtualTableView` 在垂直布局中随内容增高，超过独立的自适应高度上限后显示竖向滚动条。

先看 `example.cpp` 的 `Example::setupExample()` 和 `applyOptions()`：

```cpp
view->setAutoHeightEnabled(true);
view->setAutoHeightMaximumHeight(260);
view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
layout->addWidget(view);
layout->addStretch();
```

自适应默认关闭，开启后由框架计算内容高度并调用 `setFixedHeight()`，业务无需手动 resize 或遍历所有项。`setAutoHeightMaximumHeight()` 设置整个控件的独立上限，默认 0 表示不限。父布局不能压缩视图，空间不足由业务处理；关闭自适应后恢复接管前的高度约束和垂直策略。

页面的“控件最大高度”设为“不限”可测试没有显式上限的情况。“父区域上限（不压缩视图）”仅限制容器，不参与视图高度计算，空间不足由业务处理；蓝色区域展示其他控件占用的剩余空间。切换水平表头与宽列，观察高度计算包含表头和水平滚动条；本章使用 CellWidgets，行控件模式使用同一接口。行数设为 0 可观察空模型。状态栏显示实际高度、建议高度、内容高度、视口高度和竖向滚动范围。

保留默认 `ScrollBarAsNeeded`，内容能放下时滚动范围为 0；内容超出实际视口后才滚动。开启期间通过 `setAutoHeightMaximumHeight()` 配置上限，不要使用 QWidget 的最小、最大或固定高度接口。动态行高沿用框架的估计与物化测量反馈，不会为了计算总高度创建所有行控件。

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
    QWidget *page = table_auto_height::createExample();
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

- 保持自适应高度开启，减少数据使内部竖向滚动范围为 0，在项目、空白区域上滚动，应移动外层内容。带表头的视图也可把鼠标放在水平、垂直表头上观察。
- 增加数据，使内容超过最大高度，再把鼠标放在视图上滚动，应滚动内部项目。
- “父区域上限”仅限制 `previewHost`，不会压缩视图；空间不足需要业务处理。自适应高度上限设为 0 时完整展示内容，外层视口不作为上限。

外层预留内容用于演示嵌套滚动，业务中替换为自己的其他控件即可。本章节保持独立，不依赖其他示例代码。

外层内容容器不设置固定最小高度；额外的 500px 留白由布局 spacer 提供，用来演示其他表单内容。视图自身按内容固定高度，`setAutoHeightMaximumHeight()` 限制其总高度，0 表示不限；不依赖祖先容器类型或可用空间。
