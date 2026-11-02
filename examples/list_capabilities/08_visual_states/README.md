# 08 Hover 和选中状态

框架默认绘制 hover 和 selected 背景，adapter 只绑定透明标签文本。使用 `setVisualStateCornerRadius()`、颜色和动画接口即可配置外观，无需重写 paintEvent 或 visualStateChanged。状态背景只在 item 内绘制，行间距保持中性；动画时长为 0 时立即切换。

圆角控件可在 0–24 个逻辑像素之间调节，0 为直角；同一个半径作用于 hover 和 selected 背景。

范围下拉框选择 `FullWidth`（默认整行）或 `WidgetOnly`（只绘制实际行控件矩形）。调用 `setVisualStateBackgroundExtent()` 配置，查询用 `visualStateBackgroundExtent()`。本页每次 bind 按文本的 `sizeHint()` 设置控件最大宽度，方便对比；若业务控件本身铺满整行，两种模式看起来会相同。示例使用默认零行间距，避免与状态背景边距混淆。

`setVisualStateBackgroundMargins()` 设置四向内边距，`setVisualStateBackgroundHeight()` 设置固定高度，单位均为逻辑像素。先扣除边距，再将固定高度垂直居中，并限制在剩余高度内；高度 `0` 为自动，负值归零。边距耗尽可用宽高时背景与命中均为空。配置同时作用于 hover 和 selected，不改变行高和业务控件大小：

```cpp
view->setVisualStateBackgroundExtent(viv::VirtualListView::VisualStateBackgroundExtent::WidgetOnly);
view->setVisualStateBackgroundMargins(0, 4, 0, 4);
view->setVisualStateBackgroundHeight(24);
// Restore automatic height while retaining the margins.
view->setVisualStateBackgroundHeight(0);
```

鼠标命中与调整后的背景矩形一致，并裁剪到所在滚动／冻结区域：矩形外不产生该行 hover、点击选择或双击。修改配置会按当前鼠标位置立即更新 hover；关闭背景绘制仍保留命中范围。键盘导航、程序选择及业务控件事件保持原有行为。

hover 身份变化时框架立即更新动画目标，不依赖绘制或 adapter 查询状态。快速离开时按实际经过的时间计算进度，再从当前位置淡出。

`setVisualStateAnimationEasingCurve()` 配置 hover 和 selected 共用的缓动曲线，getter 为 `visualStateAnimationEasingCurve()`。框架和本例默认使用起步更快的 `OutCubic`、时长 180 ms，下拉框可切换 `OutCubic`、`InOutCubic`、`Linear`，与时长控件配合比较快速扫过时的效果。修改曲线会重置当前过渡，与修改动画时长的行为一致。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 8`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_visual_states::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)
