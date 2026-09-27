# Table 阶段的设计约束（HeaderGeometry 单一事实来源）

> 状态：**v0.4 Table MVP 已实现**（`HeaderGeometry` + `NativeHeaderView` + Row Widget Mode +
> 列 resize/move/hide + 横向像素滚动 + 表头状态持久化 + 排序 + 垂直行号表头）；
> **v0.5 Cell Widget Mode / 二维虚拟化已实现**（`CellWidgetAdapter`，只 materialize
> `visibleRows x visibleColumns`）；**v0.7 起 `VirtualHeaderView` + `HeaderWidgetAdapter` 已实现**
> （§15/§17–§19），换序的 committed/visual 两层几何与按需过渡见
> [header-animation.md](header-animation.md)；**1.0 起列方向的默认渲染器是 widget 表头
> `LabelHeaderView`**（只画 label 的 section，见 §8），native 渲染器仍可显式安装。

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

* Native 模式保留 QHeaderView 的轻量优势（文本、sort indicator、标准 resize/move），仍是
  可替换的渲染器之一。
* Widget 模式使用 `VirtualHeaderView + HeaderWidgetAdapter + Recycler`，只 materialize
  `visible columns + overscan + pinned`，不随总列数线性增长。
* **表格默认装的是 Widget 表头**：`LabelHeaderView`（`VirtualHeaderView` + 库里自带的
  `LabelHeaderAdapter`，每个 section 一个只画 label 的控件，用当前样式把 section/排序箭头画成
  原生样子）。所以 section 过渡、"整列一起动"、pane 克隆在默认配置下就有，而**拖动换序是
  选项**（1.0 起默认关闭）：列方向 `setColumnDragEnabled(true)`，行方向
  `setVerticalHeaderDragEnabled(true)`（见 §6/§8）；
  `setHorizontalHeader(nullptr)` 回到它，`setHorizontalHeaderVisible(false)` 隐藏表头，
  `setHorizontalHeader(new NativeHeaderView(Qt::Horizontal))` 才是 QStyle 绘制的那个。
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
* **拖动换行序 = 请求模型**（1.0，**默认关闭**：`setVerticalHeaderDragEnabled(true)`）：打开后
  行号条拖动期间只有视觉预览（整行跟着行号走），松手后渲染器发
  `sectionMoveRequested(fromVisual, toVisual)`，表格转成
  `VirtualTableView::rowMoveRequested()` 再调用
  `model()->moveRows(QModelIndex(), from, 1, QModelIndex(), destination)` —— 行序是模型的，
  行号条不自己改。**模型没实现 `moveRows()`**（`QAbstractTableModel` 的默认实现直接返回
  false）时这次移动会被拒绝：预览回弹、行序与所有 section 位置保持原样。所以"行号条能拖但
  落不到新位置"通常不是表头的默认值问题，而是模型侧少了 `moveRows()`。库为此提供了
  `ReorderableTableModel`（`rowCount`/`columnCount` + "视图行 → 数据行"的顺序记录 +
  `moveRows()`），示例 `examples/table_stretch_columns` 的 `OrderModel` 与
  `examples/table_custom_header` 的 `WideModel` 都直接继承它；不继承的话就自己实现
  `moveRows()`，并像它们一样用稳定身份读数据（整行内容一起换位，而不是原地换号）。
  **打开开关时视图若还没有模型**，`VirtualTableView` 会自己实例化一个
  `ReorderableTableModel`（视图持有，0 行 0 列），这样手势至少是可用的；应用自己的模型
  ——无论先给还是后给——永远优先，那时不再实例化。列的顺序正相反：它属于 `HeaderGeometry`，
  拖动列松手就写进几何，不需要模型配合（只需要 `setColumnDragEnabled(true)`）。
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
  列宽、顺序、隐藏、排序仍然只存在 `HeaderGeometry` 里。冻结 pane 的表头是**同类型渲染器的
  另一个实例** + `setPaneFilter()`（widget 表头交给 `createHorizontalPaneHeader()` 克隆，
  只显示本 pane 的 section；冻结 pane 忽略 offset，非主滚动组带自己的组偏移），因此冻结表头
  同样没有独立列宽副本，拖动列宽会经 geometry 同时影响三个 pane。
* **pane 表头按自己那组列打包**：冻结列是**集合**，不保证落在视觉序的最前面 —— 先把冻结列拖到
  别的列后面、或者先把几列拖到最前再冻结它们（用户实际这么用），pane 的列就会散落在 committed
  视觉序里。所以每个 pane 的表头都按**自己那份列清单**（committed 视觉序过滤后）从自己的左边缘
  打包，并且只物化自己 pane 的 section；主表头（滚动 pane）也一样，它带的 offset 就是自己滚动组
  的偏移。判据是"flat committed x 只在冻结列正好排在视觉序最前时才等于 pane 自己的打包"：
  一旦不相等，section 会落到别的列的槽位上，物化窗口也会按 flat x 算错 —— 表现就是表头里整列
  "消失"，随便再拖一下（触发一次重排）才回来。
  **pane 交界的分割线**：`QHeaderView` 只在 section **之间**画分隔线、不在控件边缘画，
  所以冻结 pane 表头用 `setPaneSeparatorEdge()` 在朝向滚动区的一侧（左 pane 画右边、
  右 pane 画左边）自己补 1px 分隔线，否则冻结/滚动交界处会少一条线（这条对 widget 表头的
  pane 克隆同样成立——它也是同一个渲染器类）。
  body 里同一条线由框架的 1 px 覆盖控件（`vivPaneSeparatorLine`）画在**所有 item 之上**
  （viewport 自己画的线会被行控件/单元格盖住；该控件 `WA_TransparentForMouseEvents`，
  不挡输入，每次 materialization 之后重新 `raise()`）。两条线的颜色都不靠调色板猜，而是
  `VirtualTableView::sectionSeparatorColor()` —— 让当前样式渲染一小段 section，取它右边缘像素，
  所以和"其它列之间的分隔线"完全一致；没有冻结列时这些线和裁剪容器都不存在。
  **可定制**：`setPaneSeparatorStyle(PaneSeparatorStyle)`，字段为 `width`（像素，0 = 隐藏）、
  `color`（invalid = 用上面的样式色）与 `lineStyle`；表头线与 body 线共用同一份样式，
  所以怎么改都是连续的一条。线带总是落在**冻结 pane 内侧**（左 pane 取最右侧 width 像素、
  右 pane 取最左侧 width 像素），表头与 body 因此不会各露一半。

## 8. Widget Header（VirtualHeaderView，v0.7，§15/§17-§19）

复杂表头（badge、状态灯、进度、过滤按钮、搜索框、排序箭头动画）用控件实现，而不是交给
`QStyle` 绘制：

```cpp
// 默认就是 Widget 表头：LabelHeaderView = VirtualHeaderView + 只画 label 的 adapter
auto *labels = new viv::LabelHeaderView(Qt::Horizontal);
table->setHorizontalHeader(labels);

// 换成自己的 section（badge、过滤按钮、搜索框……）
auto *header = new viv::VirtualHeaderView(Qt::Horizontal);
header->setAdapter(&myHeaderAdapter);   // HeaderWidgetAdapter
header->setLabelModel(model);
header->setSortInteractionEnabled(true);
table->setHorizontalHeader(header);     // 传给 nullptr 回到默认的 label 表头
```

**默认渲染器**：`setHorizontalHeader(nullptr)`（以及视图自己 `ensureHeaders()` 时）装的就是
`LabelHeaderView`。它把每个 section 交给一个只画 label 的控件，用当前样式（`CE_Header`）画出
原生样式的 section 与排序箭头——实测与 `NativeHeaderView` 的 section **逐像素一致**，唯一差别是
最后一节的宽度：`QHeaderView` 会把最后一节拉伸填满表头，而 widget 表头按 committed 几何画，
所以表头与 body 的最后一列严格一致（想两者都填满就 `setStretchLastColumn(true)`，那是写进
几何的、表头与 body 共用的伸缩）。

`LabelHeaderAdapter` 也直接可用/可继承：覆写 `labelText()` / `sortOrderFor()` 就能只改"每个
section 显什么"而不用写 section 控件。

* **与 native 完全可替换**：`HeaderViewInterface` 是唯一的接缝（§15）。表格只通过
  `headerWidget()`/`setGeometryModel()`/`setLabelModel()`/`setSortInteractionEnabled()`/
  `setViewportOrigin()`/`setPaneFilter()` 与表头交互，所以机身在换表头时一行都不用改。
* **只 materialize 窗口内的 section**（§19）：集合 = 可见列 + 横向 overscan + pinned section，
  复用走的是框架同一个 `WidgetRecycler`（create / acquire / bind / unbind / recycle）。
  实测：200 列、1000 px 宽的表格只创建 9 个 section 控件（`table_custom_header` 示例输出）。
* **几何仍只来自 `HeaderGeometry`**：section 的位置/宽度/顺序/隐藏/偏移全部读 geometry，
  表头不保存任何列状态；拖动分隔线、拖动 section 重排、点击排序都是把结果写回 geometry。
* **两个方向同一个渲染器**：`VirtualHeaderView` 按轴摆放 section（横向沿 x、纵向沿 y），
  `VirtualTableView` 的两个默认表头都是它 + 只画 label 的 adapter（列表头 `LabelHeaderView`、
  行号条 `LabelHeaderView(Qt::Vertical)`），所以拖动改尺寸、section 过渡、整列/整行跟帧与 pane
  克隆在默认配置下都有；**拖动换序要显式打开**（`setSectionDragEnabled(true)`，表格侧
  `setColumnDragEnabled()` / `setVerticalHeaderDragEnabled()`）。`setGeometryModel()` 仍然拒绝
  另一个方向的几何，表格侧也拒绝方向不匹配的渲染器（`setHorizontalHeader()` /
  `setVerticalHeader()` 警告并保持原渲染器不变）。
* **`bindSection()` 是唯一的"状态变了"钩子**：重命名一列（`headerDataChanged`）只会重绑被点名的
  logical 区间；列插入 / 删除 / 移动与 `modelReset` 会先把已物化 section 全部回收（在**旧身份**仍
  有效时 `unbindSection()`），随后整批重新 acquire + `bindSection()`；几何的排序指示器变化同样会
  重绑。也就是说 `bindSection()` 必须能从零重建这个 section 的 UI（标题、排序箭头、按钮状态……），
  而 `unbindSection()` 只在控件离开物化集合时才被调用 —— 不要把它当成"重绑之前一定会来一次"。
* **非 owning 协作者的生命周期**：`setGeometryModel()` / `setLabelModel()` / `setAdapter()` 都不接管
  所有权（`HeaderWidgetAdapter` 可用 `takeOwnership` 交给渲染器，但默认不接管）。几何、标签模型与
  adapter 都要比表头活得久；几何与标签模型是 `QPointer` 观察的，业务先删它们不会造成悬空解引用
  （表头会当作"没有几何 / 没有模型"处理），但 adapter 不是 QObject，删早了就是未定义行为。
* **交互**：离 section 边缘 ±3 px 按住拖动 = 改列宽（§21/§25，**默认开**，可用
  `setSectionResizeEnabled(false)` / 表格侧 `setColumnResizeEnabled(false)`、
  `setVerticalHeaderResizeEnabled(false)` 关掉——关掉后边界不再是把手，光标也不再提示）；
  按住 section 拖过拖动距离阈值 = 重排（§22，**默认关闭**，`setSectionDragEnabled(true)` 或
  表格侧 `setColumnDragEnabled(true)` 打开；拖动期间只有视觉预览 —— 整列跟着 section 一起走，
  松手才提交一次，详见 [header-animation.md](header-animation.md)）；
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
* 表头动画（§23/§24）：已按"committed vs visual 两层几何 + 按需过渡"实现，并且拖动/过渡期间
  **整列跟着 section 一起走**（`setColumnFollowsHeaderVisual()`，只跟 x），见
  [header-animation.md](header-animation.md)。
* **滚动范围不变**：可滚动内容减少的宽度正好等于冻结宽度，`maximumHorizontalOffset()` 仍是
  `总可见宽度 - 视口宽度`。冻结不会凭空制造滚动空间，也不会让某些列永远滚不到。
* **body 与 pane 的关系**：Row Widget Mode 下仍是一行一个业务控件，冻结列的 `ColumnHost`
  被 `raise()` 到兄弟之上以遮住滚到它下面的列；Cell Widget Mode 下冻结 cell 里**窗口内**的那些
  实例化（`columnsForLayout()` 对冻结 pane 也用同一个窗口：冻结 pane 的 offset 固定 0，
  所以窗口就是"从该 pane 第一列起、宽度等于 pane 宽度的那些列" + overscan），并同样 `raise()`。
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
  body 不因为冻结而增加控件（Cell Mode 除外：窗口内的冻结列在任何滚动位置都会被实例化，
  所以实例化上限从 `visibleRows x visibleColumns` 变成
  `visibleRows x (窗口内冻结列 + visibleColumns)` —— 冻结 pane 比视口宽时，多出来的列既不显示
  也不物化，见 §43 与第三轮审查 Wave 2）。

## 9. 固定列宽 + 剩余宽度按比例分配（1.0）

默认行为是"每列一个自己的宽度"：`setDefaultColumnWidth()` 给没设置过的列一个宽度，
`setColumnWidth()` 固定某一列，宽度加起来小于视口时右边留白。要让列去**填满视口**，
用比例：

```cpp
table->setColumnWidth(0, 60);            // 固定：保持 60
table->setColumnStretchFactor(1, 2.0);   // 参与分配，份额 2
table->setColumnStretchFactor(2, 1.0);
table->setColumnStretchFactor(3, 1.0);
```

* **只在几何里实现一次**：比例是 `HeaderGeometry::sectionStretchFactor()`，计算出的宽度也写回
  `sectionSize()`，所以列表头、行号条、pane 克隆、Row Widget Mode 的 `ColumnHost`、Cell Widget
  Mode 的 cell、`columnGeometry()`、`columnAtViewportX()`、accessibility 全都自动一致——它们本来
  就只读这一份几何，没有第二套宽度。
* **分配规则**：参与者（可见、factor > 0）平分
  `stretchExtent - 其他可见 section 的宽度`，按 factor 比例取整，**最后一个参与者吃掉取整余数**，
  所以几列相加正好等于目标宽度。`stretchExtent` 由视图给出：`VirtualTableView` 用视口宽度
  （冻结列也在其中，它们通常固定），独立的 `VirtualHeaderView` 用自己的 `width()`。
  `minimumSectionSize()/maximumSectionSize()` 仍然优先：碰到上下限时列会顶到限值，
  这时总和可能不再等于视口宽度（和"固定列宽加起来超过视口"是同一种结果：出现横向滚动）。
* **`stretchExtent` 不参与持久化**：比例属于列状态（`saveHeaderState()` 里存的是 factor），
  目标宽度属于"当前这个视图有多宽"，所以恢复出来的状态按恢复它的视图重新量一遍。
* **拖动即固定**：用户拖动某一列的边界就是"这一列以后按我拖的宽度"，和 `QHeaderView` 的
  Stretch → Interactive 一致（`resizeSection()` 会清掉该列的 factor，其余参与者重新分）；
  想要回比例就再 `setColumnStretchFactor()` 一次。
* **`setStretchLastColumn(true)`** 是它的退化形式：最后一列（`stretchLastSection()`）作为唯一
  参与者吃掉全部剩余宽度，可见性/顺序变化后"最后一列"自动跟着换。
* **横向滚动**：全部列都在分配比例时，几何总宽 = 视口宽，`maximumHorizontalOffset()` 为 0，
  横向滚动条自然失效；只要还有固定列超出视口，滚动的语义与以前一样。
* **表示的代价**：比例走的是 sparse 层——`HeaderGeometry` 仍是 uniform（千万行的行号条不会被
  物化），只有存了 factor 的那几列各占一条记录；`storedSectionStateCount()` 可以直接断言这一点
  （`tst_headergeometry::sparseStretchKeepsTheLargeCountCompact`）。

示例：`examples/table_stretch_columns`（5 列的小表：第 0 列固定、其余按 2 : 4 : 1 : 1 分，
工具栏可以换分配方式、首列宽度、"固定后两列"（固定宽度放在末尾，比例只描述还在流动的列）
与冻结首列；状态栏实时显示每一列的宽度与"合计 = 视口"，`--check` 自检"正好填满 +
没有横向滚动 + 比例正确 + 拖动即固定 + 固定后两列"）。
