# Drag & Drop（§38）

拖放被切成两半：**视图只负责交互，模型负责语义**。视图不会插入或移动任何东西，也不会碰
Recycler；真正"放下去以后发生什么"全部由 `QAbstractItemModel` 决定。唯一的例外是显式打开的
`setMoveRemovesSourceRows(true)`（§6）：那时视图在 Move 拖放结束后删掉**自己拖动的行** ——
跨视图的移动没法由模型表达（源模型不知道"外部的 drop 被接受了"），Qt 的 `QAbstractItemView`
就是这么做的。

## 1. 谁负责什么

| 环节 | 归属 | 入口 |
| --- | --- | --- |
| 能不能作为拖拽源 | 模型 | `flags(index) & Qt::ItemIsDragEnabled` |
| 参与拖放（源 + 目标） | 应用 | `view.setDragEnabled(true)` |
| 拖拽载荷 | 模型 | `mimeData(indexes)`：视图把**选中的、可拖动的索引按列**交给它（行选中 = 整行的每一列，单元格选中 = 那一格） |
| 允许的动作 | 模型（可覆盖） | `supportedDragActions()` → `supportedDropActions()` → `Move|Copy`，或 `setDragDropActions()` |
| 落到哪里 | 视图 | `dropTargetAt()` / `resolveDropTarget()`（subclass hook） |
| 指示器怎么画 | 视图 | `dropIndicatorStyle()` / `dropIndicatorRect()` |
| 边缘自动滚动 | 视图 | `updateDragAutoscroll()` |
| 接不接受 | 模型 | `canDropMimeData(data, action, row, column, parent)` |
| 插入 / 移动 / 拒绝 | 模型 | `dropMimeData(data, action, row, column, parent)` |
| Move 拖放结束后的源行清理 | 视图（可选，默认关闭） | `setMoveRemovesSourceRows(true)` |

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

* `SelectionBehavior::SelectRows`：**整行**是拖放单位。载荷是整行的每一列，`column == -1`，插入线
  横跨整个视口，拖动预览就是那一行的控件；落到模型里是一整行、列一一对应。
* `SelectionBehavior::SelectItems`：**单元格**是拖放单位。载荷只有被拖的那一格，
  `columnAtViewportX()` 解析光标下的逻辑列、插入线只跨该列，拖动预览也按几何把该格从行控件里
  裁出来（`dragPixmapRect()`）。

两种粒度都只是"**往哪里插、带什么数据**"：表格的 drop 永远是"在行之间插入"，没有"落在单元格上
填充/覆盖"的语义（Qt 里那是 `setDragDropOverwriteMode(true)`，本库未实现）。

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
* 拖拽时抓图的 pixmap 就是该行的控件（`QWidget::grab()`）。行身份是 `(row, 0)` 那一格，所以
  从**任何一列**发起拖动都抓这一行的控件：表格（Row Widget Mode）从第 1 列以后发起拖动以前
  会因为按"被拖的那一格"查找而找不到控件，于是没有拖拽预览。表格的**单元格**粒度再把这个控件
  按列几何裁到被拖的那一格（`VirtualItemView::dragPixmapRect()` 的子类钩子）；
* `setModel()` 会释放拖拽状态（指示器、自动滚动、源列表、pin）；
* 模型接受 drop 后会发出 `itemDropped(parent, row, column, action)`，随后 `markDirty()`
  触发一次重排（merge 到一个事件循环周期内）；
* 示例：`examples/drag_drop`（列表 + 树 + 表格，`--hover <view>:<y>[:<x>]` 可合成一次悬停把
  指示器画出来，配合 `--snapshot` 截图自检）。

## 6. 移动（`Qt::MoveAction`）与源行的归属

`dragDropActions()` 默认按模型给出 `Move | Copy`，但"移动"到底意味着什么由两件事共同决定：

* **落到哪里 / 插入什么** —— 模型（`canDropMimeData()` / `dropMimeData()`，§1）；
* **源行什么时候消失** —— 两种做法，二选一：

  1. **模型自己做**（默认；视图什么都不删）：`dropMimeData()` 里 `removeRows()` + 插入，像
     `examples/drag_drop` 的列表模型那样。这只对**同一个模型内部**的移动成立 —— 跨视图拖放时
     源模型根本不知道目标接受了 drop。
  2. **视图做**：`setMoveRemovesSourceRows(true)`。拖放以 `Qt::MoveAction` 结束（即目标接受了
     移动，按住 Ctrl 时是 Copy，不会触发）时，源视图删掉自己拖动的那些行 —— 与
     `QAbstractItemView` 在 `QDrag::exec()` 之后调用 `clearOrRemove()` 完全一致。父项按"深度优先
     从深到浅"、同一父项内按"从后往前"删，所以不会因为删除而挪动待删的索引；模型**已经删掉**
     的源索引（做法 1）会被跳过，因此两种做法可以同时存在而不会删两次。

  注意方法 2 的前提：模型不能"保留行身份地移动"（例如用 `moveRows()` 实现移动）—— 那样源行看起来
  仍然原封不动，会被删第二次。要么按方法 1 用 remove + insert，要么保持本开关关闭。

### 让"行自己的视图状态"跟着行走（行高）

拖放的载荷里只有**数据**，行高是**视图状态**，不在里面 —— 所以"拖动一行后它还是原来那么高"这件事，
库不做，应用想做就得自己带上。`examples/drag_drop` 的表格模型是一个完整例子（方法 1 的延伸）：

```text
模型：载荷里额外写 "源行|显式高度"（视图行高变化时通过 rowHeightChanged 镜像进模型）
dropMimeData()：插入（交给基类）→ 自己删源行（同一个模型内部才行）
              → 用应用自己的信号告诉视图：新行应该用什么高度
应用：把这个信号接到 table->setRowHeight() 上
```

库里提供的只有两个入口：`VirtualTableView::rowHeightChanged(row, height)`（行高的事实来源还是视图）
和 `setRowHeight()`。要不要把行高搬过去、怎么搬，完全由应用的语义决定。
