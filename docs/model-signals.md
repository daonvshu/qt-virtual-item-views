# Model 信号处理矩阵

所有信号都在 `VirtualItemView` 内部转成失效（invalidation），业务层任何时候都不需要调用
`reload()`。parent 与 `isLayoutParent()` 不匹配的信号会被忽略（例如 List 中嵌套子节点的信号）。
树的 `isLayoutParent()` 恒为 false，所以表里的通用规则对树都不生效，树的路径在下面单独列出。

| 信号 | SizeIndex / 布局 | 已实例化项 | 身份 / 选择 | 滚动位置 |
| --- | --- | --- | --- | --- |
| `dataChanged` | 尺寸可能变化（Variable 模式由测量反馈更新） | 只 rebind 落在 [topLeft, bottomRight] 且 parent 相同的可见项 | `QPersistentModelIndex` 不变 | 捕获 anchor，重排后恢复 |
| `rowsAboutToBeInserted` | — | — | — | 捕获 anchor |
| `rowsInserted` | `insertItems(first, count, estimate)`；estimate 来自 `estimateItemSize()`（List 用适配器估计值） | 新进入窗口的行 acquire/bind | Qt 维护后续索引 | 应用 anchor（在视口上方插入不跳动） |
| `rowsAboutToBeRemoved` | — | 立即回收被删行控件（索引此刻仍有效，之后即将失效），并清除其显式 pin | — | 捕获 anchor |
| `rowsRemoved` | `removeItems(first, count)` | 其余控件由随后的 pass 重排 | `QItemSelectionModel` 自行清理无效 index | 应用 anchor（锚点项被删则保持数值偏移） |
| `rowsAboutToBeMoved` | — | — | — | 不动 |
| `rowsMoved`（同 parent） | `moveItems(start, count, destRow)`，逐个恢复原尺寸 | 按持久身份重新定位，身份不变则控件不变 | `QPersistentModelIndex` 跟随 | 保持数值偏移 |
| `rowsMoved`（跨 parent） | 重新 reset 布局 | 全部回收 | 重建 | 保持偏移（clamp） |
| `layoutAboutToBeChanged` | — | 全部回收（行与项的映射可能被完全改写） | — | 捕获 anchor |
| `layoutChanged` | `resetItems(count, estimate)`：尺寸按 row 保存，重排后不再可信，回到估计值（Variable 模式会重新测量） | 下一 pass 重建 | `changePersistentIndexList` 由模型负责 | 应用 anchor |
| `modelAboutToBeReset` | — | 全部回收，清除显式 pin | — | 取消 anchor |
| `modelReset` | `resetItems(count, estimate)` | 下一 pass 重建 | `QItemSelectionModel` 清空 | 复位到 0 |

## Table 的列信号

三件事按固定顺序做，顺序本身是契约（第二轮审查 P1-1 / P1-2，第三轮审查重新确认）：

1. **先重绑已物化的行**（`rebindMaterializedRows()`）：列结构变化不改行身份，但业务行控件的
   schema（"一列一个 `ColumnHost`"）刚刚变了，`bindWidget()` 是业务唯一能感知这件事的钩子。
2. **再 remap pane 状态**（`TablePaneLayout::insert/remove/moveLogicalColumns()`）：冻结集合、
   显式 pane 规格、每列的 pane 槽位与前缀和都跟着列走。
3. **最后改几何**（`HeaderGeometry::insert/remove/moveLogicalSections()`）：几何同步发
   `geometryChanged`，那一次信号就是 pane 缓存的统一重建点。反过来做会让缓存落后一次结构变化
   （`paneOfColumn()` / `columnViewportX()` 仍按插入前的布局回答）。

排序指示器指名的是某一列，remap 可能让它改名或消失：几何会在 `sortIndicatorChanged` 里如实上报，
而表格在这三条路径上用 `m_sortGuard` 把这次"结构性改名"和"用户要求排序"区分开 —— 后者才会去
`model->sort()`。

| 信号 | 顺序 | 说明 |
| --- | --- | --- |
| `columnsInserted` | 重绑行 → `insertLogicalColumns()` → `insertLogicalSections(first, count)` | 新列使用 `defaultSectionSize`；已有列的尺寸与隐藏状态保留；排序与冻结集合按列改名 |
| `columnsRemoved` | 重绑行 → `removeLogicalColumns()` → `removeLogicalSections(first, count)` | 被删列从 pane 与几何里同时消失；指名它的排序指示器随之清理 |
| `columnsMoved` | 重绑行 → `moveLogicalColumns()` → `moveLogicalSections(start, count, destination)` | 尺寸与隐藏状态随列移动，视觉顺序按同一置换重映射 |
| `modelReset` | 行走通用路径（`resetItems()` → `resetLayoutForNewModel()`），列由 `setSectionCount(columnCount())` 收口 | 列状态按"新模型"重建；冻结集合与显式 pane 规格按列号保留，越界的列号在下一次 pane 重建时自然消失（pane 只保留几何里存在的列） |
| `headerDataChanged` | — | 表头渲染器自己重绑被点名的区间（Widget 表头）或 QHeaderView 自行重绘（native） |

### List / Tree 的列方向结构变更

List 与 Tree 的**显示身份**是 `(row, 0)`（`viewIndex()` 就是取第 0 列那个 cell），所以：

* 触碰 **column 0** 的 insert / remove / move 由内核统一处理，对三个视图一致：变更**之前**把所有
  已物化控件交还 adapter（此时旧索引仍有效，`unbindWidget()` 拿到的是真正绑定过的身份），变更
  之后按 canonical `(row, 0)` 重新物化 —— 顺带重算 `WidgetType`、重建索引查找表。显式 pin 也按
  行号重键到新身份。
* 不碰 column 0 的列变化对 List / Tree **没有可见影响**：它们只读第 0 列。这类变化只有
  Table 会映射到 `HeaderGeometry` / pane 布局与行内 schema 重绑。
* Tree 另外需要重建可见行映射：`TreeVisibilityIndex` 存的是 `QModelIndex` 值，列变化会让它们
  指向的 cell 改名（删掉 column 0 时甚至失效），所以 Tree 在列结构信号上会重新推导可见行
  （`onColumnStructureChanged()`：只重建映射，**不**重置实测行高与滚动锚点）。
  **代价**：展开状态是按 **cell** 记的持久索引，列变化会让这些 cell 改名 / 失效（Qt 的模型可能
  把它们指向移动后的列），所以 column 0 的 insert / remove / move 之后展开状态可能丢失 ——
  需要保留就重新 `expand()`。列方向不是 Tree 的契约，这条与"只读 column 0"是一回事。

换句话说：**列方向的完整契约是 Table 的**（多列、列宽、列序、冻结 / 多滚动组都在那里）；
List / Tree 承诺的是行方向的完整性，"往 List 的模型里插一列"只是把第 0 列换了内容，不会
变出第二列来。

## 设计要点

* **合并刷新**：每个处理函数只调用 `markDirty()`，由一次事件循环内的合并 `relayout()` 完成重排，
  不会为 N 个信号做 N 次全量重排。`flushPendingRelayout()` 可同步应用挂起的失效（测试/测量场景）。
* **安全网**：`relayout()` 开始时检查 `LayoutPolicy::itemCount()` 与 `viewItemCount()` 是否一致，
  不一致时按模型重建。这对优雅的代理模型并不必要，只是对抗粗糙的自定义模型信号。
* **代理模型**：`QSortFilterProxyModel` 的 `invalidateFilter()` / `sort()` 走 `layoutChanged` 路径，
  因此过滤与排序后不需要业务层介入（见 `tst_virtualitemview::proxyModelCanBeUsedAsModel`）。
* **`rowsMoved` 不做 anchor**：移动是用户主动的结构变化，保持视口数值偏移（让持久身份跟随）比
  "把某一项钉在顶部"更符合预期。
* **Tree 压平索引**：`TreeVisibilityIndex` 对结构变更提供 `handleModelChanged()`（保留展开状态
  重建可见行）与 `handleModelReset()`（同时清空展开状态）两个钩子；expand/collapse 本身是增量更新，
  不重走整棵树。

## 拖放：视图调用模型的时机（§38）

拖放不引入任何新的模型信号，视图只在拖放事件里**同步调用**模型的方法，并且完全不参与
insert/remove 的实现细节（细节见 [drag-and-drop.md](drag-and-drop.md)）：

| 时机 | 调用 | 备注 |
| --- | --- | --- |
| 开始拖拽 | `flags(index)` → `mimeData(indexes)` | 需要 `ItemIsDragEnabled`；拖拽期间对应控件被 pin |
| 拖拽经过 | `canDropMimeData(data, action, row, column, parent)` | 每个 `dragMove` 一次；返回 false 时不画指示器 |
| 松手 | `canDropMimeData()` + `dropMimeData()` | 模型返回 true 时发 `itemDropped()`，然后 `markDirty()` |
| 拖拽结束（任意结果） | — | 释放拖拽源 pin、指示器与自动滚动状态 |

`dropMimeData()` 是模型唯一的写入口：视图从不自己调用 `insertRows()` / `moveRows()` /
`removeRows()`。模型插入/移动后照常发自己的信号，视图按上面的矩阵重排（与其他模型变更同一条路径）。

## 辅助功能也走同一条模型路径（§37）

`docs/accessibility.md` 里的桥接不缓存任何数据：节点被查询时才去读 model 的角色与视图的已提交
几何。事件侧只有两个来源，都是"跟着模型本身走"：

| 来源 | 事件 | 说明 |
| --- | --- | --- |
| selection model 的 `currentChanged` / `selectionChanged` | `QAccessible::Focus` / `QAccessible::Selection` | 屏幕阅读器据此知道 current 与选中项 |
| model 的 `modelReset` / `rowsInserted` / `rowsRemoved` / `dataChanged` | `QAccessibleTableModelChangeEvent` | 结构或内容变化，与视图自己的重排使用同一批信号 |

两个来源都在接口被真正创建时才连接（`QAccessible` 是懒加载的），并且 `QAccessible::isActive()`
为假时事件是空操作，所以普通运行时没有额外开销。

## Tree 的模型变更路径

树对内核隐藏了"模型 row"这一概念：`isLayoutParent()` 恒为 false，因此内核不会按模型 row 直接改
布局，所有结构信号都先经过 `TreeVisibilityIndex` 重建"可见行 -> QModelIndex"的映射，再由内核
重排。`VirtualTreeView` 的连线顺序如下（顺序本身是不变量）：

| 信号 | VirtualTreeView 的处理 |
| --- | --- |
| `rowsAboutToBeInserted` | 记录 anchor（**在**可见行变化之前，否则视图会跳过一个插入行数） |
| `rowsAboutToBeRemoved` | 记录 anchor，并按身份回收被删子树的控件（此刻索引仍有效） |
| `rowsInserted` / `rowsRemoved` / `rowsMoved` / `layoutChanged` | `handleModelChanged()` 重建可见行 -> `refreshVisibility()`（重置行尺寸缓存，重新测量可见行）-> 应用 anchor |
| `modelReset` | `handleModelReset()`（同时清空展开状态与 root index）-> 重排 |
| `dataChanged` | 走内核默认路径：只 rebind 受影响的可见行 |

* **展开状态跨变更保留**：`handleModelChanged()` 保留 `expand()` 记录过的节点（按
  `QPersistentModelIndex`），因此上面插入/删除兄弟节点后，展开的子树保持展开。
* **锚点顺序**：`rowsAboutToBeInserted`/`rowsAboutToBeRemoved` 里先 `captureAnchor()`，而
  `expand()`/`collapse()` 内部也在改动可见行之前先捕获锚点 - 见
  `tst_virtualtreeview::anchorKeepsTheTopItemWhileExpandingAbove`。锚点项消失时保持数值偏移。
* **根限制**：`setRootIndex()` 之后，映射与 `depth()` 都以该子树为根；`modelReset` 会把 root index
  复位到无效（与 `QAbstractItemView` 的语义一致）。
