# 架构与不变量

本文记录 VirtualItemViews v0.1 的内核结构与必须始终成立的不变量。任何改动都应先回到这里确认
没有破坏它们。

## 1. 分层

库同时支持 Qt 6.2+ 与 Qt 5.15+（见 README「Qt 5 / Qt 6 兼容约定」）；内核与索引层的实现
不依赖具体 Qt 大版本，只在数组容器与少量事件 API 上做了兼容处理。

```
QAbstractItemModel           业务数据，库不重新发明模型
        |
QItemSelectionModel          current / selection（直接复用 Qt）
        |
VirtualItemView          QAbstractScrollArea + 虚拟化内核
        |                    · 64 位滚动空间（ScrollMapper）
        |                    · 可见区 / overscan / materialization 调度
        |                    · WidgetRecycler、pin、失效合并（coalescing）
        |                    · 鼠标 / 键盘 / scrollTo / resize
        |
   +----+----------------------+
   |                           |
LayoutPolicy（几何）      WidgetAdapter（业务控件生命周期）
   |                           |
ListLayout / SizeIndex    createWidget / bindWidget / unbindWidget
```

差异全部收敛到 `LayoutPolicy`（几何）与子类的身份映射（view row 与 QModelIndex 的互相转换）：

* `VirtualListView`：view row == 模型 row（在 `rootIndex()` 之下）。
* `VirtualTableView`（v0.4）：行仍由同一个 list kernel 虚拟化，列几何与列状态由
  `HeaderGeometry` 唯一持有；`HeaderGeometry` 是 `NativeHeaderView`（QHeaderView 适配器）与
  行控件（`ColumnHost` / `TableRowLayoutContext`）共同消费的 committed geometry。
* `VirtualTableView`（v0.5，可选）：`MaterializationMode::CellWidgets` 时每个可见 cell 一个
  QWidget，二维虚拟化 `visibleRows x visibleColumns`；此时内核只负责区间/滚动/锚点
  （`usesItemWidgets() == false` + `materializeItems()` 钩子），控件由表格的
  `CellWidgetAdapter` + Recycler 池管理。
* `VirtualTreeView`（v0.6）：`TreeVisibilityIndex` + 同一个 list kernel。树只替换身份映射：

  ```
  QAbstractItemModel 树
          |
          v
  展开状态（TreeVisibilityIndex 的 m_expanded）
          |
          v
  TreeVisibilityIndex             可见行压平：indexAtVisibleRow() / visibleRowForIndex() / depth()
          |
          v
  VirtualItemView（list kernel）   行高、滚动、复用、锚点、current/selection 与 List 完全共用
  ```

  view row 是"可见压平行"，不是模型 row：`isLayoutParent()` 恒为 false，模型的
  insert/remove/move/layoutChanged 全部先交给 `TreeVisibilityIndex::handleModelChanged()`
  重建映射（保留展开状态），再由内核重排；`expand()`/`collapse()` 只遍历被展开的子树
  （见 `docs/performance.md`）。
* 缩进与分支指示不进入业务控件：`geometryForViewRow()` 把行矩形内缩
  `indentation * (depth + 1)`，分支三角由视图在 `paintEvent()` 里画、在
  `mousePressEvent()` 里命中（行控件完全不知道自己在树里）。因为指示条是视图画的、不随行控件
  移动，`afterMaterialize()` 会在每次 materialization pass 之后失效并重绘"可能含指示条的那一条
  区域"（滚动、展开折叠、改缩进、resize 都走这条路径），上一次失效的区域也会一并重绘，
  以免滚出视口的指示条留在原处。点指示条只切换展开状态：这次手势不产生 `clicked()`，
  也不会把它后面的双击再切换一次。
* 分支装饰可替换：`setBranchIndicatorRenderer()` 接管整条指示条（见 branchindicator.h）。
  视图把每个可见行的**每一格**交给渲染器：格子 `L` 宽 `indentation`、左边界 `L * indentation`，
  `L == itemDepth` 的格子是行自己那格（`adjoinsItem`，带展开/收起图标），更浅的是祖先格
  （`hasChildren == true && isExpanded == true`，用来画 `├ └ │` 连接线）。状态字段与
  `QTreeView::branch` 的伪状态一一对应（has-children / has-siblings / adjoins-item / open /
  closed），但由 C++ 结构体表达，不做样式表解析；渲染器一次调用只管一格，视图不假设它画什么。

公开头文件位于 `include/virtualitemviews/`，实现位于 `src/<module>/`；消费者只使用
`#include <virtualitemviews/...>`。诊断快照见 `VirtualViewStats`（`docs/focus-ime.md`）。

## 7. 选择与诊断

* 选择策略由 `SelectionMode`（NoSelection / SingleSelection / MultiSelection /
  ExtendedSelection）与 `SelectionBehavior`（SelectItems / SelectRows）决定；内核在
  点击与键盘导航路径上统一走同一套规则，Shift 范围选择使用显式锚点（`m_selectionAnchor`）。
* `VirtualViewStats stats()` 提供 logical / materialized / pooled / pinned 与
  create / bind / recycle 计数；`setLifecycleLoggingEnabled(true)` 打开有界（400 条）事件日志，
  用于定位"widget 数量只增不减"。

## 2. 核心不变量

1. **实例化集合** = 可见项 ∪ overscan 项 ∪ pinned 项。除此之外不存在任何 item 级 QWidget。
2. **池中控件不带身份**：进入 `WidgetRecycler` 之前必须已完成 `unbindWidget()`，池内控件不持有有效
   `QPersistentModelIndex`。
3. **一个 QModelIndex 同时最多对应一个已实例化控件**（`m_items` 与 `m_itemLookup` 保证）。
4. **身份只用 `QPersistentModelIndex`**（`MaterializedItem::index`、pin 集合、滚动锚点）。row 只允许
   作为一次布局过程中的短生命周期加速量；树的可见行查询表是按 index 值建立的派生结构，
   每次结构变更后整体重建，因此它只缓存 row，不承担身份（细节见 `docs/performance.md`）。
5. **复用前必须先 bind**：`acquire -> hide -> bind -> setGeometry -> show`，因此不会出现旧数据闪现。
6. **unbind 后不得残留旧 item 语义**：`unbindWidget()` 是适配器的责任，必须停掉定时器、动画、
   订阅、异步请求，并清空可视内容。
7. **模型变更后不需要业务层 reload()**：`dataChanged / rowsInserted / rowsRemoved / rowsMoved /
   layoutChanged / modelReset` 全部由内核转成内部失效（invalidation）。
8. **滚动稳态不分配**：稳态滚动只做 bind/geometry/show/hide 与池的进出，不 new/delete。
9. **尺寸变化保持视觉位置**：视口上方项高度变化时用 `ScrollAnchor` 补偿，避免跳动。
10. **pin 有代价也要有边界**：pin 只能来自焦点/弹出窗口或业务显式请求；pin 的项仍会被
    `rowsRemoved` 回收（数据已不存在）。

## 3. 一次 materialization pass

`VirtualItemView::relayout()` 是唯一的重排入口（调用点会经过一次事件循环合并）：

1. 若存在待恢复的 `ScrollAnchor`，先按锚点重算 `m_scrollOffset`。
2. 把 `m_scrollOffset` 夹到 `[0, contentExtent - viewportExtent]`（qint64）。
3. 通过 `LayoutPolicy` 计算可见区间，再扩展 overscan 得到 `[firstRow, lastRow]`。
4. 先决定复用：对窗口内每个 row 取出 `QModelIndex` 的持久身份，与 `m_items` 做匹配。
5. **先回收、后创建**：不在窗口内且未 pin 的项 `unbind -> hide -> recycle`，因此新进入窗口的项可以
   直接复用刚回池的控件（滚动一行时通常只 rebind 一行）。
6. 再为窗口内未复用的项 `acquire -> hide -> bind`。
7. 按 row 排序后统一 `setGeometry` 与 `show`，池中控件保持 hidden。
8. 同步 `QScrollBar`（`ScrollMapper`，range/pageStep/singleStep/value，信号屏蔽避免回环）。
9. 调用 `afterMaterialize()` 钩子（动态高度测量在这里反馈尺寸），最后发出
   `virtualizationUpdated()`。

重入保护：pass 期间 `m_inRelayout == true`，`scrollContentsBy()` 等入口直接返回；钩子里的
`markDirty()` 只会安排下一次合并 pass。

## 4. 滚动空间：ScrollMapper

* 内部偏移与内容高度一律是 `qint64`；`QScrollBar` 的 int range 只作为映射器。
* **滚动粒度是像素**：滚轮默认按固定像素步长（`WheelScrollMode::Pixels`，
  `setWheelScrollPixels()` 可改，默认 48 px），触控板/高精度滚轮的 `pixelDelta` 按 1:1 应用；
  需要"整行滚动"的业务可显式切到 `WheelScrollMode::Items`。滚动条箭头取 1/3 滚轮步长，
  拖动 thumb 与 `scrollByPixels()` / `setVerticalOffset()` 都是像素级：行从不作为滚动单位。
* 内容不超过 `ScrollMapper::kMaxScrollRange`（2e9 px）时是一一映射（scale 为 1，完全精确）。
* 超过时使用**带锚点的压缩映射**：`(anchorOffset, anchorValue)` 对应当前视口位置，
  `value -> offset -> value` 精确可逆，`offset -> value -> offset` 误差不超过一个滚动步长。
  每次滚动/重排后重新锚定，因此精度始终集中在用户正在看的位置，拖动 thumb 不会抖动。

## 5. SizeIndex

`SizeIndex` 是稳定接口，内部算法可替换（见 `docs/performance.md`）：

* `FixedSizeIndex`：固定高度，全部 O(1)。
* `BlockSizeIndex`：动态高度，分块存储 + 懒重建的两张前缀表（块起始行、块起始像素），
  `offsetOf/indexAt/setSize` 为 O(log B + capacity)，`insert` O(capacity)，`remove` O(capacity + B)。
  B = 1024 时，一百万行的前缀重建约 10^3 次加法。

## 6. 与业务层的边界

库负责：可见性、几何、复用、滚动、current/selection 转发、键盘导航、pin 判定。

业务层负责：

* `WidgetAdapter::createWidget/bindWidget/unbindWidget/estimatedSize`；
* 行内交互（按钮、编辑器、异步请求）；
* 需要在滚动中保持存活的东西，显式 `view.setItemPinned(index, true)`。

库**不**负责：绘制、动画、Drag & Drop 语义、accessibility 虚拟节点桥接（见 README 的路线图）。
