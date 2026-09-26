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
* `VirtualTableView::isVerticalHeaderShown()`：行号条**实际**是否上屏。`isVerticalHeaderVisible()`
  仍然只表示应用的请求，于是"变高模型超过镜像上限而临时隐藏"不再把请求本身改掉
  （见 [docs/row-freezing.md](docs/row-freezing.md) §8；旧行为是内部调用
  `setVerticalHeaderVisible(false)`，请求被吃掉且模型变小后不会恢复）。
* `include/virtualitemviews/global.h` + `VIRTUALITEMVIEWS_EXPORT`：**静态库与动态库都支持**。
  `-DVIRTUALITEMVIEWS_BUILD_SHARED=ON` 产出 `bin/VirtualItemViews.dll` + `lib/VirtualItemViews.lib`；
  `VIRTUALITEMVIEWS_LIBRARY` / `VIRTUALITEMVIEWS_STATIC` 由 CMake 目标自动传播，业务代码不必手工
  define。ABI 规则、SOVERSION 与支持矩阵见 [docs/abi.md](docs/abi.md)。
* `tests/install/consumer/`：安装包的消费端冒烟测试（独立 CMake 工程，`find_package(VirtualItemViews)`
  之后跑 28 项运行期自检）。README 新增「安装与消费」一节，`docs/abi.md` 补上装出来的目录布局。
* `scripts/validate.ps1`：一键验证。对每个 Qt kit × 库形态组合做 configure → `all` 构建 → CTest →
  12 个示例退出码 → benchmark 不变量 → 安装 + 消费端冒烟测试，退出码 = 失败步数。新增 `-Asan`
  开关：为每个 kit 额外建一个 MSVC AddressSanitizer 构建树（`-DCMAKE_CXX_FLAGS=/fsanitize=address`，
  运行期 `ASAN_OPTIONS=detect_leaks=0`，跳过基准与安装消费端），实测 Qt 6.11.2 与 5.15.2 的
  28 个 CTest 目标 + 12 个示例全绿、无 ASan 报告（见 [docs/ci.md](docs/ci.md) §5）。
  示例一步现在还会捕获输出并拒绝**任何库诊断**（库的 `qWarning` 一定带类名，如
  `VirtualTableView::setHorizontalHeader(): …`）：审查建议的 `QT_FATAL_WARNINGS=1` 在
  offscreen 平台下不可用（Qt 自己的 offscreen 插件与缺失字体目录就会警告，实测 12 个示例全部
  abort），所以用这条等价的检查代替；Linux/CI 侧用 Xvfb + xcb 时可以开真正的 fatal warnings。
  另有 `-Release` 开关：用 Release 构建树（`cmake-build-release-qt{6,5}[-shared]`）跑
  构建 → 28 个 CTest 目标 → 12 个示例 → 三档基准 → 安装 + 消费端，两个 Qt 版本 14 步全绿。
  还有 `-MinGW` 开关：Windows 上的 GCC / Clang 覆盖（Qt 安装器的 `mingw_64` +
  `mingw1310_64`、`llvm-mingw_64` + `llvm-mingw1706_64`、`mingw81_64` + `mingw810_64`），
  三个 kit 各建 `cmake-build-{debug,release}-mingw-<kit>`，Debug 与 Release 各 21 步全绿。
  以及 `-UBSan` 开关：llvm-mingw Clang 加
  `-fsanitize=undefined -fno-sanitize-recover=undefined`（MSVC 没有 UBSan），
  28 个 CTest + 12 个示例 + 3 档基准全绿，插桩由 `__ubsan_handle_*` 符号确认。
* [docs/performance.md](docs/performance.md) §3 的 v1.0 基线：列表 1M 行、表格 20 万行 x 100 列
  （Row/Cell 两种模式）、树 1M 顶层节点的实测数字与确切命令，供后续回归对比。
  **Release 基线**（2026-09-26 补）：同一台机器的 Debug/Release 对照表（打开快 3~35 倍、稳态滚动
  0.03–0.05 ms/步），外加 Release 下 28 个 CTest 目标全绿（`scripts/validate.ps1 -Release`）。
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

修复来自 `VirtualItemViews_Full_Code_Review.md`（第一轮审查，本地保留、不入库）的 P0/P1
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
* **行号条的"禁用"不再是粘滞状态（P2-9）**：变高模型超过 100 万行时行号条会被隐藏，但那个标志
  是**粘的** —— 模型变小或切回均匀行高后不会自己回来，而应用手工 `setVerticalHeaderVisible(true)`
  之后标志仍是"已禁用"，下一个大模型也不会再隐藏。现在拆成两个概念：`m_verticalHeaderVisible`
  是**应用的请求**，`m_verticalHeaderSupported` 是当前模型能否镜像（变高且超限时 false），上屏与否
  是两者的与（`isVerticalHeaderShown()`）。于是隐藏只在"状态切换"时发生并只警告一次，模型变小 /
  改回均匀行高时条子**自己回来**，应用的请求也不会被后台改写。回归测试：
  `tst_virtualtableview::theRowHeaderComesBackWhenTheModelShrinks`（已确认把状态改回粘滞后该用例
  会失败）。
* **pin 一个离屏项会真的把它物化出来（P2-2）**：`setItemPinned()` 的注释写的是"离开 overscan 窗口
  也保持物化"，但物化范围只来自"可见 + overscan + 冻结"，显式 pin 仅能阻止**已经存在**的控件被
  回收 —— pin 一个从未上屏的行等于什么都没发生（业务按注释写"pin 住它，等我异步结果回来"就会拿到
  空指针）。现在显式 pin 的行会作为独立的单行范围进入物化集合（与既有范围去重，所以不会物化两份，
  Cell Widget Mode 下同样生效），几何仍取该行自己的（离屏很远时按既有规则夹到 int 范围内）。
  代价是"每个 pin 一个真实控件"成为事实，这也是 `setMaxPinnedItems()` 那条诊断的意义所在。
  回归测试：`tst_virtualitemview::pinningAnOffscreenItemMaterializesIt`（已确认还原旧实现时该用例
  会失败）。
* **树的 expand() 不再整体重建可见行表（P2-10）**：`TreeVisibilityIndex::expand()` 以前是
  `mid(0, row+1) + 子树 + mid(row+1)` —— 分配一个 `可见行 + 子树` 的新向量，再把旧表**整份**拷贝
  两次。现在原地 splice：一次 `resize` + 一次尾部 `memmove` + 只写入新的子树。基准
  （`bench_listview --tree`，100 万可见行，新增了一段"只看索引、不看视图"的场景）显示：在列表
  **顶部**展开一条分支从 9.0 ms 降到 **1.4 ms**（`std::move_backward` 在 MSVC 上并没有变成
  memmove，这才是大头），"在**末尾**展开"（没有尾部搬移）是 0.05 ms，稳态的 expand+collapse
  一对是 **2.9 ms**。**尾部搬移本身仍然存在**（1M 行 ≈ 8 MB ≈ 1.5 ms，这是扁平向量的固有代价）：
  去掉它要把可见行表换成 rope / 分块，而那会改变 `TreeVisibilityIndex`（公开的 B 层类）的成员
  布局 —— 当时的理由是 [abi.md](docs/abi.md) §4 把"改数据成员布局"列为 ABI 破坏，所以审查说的
  "后续优化"被排到**下一个主版本**；同一天的发布策略定案（见本节上面的 Changed 段）改成只承诺
  源码兼容之后，这条硬约束不再成立，推迟变成排期选择（决定记在 roadmap 的决策表里）。
* **多滚动组的宽度分配不再"越排越窄"（P2-7）**：非主滚动 pane 的份额用"还在流动的剩余宽度"
  当分子，分母却始终是**全部**滚动组的 extent，于是每个 pane 都比前一个按比例小一点：900 px
  视口里三个等宽组得到 134 / 100 / 66，组再多尾巴会塌成 0（业务看到的是"第二个组几乎没法滚、
  第三个组根本看不见"）。现在每个非主 pane 都按**留给滚动 pane 的总宽度**算自己的份额，主 pane
  仍然吃取整余数，所以宽度之和恒等于视口宽度、组间比例与 extent 一致（等宽组就是等宽）。
  只有一个滚动 pane 时逐像素不变。回归测试：
  `tst_tablepanes::scrollingPanesShareTheWidthProportionally`（已确认还原旧公式时该用例会失败）。

### Fixed（第二轮代码审查 Wave A：生命周期 / 表头身份，2026-09-26）

第二轮全量审查（`VirtualItemViews_Second_Full_Code_Review.md`，本地保留、不入库）列出 3 个 P0 +
10 个 P1 + 4 个 P2。Wave A（崩溃 / 过期身份）四条的落地：

* **`~VirtualTableView()` 先删 owned table adapter、基类才 unbind 已物化行（P0-1 UAF）**：
  Row Widget Mode 下"自己拥有 table adapter 且销毁时还有已物化行"的路径会在**已释放**的 adapter 上
  调 `unbindWidget()`。新增回归用例（`tst_adapterreplacement::destroyingTheViewUnbindsRowsBeforeItDeletesTheOwnedTableAdapter`）
  在旧代码下**直接崩溃**（MSVC Debug：`VirtualItemView::recycleItem()` 里的访问违例，调用栈就是审查
  推演的 `~VirtualTableview → recycleAllItems → m_adapter->unbindWidget`）。现在派生析构的第一
  阶段就 `recycleAllCells(); recycleAllItems(); recycler()->clear();`，即"所有 adapter 都还活着
  时"把行与池清干净，基类那一趟就是空操作。
* **Tree 与 Header 的非 owning QObject 协作者仍是裸指针（P0-2）**：
  - `TreeVisibilityIndex::m_model` 改成 `QPointer`：模型被删除后 `visibleRowCount()` 返回 0、
    `indexAtVisibleRow()` 返回无效索引、`rebuild()` 顺带清掉展开状态与 root；`depth()` /
    `isExpanded()` 也先查模型（构造 `QPersistentModelIndex` 会**注册**到模型上，拿一个过期索引进来
    就是一次悬空解引用）。
  - `VirtualHeaderView::m_labelModel` / `m_geometry` 与 `NativeHeaderView::m_geometry` 改成
    `QPointer`：standalone 表头在业务先删 model / geometry 之后不再解引用悬空指针。
  实测（修复前）：树在模型删除后仍报 23 个"可见行"；standalone Widget Header 的 `labelModel()`
  仍返回悬空指针，随后在 `QList` 里触发 Qt 的 `ASSERT: size_t(d.size) <= MaxSize` 致命断言。
  回归测试：`tst_modellifetime::deletingTheModelBehindATreeIsSafe` /
  `deletingTheLabelModelBehindAWidgetHeaderIsSafe` / `deletingTheGeometryBehindAStandaloneHeaderIsSafe`。
  **内部成员类型变化**（`TreeVisibilityIndex` 与两个表头渲染器的私有成员）：按当时
  [docs/abi.md](docs/abi.md) §4 的口径属于 ABI 变化；因为 1.0 仍未打 tag，记在这里作为
  1.0 定稿前的内部收口（1.0 的发布策略定案后，这类**私有**成员变化在 1.x 的次版本里本来也是
  允许的 —— 见本节上面的 Changed 段）。
* **Widget Header 不会重新 bind 已存在的 section（P0-3）**：`headerDataChanged` / 列结构变化 /
  `modelReset` 都只是 `relayout()`，而 `relayout()` 只 bind **新 acquire** 的控件 —— 重命名一列后
  屏幕上的 section 仍显示旧标题，结构变化后 section 与逻辑列身份错位。
  现在：`headerDataChanged` 只重绑被点名的 logical 区间；`columnsAboutToBeInserted/Removed/Moved`
  与 `modelAboutToBeReset` 先 `recycleAllSections()`（在**旧身份**仍有效时 unbind），随后的
  inserted/removed/moved/reset 再 `relayout()` 重新 acquire + bind；几何的
  `sortIndicatorChanged` 也重绑已物化 section（排序箭头这类状态由 `bindSection()` 画）。
  `HeaderWidgetAdapter` 的契约随之写明：`bindSection()` 会对**已绑定**的同一个控件再次调用，
  `unbindSection()` 只是回收钩子。回归测试：
  `tst_virtualheaderview::modelChangesRebindTheMaterializedSections`、
  `sortIndicatorChangesRebindTheSections`（修复前：标题保持 "c1"、结构变化后文本错位）。
* **列结构变更时 pane 状态的 remap 顺序反了（P1-1）**：模型处理器先改 `HeaderGeometry`（它同步
  发 `geometryChanged` → `updatePaneLayout()` 重建 pane 缓存），之后才 remap `TablePaneLayout` 里
  的 frozen 集合 / 显式 pane 规格 —— 于是缓存里的 pane 归属、每列的 pane 槽位与前缀和都是"上一次
  结构"的答案。现在 insert/remove/move 三个路径都**先 remap pane 状态、再改几何**（几何的
  `geometryChanged` 就是那次统一重建）。回归测试：
  `tst_headerstructure::paneCacheIsUpToDateImmediatelyAfterAColumnInsert`（修复前 pane 的列集合
  仍是 `{0,1}`，正确的 `{0,2}` 要等下一次结构变化）。

### Fixed（第四轮 Release Gate：A/B/C 三组，2026-09-26）

第四轮全量审查（`VirtualItemViews_Fourth_Release_Gate_Review.md`，本地保留、不入库）定位为
v1.0 tag 前的 Release Gate：确认第三轮的问题都已修复，另外提出 4 个 tag 前必修项（Gate A）、
3 个 API 契约定稿项（Gate B）与 3 个文档收口项（Gate C）。三组都已落地：

* **`VirtualItemView::setAdapter()` 改成 virtual，`VirtualTableView` 同签名 override（P1）**：
  第三轮只修好了直接调用（typed 重载 + 名字隐藏），通过 `VirtualItemView *` 的多态调用仍然只写
  基类指针 —— 于是 `m_tableAdapter = A`、`m_adapter = B`，A 的 recycler 继续造控件而 B 去
  `bindWidget()`，两者 WidgetType 语义不同时就是 UB。现在 override 里 `dynamic_cast` 到
  `TableWidgetAdapter`：不是表格 adapter 就警告并保留现有配置（`nullptr` 表示清空），
  `setTableAdapter()` 仍是 typed 便捷入口。回归：
  `tst_adapterreplacement::thePolymorphicSetAdapterKeepsTheTableInvariant`。
* **column-0 结构变化不再"改活着的 MaterializedItem 的索引"（P1）**：第三轮的 re-key 路线缺三件
  事 —— `unbindWidget()` 拿不到旧身份（业务在 unbind 里退订 / 取消异步都会漏）、`WidgetType` 没有
  重算、`m_itemLookup` 没有重建（`widgetForIndex(canonical)` 返回 nullptr）。现在 about-to-change
  时 `recycleAllItems()`（旧索引仍有效，老实 unbind，控件进池复用），after-change 时按 canonical
  `(row, 0)` 重新物化并重键显式 pin；Tree 额外在列结构信号上重建可见行映射（不重置实测行高）。
  回归：`tst_virtualtableview::columnZeroChangesReleaseTheRowWidgetsFirst`。
* **`VirtualTableView::setModel()` 同步所有派生表头（P1）**：主表头会换 label model，但已经物化的
  pane 渲染器与冻结行条只在"第一次创建"时拿到 model，列数相同的模型切换会让它们继续画旧标题
  （Widget 表头还会通过 B 的 adapter 重绑旧模型的信号）。现在 setModel() 统一把新 model 交给
  `m_paneHeaders` / 两个冻结行条。回归：
  `tst_virtualtableview::switchingTheModelRebindsEveryPaneHeader`。
* **`VirtualHeaderView::setAdapter()` 交接当前 label model（P1）**：
  `setLabelModel(); setAdapter();` 这个顺序原来让新 adapter 收不到 model（旧实现下自绘 section 会在
  `bindSection()` 里解引用空模型直接崩），现在两种顺序结果一致。回归：
  `tst_virtualheaderview::installingAnAdapterHandsOverTheCurrentLabelModel`。
* **`HeaderWidgetAdapter::setGeometryModel()` 新钩子（Gate B）**：与 `setLabelModel()` 对称 ——
  自绘排序状态的 section 原来只能自己抓 `HeaderGeometry*`，而 `setGeometryModel()` 是公开可替换
  API、几何还可能先销毁。现在表头在 `setGeometryModel()` / `setAdapter()` 时把几何交给 adapter，
  几何销毁时通知 `nullptr`（label model 同理）。README 与 `examples/table_custom_header` 的示例
  adapter 改用钩子 + `QPointer`，不再示范"构造函数里抓裸 model"。回归：
  `tst_virtualheaderview::theAdapterFollowsTheGeometryCollaborator`。
* **文档收口（Gate C）**：`abi.md` §4 补上"新增带默认实现的 non-pure 虚函数**不是**源码破坏
  （要求重编即可）；改签名 / 新增纯虚 / 删除虚函数才是"，`api-stability.md` 第 1 条与 B 层承诺
  按同一口径改写（并修正 `headerwidgetadapter.h` 的虚函数数量）；`model-signals.md` 写明
  List / Tree 的列方向契约（column 0 由内核统一处理，非 0 列只有 Table 关心，Tree 会重建可见行
  映射）；`tablewidgetadapter.h` 的 `columnsToLayout()` 注释改成"每个 pane 只给窗口 + overscan"；
  CHANGELOG 里指向"不入库的审查文档"的 Markdown 链接改成普通代码文本。

验证：四处 Gate A 修复都做了反向验证（旧实现下用例分别失败 / 崩溃），然后
`scripts/validate.ps1 -Library Both`（Qt 6.11.2 + Qt 5.15.2 x 静态/动态库）28 步全绿。

### Changed（1.0 发布策略定案：1.x 只承诺源码 API，二进制 ABI 为 best effort，2026-09-26）

第三轮代码审查的 §10 指出"现有的 ABI 承诺与无 PIMPL 设计冲突"：`docs/abi.md` 当时写着"1.x
之间保证兼容；改变公开类数据成员布局算 ABI break"，而核心公开类（`VirtualItemView`、
`VirtualTableView`、`VirtualHeaderView`、`NativeHeaderView`、`HeaderGeometry`、
`TreeVisibilityIndex`、`TablePaneLayout`、`WidgetRecycler`、`BlockSizeIndex` 等）都把实现状态
直接放在头文件里 —— 照那条写，打 tag 之后"给某个私有缓存加一个成员"也要升主版本，而 1.0
收口前后三轮审查修的正是这类内部状态。审查给了两条路，**1.0 选定方案 B**：

* **承诺**：1.x 的**源码 API 与语义**兼容（[api-stability.md](docs/api-stability.md) 的四级冻结
  规则不变）；
* **不承诺**：二进制 ABI。同一套工具链 + 重新编译是这个库的使用方式；跨编译器 / 跨运行库
  复用同一个二进制不在支持范围；
* **因此在 1.x 的次版本里允许**增删公开类的**私有**成员、改私有成员类型（使用者升级时重编即可）；
  公开类里**按值出现**的类型（`TablePane`、`ColumnGeometry`、`ItemPane`、`MaterializedItem`…）
  不享受这条，它们的成员变化仍然只能进主版本；
* `SOVERSION` 与 `find_package` 的 `SameMajorVersion` 保留，但文档明确写出"**版本匹配策略不等于
  ABI 承诺**"。

被否决的另一条路（打 tag 前把上述 9 个类 PIMPL 化，换取真正的 1.x 二进制兼容）留作 2.0 的
候选；决策记录与理由见 [roadmap.md](docs/roadmap.md) §7。配套改动：`docs/abi.md` §1 的承诺
表格重写、§4 改成"源码层面 / 二进制层面"两列的对照表、§6 检查清单加第 0 条；`docs/api-stability.md`
新增第 7 条冻结规则；`CMakeLists.txt` 里 `SameMajorVersion` 的注释同步。这是**策略文档**的变更，
没有改任何公开签名或行为，四种构建组合（Qt 5.15/6.11 × 静态/动态）的 28 步验证仍全绿。

### Added（第三轮代码审查 Wave 4 准备：宽表基准覆盖四种 pane 形态，2026-09-26）

第三轮审查 §6 指出 `bench_listview --wide-header` 只覆盖"1 个冻结列 + 主组滚动"，因此"非主组
表头是否真的走了 O(1) 路径"没有被任何基准或测试看见。现在该场景生成**四种 pane 形态 x 两种
表头渲染器**：

* `primary group`（基线）、`one frozen column`、`second scroll group`（显式 pane 列表，滚第二个
  组）、`many frozen columns`（冻结一半的列，冻结 pane 比视口宽）、`sparse pane {0, half, last}`
  （三列在全局视觉顺序里跨整张表）。
* 每种形态报告：结构性 pass 的列访问数、可见列数、实例化行数、per-step 耗时、Widget 表头物化的
  section 数；并自带两条与机器无关的不变量 —— **滚动访问数 < 100**（窗口是二分 + 可见槽位，
  不是整表扫描）与 **物化 section 数 <= 64**（pane 只物化自己窗口里的列），违反即非 0 退出码。
* `scripts/validate.ps1` 的基准一步新增 `--wide-header --wide-columns 10000 --steps 100`
  （Debug 约 9 s），于是这四个形态每次一键验证都会跑；10 万列仍是手动档。
* `docs/performance.md` §3 写明这四种形态、自动档参数与实测每步耗时，§4 补充"10 万列手动档要
  几分钟、且那段时间主线程不处理事件（native 表头的结构性整表同步），实测被 Windows 的
  '无响应'判据终止过两次"。

### Changed（第三轮代码审查 Wave 3a：API 统一与文档同步，2026-09-26）

第三轮审查的 Wave 3 里有两件不需要拍板的事先做掉，ABI 策略（PIMPL 化 vs 只承诺源码 API）
单独留待决定，见 [roadmap.md](roadmap.md) §6 与 [abi.md](abi.md)：

* **`VirtualTableView::setAdapter()` 不再是"编译得过但什么都不做"的入口（P1）**：表格公开继承
  `VirtualItemView::setAdapter(WidgetAdapter *)`，而它只写基类 `m_adapter`，行物化读的却是
  `m_tableAdapter` —— 于是 `table.setAdapter(&tableAdapter)` 能编译、`adapter()` 非空，
  却一个行控件都不创建。现在表格提供一个同名的 typed 重载
  `setAdapter(TableWidgetAdapter *, bool takeOwnership = false)`，按 C++ 的名字隐藏规则挡掉基类
  那个重载，三个视图的主入口因此统一成 `setAdapter(...)`（List / Tree 本来就是自己的类型）；
  `setTableAdapter()` 保留为别名，示例与文档不用改。内部 `setTableAdapter()` 里那句对基类
  setter 的调用改成显式限定 `VirtualItemView::setAdapter(...)`（否则会递归回自己）。
  回归测试：`tst_virtualtableview::setAdapterConfiguresTheTableNotJustTheBase`（还原旧实现时
  该用例在 `tableAdapter()` 上失败）。
* **`setLayoutPolicy()` 明确成"初始化期钩子"**：List / Table / Tree 都在构造期把自己的策略指针
  缓存成成员（`m_listLayout` / `m_rowLayout`），运行时替换策略会让这些缓存悬空。审查给了两个
  选项（写成"仅初始化期使用"，或给 Table 补一套替换契约），1.0 前选前者：`virtualitemview.h`
  现在把这条写清楚（B 层子类契约）。
* **文档 drift 同步（P1/§11）**：`model-signals.md` 的 Table 列表格重写成实际顺序
  （重绑已物化行 → pane 逻辑 remap → 几何结构变化 + sort guard，含 `modelReset` 的列收口）；
  `ci.md` 开头那句"Linux / GCC / Clang / ASan 未实测"改成"Windows 上 GCC/Clang/ASan/UBSan
  已实测，Linux/macOS 未实测"；`api-stability.md` 里那条"公开签名不出现 `QVector<int>`"
  与实现不符，改成真正的约定"公开容器统一写 `QVector<T>`"（Qt 6 里它是 `QList` 的别名，
  Qt 5 里是独立类型，混用会让同一份业务代码代入不同签名集）。

### Fixed（第三轮代码审查 Wave 2：超宽表收尾，2026-09-26）

第二轮审查把"结构性 pass 与滚动 pass"分开之后，第三轮又指出三处仍然按**总列数**付费的地方
（`VirtualItemViews_Third_Full_Code_Review.md` §6/§7/§8）：

* **Native 表头 pane 的 offset 走 O(1)（P1）**：`TablePaneLayout::setGroupOffset()` →
  `refreshScrollWindows()` 已经是"二分 + 窗口"的，但 `syncHeaderPanes()` 随后调用的
  `NativeHeaderView::setPaneOffset()` 仍然 `syncHeaderFromGeometry()` —— 一次完整的
  section 遍历（宽度 / 顺序 / 隐藏 / 排序全都重读一遍）。于是"body 快、pane 表头慢"：20,000 列
  的非主滚动组每走一格就是一次 O(总列数) 的 pass。现在 `setPaneOffset()` 只 `setOffset(clamp(偏移))`，
  和主表头跟随 `HeaderGeometry::offsetChanged` 的快路径一致：偏移不改变任何 section 的宽度、
  顺序或可见性，整表同步在这里从来就不是必需的。回归测试：
  `tst_tablepanes::nativePaneHeaderKeepsItsOffsetCheap`（20,000 列、第二个滚动组、100 步，断言
  `NativeHeaderView::fullSyncCount()` **一步都没涨**，同时断言偏移真的生效、pane 首列已滚出）；
  还原旧实现时该用例失败。
* **frozen pane 的 body 也按 pane 窗口物化（P1）**：`columnsForLayout()` 对非滚动 pane 特判成
  "把该 pane 的所有列都 append" —— `refreshScrollWindowsImpl()` 明明已经为**每个** pane（含冻结
  pane，其 offset 固定 0）算好 `firstSlot/lastSlot`。于是 5 万冻结列会变成"每个可见行 5 万次
  cell 尝试"（Cell Widget Mode）或"`columnsToLayout()` 返回 5 万列"（Row Widget Mode）。现在
  冻结 pane 与滚动 pane 用同一套窗口语义（冻结 = offset 0 + 视口宽度），body 的上限与表头对称：
  `visibleRows x (可见冻结列 + 可见滚动列 ± overscan)`。回归测试：
  `tst_tablepanes::frozenColumnsFollowThePaneWindow`（一条 3 冻结列的常规表 + 一条
  "20,000 列里冻结 10,000 列"的极端表，断言 `visibleColumnLogicalIndexes()` /
  `materializedCellCount()` 都受视口约束，且滚动仍走窗口快路径）；还原旧实现时用例失败
  （返回 10,000 列）。
* **sparse explicit pane 的 Widget 表头直接遍历 pane slots（P2）**：显式 offset pane 已经用 pane
  前缀和二分得到 `firstSlot/lastSlot`，但随后又换算成**全局视觉区间**
  `visualIndex(paneOrder[firstSlot]) … visualIndex(paneOrder[lastSlot])` 并逐 visual 扫一遍 ——
  pane 的列在全局顺序里可以稀疏到 `{0, 50000, 99999}`，三个 slot 之间隔着 10 万个 section。
  现在该路径直接把 `firstSlot - overscan … lastSlot + overscan` 映射回 `m_paneOrder[slot]`，
  真正做到 `O(log paneColumns + 可见 paneColumns)`。为了让这条性质可测（而不是只看物化数量 ——
  旧实现靠 pane 过滤集合过滤掉了扫描结果，物化数量看不出差别），`VirtualHeaderView` 新增
  诊断计数 `materializationVisits()`：上一次 materialization pass 实际看过的 section 数。
  回归测试：`tst_tablepanes::sparseExplicitPaneMaterializesOnlyItsWindow`（100,000 列、
  `{0, 50000, 99999}`、断言物化 3 个 section 且 pass 访问数 <= 16）；还原旧实现时该用例失败。

### Fixed（第三轮代码审查 Wave 1：生命周期与身份边界，2026-09-26）

第三轮全量审查（`VirtualItemViews_Third_Full_Code_Review.md`，本地保留、不入库）在"上一轮
Wave A/B/C 都已修"的基础上给出 1 个新 P0、若干 P1/P2 与一个发布策略决策。Wave 1（生命周期 /
身份）四条：

* **pane 渲染器跟随主表头的 adapter 替换（P0-1 UAF）**：表格为冻结 / 显式 pane 克隆的
  `VirtualHeaderView` **借用**主表头的 adapter（`header->setAdapter(widgetHeader->adapter())`），
  而公开的 `primaryHeader->setAdapter(newAdapter, true)` 会立刻删除旧 adapter —— pane 克隆仍然
  指向它，之后任何 relayout / recycle / 析构都会在已释放对象上调 `unbindSection()`。
  现在 `VirtualHeaderView` 新增 `adapterAboutToChange()` / `adapterChanged()` 两个信号，
  表格安装 widget 主表头时连上它们：替换前先销毁派生 pane 渲染器（此时旧 adapter 还活着，
  unbind 走的就是它），替换后重建（borrow 新 adapter）。回归测试：
  `tst_adapterreplacement::replacingThePaneHeaderAdapterRebuildsItsClones`（还原旧实现时用例
  先断言失败、随后在 `~VirtualHeaderView::recycleAllSections()` **崩溃**）。
* **列 0 结构变化后的行身份保持 canonical（P1）**：表格的行物化身份是 **(row, 0) 这个 cell**，
  而"在列 0 前插入 / 删除列 0 / 移动列 0"会重命名或直接失效那个 cell —— adapter 随之收到
  `column() != 0` 甚至 invalid 的行索引（`layoutRowWidget()` 同样）。现在内核在
  `columnsAboutToBe*` 且改到列 0 时快照已物化行与 pinned 行的**行号**（那时旧身份仍有效），
  在 `columns*` 之后按 `model->index(row, 0)` 重建身份（列变化不动行号），pinned 集合也一并
  重键。回归测试：`tst_virtualtableview::columnZeroChangesKeepTheRowIdentityCanonical`
  （insert@0 / remove@0 / move@0，逐项断言 adapter 只看到 valid 且 column()==0 的索引，
  并断言 `indexForWidget()` 仍是 (row, 0)；还原旧实现时 12 次 bind 拿到 column 1）。
* **表头协作者替换语义（P1）**：`setGeometryModel()` 现在失效 pane 缓存与"上次视觉顺序"备忘
  （不然 standalone 表头换了几何还在用旧前缀和打包）；`setLabelModel()` 除了重绑已物化 section，
  还会通过新的可选钩子 `HeaderWidgetAdapter::setLabelModel(QAbstractItemModel *)` 通知 adapter
  —— README 的示例 adapter 正是把 model 存在自己身上，否则"表头有 label model"和"adapter 里
  又存一份"永远是双状态（切换模型后 section 仍显示旧标题）。回归测试：
  `tst_virtualheaderview::switchingTheLabelModelRebindsTheSections` /
  `switchingTheGeometryInvalidatesThePaneCache`。
* **`restoreState()` 履行 order/sort 通知契约（P1）**：restore 会整体替换视觉顺序、隐藏集合与排序
  状态，但既没有 bump `orderRevision`（依赖它跳过重建的渲染器会继续用旧顺序），也不发
  `sortIndicatorChanged`（自绘排序箭头的 section 保持旧状态）。现在 restore 提交后 bump
  revision，排序状态变化时补发信号；`VirtualTableView::restoreHeaderState()` 在恢复期间置
  `m_sortGuard`，避免这个"结构性信号"被当成"用户要求排序"再去 `model->sort()`。Widget 表头也
  在 `bulkGeometryChanged` 上重绑已物化 section（限额 / 默认尺寸 / restore 这类变化没有粒度
  信号）。回归测试：`tst_headergeometry::restoringAStateBumpsTheOrderRevision`、
  `tst_virtualheaderview::restoringASortStateRebindsTheSections`。

### Fixed（第二轮代码审查 Wave C：超宽表性能，2026-09-26）

* **pane 过滤表头不再 O(N²)（P1-8）**：`VirtualHeaderView` 在 pane 模式下每次 `isFiltered()` 都线性
  扫 `m_paneFilter`、每次 `sectionX()` 都重建并按视觉顺序**排序**该 pane 的列列表再逐列累加宽度；
  `relayout()` 又对每个可见候选调用它们 —— 10 万列的 pane 就是每趟 O(N·k)。现在渲染器持有 pane
  缓存：成员集合（`QSet` 判成员）、pane 内 committed 视觉顺序、每列宽度的前缀和与 logical→槽位表，
  只在 filter / 几何变化时重建（`geometryChanged` 才置脏，纯滚动只发 `offsetChanged`）；
  `sectionX()` 变成一次槽位查表，可见范围变成"对前缀和两次二分"。`setPaneFilter()` 也加了
  等值快路径（表格每趟都会重新下发同一个 filter）。诊断接口 `paneCacheRebuildCount()` 供测试
  断言"滚动期间不重建"。回归测试：`tst_tablepanes::headerPaneCacheIsNotRebuiltWhileScrolling`
  （2 万列 + 冻结列 + Widget 表头，50 次滚动后重建次数不变，且 section 确实跟着偏移走）。
* **非主滚动组走快路径（P1-9）**：`setHorizontalOffset(group, offset)` 以前调 `updatePaneLayout()`
  （整表结构重建，O(总列数)）；现在与主组一致走 `updatePaneLayoutForScroll()`（每个 pane 二分刷新
  窗口）。回归测试：`tst_tablepanes::nonPrimaryGroupScrollDoesNotRunTheStructuralPass`（2 万列、
  第二组滚一步的列访问数 < 100，而结构 pass ≥ 2 万；已确认还原旧实现时用例失败）。
* **Native 表头的粒度通知不再被整表同步吞掉（P1-10）**：`HeaderGeometry` 以前对**每一种**变化都发
  `geometryChanged()`，而 `NativeHeaderView` 把该信号接到整表 `syncHeaderFromGeometry()`——于是
  "拖一像素列宽"除了 O(1) 的 `applySection()` 之外还要 O(列数) 重读整份几何。现在几何多了一个
  `bulkGeometryChanged()`（只在"没有粒度信号对应"的变化上发：尺寸范围 / 默认尺寸 / stretch /
  恢复状态 / 模型侧换序），native 渲染器改为：bulk、sectionCountChanged、sectionMoved → 整表同步；
  sectionResized、sectionVisibilityChanged、sortIndicatorChanged（新增粒度处理）、
  stretchLastSectionChanged、offsetChanged → 只改对应的一点。`geometryChanged()` 对其它使用者
  （表格自身、Widget 表头、测试）语义不变。诊断接口 `NativeHeaderView::fullSyncCount()`。回归测试：
  `tst_headergeometry::granularChangesDoNotEmitTheBulkSignal`、
  `tst_virtualtableview::nativeHeaderStaysInSyncWithEveryGeometryChange`（每次列宽变化都断言整表
  同步次数没有增加，同时逐项校验 native 表头与几何仍然一致）。
* **宽表基准（审查建议的 smoke）**：`bench_listview --wide-header`（默认 10 万列 × 1 冻结列 × N 次
  横向滚动，分别用 native 与 widget 表头）。Debug 实测（200 步）：1 万列时 native 0.52 ms/步、
  widget 0.92 ms/步；10 万列时 1.17 / 1.48 ms/步 —— 列数 ×10 而每步耗时只涨 1.3~2.2 倍，说明
  每趟成本由可见窗口决定，不再跟着总列数走。
* **Native 表头的 int 几何边界写进文档（P2-4）**：`docs/table-layout.md` 明确"内容宽度超过
  `INT_MAX` 的极宽表格要用 `VirtualHeaderView`"（`QHeaderView` 自己的 section 空间是 int，
  native 渲染器在那种情况下会跳过镜像并 `qWarning()`）。

### Fixed（第二轮代码审查 Wave B：HeaderGeometry 语义与业务行 schema，2026-09-26）

* **中间插入的新列放到后继列的视觉槽位（P1-4）**：`insertLogicalSections()` 把新 section 无条件
  append 到视觉顺序末尾，于是"视觉顺序 == 逻辑顺序"的普通表头在 B 前插入 X 会显示
  `A | B | C | X`，而 QHeaderView 的行为是 `A | X | B | C`。现在新 section 落在**插入点的后继列**
  原来的视觉槽位：逻辑顺序保持逻辑顺序，自定义顺序保持自己的形状（末尾 append 仍然 append）。
  回归测试：`tst_headergeometry::insertedSectionsTakeTheSuccessorVisualSlot`（三种顺序：恒等顺序、
  自定义顺序、末尾追加）。
* **`moveLogicalSections()` 补 `orderRevision`（P1-5）**：模型侧换序会重写每个视觉槽位的 logical
  编号，但这个方法没有 bump revision，而 `VirtualHeaderView` 正是靠 revision 判断"要不要重新推导
  可见顺序"——于是换序后表头可能沿用过期的顺序缓存。现在与 `moveSection()`/insert/remove 一致。
  回归测试并入 `tst_headergeometry::orderRevisionOnlyMovesWhenTheOrderCanChange`。
* **结构性 remap 如实上报排序指示器（P1-6）**：insert/remove/move 会重命名排序指示器所指的列，
  但只有"被排序的列被删除"时发过 `sortIndicatorChanged`；列号平移与 move 后的 remap 都不发。
  现在三种 remap 只要改变了 section 编号就发一次（含 `-1`），而**表格侧加 guard**
  （`m_sortGuard`）防止这个"结构性信号"被当成"用户要求排序"再去 `model->sort()` 一次。
  回归测试：`tst_headergeometry::structuralRemapsReportTheSortIndicator`、
  `tst_headerstructure::structuralRemapDoesNotResortTheModel`（Qt 5 的 `QSignalSpy` 需要先
  `qRegisterMetaType<Qt::SortOrder>()` 才能记录该枚举参数）。
* **`setSectionCount()` 缩小时清理越界排序指示器（P1-7）**：模型 reset / `setModel` 走的是这个
  入口，它以前只截断 section 与视觉顺序，把指向被删尾部列的排序指示器留着。现在与
  `removeLogicalSections()` 一致：越界即清成 `-1` 并通知。回归测试：
  `tst_headergeometry::shrinkingTheSectionSetClearsAnOutOfRangeSortIndicator`。
* **min/max 变化即使不需要 clamp 也要通知（P1-3）**：`clampSectionsToTheSizeRange()` 在没有
  section/default 需要夹取时直接返回，而渲染器（`NativeHeaderView` → `QHeaderView::
  setMinimumSectionSize()`）只在这条通知里重新读取范围 —— 于是"把最小宽度从 24 改成 30"之后
  HeaderGeometry 与原生表头对允许宽度的认知不一致。现在两个 setter 一律走一次 bulk 通知
  （重复设置同一个值仍是 no-op）。回归测试：
  `tst_headergeometry::changingLimitsNotifiesEvenWithoutClamping`、
  `tst_virtualtableview::nativeHeaderFollowsLimitChangesImmediately`。
* **列结构变化后重绑可见行（P1-2，含 P2-2 的业务级回归）**：Row Widget Mode 下行的 identity 不
  变，所以行控件不会被回收，`bindWidget()` 也不会再调用 —— 而业务通常正是在 `bindWidget()` 里
  按列数建 `ColumnHost`，于是"几何正确、业务行还是旧 schema"。现在 insert/remove/move 三个路径
  都会先 `rebindMaterializedRows()`（复用内核 dataChanged 用的那条 `rebindItemsInModelRange()`），
  随后的 pane/几何更新再摆放业务重建出来的 hosts；Cell Widget Mode 不需要（cell 有自己的
  persistent identity）。为此 `rebindItemsInModelRange()` 从 private 移到 protected（B 层子类
  契约，1.0 定稿前的调整）。回归测试：
  `tst_virtualtableview::columnStructureChangesRebindTheRowWidgets`（用一个只发列信号的模型 +
  一个"只在 bindWidget 里重建 schema"的业务行控件；已确认去掉重绑时用例失败）。
* **Widget Header 的数据刷新用例（P2-3）** 由 Wave A 的 P0-3 两条覆盖：重命名、插入/删除/reset、
  排序指示器都断言 section 控件的**实际文本**。

### Fixed（GCC / Clang 可移植性：MinGW 实测，2026-09-26）

审查的 ABI 矩阵里"GCC / Clang / MinGW"一直是未实测项。装上 Qt 安装器自带的 MinGW kit 之后，
三个组合（Qt 6.11.2 + GCC 13.1、Qt 6.11.2 + Clang 17.0.6（llvm-mingw）、Qt 5.15.2 + GCC 8.1）
的 Debug 与 Release 各 7 步全部跑通（构建 / 28 个 CTest / 12 个示例 / 3 档基准 / 安装 /
消费端），顺带修掉六处**只有在 GCC/Clang 下才暴露**的问题 —— 它们此前被 MSVC 全部放过：

* **MSVC 专有的函数式类型转换**：`benchmarks/bench_listview.cpp` 里 30 处
  `long long(x)` —— GCC/Clang 直接报 `expected primary-expression before 'long'`。全部改成
  `static_cast<long long>(x)`。库本身没有这种写法（只有基准有）。
* **`K32GetProcessMemoryInfo` 在 MinGW 8.1 头文件里不存在**：这个 Windows 7+ 的 kernel32 别名
  需要 `_WIN32_WINNT`，老 MinGW-w64 头文件里没有声明。改成 `GetProcessMemoryInfo`（psapi，
  数值相同），并在 `benchmarks/CMakeLists.txt` 里对 `WIN32` 显式链接 `psapi`。
* **3 处覆盖虚函数但没写 `override`**：`ListLayout::sizeIndex()`、
  `VirtualHeaderView::setSectionAnimationEnabled()` / `setSectionAnimationDuration()`
  （Clang 的 `-Winconsistent-missing-override`）。
* **一处未使用的常量**：`src/core/scrollmapper.cpp` 里的 `kRoundedValueLimit`
  （Clang 的 `-Wunused-const-variable`），删掉。
* **Qt 6 弃用的 `QMouseEvent` 构造**（`tst_virtualheaderview`）：改用带全局坐标的六参构造，
  同时兼容 Qt 5（`QPointF::toPoint()`）。
* **共享库没有导出公开常量的符号**（Qt 5 MinGW 组合**链接失败**才暴露）：`tst_virtualitemview`
  的 `QCOMPARE(log.size(), VirtualItemView::kLifecycleLogCapacity)` 报
  `undefined reference to __imp__ZN3viv15VirtualItemView21kLifecycleLogCapacityE`。原因是
  `static constexpr` 成员在 C++17 里是隐式 `inline` 变量：库自己只把它当常量表达式读，于是
  DLL 里**没有**任何定义，而消费者那侧看到的是 `dllimport` —— 一旦有人把常量绑到引用上
  （`QCOMPARE` 的参数就是 `const T&`）就没有符号可解析。现在库在各自 `src/**` 里保留这些常量的
  地址（`&Class::kConstant`），强制 DLL 发出并导出定义；覆盖七个公开常量
  （`VirtualItemView::kLifecycleLogCapacity`、`ScrollMapper::kMaxScrollRange`、
  `BlockSizeIndex::kDefaultBlockCapacity`、`WidgetRecycler::kDefaultMaxPoolSize`、
  `HeaderGeometry::kStateMagic`/`kStateVersion`、`HeaderViewInterface::kFollowGeometryOffset`）。
  回归测试：`tst_virtualitemview::publicConstantsAreAddressableFromAConsumer`（对全部七个取地址；
  已确认去掉库内任意一个取地址后，Qt 5 MinGW 的 DLL 构建会在该用例处链接失败）。

复跑：`pwsh -File scripts/validate.ps1 -MinGW -Library Static`（Debug 与 Release 各 21 步）。
Linux / macOS 仍在未实测清单里（字体查找、D-Bus、X11/Wayland 都不属于 Windows MinGW 的覆盖范围）。

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
