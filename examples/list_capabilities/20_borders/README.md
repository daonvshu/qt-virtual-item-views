# 20 外边框与圆角

运行 `list_capabilities --example 20`。本章不依赖 SmoothScrollbar，演示普通 `VirtualListView` 直接使用框架外框接口。

先看 `example.cpp` 的 `Example::setupExample()` 与 `applyOptions()`。外框、行背景和分割线均由框架绘制，行控件只是透明 `QLabel`：

```cpp
view->setFrameCornerRadius(20);
view->setFrameBorderWidth(2);
view->setFrameBorderColor(QColor("#4079ad"));
view->setVisualStateCornerRadius(6);
```

前三个接口继承自 `VirtualItemView`，单位为逻辑像素。外框把内容和滚动条一起合成，透明角显示父控件背景。`setVisualStateCornerRadius()` 独立控制 hover／selected 的行背景，不改变外框。无需继承视图或重写绘制函数。

页面支持调整外框半径、边框宽度、颜色和行状态圆角；“调色板默认”传入无效 `QColor`，恢复 `QPalette::Mid`。外框半径与宽度同时归零时移除框架合成效果并恢复原有 QFrame 样式；只将宽度归零则保留透明圆角。

可切换纯色、渐变和棋盘格背景，观察四角透明效果。`BackgroundStage` 只负责提供演示背景与留白，业务使用外框接口不需要这个容器。

数据可选 200 行、3 行、1 行和空模型，还可组合行间距、分割线、首末冻结行和竖向滚动条。取消“强制显示竖向滚动条”后使用 `ScrollBarAsNeeded`，少量数据不显示滚动条。“选中首行／末行”通过选择模型和 `scrollTo()` 定位边缘；空模型不执行选择。

本目录自带 model、adapter、背景容器和页面布局，不引用其他示例代码。接口约束见 [圆角外边框](../../../docs/rounded-border-proposal.md)。

## 独立运行

将本目录的 `example.cpp`、`example.h` 和 `example.ui` 加入目标，链接 VirtualItemViews 与 Qt Widgets，并启用 `AUTOUIC`。MSVC 使用 `/utf-8` 编译选项。

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_borders::createExample();
    page->resize(1000, 760);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部列表功能](../README.md)
