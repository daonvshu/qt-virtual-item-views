# 14 节点间距与分割线

切换间距中心或两侧分割线、状态跨间距、表头穿线、间距背景与自定义间距控件；仅支持冻结列。

中心模式默认在内容四周保留半间距。可分别勾选“水平表头与首行之间保留全间距”和“垂直表头与首列之间保留全间距”，把对应交界改为全间距；两侧模式下则从 0 改为全间距。树表直接使用继承的公共接口：

```cpp
view->setHeaderContentSpacingEnabled(Qt::Horizontal, true); // Full rowSpacing() below the horizontal header.
view->setHeaderContentSpacingEnabled(Qt::Vertical, true);   // Full columnSpacing() beside the vertical header.
```

方向参数表示表头方向；两项默认均为 `false`，`headerContentSpacingEnabled()` 可分别查询。开关控制内容起点的完整间距，随内容滚动，行号和列头同步对齐。内容末端、冻结列之间及节点之间的间距不受影响；关闭开关、隐藏表头或将其尺寸设为 0 后恢复普通外侧规则：中心模式为半间距，两侧模式为 0。零间距不增加留白。

全间距的分割线随模式切换：中心模式补中心线；两侧模式只补间距下方（水平表头）或右侧（垂直表头）的内容边线，另一侧沿用表头已有边线。关闭开关后保留中心模式的半间距，不在半间距内部额外补线。补线与普通网格共用显隐、颜色、物理像素线宽和贯通设置，零间距不增加额外线条。

默认所有节点使用“行间距”的统一值。勾选“节点独立间距（18 + 6）”后，节点 1.3 的列 0 设置 `NodeRowSpacingBelowRole = 18`，节点 1.4 的列 0 设置 `NodeRowSpacingAboveRole = 6`，两者之间的间距合计为 24，覆盖统一行间距。取消勾选会清除这两个 role，恢复统一值；切换数据量时保留开关选择。

行分割线横向覆盖实际列范围，包括分支与缩进；可按节点深度过滤分组边界，并设置整线端点边距。

入口：[example.cpp](example.cpp)。主要接口：`setRowSpacing`、`setColumnSpacing`、`setGridLinePlacement`、`setHeaderContentSpacingEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 14`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。

## 分组边界与整线边距

`setRowGridLineDepths({0})` 只显示主节点分组之间的分割线。展开后，线位于整组最后一个可见后代之后；不会在主节点与第一个子节点之间画线。深度相对于当前显示根，主节点为 0，子节点为 1。空集合恢复全部深度；负深度被忽略。末尾边线仍受末行自动隐藏设置控制。

`setGridLineMargins(QMargins(left, top, right, bottom))` 默认四边为 0。横线使用左右值，竖线使用上下值；正数缩短、负数伸长，绘制最终仍受视图裁剪。它修改整条线的端点，不改变项目大小、间距、命中或每个 cell 的局部分割线。外边框、冻结分隔线和拖放指示器保持各自的样式设置。

表头和内容共享同一组端点范围，边距不会在每个 section 或冻结 pane 上重复扣除。

## 分割线边距参照

`setGridLineMarginReference(VirtualItemView::GridLineMarginReference::Viewport)` 为默认值，按可见区域设置整线边距。切到 `Content` 后使用完整内容端点；未滚动到右侧或底部时，屏幕边缘不会提前留出对应边距。左右控制横线，上下控制竖线；正数缩短、负数延伸，超过视图的部分正常裁剪。表头与内容使用同一组整体端点。

`setRowVisualStateMargins(left, right)` 单独调整 hover 和选中行背景的左右边距，默认均为 0；正数缩短、负数延伸。在原有状态范围计算后应用，保持控件尺寸、命中和选择范围不变。表格仅在 `SelectRows` 模式下生效；垂直表头显示且启用状态背景时，与内容合并为整行范围，边距只计算一次，圆角跟随整体端点。表头隐藏或关闭状态背景时从内容端点计算；水平表头和单元格选择模式不受影响。List／Tree 原有的 `setVisualStateBackgroundMargins` 仍可单独配置状态范围及命中。
