# 19 自定义表头控件

本节分成两个页签，分别提供独立的 model、adapter、控件和视图配置，便于按功能查阅。

- **表头样式与绘制**：`examples/table_capabilities/19_custom_header/styled_headers.cpp`。演示默认底色、透明背景、状态圆角、点击列头选整列，以及选中渐变和图标。冻结首列使用同一个 adapter，外观设置同步到冻结副本。
- **组合控件与 corner**：`examples/table_capabilities/19_custom_header/example.cpp`。每个可见列头包含标题和排序按钮，支持冻结区复用；勾选“左上角使用全选按钮”替换 corner，取消勾选恢复默认表头样式。文件末尾的 `createExample()` 组合两个页签。

默认表头已经是 `VirtualHeaderView`，通过 `horizontalHeader()->setAdapter(new HeaderAdapter(*view), true)` 更换 section 控件即可。表头接管 adapter，并自动提供模型和列几何；冻结区使用同一个 adapter，替换时自动回收旧控件并重建。

先看各页 `Example::setupExample()` 中带英文注释的配置，再看对应 adapter 和 section；`setupUi()` 只创建页面布局。基础文本对齐、padding、字体等配置另见 [第 18 节](../18_headers/README.md)。

## 表头样式与绘制

`setVisualStateCornerRadiusIncludesHeaders(true)` 默认启用。行号条显示状态背景时，按行高亮的左侧圆角由行号条绘制，内容区仅保留右侧圆角；按格高亮时，行号条和各单元格分别绘制完整四角圆角。行号条隐藏或关闭状态背景时，内容区恢复左右圆角。关闭该选项后，表头状态背景为直角，内容区保留自身圆角。页面提供对应开关。

勾选“点击列头选整列”，再点击列头，可查看列表头四角圆角；切换到“按格选择与高亮”便于查看被选中的整列。圆角半径、颜色、时长和曲线与主体共用。

勾选“透明表头背景”分别调用 `horizontalHeader()->setBackgroundVisible(false)` 与 `verticalHeader()->setBackgroundVisible(false)`，隐藏横纵表头和默认 corner 的基础面板，显示父控件背景；文字、分割线、hover 和选中背景继续绘制。可与圆角、渐变开关组合观察，取消勾选恢复配置的底色或默认面板。

勾选“自定义表头底色”分别对横纵表头调用 `setBackgroundColor(QColor("#fff3d6"))`，将横纵表头、冻结区域副本和默认 corner 的基础背景设为浅黄色；取消勾选传入 `QColor()` 恢复原生样式。此接口直接支持默认表头，不需要本页为渐变和图标创建的自定义 section 或 adapter。hover、选中背景与动画继续叠加；关闭基础背景显示时底色暂不绘制，恢复显示后继续使用配置的颜色。未显式指定颜色的表头间距跟随底色。普通文字样式参考第 18 节。

勾选“选中表头使用彩色渐变”后，选中的行号条和列头使用蓝、紫、粉、黄渐变，hover 保持默认效果。点击行号可观察行号条；同时勾选“点击列头选整列”后点击列头，可观察列表头。取消选中时，渐变按现有时长和曲线淡出；切换此开关不会重建表头或清空选择。

勾选“选中表头显示图标”后，点击行号可查看选中图标；同时勾选“点击列头选整列”后点击列头，可查看列头图标。图标使用 Qt 内置资源，无需额外图片；透明度跟随 `selectedProgress`，选中与取消时均与背景同步过渡。此选项可与渐变、透明背景组合使用，单独 hover 不显示图标。

参考 `GradientHeaderSection::visualStateBackgroundBrush()`：仅在 `selected` 为 `true` 时返回 `QLinearGradient` 画刷，其他情况调用基类。框架继续负责状态进度、圆角与分割线。`StateHeaderAdapter` 只替换 section 的创建，绑定和解绑沿用 `LabelHeaderAdapter`；`setupExample()` 将适配器安装到横、纵表头。

表头绘制按职责继承：`HeaderSection` 提供当前绑定索引和 `visualState()`；`StyledHeaderSection` 提供背景、圆角、分割线与画刷重载点；`LabelHeaderSection` 绘制文字和排序箭头。本例由 `GradientHeaderSection` 定制选中画刷，再由 `SelectedIconHeaderSection::paintContent()` 绘制图标，并将文字和排序箭头布局到剩余区域。`initContentStyleOption()` 保留文字样式并应用 header 的内容 padding，不改变背景范围。启用图标后固定预留左侧空间，避免文字随选中动画移动；过窄或过矮的 section 自动回退到默认文字绘制。

在统一程序中选择本项，或使用 `table_capabilities --example 19`。

## 独立运行

复制本目录，增加以下入口，将本目录 `example.cpp` 和 `styled_headers.cpp` 一起编译并链接 VirtualItemViews 和 Qt Widgets：

```cpp
#include "example.h"
#include <QApplication>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget *page = table_custom_header::createExample();
    page->resize(1000, 680);
    page->show();
    const int result = app.exec();
    delete page;
    return result;
}
```

[全部表格功能](../README.md)
