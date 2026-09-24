# Table 阶段的设计约束（HeaderGeometry 单一事实来源）

> 状态：**v0.4 Table MVP 已实现**（`HeaderGeometry` + `NativeHeaderView` + Row Widget Mode +
> 列 resize/move/hide + 横向像素滚动 + 表头状态持久化 + 排序 + 垂直行号表头）；
> **v0.5 Cell Widget Mode / 二维虚拟化已实现**（`CellWidgetAdapter`，只 materialize
> `visibleRows x visibleColumns`）。仍待实现：`VirtualHeaderView`（QWidget 版表头 +
> `HeaderWidgetAdapter`，接口已按 §15/§17/§18 预留）。

本文记录 `VirtualTableView` 尚未实现、但必须从第一天遵守的边界。方案文档把它列为项目级
architecture invariant：**Header owns geometry; VirtualTableView owns virtualization;
Row/Cell Widget owns business UI.**

## 1. HeaderGeometry 是列几何与列状态的唯一事实来源

`HeaderGeometry`（QObject）拥有并广播：

* logical / visual index 映射
* section size、minimum / maximum 与 resize 约束
* section order、hidden state
* section content position 与 viewport position
* logical horizontal offset 与 total extent

Table Body、Native Header（QHeaderView）与 Widget Header（VirtualHeaderView）都消费同一份状态。

禁止维护第二套权威宽度，也不允许用 "logical index 前缀宽度求和" 推导 visual position；必须查询
`visualIndex(logical)` 与 `sectionViewportPosition(logical)`。

## 2. Header Renderer 可替换

```text
              HeaderGeometry
                    |
        +-----------+-----------+
        v                       v
NativeHeaderView          VirtualHeaderView
 (QHeaderView, 轻量)        (QWidget, 复杂业务)
```

* Native 模式保留 QHeaderView 的轻量优势（文本、sort indicator、标准 resize/move）。
* Widget 模式使用 `VirtualHeaderView + HeaderWidgetAdapter + Recycler`，只 materialize
  `visible columns + overscan + pinned`，不随总列数线性增长。
* `VirtualTableView` 不关心 Header 用 painter 还是 QWidget 呈现。

## 3. Resize 与动画的两种几何

| 交互 | 影响 committed geometry | Table Body 是否逐帧更新 |
| --- | --- | --- |
| hover fade / sort icon / badge / loading | 否 | 否 |
| section move transition | 最终影响 | 默认否（动画期间只动 Header 的 Visual Geometry） |
| resize drag | 是 | 是（只更新 materialized rows/cells） |
| hide/show 宽度动画 | 可配置 | 默认最终提交 |

原则：**Resize 期间 Visual Geometry = Committed Geometry**（拖动时 Header 与 Body 必须同步）；
纯视觉动画留在 Header Renderer，避免每帧 relayout 大量 RowWidget。

## 4. Table Body 的虚拟化约束

* Row Widget Mode（默认）：一行一个 QWidget，列边界由 `ColumnGeometry` 驱动，Widget 数量约为
  `visible rows + overscan + pinned`。
* Cell Widget Mode（显式开启）：二维虚拟化，materialize `visibleRows x visibleColumns`
  （例如 21 x 11 = 231 cells，而不是 21 x 100）。
* `visibleRows()` / `visibleColumns()` 是 Table 的公开查询能力。
* Frozen Left / Right pane 见下面第 7 节：pane 只是对同一份 committed geometry 的不同投影。
* Header state 持久化属于 `HeaderGeometry::saveState()/restoreState()`（带版本号），
  `QHeaderView::saveState()` 只能作为 Native adapter 的补充。

## 5. 与 List 内核的关系

Table 复用同一套 kernel：`SizeIndex`（行高/列宽）、`ScrollMapper`（水平 + 垂直）、
`WidgetRecycler`、`WidgetAdapter`、invalidation coalescing、pin 规则、`VirtualViewStats`。
Table 新增的只有：行/列两级几何、`HeaderGeometry`、二维可见区间与 cell 级 adapter。

## 6. 垂直行号表头（v0.4 实现细节）

* **共享偏移**：行号条与 body 使用同一个纵向偏移（`HeaderGeometry::offsetChanged` →
  `QHeaderView::setOffset()`），所以行号始终贴住对应的行；偏移变化只做一次 shift，
  不做 O(sections) 的全量同步。
* **不复制行高**：默认 section 尺寸 = 未测量行的高度；被测量或被用户拖动的行按行镜像
  （≤ 100 万行，约 8 MB 镜像状态）。行数超过上限且高度可变时，行号条会禁用并给出告警
  （v0.5 的 Widget Header 会解除这个上限）。
* **拖动 = 显式行高**：拖动行号条分隔线写入 `setRowHeight()`；uniform 表会自动切换为
  variable（其余行保持原高度），`RowSizePolicy::ExplicitWins` 保证测量不会覆盖它。
* **单 section 应用**：`sectionResized` / `sectionVisibilityChanged` 只更新对应 section
  （O(1)），因此滚动与拖动列/行分隔线都不会退化成 O(总列数/总行数)。
* **表头顺序同步**：列顺序的每次变化（`moveColumn()`、模型 `columnsMoved`、`restoreHeaderState()`、
  冻结 pane 过滤）都会经 `NativeHeaderView::applyVisualOrder()` 写回 QHeaderView 的视觉顺序——
  否则头会停在旧顺序、body 用新顺序，出现"列和表头错位"。用户直接拖动表头时反向走
  `sectionMoved` 写回 geometry；两个方向都先比较再写入，所以不会互相触发。

## 7. Frozen / Pinned Columns（v0.7，§31）

```
┌──── FrozenLeft ─────┬──── Scrollable ─────────────┬─ FrozenRight ─┐
│ c0 │ c1             │ c2 │ c3 │ c4 │ c5 │ c6 ...  │ cN            │
└─────────────────────┴─────────────────────────────┴───────────────┘
   固定 x，不随偏移        唯一的滚动面板，消费 offset      右对齐固定在视口右侧
```

* **API**：`setFrozenColumns({...})` / `setFrozenRightColumns({...})` / `clearFrozenColumns()`，
  查询 `frozenColumns()` / `isColumnFrozen(logical)` / `panes()` / `paneTypeForColumn(logical)`。
  同一列同时出现在两侧时留在左侧；隐藏的冻结列不属于任何 pane（`isColumnFrozen()` 变 false），
  取消隐藏后自动回到 pane。
* **单一事实来源**：`TablePaneLayout`（`tablepane.h`）只缓存"列 -> 视口 x"和 pane 矩形，
  列宽、顺序、隐藏、排序仍然只存在 `HeaderGeometry` 里。冻结 pane 的表头是
  `NativeHeaderView` 的另一个实例 + `setPaneFilter()`（只显示本 pane 的 section，冻结 pane
  忽略 offset），因此冻结表头同样没有独立列宽副本，拖动列宽会经 geometry 同时影响三个 pane。
  **pane 交界的分割线**：`QHeaderView` 只在 section **之间**画分隔线、不在控件边缘画，
  所以冻结 pane 表头用 `setPaneSeparatorEdge()` 在朝向滚动区的一侧（左 pane 画右边、
  右 pane 画左边）自己补 1px 分隔线，否则冻结/滚动交界处会少一条线。
  body 里同一条线由框架的 1 px 覆盖控件（`vivPaneSeparatorLine`）画在**所有 item 之上**
  （viewport 自己画的线会被行控件/单元格盖住；该控件 `WA_TransparentForMouseEvents`，
  不挡输入，每次 materialization 之后重新 `raise()`）。两条线的颜色都不靠调色板猜，而是
  `NativeHeaderView::sectionSeparatorColor()` —— 让当前样式渲染一小段 section，取它右边缘像素，
  所以和"其它列之间的分隔线"完全一致；没有冻结列时这些线和裁剪容器都不存在。
  **可定制**：`setPaneSeparatorStyle(PaneSeparatorStyle)`，字段为 `width`（像素，0 = 隐藏）、
  `color`（invalid = 用上面的样式色）与 `lineStyle`；表头线与 body 线共用同一份样式，
  所以怎么改都是连续的一条。线带总是落在**冻结 pane 内侧**（左 pane 取最右侧 width 像素、
  右 pane 取最左侧 width 像素），表头与 body 因此不会各露一半。
* **滚动范围不变**：可滚动内容减少的宽度正好等于冻结宽度，`maximumHorizontalOffset()` 仍是
  `总可见宽度 - 视口宽度`。冻结不会凭空制造滚动空间，也不会让某些列永远滚不到。
* **body 与 pane 的关系**：Row Widget Mode 下仍是一行一个业务控件，冻结列的 `ColumnHost`
  被 `raise()` 到兄弟之上以遮住滚到它下面的列；Cell Widget Mode 下冻结 cell 总是实例化
  （`columnsForLayout()` = 冻结列 + 可见窗口 ± overscan），并同样 `raise()`。
  自定义 `layoutRowWidget()` 的业务可以用 `TableRowLayoutContext::isColumnFrozen()` /
  `paneRect()` 自己处理裁剪与叠放。
* **用裁剪，不用遮盖**：可滚动列由框架放进一个"pane 裁剪容器"（`PaneClipHost`，本身不画任何东西），
  因为 Qt 会把子控件裁剪到父控件的 rect，所以滚动列**根本画不到冻结 pane 区域**。
  这样做的好处是**不碰任何业务控件的绘制**：行控件自己画的不透明背景、卡片样式、圆角、
  交替行色都原样保留；也不需要 `autoFillBackground()`（Qt 6.8 的 Windows 11 样式是样式表驱动的，
  它忽略这个标志，而且它的 `QPalette::Base` 是半透明的，用它做遮盖会得到"半透明白"，滚动列照样透出来）。
  - Row Widget Mode：每行一个容器，覆盖可滚动 pane；`ColumnHost` 属于可滚动列时由框架挪进容器
    （坐标随之换算，视口位置不变），冻结列仍直接挂在行控件上并 `raise()`。注意裁剪只作用于
    可滚动列：冻结列永远可见，而**完全**压在冻结 pane 下面的可滚动列会被隐藏（不是画出来再盖掉）。
  - Cell Widget Mode：整个视图一个容器，覆盖可滚动 pane，可滚动 cell 由框架挪进容器。
  - 没有冻结列时容器会被拆掉、宿主还给原来的父控件，所以不用这个功能时行为与之前完全一致。
  - **自己摆放子控件的** `layoutRowWidget()` 要把自己的控件挂到
    `TableRowLayoutContext::scrollablePaneHost()` 返回的容器里（空表示没有冻结列），
    否则它们仍会出现在冻结 pane 下面。
* **代价**：pane 布局是派生缓存，几何变化、resize、偏移变化后各重算一次 O(可见 section)；
  body 不因为冻结而增加控件（Cell Mode 除外：冻结列在任何滚动位置都会被实例化，
  所以实例化上限从 `visibleRows x visibleColumns` 变成
  `visibleRows x (frozenColumns + visibleColumns)`）。
