# 30 外边框与圆角

使用框架内置的整体 Alpha 合成，让表头、单元格和滚动条统一遵循圆角外框，透明角透出外部背景。运行 `table_capabilities --example 30`；本页不依赖 SmoothScrollbar。

`example.cpp` 中的 `Example::setupView()` 创建普通 `VirtualTableView`，`applyOptions()` 直接调用公共接口，不再需要额外的外框容器或示例专属 effect。

```cpp
view->setFrameCornerRadius(20);
view->setFrameBorderWidth(2);
view->setFrameBorderColor(QColor("#4079ad"));
view->setVisualStateCornerRadius(6);
```

外框半径和边框宽度默认均为零；同时归零可关闭合成。行状态圆角独立配置。此能力继承自 `VirtualItemView`，也适用于列表和树视图。接口约束与实现说明见 [圆角外边框](../../../docs/rounded-border-proposal.md)。

页面可调外框半径、宽度、颜色及行状态圆角；背景可选纯色、渐变、棋盘格。数据可选 200 行 × 12 列、3 行 × 3 列、1 行 × 1 列及 0 行 × 3 列。还可组合显示隐藏表头、行列间距、四边冻结和滚动条策略，选择首行或末行。

少量数据时，空白视口保持统一底色，行号背景及列间距竖线不再向末行之外延伸；冻结列之间的整行状态背景连续。冻结末行和末列仍固定在视口底部与右侧。空模型不执行行选择。

末行仍绘制底边；普通行分隔线和冻结行分隔线只覆盖实际列与行号，冻结列之间的空白处不绘制横线。冻结边线连通真实列间距，底部冻结行的上边线位于行顶边外侧，使行号、单元格及空白区的状态背景顶边一致。中间空白不会命中冻结行，也不会作为该行的拖放落点。

## 独立运行

复制本目录，将 `example.cpp` 和 `example.ui` 编入目标，链接 VirtualItemViews 与 Qt Widgets，并启用 `AUTOUIC`。MSVC 使用 `/utf-8` 编译选项。

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_borders::createExample();
    page->resize(1100, 760);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
