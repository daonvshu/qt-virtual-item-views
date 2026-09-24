# VirtualItemViews

Qt Widgets 的**虚拟化 Item View 框架**：用 `QAbstractItemModel` 做数据源，只实例化可见区、
overscan 和 pinned 范围内的真实 QWidget，并在滚动时复用它们。

它**不是** `QListView` 的替代品。它解决的是另一个工程折中：复杂业务行用 Delegate 绘制会带来大量
`paint`、`geometry`、`hit-test`、`editorEvent` 样板代码，而 `QListWidget + setItemWidget` 又会为
每一行创建真实控件。VirtualItemViews 提供第三条路：**只创建看得见的行，且这些行是真正的
QWidget**。

## 定位与适用场景

适合：

* 行内包含真实控件（按钮、开关、进度条、编辑器、异步图片）的企业列表；
* 行高由业务内容决定、甚至会异步变化的长列表；
* 十万到千万级逻辑行，需要稳定内存与稳定滚动性能的场景。

不适合：

* 只需要纯绘制、追求极限吞吐的表格（请用 `QTableView` + `QStyledItemDelegate`）；
* 需要行冻结、span、GPU/scenegraph 渲染的场景（未实现，见路线图）。

## 当前状态（v0.7）

| 能力 | 状态 |
| --- | --- |
| `VirtualItemView`（基于 QAbstractScrollArea 的虚拟化内核） | 已实现 |
| `WidgetAdapter` / `WidgetRecycler`（按 WidgetType 分池） | 已实现 |
| `ScrollMapper`（64 位逻辑滚动空间 + 带锚点压缩映射） | 已实现 |
| `SizeIndex`：`FixedSizeIndex` + `BlockSizeIndex`（分块） | 已实现 |
| `LayoutPolicy` / `ListLayout`（几何策略，含 margins、横向布局预留） | 已实现 |
| `VirtualListView`：固定高度 + 动态高度（估计值 + 测量反馈） | 已实现 |
| `VirtualTableView`（v0.4 Table MVP，Row Widget Mode） | 已实现 |
| `HeaderGeometry`：列宽/顺序/隐藏/排序状态的唯一事实来源（§14/§45.10） | 已实现 |
| `NativeHeaderView`：QHeaderView 与 HeaderGeometry 双向同步（无信号回环） | 已实现 |
| `VirtualHeaderView` + `HeaderWidgetAdapter`（§17-§19）：每个可见 section 一个真实 QWidget，只 materialize 可见列 + 横向 overscan + pinned | 已实现 |
| `ColumnHost` / `TableRowLayoutContext`：框架定位列，业务只管内容（§26/§27） | 已实现 |
| 列 resize/move/hide、表头点击排序、横向像素滚动、表头状态 save/restore | 已实现 |
| 冻结列（v0.7，§31）：`setFrozenColumns()` / `setFrozenRightColumns()`，冻结 pane 与可滚动 pane 共享同一份 `HeaderGeometry` | 已实现 |
| 垂直行号表头：与 body 共享纵向偏移（行号始终对齐）、拖动分隔线写入显式行高（uniform 自动转 variable） | 已实现 |
| Cell Widget Mode（v0.5）：`CellWidgetAdapter` + 二维虚拟化，只 materialize visibleRows x visibleColumns | 已实现 |
| `visibleRows()` / `visibleColumns()` 可见区间查询 + 大列数 benchmark（100 列 x 1M 行，row vs cell 对照） | 已实现 |
| `VirtualTreeView`（v0.6 Tree MVP）：`TreeVisibilityIndex` 压平可见行 + 同一个 list kernel | 已实现 |
| 拖放（v0.7，§38）：视图侧交互（model flags 决定拖拽源、插入指示器、边缘自动滚动、拖拽期 pin 住拖拽源控件）+ 模型侧语义（`mimeData()`/`canDropMimeData()`/`dropMimeData()` 决定插入、移动或拒绝） | 已实现 |
| 拖放目标：列表按行二分插入、表格按行/单元格（跟随 `SelectionBehavior`，冻结列 pane-aware 命中）、树支持"插到节点之间"与"成为子节点"（`ontoItem`，框选指示器）与末尾追加 | 已实现 |
| 拖放观测：`itemDropped(parent, row, column, action)` 信号、`dropTargetAt()`/`dropIndicatorRect()`/`dropIndicatorStyle()` 诊断接口 | 已实现 |
| Accessibility（v0.7，§37）：`installAccessibilityFactory()` 注册 `QAccessibleInterface` 桥接，按需暴露**可见行**（列表项 / 树节点 / 表格行 + 可见列的 cell），文本与状态取自 model 与已提交几何，100 万行模型仍是十几个节点 | 已实现 |
| Accessibility 导航：current 作为 `focusChild()`、`childAt()` 命中、树层次（parent/children 往返）、表格行列语义、`press` 等价 `activateIndex()`（发 `clicked()`/`activated()`）、`setFocus`/`scrollUp/Down/Left/Right` 动作、Focus/Selection/ModelChange 事件 | 已实现 |
| Span（v0.7，§43）：`TableSpanProvider` / `TableSpanMap` + `setSpan()/removeSpan()/clearSpans()`；合并矩形完全由已提交列几何与行高推出（不存第二份几何），`indexAt()/cellRect()` 折回锚点，Cell Widget Mode 只物化锚点并把锚点控件放大到合并矩形 | 已实现 |
| Span 一致性：拖放落点与插入指示器按锚点/合并矩形、accessibility 合并区域只暴露一个 cell、span 不跨 pane（裁剪到锚点 pane）、隐藏列自动变窄、列宽/行高变化后合并矩形自动跟随 | 已实现 |
| Span 两种模式：Cell Widget Mode 只物化锚点（跨行合并由框架渲染）；Row Widget Mode 隐藏被覆盖列的 `ColumnHost`、锚点 host 占合并矩形，并在 `TableRowLayoutContext::spans()` 里把决定交给业务（`examples/table_spans`） | 已实现 |
| Advanced panes（v0.7，§43）：`setPanes()` 取有序 `TablePaneSpec{columns, scroll, scrollGroup}` 列表（任意数量冻结 pane + 一个滚动组），每个 pane 一个表头渲染器、一条交界线，`setFrozenColumns()` 退化为默认三段的语法糖 | 已实现（多滚动组待续） |
| 树：expand/collapse、`expandRecursively()`（`*` 键递归展开）、Left/Right 导航、缩进、分支指示绘制与点击、双击展开 | 已实现 |
| 树：分支图标可按状态自定义（`BranchIndicatorRenderer`，对应 `QTreeView::branch` 的 has-children / has-siblings / adjoins-item / open / closed，不解析样式表） | 已实现 |
| 树：结构变更（insert/remove/move/layoutChanged/reset）保持展开状态与滚动锚点 | 已实现 |
| overscan、`scrollTo`、ensureVisible、resize、滚轮、方向键/PageUp/PageDown/Home/End | 已实现 |
| `QItemSelectionModel` current/selection、click/doubleClick/activated | 已实现 |
| `dataChanged`/`rowsInserted`/`rowsRemoved`/`rowsMoved`/`layoutChanged`/`modelReset` | 已实现 |
| `QSortFilterProxyModel` 直接作为 model | 已实现 |
| ScrollAnchor（异步高度变化不跳动） | 已实现 |
| focus / IME / popup pinning：`setItemPinned()`、`pinWidget()/unpinWidget()`、`setMaxPinnedItems()` | 已实现 |
| `VirtualViewStats stats()` 诊断（logical/materialized/pooled/pinned + create/bind/recycle） | 已实现 |
| 选择模式：`SelectionMode`（No/Single/Multi/Extended）+ `SelectionBehavior`（Items/Rows） | 已实现 |
| 像素滚动：`WheelScrollMode`（Pixels 默认 / Items）、`setWheelScrollPixels()`、`scrollByPixels()`、`setVerticalOffset()`、触控板 `pixelDelta` 1:1 | 已实现 |
| 可选生命周期日志 `setLifecycleLoggingEnabled()`（create/bind/unbind/recycle/pin） | 已实现 |
| `TreeVisibilityIndex`（可见行压平、增量展开/折叠、深度、row 双向查询） | 已实现 |
| 单元测试 206 个用例 + 4 个变异测试 + 10 个 GUI 交互场景（共 19 个 CTest 目标） | 已实现 |
| 10 个示例（simple list / order cards / dynamic height / million rows / table row widgets / table many columns / table custom header / tree / drag & drop / table spans） | 已实现 |
| benchmark（1M 行、表格 row vs cell、树：宽树 + 变更 + 锚点，稳态滚动零分配校验） | 已实现 |

未实现（按 §43 路线图）：表头动画（§23/§24）、advanced panes 的**多滚动组**
（>3 个 pane 与任意数量冻结 pane 已可用；多滚动组要求每个滚动 pane 一个裁剪容器，
见 [docs/spans.md](docs/spans.md) 第 5/7 节）；accessibility
还没有 `QAccessibleTableInterface`（行列朗读）与文本/编辑接口；行冻结
（文档 §31 只写列方向）；树在"可见行数极大"时的增量行映射优化
（现在一次 expand/collapse 需要重建可见行索引表，见 [docs/performance.md](docs/performance.md)）。

## 快速开始

```cpp
#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

class OrderAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new OrderCardWidget(parent);            // 行内真实业务控件
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderCardWidget *>(widget)->bind(index);
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderCardWidget *>(widget)->stopAsyncWork(index);
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(600, 96); }
};

OrderAdapter adapter;
QMainWindow window;
// 视图交给窗口持有：不要在栈上创建视图后再 setCentralWidget()，
// 否则窗口析构时会 delete 一个栈对象（退出时崩溃）。
auto *view = new viv::VirtualListView(&window);
view->setAdapter(&adapter);
view->setUniformItemHeight(96);       // 固定行高；动态行高见 setItemHeightMode(Variable)
view->setOverscan(2, 2);
view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
view->setWheelScrollPixels(48);       // 一个滚轮刻度 48 px，与行高无关
view->setModel(model);                // 任意 QAbstractItemModel，含 QSortFilterProxyModel

connect(view, &viv::VirtualItemView::clicked, [](const QModelIndex &index) {
    // 行内 QPushButton 等控件仍然自己处理事件；空白区域点击会走到这里
});

view->scrollByPixels(24);             // 程序内同样按像素滚动
const viv::VirtualViewStats stats = view->stats();  // 诊断/debug overlay
// logicalItems / materializedItems / pooledWidgets / pinnedWidgets
// createCount / bindCount / recycleCount
```

完整的可运行示例在 `examples/`（全部为像素滚动；都支持 `--exit-after <ms>`，另有各自的规模与
滚动参数，如列表的 `--rows`、树的 `--devices`、统一的 `--wheel-pixels`，便于无人值守运行；
退出码 0 表示正常结束）：

表格（Row Widget Mode）：一行一个业务 QWidget，列由 `ColumnHost` 承载，
列几何全部来自 `HeaderGeometry`（表头与行共享同一份 committed geometry）：

```cpp
class OrderRowWidget : public QWidget
{
public:
    explicit OrderRowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_order = new QLabel(new viv::ColumnHost(0, this));     // 框架负责定位
        m_status = new QLabel(new viv::ColumnHost(2, this));
    }
    // ...
};

auto *table = new viv::VirtualTableView(&window);
table->setTableAdapter(&adapter);              // TableWidgetAdapter（可选 layoutRowWidget()）
table->setUniformItemHeight(44);
table->setDefaultColumnWidth(150);
table->setSortingEnabled(true);
table->setModel(model);

table->setColumnHidden(1, true);               // 列状态只有一个写入口
table->moveColumn(2, 0);
const QByteArray state = table->saveHeaderState();
table->restoreHeaderState(state);

// Cell Widget Mode（可选）：每个可见 cell 一个 QWidget，二维虚拟化
table->setCellAdapter(&cellAdapter);
table->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
```

冻结列（§31）：冻结列固定在自己的 pane 里，不参与横向滚动；三个 pane 都从同一份
`HeaderGeometry` 派生，所以没有"冻结表头自己的列宽副本"，拖动列宽会同时影响所有 pane：

```cpp
table->setFrozenColumns({0, 1});        // 左侧冻结（顺序无关，按视觉顺序排列）
table->setFrozenRightColumns({N - 1});  // 右侧冻结，右对齐贴住视口右边
table->isColumnFrozen(0);               // 查询
const QVector<viv::TablePane> panes = table->panes();   // 三个 pane 的矩形与列集合
table->clearFrozenColumns();
```

pane 交界那条线（表头 + body 连成一条）可以自定义颜色 / 线宽 / 线型：

```cpp
viv::PaneSeparatorStyle separator;      // 默认：1px，颜色取"当前样式画列分隔线用的颜色"
separator.width = 3;                    // 像素；0 = 不画这条线
separator.color = QColor("#e05555");    // 不设置（invalid）就自动与列分隔线同色
separator.lineStyle = Qt::SolidLine;    // 也支持 DashLine / DotLine（虚线在以该宽度为界的带内居中）
table->setPaneSeparatorStyle(separator);
```

Row Widget Mode 下一行仍然只有一个业务控件（冻结列的 `ColumnHost` 被框架 `raise()` 到上层，
遮住滚到它下面的列）；Cell Widget Mode 下冻结 cell 在任何滚动位置都保持实例化。
`examples/table_many_columns` 勾选「冻结前 2 列」即可看到效果。

树（v0.6）：树是"可见行压平 + 同一个 list kernel"，业务只管提供标准的
`QAbstractItemModel` 树，展开状态、缩进、分支指示与键盘导航都由框架处理：

```cpp
auto *tree = new viv::VirtualTreeView(&window);
tree->setAdapter(&adapter);                 // 与 List 相同的 WidgetAdapter
tree->setUniformItemHeight(26);
tree->setIndentation(20);                   // 每层缩进像素
tree->setBranchIndicatorsVisible(true);     // 由视图绘制并可点击（行控件不受影响）
tree->setModel(&treeModel);

tree->expand(model.index(0, 0));            // 也可以双击 / 点分支指示 / Right 键
tree->setRootIndex(model.index(3, 0));      // 只看某一棵子树
const qsizetype visible = tree->visibleRowCount();
```

分支图标可以按状态自定义（不需要图片资源，也不解析样式表）。视图会把每个可见行的**每一格**交给
渲染器：`cellDepth == itemDepth` 的那一格就是行自己那格（带展开/收起图标），更浅的格子是祖先格，
可以画 `├ └ │` 这类连接线。状态字段与 `QTreeView::branch` 的伪状态一一对应：

| `BranchIndicatorState` | `QTreeView::branch` | 含义 |
| --- | --- | --- |
| `hasChildren` | `:has-children` | 该格对应的节点有子节点 |
| `hasSiblings` | `:has-siblings` | 同层下面还有兄弟节点（竖线要继续） |
| `adjoinsItem` | `:adjoins-item` | 这一格就是行自己那格（只有它带展开/收起图标） |
| `isExpanded` / `isOpen()` / `isClosed()` | `:open` / `:closed` | 展开状态（只对有子节点有意义） |
| `isLeaf()` | `:!has-children` | 叶子 |
| `cellDepth` / `itemDepth` | — | 格子的层级 / 行的深度 |

```cpp
class BranchGlyphRenderer : public viv::BranchIndicatorRenderer
{
public:
    // 简单情形：一个状态一个图标。返回空 QIcon 表示这格不画东西。
    QIcon branchIcon(const viv::BranchIndicatorState &state, const QModelIndex &) const override
    {
        if (!state.adjoinsItem || !state.hasChildren)
            return QIcon();                       // 祖先格 / 叶子交给下面继续画线
        return state.isExpanded ? m_openIcon : m_closedIcon;
    }

    // 复杂情形：直接画（连接线 + 图标），QTreeView::branch 的图片资源可以整段搬过来。
    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &, const QRect &cellRect) const override
    {
        if (!state.adjoinsItem) {                 // 祖先格：竖线 + 到下一层的横线
            const int cx = cellRect.center().x();
            painter->drawLine(cx, cellRect.top(), cx,
                              state.hasSiblings ? cellRect.bottom() : cellRect.center().y());
            painter->drawLine(cx, cellRect.center().y(), cellRect.right() - 2, cellRect.center().y());
            return;
        }
        viv::BranchIndicatorRenderer::paintBuiltinBranch(painter, state, cellRect);
    }
};

glyphRenderer;                                 // 生命周期由业务持有（takeOwnership = false）
tree->setBranchIndicatorRenderer(&glyphRenderer);
tree->setBranchIndicatorRenderer(nullptr);     // 回到内置三角箭头
```

`examples/tree_view` 勾选「自定义图标」就能看到这套渲染器（方块 = 有子节点、圆点 = 叶子、
连接线按 `hasSiblings` / `adjoinsItem` 变化）；`--custom-icons --snapshot <file.png>` 可直接导出
PNG，用于无人值守的视觉检查。

拖放（v0.7，§38）：视图负责交互，模型负责语义。视图侧只要打开开关并在模型里给出 flags；插入、
移动、拒绝全部写在模型里（`canDropMimeData()` / `dropMimeData()`），框架不替业务做决定：

```cpp
// 模型侧：可拖动 + 可接收，载荷自定义 MIME，落点语义自己实现
Qt::ItemFlags Model::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable
         | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}
Qt::DropActions Model::supportedDropActions() const { return Qt::MoveAction | Qt::CopyAction; }
QMimeData *Model::mimeData(const QModelIndexList &indexes) const { /* 打包载荷 */ }
bool Model::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                           int row, int column, const QModelIndex &parent) const { /* 接不接受 */ }
bool Model::dropMimeData(const QMimeData *data, Qt::DropAction action,
                        int row, int column, const QModelIndex &parent) { /* 插入/移动/拒绝 */ }

// 视图侧：打开拖放（同时把视口设为接受拖放），其余交给框架
view->setDragEnabled(true);
view->setDropIndicatorShown(true);              // 2px 插入线 / 树的框选；false 只是不画
view->setDefaultDropAction(Qt::MoveAction);     // 列表拖动 = 移动
view->setDragDropActions(Qt::MoveAction | Qt::CopyAction);   // 可选：覆盖模型给的动作

const viv::VirtualItemView::DropTarget target = view->dropTargetAt(viewportPos);   // 诊断/自测
const QRect indicator = view->dropIndicatorRect(target);
connect(view, &viv::VirtualItemView::itemDropped,
        [](const QModelIndex &parent, int row, int column, Qt::DropAction action) { /* ... */ });
```

落点语义（详见 [docs/drag-and-drop.md](docs/drag-and-drop.md)）：列表按行的上/下半段插入；表格跟随
`SelectionBehavior`（`SelectRows` = 整行，`SelectItems` = 单元格，冻结列用 `columnAtViewportX()`
做 pane-aware 命中）；树的上/下 1/4 是"插到节点之间"，中间 1/2 是"成为该节点的子节点"
（`DropTarget::ontoItem`，用框选指示器表示，对应 `QTreeView` 的 `OnItem`），最后一行之下的空白区
追加到末尾。拖到视口上/下边缘会自动滚动，并按同一个视口位置重新解析目标。

```bash
cmake -S . -B cmake-build-debug -DCMAKE_PREFIX_PATH=<Qt6 路径>
cmake --build cmake-build-debug --config Debug
ctest --test-dir cmake-build-debug -C Debug --output-on-failure
cmake-build-debug/examples/simple_list        # 10 万行
cmake-build-debug/examples/order_cards        # 复杂业务卡片（动态高度）
cmake-build-debug/examples/dynamic_height     # 异步高度变化 + anchor
cmake-build-debug/examples/million_rows       # 100 万行，观察控件数是否稳定
cmake-build-debug/examples/table_row_widgets  # 表格 Row Widget Mode
cmake-build-debug/examples/table_many_columns # 表格 Cell Widget Mode + 百列横向虚拟化
cmake-build-debug/examples/tree_view          # 树：展开/折叠/缩进/分支指示
cmake-build-debug/examples/drag_drop          # 拖放：列表 / 树 / 表格（冻结列）三种落点语义
cmake-build-debug/examples/drag_drop --hover tree:120 --snapshot drop.png   # 合成悬停 + 截图
cmake-build-debug/examples/table_spans        # 合并单元格：跨列分组标题 + 跨行合并 + 冻结列对照
cmake-build-debug/examples/table_spans --cell-mode --snapshot spans.png     # 跨行合并由框架渲染
cmake-build-debug/benchmarks/bench_listview --rows 1000000 --steps 2000
cmake-build-debug/benchmarks/bench_listview --table --table-columns 100
cmake-build-debug/benchmarks/bench_listview --tree
```

## 目录结构

```
include/
  virtualitemviews/          公共头：virtualitemview.h  virtuallistview.h  virtualtableview.h
                             virtualtreeview.h
                             headergeometry.h  nativeheaderview.h  tablewidgetadapter.h
                             widgetadapter.h  widgetrecycler.h  sizeindex.h  scrollmapper.h
                             listlayout.h  layoutpolicy.h  materializeditem.h
                             treevisibilityindex.h  types.h  accessibility.h
                             tablepane.h  tablespan.h  headerwidgetadapter.h
                             virtualheaderview.h
src/
  core/       scrollmapper.cpp  virtualitemview.cpp
  index/      sizeindex.cpp (FixedSizeIndex / BlockSizeIndex)  treevisibilityindex.cpp
  layout/     headergeometry.cpp  listlayout.cpp  tablepane.cpp  tablespan.cpp
  recycler/   widgetrecycler.cpp
  widgets/    accessibility.cpp  branchindicator.cpp  columnhost.cpp  nativeheaderview.cpp
              virtualheaderview.cpp  virtuallistview.cpp  virtualtableview.cpp
              virtualtreeview.cpp
tests/
  unit/       sizeindex / scrollmapper / widgetrecycler / listlayout / headergeometry /
              treevisibilityindex / 内核 / ListView / TableView / TableCellMode / TreeView /
              VirtualHeaderView / DragDrop / Accessibility / TableSpan / TablePanes
  fuzz/       随机 insert/remove/move/dataChanged/reset
  gui/        List：鼠标/键盘/焦点 pinning/滚动数据新鲜度；Table：表头排序/横向滚轮/拖动列宽
benchmarks/   1M 行与稳态滚动零分配校验（可选 QListView/QListWidget 参考）
              --table：row/cell 模式对照   --tree：宽树 + 结构变更 + 锚点
examples/     simple_list / order_cards / dynamic_height / million_rows
              table_row_widgets / table_many_columns / table_custom_header / tree_view / drag_drop
docs/         architecture.md  lifecycle.md  model-signals.md  focus-ime.md
              table-layout.md  drag-and-drop.md  accessibility.md  spans.md
              performance.md
```

公共头以 `include/` 为根（例如 `#include <virtualitemviews/virtuallistview.h>`），安装后会放到
`include/virtualitemviews/`。

## 文档

* [docs/architecture.md](docs/architecture.md)：分层、核心不变量、一次 materialization pass 的细节
* [docs/lifecycle.md](docs/lifecycle.md)：控件生命周期状态机、适配器契约、pin 规则
* [docs/model-signals.md](docs/model-signals.md)：模型信号处理矩阵与设计要点
* [docs/focus-ime.md](docs/focus-ime.md)：焦点 / IME / popup pin 规则、诊断与 pin 上限
* [docs/table-layout.md](docs/table-layout.md)：Table 阶段约束（HeaderGeometry 单一事实来源）
* [docs/drag-and-drop.md](docs/drag-and-drop.md)：拖放契约（视图侧交互 vs 模型侧语义、三种落点语义）
* [docs/accessibility.md](docs/accessibility.md)：辅助功能桥接（虚拟节点、可见行、树层次与行列语义）
* [docs/spans.md](docs/spans.md)：span 与 advanced panes 的规格（待实现项的语义与顺序）
* [docs/performance.md](docs/performance.md)：复杂度、规模特性、基准使用与已知取舍

## 构建

* C++17，CMake 3.16+，**Qt 6.2+ 或 Qt 5.15+**（Core/Gui/Widgets；测试需要对应版本的 Qt::Test）。
  配置时给出 Qt kit 即可，无需改动工程：

```bash
# Qt 6
cmake -S . -B cmake-build-debug-qt6  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_PREFIX_PATH=E:/dev_library/Qt/6.8.3/msvc2022_64
# Qt 5
cmake -S . -B cmake-build-debug-qt5  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_PREFIX_PATH=E:/dev_library/Qt/5.15.2/msvc2019_64
```

* 选项：`VIRTUALITEMVIEWS_BUILD_SHARED`、`VIRTUALITEMVIEWS_BUILD_TESTS`、
  `VIRTUALITEMVIEWS_BUILD_EXAMPLES`、`VIRTUALITEMVIEWS_BUILD_BENCHMARKS`、
  `VIRTUALITEMVIEWS_BUILD_GUI_TESTS`。
* 安装：`cmake --install` 会导出 `VirtualItemViews::VirtualItemViews` 目标与头文件
  （`include/virtualitemviews/**`）。

### Qt 5 / Qt 6 兼容约定

CMake 用 `find_package(QT NAMES Qt6 Qt5 …)` 选择版本（Qt5Config 不定义 `QT_VERSION_MAJOR`，
因此从 `Qt6::Core` / `Qt5::Core` target 推断），之后统一链接 `Qt${QT_VERSION_MAJOR}::*`。
代码层面的差异只有三处，都集中在头文件注释里说明：

| 差异 | 处理方式 |
| --- | --- |
| Qt 5 的 `QList` 没有 `QVector` 的 API（`resize/fill/remove(n)/insert(n)`、按长度构造） | 需要这些操作的可变长数组统一使用 `QVector<T>`（Qt 6 中 `QVector` 就是 `QList`），例如 `BlockSizeIndex` 的块、`TreeVisibilityIndex` 的可见行列表 |
| `QAbstractItemModel::dataChanged()` 的 roles 参数在 Qt 5 是 `QVector<int>`、Qt 6 是 `QList<int>` | 槽函数统一声明为 `QVector<int>`（Qt 6 中两者同一类型） |
| `QMouseEvent::position()` 只存在于 Qt 6 | 内部用 `QT_VERSION_CHECK(6,0,0)` 分支到 `QMouseEvent::pos()` |

同一份代码已在 Qt 6.8.3 / msvc2022_64 与 Qt 5.15.2 / msvc2019_64 两套配置下编译并跑通全部测试。

### 验证

* 单元测试 / 变异测试 / GUI 交互测试都注册进 CTest（固定 `QT_QPA_PLATFORM=offscreen`）：
  `ctest --test-dir <build> -C Debug --output-on-failure`。
* **跑测试/示例/基准前必须把 Qt 的 `bin` 放进 `PATH`**（或用导入 MSVC + Qt 环境的验证脚本）：
  否则测试会以"找不到 Qt6Core.dll/Qt5Core.dll"之类的缺 DLL 错误失败，看起来像大面积用例失败。
  例如 `set PATH=E:\dev_library\Qt\6.8.3\msvc2022_64\bin;%PATH%` 后再运行；
  `cmake --build` 自身不需要（构建系统用的是导入库）。
* 示例与基准程序都可无人值守运行：示例传 `--exit-after <ms>` 时会在退出前打印一行统计
  （可见行 / 实例化 / 累计创建），退出码 0 表示正常结束；基准程序在违反虚拟化不变量时返回非 0。
* Debug 构建里 Qt 的 `Q_ASSERT` 失败在 MSVC 上会弹出模态对话框，headless 运行时表现为
  "程序不动、CPU 为 0"。排查疑似卡住时先按"断言失败"看（最常见来源是模型不满足
  `QAbstractItemModel` 契约，例如 `rowCount()` 对无效 parent 返回了 0），不要先怀疑滚动/回收路径。

## 命名与许可证

* 命名空间 `viv`，公开类不使用 Qt 保留风格的 `Q` 前缀（例如 `VirtualListView`），v0.1 前冻结。
* MIT，见 [LICENSE](LICENSE)。
