# 07 行间距和分割线

示范 setRowSpacing、分割线开关 / 线宽 / 颜色，以及自定义间距 factory + binder。工厂只负责创建，binder 完整刷新复用控件；索引是间距上方的行。线宽按物理像素处理，行高和间距使用逻辑像素。间距太小时自定义内容会被裁剪。状态延伸到间距见独立的 08 示例。

## 阅读入口

- [example.cpp](example.cpp)：本例的所有 model、adapter、行控件和页面装配。
- [example.h](example.h)：只导出本例的 `createExample()`。
- UI 布局集中在 `setupUi()`，行为配置和连接集中在 `setupExample()`。

在统一程序中选择本项，或启动时使用 `list_capabilities --example 7`。

## 复制本例独立运行

只复制当前目录，增加以下 `main.cpp`，将它与本目录 `example.cpp` 一起编译；链接 VirtualItemViews 和 Qt Widgets，无需其他示例文件：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = list_row_spacing::createExample();
    page->resize(900, 640);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[返回全部能力](../README.md)

## 分割线边距参照

`setGridLineMarginReference(VirtualItemView::GridLineMarginReference::Viewport)` 为默认值，按可见区域设置整线边距。切到 `Content` 后使用完整内容端点；未滚动到右侧或底部时，屏幕边缘不会提前留出对应边距。左右控制横线，上下控制竖线；正数缩短、负数延伸，超过视图的部分正常裁剪。表头与内容使用同一组整体端点。

`setRowVisualStateMargins(left, right)` 单独调整 hover 和选中行背景的左右边距，默认均为 0；正数缩短、负数延伸。在原有状态范围计算后应用，保持控件尺寸、命中和选择范围不变。表格仅在 `SelectRows` 模式下生效；垂直表头显示且启用状态背景时，与内容合并为整行范围，边距只计算一次，圆角跟随整体端点。表头隐藏或关闭状态背景时从内容端点计算；水平表头和单元格选择模式不受影响。List／Tree 原有的 `setVisualStateBackgroundMargins` 仍可单独配置状态范围及命中。
