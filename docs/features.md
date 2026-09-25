# 能力清单与状态（v1.0.0）

> 逐项能力状态，按实现顺序逐条列出。每一项都有用例、示例或基准守着，验证方式见
> [performance.md](performance.md) §2 与 `scripts/validate.ps1`。

| 能力 | 状态 |
| --- | --- |
| `VirtualItemView`（基于 QAbstractScrollArea 的虚拟化内核） | 已实现 |
| `WidgetAdapter` / `WidgetRecycler`（按 WidgetType 分池） | 已实现 |
| `ScrollMapper`（64 位逻辑滚动空间 + 带锚点压缩映射） | 已实现 |
| `SizeIndex`：`FixedSizeIndex` + `BlockSizeIndex`（分块，每块一个基值 + 只记"与估计值不同"的稀疏例外） | 已实现 |
| `LayoutPolicy` / `ListLayout`（几何策略，含 margins、横向布局预留） | 已实现 |
| `VirtualListView`：固定高度 + 动态高度（估计值 + 测量反馈） | 已实现 |
| `VirtualTableView`（v0.4 Table MVP，Row Widget Mode） | 已实现 |
| `HeaderGeometry`：列宽/顺序/隐藏/排序状态的唯一事实来源（§14/§45.10） | 已实现 |
| `NativeHeaderView`：QHeaderView 与 HeaderGeometry 双向同步（无信号回环） | 已实现 |
| `VirtualHeaderView` + `HeaderWidgetAdapter`（§17-§19）：每个可见 section 一个真实 QWidget，只 materialize 可见列 + 横向 overscan + pinned | 已实现 |
| 表头动画（§23/§24）：committed geometry 与 visual geometry 分离 —— 拖动列（`VirtualHeaderView`：拖动距离阈值、被拖列跟随指针、**邻居同曲线平滑让位**、松手一次提交）、或显式请求的换序（`moveColumn(..., MoveAnimation::Animate)`）由渲染器滑到新位置（默认 300 ms、OutCubic，`setHeaderAnimationDuration()` 可调、0 关闭），body 只在 commit 时重排一次；程序化换序默认**即时**，resize / 滚动 / pane 变化保持逐帧同步，native 表头渲染器忽略该设置 | 已实现 |
| `ColumnHost` / `TableRowLayoutContext`：框架定位列，业务只管内容（§26/§27） | 已实现 |
| 列 resize/move/hide、表头点击排序、横向像素滚动、表头状态 save/restore | 已实现 |
| 冻结列（v0.7，§31）：`setFrozenColumns()` / `setFrozenRightColumns()`，冻结 pane 与可滚动 pane 共享同一份 `HeaderGeometry` | 已实现 |
| 行冻结（v0.8，§31 行方向，[row-freezing.md](row-freezing.md)）：`setFrozenRows()` / `setFrozenBottomRows()`，冻结行钉在上下边缘、`itemPanes()` / `isRowFrozen()` / `itemPaneSeparatorRects()` 查询、每个可滚动行 pane 一个裁剪容器、命中与键盘/滚动按 pane 折回、**行号条按 pane 切分**（每条带子贴着自己的行）、**交界线与列冻结同款**（同一颜色/宽度/线型，横向线也跨过行号条）、冻结行数随表状态持久化（v2，旧格式仍可恢复）；冻结不产生额外滚动空间 | 已实现 |
| 垂直行号表头：与 body 共享纵向偏移（行号始终对齐）、拖动分隔线写入显式行高（uniform 自动转 variable） | 已实现 |
| Cell Widget Mode（v0.5）：`CellWidgetAdapter` + 二维虚拟化，只 materialize visibleRows x visibleColumns | 已实现 |
| `visibleRows()` / `visibleColumns()` 可见区间查询 + 大列数 benchmark（100 列 x 1M 行，row vs cell 对照） | 已实现 |
| `VirtualTreeView`（v0.6 Tree MVP）：`TreeVisibilityIndex` 压平可见行 + 同一个 list kernel | 已实现 |
| 拖放（v0.7，§38）：视图侧交互（model flags 决定拖拽源、插入指示器、边缘自动滚动、拖拽期 pin 住拖拽源控件）+ 模型侧语义（`mimeData()`/`canDropMimeData()`/`dropMimeData()` 决定插入、移动或拒绝） | 已实现 |
| 拖放目标：列表按行二分插入、表格按行/单元格（跟随 `SelectionBehavior`，冻结列 pane-aware 命中）、树支持"插到节点之间"与"成为子节点"（`ontoItem`，框选指示器）与末尾追加 | 已实现 |
| 拖放观测：`itemDropped(parent, row, column, action)` 信号、`dropTargetAt()`/`dropIndicatorRect()`/`dropIndicatorStyle()` 诊断接口 | 已实现 |
| Accessibility（v0.7，§37）：`installAccessibilityFactory()` 注册 `QAccessibleInterface` 桥接，按需暴露**可见行**（列表项 / 树节点 / 表格行 + 可见列的 cell），文本与状态取自 model 与已提交几何，100 万行模型仍是十几个节点 | 已实现 |
| Accessibility 导航：current 作为 `focusChild()`、`childAt()` 命中、树层次（parent/children 往返）、表格行列语义、`press` 等价 `activateIndex()`（发 `clicked()`/`activated()`）、`setFocus`/`scrollUp/Down/Left/Right` 动作、Focus/Selection/ModelChange 事件 | 已实现 |
| Accessibility 表格接口（v0.9，§37 / roadmap 2b）：表格视图节点实现 `QAccessibleTableInterface`（按**模型坐标**读：`rowCount()`/`columnCount()`/`cellAt()`、`columnDescription()`/`rowDescription()` 给表头文字、选择查询与整行整列选择），单元格节点实现 `QAccessibleTableCellInterface`（坐标、`table()`、`isSelected()`、合并单元格的 `rowExtent()`/`columnExtent()`）；`selectedCells()` 仍然只给窗口内的节点，百万行"全选"不会物化一百万个接口 | 已实现 |
| `BlockSizeIndex` 内存（v0.9，roadmap 2c，[performance.md](performance.md) §4）：只记录"实测过且与估计值不同"的行，没测量过的行不占存储，一千万元素均匀内容从约 40 MB 降到约 0.6 MB；`setSize()` 恰好写回基值时就释放例外，公开接口不变 | 已实现 |
| Span（v0.7，§43）：`TableSpanProvider` / `TableSpanMap` + `setSpan()/removeSpan()/clearSpans()`；合并矩形完全由已提交列几何与行高推出（不存第二份几何），`indexAt()/cellRect()` 折回锚点，Cell Widget Mode 只物化锚点并把锚点控件放大到合并矩形 | 已实现 |
| Span 一致性：拖放落点与插入指示器按锚点/合并矩形、accessibility 合并区域只暴露一个 cell、span 不跨 pane（裁剪到锚点 pane）、隐藏列自动变窄、列宽/行高变化后合并矩形自动跟随 | 已实现 |
| Span 两种模式：Cell Widget Mode 只物化锚点（跨行合并由框架渲染）；Row Widget Mode 隐藏被覆盖列的 `ColumnHost`、锚点 host 占合并矩形，并在 `TableRowLayoutContext::spans()` 里把决定交给业务（`examples/table_spans`） | 已实现 |
| Advanced panes（v0.7，§43）：`setPanes()` 取有序 `TablePaneSpec{columns, scroll, scrollGroup}` 列表（任意数量冻结 pane + 任意滚动组），每个 pane 一个表头渲染器、一条交界线，每个滚动 pane 一个裁剪容器，`setFrozenColumns()` 退化为默认三段的语法糖；主组跟随 `HeaderGeometry`/滚动条，其余组由 `setHorizontalOffset(group, offset)` 驱动。列表会被规范化 + 校验（一列只属于第一个声明它的 pane、`scrollGroup` 不为负、同组的 pane 必须相邻；每类问题每次调用警告一次，非法输入重复传入是 no-op） | 已实现 |
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
| `TreeVisibilityIndex`（可见行压平、**增量**展开/折叠、深度、row 双向查询）：每个已展开父节点一棵 Fenwick 前缀和，索引 → 可见行 O(depth × log siblings)，展开/折叠只沿路径更新（百万可见行下 4 层展开 969 ms → 116 ms、折叠 230 ms → 4 ms、工作集 114 → 76 MB） | 已实现 |
| 单元测试 248 个用例 + 4 个变异测试 + 10 个 GUI 交互场景（共 20 个 CTest 目标） | 已实现 |
| 12 个示例（simple list / order cards / dynamic height / million rows / table row widgets / table many columns / table custom header / tree / drag & drop / table spans / table panes / table frozen rows） | 已实现 |
| benchmark（1M 行、表格 row vs cell、树：宽树 + 变更 + 锚点，稳态滚动零分配校验）；v1.0 实测基线见 [performance.md](performance.md) §3 | 已实现 |
| API 稳定性（v1.0，[api-stability.md](api-stability.md)）：23 个公开头文件按"应用 / 扩展 / 诊断 / 私有"四级冻结，只加不删、不改默认值语义、不新增"接受但忽略"的入口；变更记进 [CHANGELOG.md](../CHANGELOG.md) | 已实现 |
| 静态库与动态库（v1.0，[abi.md](abi.md)）：`VIRTUALITEMVIEWS_EXPORT` 统一符号可见性、宏由 CMake 目标自动传播；共享构建产出 `bin/VirtualItemViews.dll` + 导入库；Qt 5.15.2 / Qt 6.11.2 × 静态 / 动态四种组合都已构建并跑通全部测试 | 已实现 |
| 安装与消费（v1.0）：`cmake --install` 导出标准 CMake 包（头文件 + 库 + `VirtualItemViewsConfig.cmake`，内含 `find_dependency(Qt…)`），消费端只写 `find_package(VirtualItemViews)` + `target_link_libraries(app PRIVATE VirtualItemViews::VirtualItemViews)`；[tests/install/consumer](../tests/install/consumer) 是只认安装包的冒烟测试（28 项自检），四种组合都已实跑通过 | 已实现 |

未实现（按 §43 路线图）：accessibility 还没有文本/编辑接口
（`TextInterface`/`EditableTextInterface`，行内编辑器自己是真实控件会自己暴露）；
树的**结构变更**（insert/remove/move/reset）仍然整体重建可见行列表（O(可见行)，见
[performance.md](performance.md) §4）。推进顺序见 [roadmap.md](roadmap.md)。

