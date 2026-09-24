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
* Frozen Left / Right pane 暂不实现，但 `TableLayoutEngine` 不得假设未来永远只有一个连续 pane；
  pane 只是对同一份 committed geometry 的不同投影。
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
