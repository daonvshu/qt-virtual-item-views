# Drag & Drop（§38）

拖放被切成两半：**视图只负责交互，模型负责语义**。视图不会插入、移动或删除任何东西，也不会
碰 Recycler；真正"放下去以后发生什么"全部由 `QAbstractItemModel` 决定。

## 1. 谁负责什么

| 环节 | 归属 | 入口 |
| --- | --- | --- |
| 能不能作为拖拽源 | 模型 | `flags(index) & Qt::ItemIsDragEnabled` |
| 参与拖放（源 + 目标） | 应用 | `view.setDragEnabled(true)` |
| 拖拽载荷 | 模型 | `mimeData(indexes)` |
| 允许的动作 | 模型（可覆盖） | `supportedDragActions()` → `supportedDropActions()` → `Move|Copy`，或 `setDragDropActions()` |
| 落到哪里 | 视图 | `dropTargetAt()` / `resolveDropTarget()`（subclass hook） |
| 指示器怎么画 | 视图 | `dropIndicatorStyle()` / `dropIndicatorRect()` |
| 边缘自动滚动 | 视图 | `updateDragAutoscroll()` |
| 接不接受 | 模型 | `canDropMimeData(data, action, row, column, parent)` |
| 插入 / 移动 / 拒绝 | 模型 | `dropMimeData(data, action, row, column, parent)` |

`setDragEnabled(true)` 同时把视口设为接受拖放（`viewport()->setAcceptDrops(true)`）：Qt 只把
拖放事件投递给接受拖放的控件，而光标下永远是视口，再由 `QAbstractScrollArea` 转发给视图
（与鼠标事件同一条路径，所以事件坐标始终是视口坐标）。

## 2. 落到哪里

`DropTarget` 是视图交给模型的目标：

| 字段 | 含义 |
| --- | --- |
| `parent` | 插入位置所在的父项 |
| `row` | 在 `parent` 的第几行之前插入 |
| `column` | 命中的逻辑列；`-1` = 整行/列表 |
| `ontoItem` | 落在某个 item **里面**（树），`row` = 该 item 的 `rowCount()` |
| `trailing` | 落在最后一行下方的空白区（追加到末尾） |

`row` 的取值沿用 `QAbstractItemModel::dropMimeData()` 的约定：`0 <= row <= rowCount(parent)`。

### 列表（`VirtualListView`，内核默认）

行的垂直中点分界：上半 = 插到该行之前，下半 = 插到该行之后，`parent` 是无效索引（根）。
内容下部的空白区不是目标（与 `QListView` 一致）。

### 表格（`VirtualTableView`，§38 "row/cell drop semantics"）

粒度跟随选择语义，与 `QTableView::dropOn()` 相同：

* `SelectionBehavior::SelectRows`（表格默认）：整行是拖放单位，`column == -1`；
* `SelectionBehavior::SelectItems`：`columnAtViewportX()` 解析光标下的逻辑列，插入线只跨该列。

`columnAtViewportX()` 是 pane-aware 的命中测试：冻结列用它自己的视口 x（不叠加横向偏移），
可滚动列才叠加偏移；`indexAt()` 走的也是同一个函数（冻结 pane 在横向滚动后仍然命中正确的列）。

### 树（`VirtualTreeView`，§38 "Tree drop parent"）

每行分三段，与 `QTreeView` 的 `AboveItem / BelowItem / OnItem` 对应：

| 区域 | 目标 | 指示器 |
| --- | --- | --- |
| 上 1/4 | `(parent, row)`：插到该节点之前，同级 | 2 px 插入线 |
| 中 1/2（该节点 `ItemIsDropEnabled`） | `(item, item.rowCount())`：成为该节点的子节点 | 框住该行（`Frame`） |
| 下 1/4 | `(parent, row + 1)`：插到该节点之后，同级 | 2 px 插入线 |
| 最后一行下方的空白区 | `(root, rowCount(root))`，`trailing = true` | 内容末尾的 2 px 线 |

中段只有在模型真的接受"放到里面"时才生效（`flags & ItemIsDropEnabled`），否则退化成更近的
上/下半段。指示器的 2 px 条与内核一致地**跨在边界上**（`y = 边界 - 1`）。

## 3. 指示器与自动滚动

指示器是视口的一个子控件（`vivDropIndicator`），自己画（`QPalette::Highlight`），不依赖
`autoFillBackground()`（样式表驱动的样式会忽略它）；每次 materialization 之后都会 `raise()`，
所以在冻结列 / Cell Widget Mode 下也不会被行控件盖住。`setDropIndicatorShown(false)` 关闭绘制
（拖放本身仍然工作）。

光标进入视口上/下 1/12 高度时启动 40 ms 定时器，每拍滚动最多 40 px，并按**同一个视口坐标**
重新解析目标 —— 所以自动滚动时指示器跟着插入点走，而不是停在旧位置。

## 4. "放到自己身上"

内部**移动**（`Qt::MoveAction` 且拖拽源是本视图）不会落到"原地"：目标是拖拽项自己、它的子孙，
或它前面/后面的相邻插入位（`source.row()` / `source.row() + 1`）时，视图隐藏指示器并拒绝这次
drop，模型不会被调用（与 `QAbstractItemView` 的 dropping-on-itself 一致）。复制（Copy）不受
限制 —— 把一项复制到自己旁边是合法操作。本视图之外的拖拽（`m_dragSourceIndexes` 为空）不做
任何拦截，完全交给模型判断。

## 5. 其他约定

* 拖拽源的控件在拖拽期间会被 pin 住（§36），否则滚动引发的回收会把拖拽源控件删掉；
* 拖拽时抓图的 pixmap 就是该行的控件（`QWidget::grab()`）；
* `setModel()` 会释放拖拽状态（指示器、自动滚动、源列表、pin）；
* 模型接受 drop 后会发出 `itemDropped(parent, row, column, action)`，随后 `markDirty()`
  触发一次重排（merge 到一个事件循环周期内）；
* 示例：`examples/drag_drop`（列表 + 树 + 表格，`--hover <view>:<y>[:<x>]` 可合成一次悬停把
  指示器画出来，配合 `--snapshot` 截图自检）。
