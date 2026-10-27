# 表格布局：列宽、冻结、分区与合并

本页适用于 `VirtualTableView` 的行控件和单元格控件模式。列的尺寸、顺序、隐藏和排序指示统一写入 `HeaderGeometry`；表头与内容读取同一份已提交几何。以下片段假定表格已设置模型与 adapter，模型至少有 7 列、6 行；具体建表方式见 [表格与单元格](table.md)。

可运行源码按功能阅读：[列宽与换序](../examples/table_capabilities/07_columns/example.cpp)、[比例列宽](../examples/table_capabilities/08_stretch_columns/example.cpp)、[冻结行列](../examples/table_capabilities/16_frozen_areas/example.cpp)、[多滚动组](../examples/table_capabilities/17_panes/example.cpp)、[合并格](../examples/table_capabilities/23_spans/example.cpp)。各目录实现独立，完整索引见 [VirtualTableView 功能示例](table.md)。

## 列宽与比例分配

```cpp
table.setDefaultColumnWidth(150);
table.setColumnWidth(0, 60);          // 固定宽度
table.setColumnHidden(4, true);
table.moveColumn(2, 1);              // 调整视觉顺序，不修改模型列顺序
table.setColumnStretchFactor(1, 2.0);
table.setColumnStretchFactor(2, 1.0); // 其余宽度按 2:1 分给这两列
```

因子为 0 的列保持自身宽度。拖动任意列的边界，或调用 `setColumnWidth()` 为任意列指定具体宽度，都会立即清除全部比例因子并关闭末列拉伸，回退到普通固定列宽模式；其余列保留退出时的宽度，不再重新分配。原本固定的列也适用，设置与当前相同的宽度同样会退出比例模式。独立的 `FitViewport` 模式继续保持总宽并由拖动侧分摊，见下节。

重新调用 `setColumnStretchFactor()` 设置正因子即可恢复比例分配；同时配置固定列宽和比例时，应先设置固定宽度，再设置各列因子。只需要最后一列填满剩余空间时可用 `setStretchLastColumn(true)`；手动拖动或指定任意列宽度同样会退出拉伸。

## 始终填满视图的调宽模式

```cpp
table.setColumnResizeMode(viv::VirtualTableView::ColumnResizeMode::FitViewport);
```

此模式把当前列宽作为比例，让可见列宽与列间距的总和始终等于 viewport 宽度，因此无需水平滚动。窗口缩放、隐藏列、增加或删除列后都会重新分配；隐藏列不参与计算。开启时会清除普通比例因子和末列拉伸设置。

使用默认控件表头时，在目标列内部靠近左边界拖动，会改变该列宽度，由左侧所有可见列按按下时的宽度比例分摊；靠近右边界拖动，则由右侧列分摊。另一侧列宽不变，最外侧两个边界固定。向回拖动使用同一份按下时快照，避免反复调宽累积像素舍入误差。

调用 `setColumnWidth()` 同样保持填满模式，默认由右侧列分摊；设置最后一列时改由左侧分摊。只剩一列时，该列始终填满可用宽度。

通常遵守表头几何的最小、最大列宽。如果窗口过窄或过宽，无法同时满足限制和总宽，此模式优先保持填满，临时放宽列宽限制；列间距本身超出可用宽度时也会压缩。重新扩大窗口仍按保留的比例分配。

调用 `setColumnResizeMode(ColumnResizeMode::Independent)` 可退出，保留当前列宽。显式设置正的 `setColumnStretchFactor()` 或启用 `setStretchLastColumn(true)` 也会退出，切回对应的普通拉伸方式。模式属于视图配置，不写入列状态；恢复列状态后，仍使用当前模式重新布局。完整示例见第 08 节。

## 间距背景色

```cpp
table.setSpacingBackgroundColor(QColor("#e3edf7"));
table.setVisualStateThroughColumnSpacing(true); // Default: true; whole-row states only.
table.horizontalHeader()->setSpacingBackgroundColor(QColor("#e3edf7"));
table.verticalHeader()->setSpacingBackgroundColor(QColor("#e3edf7"));
```

视图的 `setSpacingBackgroundColor()` 设置内容区行列间距的背景；通过 `horizontalHeader()`、`verticalHeader()` 分别调用同名接口设置对应表头的间距背景，冻结区域也会同步。颜色只用于间距，不改变项目、表头 section 和左上角控件的背景，也不改变分割线配置。自定义间距控件仍可绘制自己的内容，完全自定义的表头 renderer 继续管理自己的背景。

按行选择时，hover 和 selected 背景默认贯通内容区的列间距，覆盖在间距底色之上，动画与整行保持一致。调用 `setVisualStateThroughColumnSpacing(false)` 可让列间距保持自身背景，通过 `visualStateThroughColumnSpacing()` 查询。按单元格选择时只高亮单元格；纵向行间距和表头间距始终保留自身外观。自定义间距控件需要保持背景透明，才能显示框架绘制的状态背景。

传入 `QColor()` 恢复默认行为：内容间距和表头间距统一使用 `QPalette::Base`，包括内容区四周和表头首尾的半间距。调用 header 的 `setBackgroundColor()` 后，未指定颜色的表头间距跟随该底色；调用 `setBackgroundVisible(false)` 后，未指定颜色的表头间距也保持透明。显式间距颜色与基础背景开关独立。对应 getter 返回配置值，未设置时为无效颜色。`VirtualListView` 和 `VirtualTreeView` 也可以使用继承的 `setSpacingBackgroundColor()` 配置行间距背景。可运行示例和动态恢复写法见 [第 09 节源码](../examples/table_capabilities/09_spacing/example.cpp)。

## 冻结列和冻结行

```cpp
table.setFrozenColumns({0, 1});
table.setFrozenRightColumns({5});
table.setFrozenRows(2);
table.setFrozenBottomRows(1);
```

列集合使用逻辑列号，显示时按视觉顺序排列；同时位于左右集合的列留在左侧。行参数是顶部/底部的行数，而不是行号。冻结区域不随对应方向滚动，也不会额外增加滚动空间。行号条跟随冻结行分带，且仍与内容行对齐。

## 多个水平滚动组

简单布局用冻结列 API 即可。需要多个独立水平偏移时，显式给出按视觉顺序排列的 pane：

```cpp
viv::TablePaneSpec frozen;
frozen.logicalColumns = {0};
frozen.scroll = viv::PaneScroll::Frozen;

viv::TablePaneSpec primary;
primary.logicalColumns = {1, 2, 3};
primary.scrollGroup = 0;

viv::TablePaneSpec secondary;
secondary.logicalColumns = {4, 5, 6};
secondary.scrollGroup = 1;

table.setPanes({frozen, primary, secondary});
table.setHorizontalOffset(1, 120); // 第二个滚动组
```

第一个可滚动 pane 的组是主组，由表头几何和水平滚动条驱动；其他组通过 `setHorizontalOffset(group, offset)` 调整。调用 `setPanes({})` 可恢复默认布局。每列只应出现在一个 pane 中，同组可滚动 pane 应相邻。

横向滚轮、触控板横向手势和 `Shift + 滚轮` 自动滚动鼠标所在 pane 的滚动组，表头、间距和空白单元格也按所在区域判断。目标组到达边缘后不会转而滚动其他组；鼠标位于冻结区域时仍滚动主组。普通滚轮保持纵向滚动。

## 合并单元格

```cpp
table.setSpan(0, 0, 1, 3); // 第 0 行第 0 列开始，跨 1 行 3 列
table.setSpan(4, 1, 2, 2);
// 需要取消时：table.removeSpan(0, 0) 或 table.clearSpans();
```

规则来自业务数据时，实现 `TableSpanProvider`，只在锚点返回跨度：

```cpp
class GroupSpans : public viv::TableSpanProvider
{
public:
    viv::TableSpan spanAt(const QModelIndex &index) const override
    {
        return index.column() == 0 && index.row() % 5 == 0
            ? viv::TableSpan{1, 3} : viv::TableSpan{};
    }
    viv::TableSpan maximumSpan() const override { return {1, 3}; }
};

GroupSpans spans; // 生存期长于 table
table.setSpanProvider(&spans);
```

`setSpanProvider()` 默认由业务持有 provider。可用 `anchorIndex(index)` 找到被覆盖单元格的锚点。合并区域不能跨 pane，实际矩形会裁剪到锚点 pane。单元格模式只物化锚点控件；行控件模式可从 `TableRowLayoutContext::spans()` 读取覆盖关系。

合并跨度只描述模型坐标中的覆盖关系；列宽、行高、隐藏和冻结变化时，合并区域的显示矩形由视图重新计算。
