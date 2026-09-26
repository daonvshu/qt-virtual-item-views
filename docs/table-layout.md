# Table 阶段的设计约束（HeaderGeometry 单一事实来源）

> 状态：**v0.4 Table MVP 已实现**（`HeaderGeometry` + `NativeHeaderView` + Row Widget Mode +
> 列 resize/move/hide + 横向像素滚动 + 表头状态持久化 + 排序 + 垂直行号表头）；
> **v0.5 Cell Widget Mode / 二维虚拟化已实现**（`CellWidgetAdapter`，只 materialize
> `visibleRows x visibleColumns`）；**v0.7 起 `VirtualHeaderView` + `HeaderWidgetAdapter` 已实现**
> （§15/§17–§19），换序的 committed/visual 两层几何与按需过渡见
> [header-animation.md](header-animation.md)。

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
* **极宽表格要用 Widget 表头**：body、`HeaderGeometry`、滚动条与 `columnGeometry()` 都是 64 位
  像素空间（`docs/abi.md` 的 `ScrollMapper`），但 **`QHeaderView` 自己的 section 空间是 int**
  —— 当可见内容宽度超过 `INT_MAX` 时，native 渲染器无法完整镜像几何，`NativeHeaderView` 会
  跳过镜像并 `qWarning()` 一次（横向上会与 body 失步）。这个边界来自 Qt，不是框架可以消除的：
  超过 int 几何范围的表格请用 `VirtualHeaderView`（它按 `HeaderGeometry` 自己摆放 section，
  没有这个限制）。

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

## 8. Widget Header（VirtualHeaderView，v0.7，§15/§17-§19）

复杂表头（badge、状态灯、进度、过滤按钮、搜索框、排序箭头动画）用控件实现，而不是交给
`QStyle` 绘制：

```cpp
auto *header = new viv::VirtualHeaderView(Qt::Horizontal);
header->setAdapter(&myHeaderAdapter);   // HeaderWidgetAdapter
header->setLabelModel(model);
header->setSortInteractionEnabled(true);
table->setHorizontalHeader(header);     // 传给 nullptr 回到 native 表头
```

* **与 native 完全可替换**：`HeaderViewInterface` 是唯一的接缝（§15）。表格只通过
  `headerWidget()`/`setGeometryModel()`/`setLabelModel()`/`setSortInteractionEnabled()`/
  `setViewportOrigin()`/`setPaneFilter()` 与表头交互，所以机身在换表头时一行都不用改。
* **只 materialize 窗口内的 section**（§19）：集合 = 可见列 + 横向 overscan + pinned section，
  复用走的是框架同一个 `WidgetRecycler`（create / acquire / bind / unbind / recycle）。
  实测：200 列、1000 px 宽的表格只创建 9 个 section 控件（`table_custom_header` 示例输出）。
* **几何仍只来自 `HeaderGeometry`**：section 的位置/宽度/顺序/隐藏/偏移全部读 geometry，
  表头不保存任何列状态；拖动分隔线、拖动 section 重排、点击排序都是把结果写回 geometry。
* **只支持横向**：渲染器把每个 section 的 x 都从表头几何推出来、沿 x 排布，所以竖着构造
  （`VirtualHeaderView(Qt::Vertical)`）只会得到一个永远空的条子 —— 构造函数对这种用法
  `qWarning()`，`setGeometryModel()` 也拒绝另一个方向的几何。行号条用
  `NativeHeaderView(Qt::Vertical)` 或自己实现 `HeaderViewInterface`。表格侧同样会拒绝方向不匹配的
  渲染器（`setHorizontalHeader()` / `setVerticalHeader()` 警告并保持原渲染器不变）。
* **`bindSection()` 是唯一的"状态变了"钩子**：重命名一列（`headerDataChanged`）只会重绑被点名的
  logical 区间；列插入 / 删除 / 移动与 `modelReset` 会先把已物化 section 全部回收（在**旧身份**仍
  有效时 `unbindSection()`），随后整批重新 acquire + `bindSection()`；几何的排序指示器变化同样会
  重绑。也就是说 `bindSection()` 必须能从零重建这个 section 的 UI（标题、排序箭头、按钮状态……），
  而 `unbindSection()` 只在控件离开物化集合时才被调用 —— 不要把它当成"重绑之前一定会来一次"。
* **非 owning 协作者的生命周期**：`setGeometryModel()` / `setLabelModel()` / `setAdapter()` 都不接管
  所有权（`HeaderWidgetAdapter` 可用 `takeOwnership` 交给渲染器，但默认不接管）。几何、标签模型与
  adapter 都要比表头活得久；几何与标签模型是 `QPointer` 观察的，业务先删它们不会造成悬空解引用
  （表头会当作"没有几何 / 没有模型"处理），但 adapter 不是 QObject，删早了就是未定义行为。
* **交互**：离 section 边缘 ±3 px 按住拖动 = 改列宽（§21/§25）；按住 section 拖过拖动距离阈值 =
  重排（§22，拖动期间只有视觉预览，松手才提交一次，详见 [header-animation.md](header-animation.md)）；
  单击 = 排序（§33）；子控件获得焦点或打开 popup 的 section 会被 pin，不回收（§36）。
* **命中与光标（§25）**：section 是真控件、铺满整个表头，所以鼠标事件大多落在它们（以及业务塞进去
  的子控件）身上，而不是表头本身。渲染器在绑定 section 时对整棵子树打开鼠标跟踪并安装事件过滤器，
  把指针位置换算回表头坐标后再算"是否在某个 section 边缘 ±3 px"——否则"调整宽度"光标一旦设上就
  永远不重置，而且因为子控件继承父控件光标，整片表头都会变成那个图标。过滤器**从不消费**事件，
  业务控件的 hover / 点击 / 拖拽全都照旧。绑定之后再出现的子控件（按需创建的状态标签、按钮）
  通过 `QEvent::ChildAdded` **递归**补装，所以"绑定之后才建出来的子树"和绑定时就存在的一样参与
  光标换算。
* **冻结列**：`setPaneFilter(columns, frozen)` 让同一个类也能当冻结 pane 的表头
  （只 materialize 本 pane 的 section），表格会自动用同类渲染器创建 pane 表头（§31）。
* 表头动画（§23/§24）：已按"committed vs visual 两层几何 + 按需过渡"实现，见
  [header-animation.md](header-animation.md)。
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
