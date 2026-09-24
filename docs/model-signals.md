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

| 信号 | HeaderGeometry | 说明 |
| --- | --- | --- |
| `columnsInserted` / `columnsRemoved` / `modelReset` | `setSectionCount(columnCount())` | 新列使用 `defaultSectionSize`，已有列的尺寸与隐藏状态保留 |
| `columnsMoved` | `moveLogicalSections(start, count, destination)` | 尺寸与隐藏状态随列移动，视觉顺序按同一置换重映射 |
| `headerDataChanged` | — | QHeaderView 自行重绘，geometry 无需变化 |

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
