# 瀑布流：VirtualMasonryView

`VirtualMasonryView` 将根索引下的一维项目按模型顺序放入当前底部最短的列。每个项目可以有独立高度，后续项目不需要与其他列对齐；视图只物化可见区、预加载区和 pin 项。

## 最小用法

```cpp
#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>

class CardAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(160, 120);
    }
};

CardAdapter adapter;
QStandardItemModel model(1000, 1);
viv::VirtualMasonryView view;
view.setAdapter(&adapter);
view.setModel(&model);
view.setCellWidthRange(100, 200);
view.setColumnSpacing(12);
view.setRowSpacing(12);
view.setFixedItemHeight(120);
view.show();
```

仓库中的 `masonry_view` 示例启动时显示合成卡片，工具栏的“加载图片文件夹”可以切换到真实图片清单。图片只在项目进入物化范围后异步读取，缩略图回调会把原图尺寸写回模型并触发 `AdapterEstimate` 重排；未加载完成的项目使用 `150x120` 的回退估算，因此加载大量图片时不会在主线程同步解码整目录。

示例工具栏分行提供布局、分割线、高度动画和状态背景设置。卡片保留透明背景，以便直接观察 hover/select 的圆角和间距覆盖。单击一个项目，设置“当前项高度”后点击“应用高度”，即可触发该源项目的权威高度更新；勾选“高度动画”后可观察逐帧重排，状态栏显示当前项目序号及实际高度。“恢复示例”会回到 1000 个合成卡片并清除逐项高度设置，工具栏配置继续保留。

示例已启用无障碍工厂，启动时当前项位于第一个卡片。开启 Windows 讲述人后，单击卡片取得视图焦点即可用方向键验证名称和几何导航，无需修改示例源码。图片内容会遮住自身区域的背景颜色，背景覆盖边界可先用合成卡片验收。

项目身份始终是模型的持久索引。列号和 y 坐标是布局结果，不能保存为业务身份；窗口宽度改变、模型排序或列数改变后，应重新查询 `visualRect()` 和 `itemColumn()`。

模型排序或跨父节点移动前，视图按持久源索引保存项目估算尺寸及 `setItemHeight()` 的权威高度，重排后一次恢复到仍属于当前根的项目；adapter 估算模式随后读取当前模型的最新估算。移入的新项目使用当前高度模式，移出当前根的尺寸状态被丢弃。根节点自身移动时，其仍展示的子项保留尺寸和控件身份；删除、reset 或切换根/模型则清除旧尺寸。

`setRootIndex(index)` 只展示该节点第 0 列下的直接子项。删除根或它的祖先、模型 reset 后，根索引失效，视图恢复展示顶层项目。删除开始前解绑旧子项控件，随后清除旧子项的高度与 pin 状态，再按顶层模型尺寸重建。

模型第 0 列是项目的规范源单元格。插入、删除或移动导致第 0 列身份变化时，旧控件在索引有效期间解绑，新第 0 列重新分类、绑定并读取估算尺寸，旧单元格的逐项高度被清除；显式 pin 按源行恢复，滚动锚点保留该行及内容内偏移。其他列变化保持项目控件和高度。模型没有列时不展示项目；删除根或祖先所在列会恢复顶层展示。

上述列身份规则同样适用于模型支持的跨父节点列移动：当前根的第 0 列移出或有列移入其第 0 列时，重新绑定当前根下的新规范单元格。未触及当前根第 0 列的移动保持原项目控件和尺寸。

## 列宽与高度

默认列宽范围为 100～200 逻辑像素。视图会选择能让列宽落在目标范围内的最少列数，再把扣除列间距后的宽度精确均分到所有列；余数从左列开始分配。显式 `setColumnCount(n)` 仍会受当前宽度和最小列宽约束，窄视口无法满足下限时保留一列并允许它变窄。

为避免异常大的整数视口创建无界列元数据，自动列数和显式列数都限制在 4096 以内；正常窗口尺寸和项目布局不会触及此上限。

项目高度有三种来源：

```cpp
view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::Fixed);
view.setFixedItemHeight(160);

view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AspectRatio);
view.setItemAspectRatio(1.5); // 宽 / 高

view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
view.setItemHeight(model.index(3, 0), 240); // 提交某一项的实际高度

view.setItemHeightAnimationDuration(300);
view.setItemHeightAnimationEnabled(true); // 默认关闭
```

`AdapterEstimate` 在控件创建前读取 `estimatedSize()`，适合图片清单。先设置模型再设置 adapter，或替换 adapter 时，也会读取当前模型的估算值。数据或图片尺寸变化后调用 `invalidateItemSize(index)`，或者批量调用 `setItemEstimatedSizes()`。视图会保存滚动顶部的源项目和内容内偏移，重排时恢复该锚点。

`invalidateItemSize()` 和模型的 `dataChanged` 尺寸通知会合并到下一轮事件循环：同一项目只读取一次最终估算，整批从最早变化项开始重排一次。调用后立即查询的几何仍是旧值。单项估算不会改变其他项目使用的回退高度；模型或 adapter 替换、根索引切换会丢弃旧的尺寸通知。

估算宽或高小于等于零时，项目使用 `fixedItemHeight()`（默认 100）作为固定回退高度，不从其他项目的估算中推导。`setFixedItemHeight()` 会同步更新这些项目；有效估算仍按列宽保持比例，`setItemHeight()` 的权威高度优先于两者。诊断分类 `viv.masonry.sizes` 默认关闭 debug 输出；设置环境变量 `QT_LOGGING_RULES=viv.masonry.sizes.debug=true` 可查看无效单项、批量和稀疏估算及当前回退高度。

开启高度动画后，`setItemHeight()` 的权威高度会在指定时长内逐帧提交到布局；后续列项目跟随几何重排，滚动锚点仍绑定源索引。动画默认关闭，关闭或改变布局模式时会提交当前目标高度。

瀑布流只有垂直滚动；横向列区域始终铺满，纵向底部由最高列决定，其他列提前结束产生的空白不会创建虚构项目。

`setOverscan(before, after)` 按每列独立控制视口之前和之后预加载的项目数量，默认各 2 项。`setOverscan(0, 0)` 只物化与视口相交的项目及 pin 项；即使很高的项目起点远离视口，也会保持物化。编辑器、popup 和原生拖动仍由共享生命周期保留，不受预加载数量限制。

## 查询、选择与拖放

`indexAt(point)` 只命中真实项目，列间空白、项目之间的纵向间距和列下方空白均不命中。方向键按实际几何邻居移动，PageUp/PageDown 按视口高度寻找中心点最近的项目，Home/End 按源顺序移动。

左右键寻找指定方向上最近的非空列，再比较项目中心的纵向距离；距离相同取源顺序更早的项目。上下键先比较目标方向的中心纵向距离，再比较横向距离，最后按源顺序决胜。Page 导航比较目标纵坐标与项目中心的距离，同距取源顺序更早的项目。

默认使用 `ExtendedSelection`：Shift 点击或 Shift 方向键选择锚点到目标之间的源顺序范围，即使几何导航跳过了源序号，中间项目也被选中。Ctrl 点击切换单项，Ctrl 方向键只移动当前项并保留选择，Space 切换当前项。列间、纵向间距和缺项区域的点击保持当前项与选择。换列、尺寸更新、模型插删和排序后，选择仍跟随持久源索引；`SingleSelection`、`MultiSelection` 和 `NoSelection` 使用共享控件的相同规则。

启用 `setDragEnabled(true)` 后，拖放目标会转换成根索引下的源模型插入行；列号和屏幕 y 不会直接传给模型。最终是否接受由 `canDropMimeData()` 和 `dropMimeData()` 决定。拖动源、编辑器、输入法和弹窗使用共享 pin 生命周期；这些平台行为仍应在 Qt5 和 Qt6 上人工验收。

## 状态背景和分割线

```cpp
view.setStateBackgroundCornerRadius(8);
view.setHoverBackgroundThroughRowSpacing(false);
view.setSelectedBackgroundThroughRowSpacing(false);
view.setHoverBackgroundThroughColumnSpacing(true);
view.setSelectedBackgroundThroughColumnSpacing(true);
view.setVerticalGridLinesVisible(true);
view.setHorizontalGridLinesVisible(true);
```

背景只按真实项目矩形绘制，分割线也只绘制真实项目边界；没有项目的位置不会补线。非零间距会在两侧分别绘制边线，零间距共享一条线。分割线使用物理像素对齐并位于业务控件上方，因此可在 100%、125% 和 150% 缩放下保持一致线宽。

沿用其他控件的默认设置：hover/select 各自不覆盖纵向项目间距、覆盖列间距；两种状态的两个方向均可独立切换。覆盖范围延伸到同列下一项目顶部和下一列左边，圆角按扩展后的整体矩形绘制。

## 可访问性

调用 `viv::installAccessibilityFactory()` 后，Masonry 项目作为直接挂在视图下的 `Cell` 节点暴露。节点名称来自 `DisplayRole`，矩形来自当前 `visualRect()`；`columnIndex()` 随当前 Masonry 几何更新，源索引身份保持不变。它不伪造规则表格行，也不暴露列间空白节点。

## 原生生命周期验收

启用 GUI 测试目标后，构建会生成 `viv_native_masonry_harness`。分别启动 Qt5 或 Qt6 版本，选择“输入法”“下拉框”“菜单”“原生拖放”，每项点击“准备所选测试”并按窗口提示操作。测试会把视图从四列缩窄到两列，检查源控件、编辑器、popup、拖动 pin 和清理状态；结果通过标准输出以 `NATIVE_MASONRY_EVENT` JSON 记录，四项完成后应出现：

```text
NATIVE_MASONRY_EVENT {"acceptedCases":4,"expectedCases":4,"failedAttempt":false,"summaryPassed":true}
```

这个 harness 不能替代 Windows 讲述人视觉焦点、候选窗绘制和 DPI 像素观察；这些项目仍需在 Qt5、Qt6 和目标缩放比例下手动确认。

## 边界

瀑布流不支持树展开、规则表头、冻结行列、合并单元格或按显示行选择。布局元数据保存每个项目的列、顶部和高度，以保证可见范围和命中查询；控件数量仍只随可见范围、预加载和 pin 增长。

项目顶部、列底部、滚动范围和几何导航使用 64 位逻辑坐标；可见范围、命中和尺寸后缀重排不依赖 `QRect` 的整数纵坐标。仅在转换为视口矩形时减去滚动偏移并限制 Qt 坐标，极远离屏项目保持在视口之外。比例高度转换超过 `int` 范围时饱和到最大正高度，内容范围在 64 位安全上限内饱和。

首版布局元数据占用 `O(N + C)` 空间，N 为项目数，C 为列数；包含所有项目的几何、估算、权威高度和各列源序号，虚拟化只减少 QWidget 数量。初次布局、换列和全量估算提交按最短列扫描重建，代价为 `O(N × C)`。从第 k 项开始的尺寸后缀重排代价为 `O(C + (N - k) × C)`，k 之前的几何保留；模型排序还需按持久索引保存和恢复尺寸。

命中先定位列再在列内二分，代价为 `O(C + log N)`。可见查询按各列二分后合并源序号，代价为 `O(C × log N + V × log V)`，V 含可见及预加载项目；控件物化数量另计 pin 项。首版没有分块坐标索引或最短列堆，连续窗口宽度变化及靠前项目高度动画仍会重建大量元数据；这些代价不应解释为 QWidget 数量失去虚拟化限制。
