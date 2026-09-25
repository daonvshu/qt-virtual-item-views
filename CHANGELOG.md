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
