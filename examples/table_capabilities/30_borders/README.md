# 30 外边框与圆角

在同一张表上切换方案 2（四角覆盖层）与方案 3（整体 Alpha 合成），观察圆角边缘、内容贴边、透明度和滚动表现。保留原方案编号，默认显示方案 2。使用 `table_capabilities --example 30` 打开；没有 SmoothScrollbar 时，第 30 节仍编译显示，章节编号不前移。

本目录包含完整的 model、adapter、页面和两种方案，不引用其他章节代码。外框圆角与 hover／selected 圆角独立。最终采用哪种方案及框架接口形式仍待确认，见 [圆角外边框方案（待确认）](../../../docs/rounded-border-proposal.md)。

| 方案 | 源码相对路径 | 特点 |
| --- | --- | --- |
| 2 四角覆盖层 | `examples/table_capabilities/30_borders/cornercover.cpp` | 上层用纯色遮住四角，支持内容贴边；渐变和棋盘格背景会显示接缝 |
| 3 整体 Alpha 合成 | `examples/table_capabilities/30_borders/alphaframe.cpp` | `QGraphicsEffect` 获取完整子控件画面，用抗锯齿 Alpha 遮罩合成，支持真实透明；半径为零直接绘制源画面 |

先看 `example.cpp` 中 `Example::setupExample()`、`rebuildDemo()` 的公共表格配置，再看目标方案。公共容器、边框与绘制辅助函数在 `borderdemo.h/.cpp`，页面布局在 `example.ui`。两种方案共用本章的模型与配置，切换方案重新创建视图并选择首行。

可调外框半径、边框宽度和颜色、行状态圆角；外部背景可选纯色、渐变或棋盘格。还可显示隐藏表头、开启行列间距与四边冻结、切换大／小数据量及滚动条策略，点击按钮定位并选中首行或末行。

方案 2 的纯色接缝是该方案的边界，保留它用于对比。方案 3 缓存与设备像素比对应的 Alpha 遮罩，源画面每次仍需要合成，适合对比滚动和状态动画时的开销。两种方案均使用默认表头和 QLabel 单元格，不使用外轮廓的 `QWidget::setMask()`；视觉圆角不修改底层矩形控件的命中区域。

## 独立运行

复制本目录，增加以下入口，将本目录全部 `.cpp` 和 `example.ui` 编入目标，链接 VirtualItemViews 与 Qt Widgets，并启用目标的 `AUTOUIC`。MSVC 使用 `/utf-8` 编译选项。

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
