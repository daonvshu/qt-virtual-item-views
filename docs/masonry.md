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

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    QSize estimatedSize(const QModelIndex &) const override
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

项目身份始终是模型的持久索引。列号和 y 坐标是布局结果，不能保存为业务身份；窗口宽度改变、模型排序或列数改变后，应重新查询 `visualRect()` 和 `itemColumn()`。

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

`AdapterEstimate` 在控件创建前读取 `estimatedSize()`，适合图片清单。数据或图片尺寸变化后调用 `invalidateItemSize(index)`，或者批量调用 `setItemEstimatedSizes()`。视图会保存滚动顶部的源项目和内容内偏移，重排时恢复该锚点。

模型发出小范围 `dataChanged` 时，视图只重新读取变化区间的估算尺寸；单项估算不会改变其他项目使用的回退高度，大范围变化会合并成一次完整估算刷新，避免逐项触发重排。

开启高度动画后，`setItemHeight()` 的权威高度会在指定时长内逐帧提交到布局；后续列项目跟随几何重排，滚动锚点仍绑定源索引。动画默认关闭，关闭或改变布局模式时会提交当前目标高度。

瀑布流只有垂直滚动；横向列区域始终铺满，纵向底部由最高列决定，其他列提前结束产生的空白不会创建虚构项目。

## 查询、选择与拖放

`indexAt(point)` 只命中真实项目，列间空白、项目之间的纵向间距和列下方空白均不命中。方向键按实际几何邻居移动，PageUp/PageDown 按视口高度寻找中心点最近的项目，Home/End 按源顺序移动。

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
