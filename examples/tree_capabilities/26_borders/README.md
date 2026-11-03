# 26 外边框与圆角

运行 `tree_capabilities --example 26`。本章不依赖 SmoothScrollbar，演示普通 `VirtualTreeView` 通过框架公共接口设置整体圆角。

先看 `example.cpp` 的 `Example::setupExample()` 与 `applyOptions()`。树节点是透明 `QLabel`，分支图标、行状态和外框均由框架处理：

```cpp
view->setFrameCornerRadius(20);
view->setFrameBorderWidth(2);
view->setFrameBorderColor(QColor("#4079ad"));
view->setVisualStateCornerRadius(6);
```

外框接口继承自 `VirtualItemView`，使用逻辑像素，将节点、branch 和滚动条统一合成。透明角显示父控件背景；状态圆角仅作用于 hover／selected 背景，与外框半径独立。无需继承树视图或自行裁剪行控件。

可调整外框半径、宽度、颜色和行状态圆角。“调色板默认”传入无效 `QColor`，恢复 `QPalette::Mid`。半径与边框宽度同时设为零会移除框架合成效果并恢复原有 QFrame 样式；边框宽度为零、半径非零时仍保留透明圆角。

父背景可选纯色、渐变和棋盘格。`BackgroundStage` 只为演示透明角提供背景与留白，业务接入不需要这个容器。

数据可选多层大树、一个分组加两个子节点、单个根节点和空模型。切换数据时直接更新 `QStandardItemModel`，暂时关闭展开动画以准备初始层级；随后保留正常展开收起动画。可组合行间距、分割线、分支区域显隐及竖向滚动条；取消强制滚动条后使用 `ScrollBarAsNeeded`。隐藏分支区域仍保留层级缩进，递归展开和全部收起按钮仍可操作。

“选中首个／末个可见节点”从 `visibilityIndex()` 的可见映射取索引，再更新选择模型和调用 `scrollTo()`，不会把子节点的 `row()` 当成整棵树的行号。空树不执行选择。树不提供冻结行，本章只演示实际支持的树布局。

本目录自带层级 model、adapter、背景容器和页面布局，不引用其他示例。接口约束见 [圆角外边框](../../../docs/rounded-border-proposal.md)。

## 独立运行

将本目录的 `example.cpp`、`example.h` 和 `example.ui` 加入目标，链接 VirtualItemViews 与 Qt Widgets，并启用 `AUTOUIC`。MSVC 使用 `/utf-8` 编译选项。

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_borders::createExample();
    page->resize(1000, 760);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
