# 变更记录

格式：每个版本一节，破坏性变更单独列在 `Breaking`。稳定性分级与冻结规则见
[docs/api-stability.md](docs/api-stability.md)。

## 1.0.0 — 2026-09-25

首个发布版本：List / Table（Row 与 Cell 两种模式）/ Tree，冻结列、行冻结、span、多滚动组、
表头动画、拖放、accessibility、静态与动态库、安装包与一键验证。公开 API 的分级与冻结规则见
[docs/api-stability.md](docs/api-stability.md)，ABI 与工具链矩阵见 [docs/abi.md](docs/abi.md)。

### Breaking（1.0 定稿前的内部收口）

仓库在 1.0 之前没有打过 tag、也没有下游使用者，所以下面这些收口直接生效、不提供兼容层；
列出来只是为了让"1.0.0 与之前从源码仓库拉到的快照"之间的差异有据可查。

* `LayoutPolicy::setSizeIndex()` 的默认实现改为履行所有权：没有索引模型的策略在
  `takeOwnership = true` 时会删除传入的 `SizeIndex`（此前会静默忽略并泄漏）。自定义策略如果
  既不用索引又要接管所有权，行为变化就是"不再泄漏"。
* 删除 `ListLayout::setOwnsSizeIndex(bool)`：无调用者，所有权用
  `setSizeIndex(index, takeOwnership)` 表达。
* 删除 `ScrollMapper::toScrollbar()` / `toLogical()`：与 `toScrollBarValue()` /
  `toLogicalOffset()` 完全重复（架构文档里的旧名字）。

### Added

* `docs/api-stability.md`：公开 API 的四级分类（应用/扩展/诊断/私有）、冻结规则与复核清单。
* `include/virtualitemviews/global.h` + `VIRTUALITEMVIEWS_EXPORT`：**静态库与动态库都支持**。
  `-DVIRTUALITEMVIEWS_BUILD_SHARED=ON` 产出 `bin/VirtualItemViews.dll` + `lib/VirtualItemViews.lib`；
  `VIRTUALITEMVIEWS_LIBRARY` / `VIRTUALITEMVIEWS_STATIC` 由 CMake 目标自动传播，业务代码不必手工
  define。ABI 规则、SOVERSION 与支持矩阵见 [docs/abi.md](docs/abi.md)。
* `tests/install/consumer/`：安装包的消费端冒烟测试（独立 CMake 工程，`find_package(VirtualItemViews)`
  之后跑 28 项运行期自检）。README 新增「安装与消费」一节，`docs/abi.md` 补上装出来的目录布局。
* `scripts/validate.ps1`：一键验证。对每个 Qt kit × 库形态组合做 configure → `all` 构建 → CTest →
  12 个示例退出码 → benchmark 不变量 → 安装 + 消费端冒烟测试，退出码 = 失败步数。
* [docs/performance.md](docs/performance.md) §3 的 v1.0 基线：列表 1M 行、表格 20 万行 x 100 列
  （Row/Cell 两种模式）、树 1M 顶层节点的实测数字与确切命令，供后续回归对比。
* README 重排成面向 GitHub 的首页：徽章 + 一句话定位 + 亮点 + 能力概览 + 示例截图
  （`docs/images/`，由示例的 `--snapshot` 导出）+ 快速开始 + 安装/构建/验证；
  逐条能力状态移到 [docs/features.md](docs/features.md)。
* `BlockSizeIndex::explicitSizeCount()`（诊断）：当前"实测过且不等于基值"的行数。
* `VirtualItemView::visibleItemRanges()` / 行 pane 查询族（v0.8 行冻结，见
  [docs/row-freezing.md](docs/row-freezing.md)）。
* `VirtualTableView::paneSpecs()`/`primaryScrollGroup()`/`horizontalOffset(group)` 等显式 pane
  与滚动组查询（v0.8 多滚动组）。

### Changed

* `BlockSizeIndex` 改成"块基值 + 稀疏例外表"：没测量过的行不占存储，公开接口不变
  （[docs/performance.md](docs/performance.md) §4）。
* `TreeVisibilityIndex` 的可见行映射改成"每个已展开父节点一棵 Fenwick 树"，
  expand/collapse 不再重建整表（公开接口不变）。
* 表头动画的缓动与时长统一为 OutCubic + 300 ms（`setHeaderAnimationDuration()` 可调）。
* `CMakeLists.txt` 的 `VIRTUALITEMVIEWS_PUBLIC_HEADERS` 补上 `itempane.h`。
* 项目版本 `0.1.0 -> 0.9.0`（与 roadmap 的 v0.9 对齐；v1.0 收尾时 bump 到 `1.0.0`，SOVERSION 跟
  主版本）。
* **产物布局变化**：可执行文件从 `<build>/examples/…`、`<build>/tests/…`、
  `<build>/benchmarks/…` 统一到 `<build>/bin`，库统一到 `<build>/lib`。旧路径下的二进制不再更新，
  请按新路径调用（`<build>/bin/table_spans`）；`find_package` 的消费端不受影响。

### Fixed（全量代码审查 Wave 1：崩溃 / 悬空指针）

修复来自 [VirtualItemViews_Full_Code_Review.md](VirtualItemViews_Full_Code_Review.md) 的 P0/P1
生命周期问题（回归测试：`tst_adapterreplacement`、`tst_modellifetime`、`tst_celllifecycle`）：

* **Adapter 切换 UAF**：`VirtualTableView::setTableAdapter()` 以前先 `delete` 旧 adapter，再让
  基类 `setAdapter()` 用旧指针调 `unbindWidget()`。现在统一成"用旧 adapter 解绑 → 丢弃它的控件池
  → 再删除旧 adapter"，表头（`VirtualHeaderView::setAdapter()`）与单元格 adapter 同理。
* **Recycler 池没有 adapter 身份**：两个 adapter 都用默认 `WidgetType 0` 时，旧 adapter 的控件会
  被新 adapter 取走并 `static_cast` 成错误的类型。切换 adapter、切换
  `MaterializationMode`、替换 cell adapter 时都会清空池（见"行为变化"）。
* **Model / SelectionModel 悬空**：`m_model` / `m_selectionModel` 改成 `QPointer`；删除模型后视图
  的后续操作与析构不再解引用已释放对象。并加上不变量：`selectionModel()->model()` 必须是
  `model()`，否则 `setSelectionModel()` 拒绝并 `qWarning`；换模型时属于旧模型的 selection model
  会被摘掉（外部的不会被删除）。
* **表头 pane 渲染器析构顺序**：主表头是 widget 表头、且它拥有 `HeaderWidgetAdapter` 时，派生
  出来的 pane 表头（借用同一个 adapter）以前靠 `deleteLater()`/父子析构，会晚于 adapter 释放。
  现在换表头与析构都按"先销毁派生 pane 渲染器 → 再销毁主渲染器 → 再销毁 adapter"的顺序，
  并且按接口指针 `delete`（`HeaderViewInterface` 不要求渲染器本身就是 QWidget）。
* **视图析构不解绑**：`~VirtualItemView()` 现在先 `recycleAllItems()` + 清池，再释放自己拥有的
  adapter / layout，业务在 `unbindWidget()` 里停定时器、退订异步结果的行为在析构路径上也成立。
* **Cell Widget Mode 结构变更**：行 / 列被移除以及 `modelReset` 之前，单元格控件会先解绑（此时
  persistent index 仍然有效），业务拿到的仍是旧的行列身份，而不是无效索引。

**行为变化**：切换 adapter、切换 Row/Cell 模式、替换 cell adapter 时，旧控件池会被清空
（池里的控件立即销毁并重新创建），而不是跨 adapter / 跨模式复用 —— 这是"池没有 adapter 身份"
这一根本问题的当前解法（另一种解法是给池的 key 加上 adapter 身份，留待日后）。

### Fixed（全量代码审查 Wave 2：数据 / 状态正确性）

* **列的 insert / remove / move 不再让列状态跟错列**：`HeaderGeometry` 新增
  `insertLogicalSections()` / `removeLogicalSections()`（并按同一排列 remap 排序指示器），
  `TablePaneLayout` 新增 `insertLogicalColumns()` / `removeLogicalColumns()` /
  `moveLogicalColumns()`（remap 冻结列集与显式 pane 规格）。表格的 `columnsInserted` /
  `columnsRemoved` / `columnsMoved` 改走这些接口，于是"第 1 列插入一列"之后，老列的宽度、
  隐藏状态、显式尺寸、冻结归属与排序指示器都还跟着原来那一列，而不是整体向右错位一格。
  回归测试：`tst_headerstructure`（4 例：中间插入 / 中间删除 / 移动列 / 显式 pane 规格）。
* **动态高度的滚动锚点**：锚点以前是在测量**之后**才捕获的，于是"视口上方某行变高"时锚到的
  是变化后落在同一像素位置的那一行（视口会跳）。现在先捕获锚点再改尺寸；同时锚点的内容偏移
  改成基于**滚动 pane**（`scrollOffset + frozenTopExtent()`，`applyPendingAnchor()` 里反向换算），
  因为冻结顶部行会盖住视口顶部、滚动 pane 的上沿并不在视口 0 处。
  回归测试：`tst_dynamicanchor`（3 例：overscan 上方行变高、冻结顶部行、冻结底部行；
  已确认还原旧实现时这三个用例都会失败）。
* **rootIndex 现在是持久索引**：`VirtualListView` / `VirtualTreeView`（以及内部的
  `TreeVisibilityIndex`）的 root 改成 `QPersistentModelIndex`，在它前面插入 / 删除 / 移动兄弟
  行之后仍然指向同一个 item（此前只是个普通 `QModelIndex`，行号一变就指向别的行）；有效索引
  属于别的模型、或视图还没有模型时 `setRootIndex()` 拒绝并 `qWarning`；换模型会清掉 root，
  不再把外来 parent 交给新模型的 `rowCount()`。
  回归测试：`tst_virtuallistview::rootIndexSurvivesStructuralChangesAndRejectsForeignIndexes`。
* **选择语义统一到一个来源**：新增 `VirtualItemView::selectionFlagsFor()`，鼠标点击、键盘导航与
  Space 都通过它把 `SelectionMode` + `SelectionBehavior` 组合成同一个选择命令。于是：
  `NoSelection` 下 Space 不再能选中任何东西（此前它会 `Toggle` 当前项）；`SelectRows` 下
  Ctrl 点击与 MultiSelection 的点击切换的是**整行**而不是单个单元格（此前只切一格）。
  回归测试：`tst_selection`（4 例：NoSelection 的点击与 Space、Space 切换当前项、
  SelectRows 的 Ctrl 点击切换整行、MultiSelection 每次点击切换整行）。
* **冻结行下的拖放坐标**：`resolveDropTarget()` 与 `resolveDropIndicatorRect()` 以前用
  `scrollOffset + viewportPos.y()` 直接换算内容偏移，而 `indexAt()` 早已按 pane 折回；冻结行
  存在时鼠标指向的行与实际算出的落点会不一致（落点跑偏、插入线跑到视口外）。现在两者共用
  与命中测试相同的 pane 映射（`itemPaneAtY()` + `itemPaneScrollOffset()`），插入线也画在
  它所属那个 pane 的坐标系里。
  回归测试：`tst_dndfrozen`（2 例：冻结带与滚动带各自的落点、三条边界线的位置；
  已确认还原旧映射时用例会失败 —— 落点从第 0 行变成第 50 行、指示线跑到 y=-1500）。
* **`scrollToColumn()` 的 pane 感知**：键盘导航调用它让 current 列可见，但它以前拿"扁平
  contentX"和主滚动偏移比较，于是指向冻结列时会去滚主组，指向非主滚动组的列时会滚错组（甚至
  什么都看不到）。现在先查列属于哪个 pane / 滚动组：冻结列直接返回（本来就可见）；其它情况
  按列在**自己 pane 内**的 x 与 pane 宽度决定目标偏移，主组走 `setHorizontalOffset()`、
  其它组走 `setHorizontalOffset(group, …)`。
  回归测试：`tst_tablepanes::keyboardNavigationScrollsTheGroupOfTheColumn`
  （已确认还原旧实现时用例会失败：第二组的偏移一直是 0）。
* **span 重叠校验与上界维护**：`docs/spans.md` 一直写着"重叠的 span 视为非法，后者被忽略并
  `qWarning()` 一次"，但 `TableSpanMap::setSpan()` 是无条件 insert，视图里那个"只警告一次"的
  成员也从没被用过。现在 `setSpan()` 会检查与其它 span 的矩形是否相交（同一锚点重复设置算
  **替换**，不算重叠），重叠则忽略并只警告一次；`removeSpan()` 之后重新计算 `maximumSpan()`
  （它是 `anchorOf()` 反查的上界，删掉大 span 后不该继续按旧范围扫描）；顺手删掉视图里那个
  没用的 `m_spanWarningShown`。
  回归测试：`tst_tablespan::overlappingSpansAreIgnoredWithOneWarning`（用消息处理器断言"只警告
  一次"）、`removingASpanShrinksTheMaximum`；两个既有用例原先依赖"后者覆盖前者"的旧行为，已按
  规格改成把两个合并放在不同行。
* **`RowSizePolicy::MeasuredWins` 下 body 与行号条不再漂移**：`updateRowHeaderGeometry()`
  除了镜像 layout 的当前行高，还会把 `m_explicitRowHeights` 里的旧值再写一次 —— 而 MeasuredWins
  下测量值会合法地覆盖用户设的高度，于是 body 显示测量值、行号条显示旧显式值。现在行号条只镜像
  已提交的 layout 尺寸（显式高度表退回成纯策略标记）。`clearRowHeight()` 也改成立刻把该行恢复成
  测量值 / 估计值（带滚动锚点补偿），而不是等它再次被物化才更新。
  回归测试：`tst_virtualtableview::rowHeaderMirrorsTheCommittedSizeUnderMeasuredWins`
  （ExplicitWins 下 70 两边一致；切换 MeasuredWins 后 body 与行号条都变成测量值 30；清除显式
  高度后立刻回到测量值；未物化的行立刻回到估计值）。

### Fixed（全量代码审查 Wave 4：公开 API 的"接受但忽略"）

* **`VirtualHeaderView(Qt::Vertical)` 明确拒绝（P1-14）**：这个公开构造函数一直存在，但渲染器把
  每个 section 的 x 都从横向的 `HeaderGeometry` 推出来 —— 竖着构造只会得到一个永远空的条子，
  既不报错也没有任何提示（行号条的正确做法是 `NativeHeaderView(Qt::Vertical)` 或自己实现
  `HeaderViewInterface`）。现在构造函数在 orientation 不是 `Qt::Horizontal` 时 `qWarning()`；
  `setGeometryModel()` 也拒绝另一个方向的几何（此前写进去会让表头直接空掉）。
* **表头方向校验（P2-4）**：`setHorizontalHeader()` / `setVerticalHeader()` 收到方向不匹配的
  渲染器时 `qWarning()` 并保持原渲染器不变（`VirtualHeaderView` 与 `NativeHeaderView` 的
  `setGeometryModel()` 同样拒绝）—— 此前会被照单收下，然后拿另一个轴的表头几何去排布。
  回归测试：`tst_virtualheaderview::verticalWidgetHeaderIsRefusedLoudly`、
  `mismatchedHeaderOrientationsAreRefused`（已确认还原旧实现时两例都会失败）。
* **显式 pane 列表会被校验并规范化（P2-6）**：规格一直写着"同一组的列必须相邻"，但
  `setPaneSpecs()` 只是把列表原样存下来。于是同一个 logical column 可以被两个 pane 同时
  声明 —— 这一列会在两个表头 pane 里各画一份，而 body 的归属（`m_paneIndexByLogical`）只留
  最后一次写入，即"表头与 body 各说一套"。现在 `TablePaneLayout::setPaneSpecs()` 会规范化：
  一列只属于**第一个**声明它的 pane（后面的 pane 丢掉；负数索引同样丢掉）、负数 `scrollGroup`
  按 0 处理、同一组的 pane 不相邻（`组 0 | 冻结 | 组 0`）保持原样但警告一次。每类问题每次调用
  最多一条 `qWarning()`，而"同一份非法列表传第二遍"是 no-op（`paneSpecs()` 返回的就是规范化后
  的列表，视图也不再白跑一趟 relayout）。pane 数量与序号不变，所以 `panes()`/`paneSpecs()`
  仍然一一对应。回归测试：
  `tst_tablepanes::invalidPaneSpecsAreNormalizedWithOneWarningEach`（已确认还原旧实现时该用例
  会失败）。
* **`restoreHeaderState()` 变成事务性的（P2-3）**：它先 `m_columns->restoreState(columnState)`，
  之后才解析冻结列 / 冻结行，于是"状态尾部损坏"时函数返回 `false`，但视图的列状态已经被换掉
  ——失败的操作留下一个半恢复的视图（列顺序/宽度变了，冻结设置没变）。现在整段状态先解析校验
  到局部变量、全部合法后才一次性提交，返回 `false` 一定意味着"什么都没改"。
  顺带把 `columnStateSize` 的检查补全：它是流里的 `quint32`，直接 `int()` 转换后再和
  `state.size()` 比较，超 `INT_MAX` 的值会先变成负数、绕过检查（随后按负长度分配缓冲区）。
  回归测试：`tst_virtualtableview::aBrokenStateLeavesTheViewUntouched`（尾部截断 / 负数条数 /
  超 `int` 的长度三种损坏状态都不改变视图；已确认还原旧实现时该用例会失败）。
* **绑定之后新增的子控件也参与光标换算（P2-5）**：`watchMouse()` 只在 section 绑定时扫描一次
  `findChildren<QWidget*>()`，所以业务在绑定之后才建出来的控件（按需出现的状态标签、只在可编辑
  行上出现的按钮）不在过滤器里 —— 指针移到那片区域时表头收不到位置更新，"调整宽度"光标会一直粘在
  上一次的形状上，正是 §25 想避免的那个现象。现在渲染器自己处理 `QEvent::ChildAdded` 并**递归**
  补装过滤器与鼠标跟踪，绑定之后新建的子树和绑定时就存在的一样。回归测试：
  `tst_virtualheaderview::childrenAddedAfterBindingAreWatchedToo`（已确认还原旧实现时该用例会失败）。

### Fixed（全量代码审查 Wave 3：虚拟化热路径）

* **`BlockSizeIndex` 的分裂真正保证块上界**：`splitBlockIfNeeded()` 以前只反复切同一个块，
  切出来的 tail 不再检查 —— 一次插入 1,000,000 行会留下几十万行的块，而 `offsetOf/indexAt/
  setSize` 的复杂度都依赖"每块 ≤ 2 x capacity"这个不变量。现在用一个待处理队列把所有超限的块
  （包括切出来的 tail）都处理干净；新增诊断接口 `maxBlockRowCount()`。
  回归测试：`tst_sizeindex::blockIndexBoundsEveryBlockAfterAHugeInsert`（capacity 4 插 1 万行、
  默认 capacity 插 100 万行，断言上界与几何精确性）。
* **列宽上下限不再污染"显式尺寸"**：`setMinimumSectionSize()` / `setMaximumSectionSize()`
  以前对每个 section 调 `resizeSection()` —— 那会把它们全部变成**显式**尺寸（此后
  `setDefaultSectionSize()` 再也影响不到它们），而且每个 section 都发一次 `geometryChanged()`，
  让一次"改最小宽度"变成 O(N) 次渲染器全量同步。现在批量夹取走一趟内部通道：保持 quiet 标记，
  `m_defaultSectionSize` 也跟着夹到 [min, max]（否则之后新建的 section 会低于最小值），
  整个批量只发至多一次 `geometryChanged()`（单 section 的 `sectionResized` 照旧逐个发）。
  回归测试：`tst_headergeometry` 新增 3 例（隐式尺寸仍然跟随默认值、默认值被夹取、
  500 个 section 的批量修改只有 1 次 `geometryChanged` 且重复设置是纯 no-op）。
* **relayout 队列真正合并（P2-1）**：`scheduleRelayout()` 以前每次 `markDirty()` 都投递一个
  queued 调用（第一个跑完把标志清掉，其余都是空转），一次 100 次失效就是 100 次事件循环往返。
  现在有 queued 调用在飞就直接返回。
* **纯横向滚动不再跑纵向物化（P2-8）**：`VirtualTableView::scrollContentsBy()` 处理完 dx 后仍
  调用基类，而基类只读纵向滚动条并 relayout —— 一次横向滚动白跑一趟纵向 pass。现在 dx 已经
  更新表头与 pane 布局（行控件随之重新定位），只有 `dy != 0` 才走基类；Cell Widget Mode 例外，
  因为横向窗口本身决定要物化哪些单元格。
  回归测试：`tst_relayoutqueue`（4 例：100 次失效只投递 1 个 queued 调用且只跑 1 趟；
  纯横向滚动 0 趟纵向 pass 且列位置确实移动；纵向滚动仍然跑；Cell 模式横向滚动照样物化）。
* **横向偏移走 64 位（P1-5）**：纵向早有 `ScrollMapper`，横向没有 —— `syncHorizontalScrollBar()`
  把区间夹到 `INT_MAX` 后还把 `QScrollBar` 的 int 值写回 `HeaderGeometry::viewportOffset()`，
  于是超过 INT_MAX 的横向偏移会被静默截断；`TablePaneLayout::extentOf()` / `ResolvedPane::extent`
  也是 int，超宽表格会溢出。现在：
  - 表格持有一个横向 `ScrollMapper`，滚动条只承载压缩值，逻辑偏移始终是 qint64；
  - pane 的 extent 全部 qint64（`groupExtent`/`maximumGroupOffset` 本来就是）；
  - 离屏列的视口 x 夹到"窗口附近的哨兵范围"（±2^20）而不是 INT_MIN/INT_MAX —— 之前那种夹法
    会让后面的 QRect/QWidget 运算溢出（Qt 6.11 用 checked int，直接断言崩溃）；
  - 原生 `QHeaderView` 自身无法表示 > INT_MAX 的内容范围（它内部就是 checked int），
    所以 `NativeHeaderView` 在这种情况下跳过镜像并 `qWarning()` 一次，而不是让 Qt 断言。
  回归测试：`tst_tablepanes::horizontalOffsetSurvivesBeyondTheIntRange`（30,000 列 x 100,000 px
  = 3e9 px：逻辑偏移能到最大、中点到中点精确、最后一列的右边缘落在视口右边缘）。
* **横向热路径不再是 O(总列数)（P1-4）**：横向滚动的每一步都会走
  `offsetChanged -> updatePaneLayout -> TablePaneLayout::update`，而 `update()` 会重建
  "每个列一份"的缓存（列 x、pane 归属、每个 pane 再扫一遍自己的列），`columnsForLayout()` 又从
  visual 0 扫到 count-1 —— 100,000 列的表每个滚轮刻度都要处理 100,000 列。现在：
  - `TablePaneLayout` 为每个 pane 保存**列宽的局部前缀和** + 每个列在其 pane 里的槽位；
    `columnViewportX()` 改成按需 O(log) 计算（不再有"滚动时逐列重写"的缓存）；
  - 新增 `refreshScrollWindows()`：只对每个 pane 做两次二分（窗口起止），O(pane 数 x log 列数)；
    `columnsForLayout()` 也改成只走窗口 + overscan（+ 冻结列），不再扫描整个 visual 顺序；
  - 表格的 `offsetChanged` 改走 `updatePaneLayoutForScroll()`（快路径），结构/尺寸变化仍然走
    完整的 `updatePaneLayout()`；
  - `VirtualHeaderView::relayout()` 不再每次重建完整 visual order：只有
    `HeaderGeometry::orderRevision()` 变化（或显式请求过渡）时才重新推导顺序，整表表头的可见
    区间交给 `geometry->visibleVisualRange()`（二分）。
  回归测试：`tst_tablepanes::scrollingDoesNotWalkEveryColumn`（20,000 列：结构 pass 访问
  >= 20,000 列，一次滚动的访问数 < 100，且窗口确实移动了；诊断接口
  `horizontalLayoutColumnVisits()`）、`tst_headergeometry::orderRevisionOnlyMovesWhenTheOrderCanChange`
  （滚动/改宽/排序指示器不变，隐藏、移动、改列数会变）。
