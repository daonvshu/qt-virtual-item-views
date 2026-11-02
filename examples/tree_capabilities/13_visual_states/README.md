# 13 状态背景、范围与圆角

状态背景：hover 和 selected 由框架绘制，背景范围独立于分割线，可修改颜色、圆角和曲线。

源码相对路径：`examples/tree_capabilities/13_visual_states/example.cpp`。

`NodeOnly` 从内容开始，`NodeAndIcon` 在本节点有可见 branch 图标时包括本行图标格，叶子节点或隐藏图标时自动按 `NodeOnly` 绘制和命中，不覆盖空的 branch 占位；两者右侧均延伸至视口边缘。`FullWidth` 包括所有祖先缩进。仅需配置函数，无需继承 view 或在行控件自绘。动画默认 OutCubic、180 ms；圆角默认 0，此页设为 6。

范围下拉框统一提供五种模式，默认选择“整行”。选择“仅控件 (WidgetOnly)”调用 `setVisualStateBackgroundExtent(VisualStateBackgroundExtent::WidgetOnly)`，hover／selected 背景只覆盖节点 QWidget 的实际矩形（包含控件自身 margin），不延伸到整行，也不覆盖 branch。本页在每次 bind 时用 `setMaximumWidth(sizeHint().width())` 让节点控件按文字宽度收窄，便于对比；如果业务控件本身铺满整行，两种模式看起来会相同。

选择“控件及图标 (WidgetAndIcon)”切换为 `VisualStateBackgroundExtent::WidgetAndIcon`：有展开／收起图标的节点，背景和命中矩形包含控件及本节点自身的 branch 格，右边界仍跟随控件，不覆盖祖先缩进。无子节点的叶子、隐藏 branch 图标或隐藏 branch 区域时，范围自动按 `WidgetOnly` 处理；即使叶子仍有 branch 占位，也不绘制该空占位的状态背景。

鼠标命中与背景矩形一致，并裁剪到节点所在的可见滚动／冻结区域：移到矩形外会退出 hover，点击空白不会选中该节点，双击空白也不会展开。`NodeOnly`、`NodeAndIcon` 同样按各自背景范围命中；branch 图标仍可独立点击展开收起，不触发节点选择。切换范围会按当前鼠标位置立即更新 hover。关闭背景绘制仍保留配置的命中范围；键盘导航及程序选择不受影响。

“背景高度”和四向边距可组合使用，对 hover 和 selected 统一生效，单位为逻辑像素：先在所选范围内扣除边距，再将固定高度垂直居中。高度为 `0` 时使用扣除边距后的全部高度；固定高度超过剩余高度时会自动限制。边距和高度的负值均按 `0` 处理；边距耗尽可用宽高时，背景与节点鼠标命中均为空。控件大小、行高及 branch 的独立展开点击不变，命中始终跟随调整后的背景矩形。

例如本页行高为 36，设置上下边距各 4，可得到 28 高的状态背景；也可以指定固定高度 24：

```cpp
view->setVisualStateBackgroundMargins(0, 4, 0, 4);
view->setVisualStateBackgroundHeight(24);
// Restore automatic height while retaining the margins.
view->setVisualStateBackgroundHeight(0);
```

先看 `Example::setupExample()` 中的视图配置和信号连接，再看本目录的 adapter、model 和节点控件。`setupUi()` 仅创建页面布局，hover／selected 背景由框架绘制。

运行 `tree_capabilities --example 13` 进入本章。复制本目录即可使用，不依赖其他能力目录或 `examples/common`。独立入口如下，将 `example.cpp` 一起编译并链接 VirtualItemViews 与 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = tree_visual_states::createExample();
    page->resize(980, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部树功能](../README.md)
