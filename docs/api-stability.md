# API 稳定性与冻结清单（v1.0）

> 本文回答"哪些 API 能依赖、改动要怎么记"。它是 v1.0 的公开边界声明；版本号与
> SOVERSION 的收口在 roadmap 的 3b（ABI 策略）里做，本文只冻结**源码层面的** API 语义。

## 1. 稳定性分级

公开头文件都装进 `include/virtualitemviews/`，但它们的承诺强度不同，分四层：

| 层 | 含义 | 兼容承诺 |
| --- | --- | --- |
| **A 应用 API** | 业务代码直接调用的入口 | 冻结：类名/函数名/信号名/枚举名到下一个主版本不变；只允许**追加**（新类、新函数、新枚举值、新信号），不允许删函数、改签名、改默认值语义 |
| **B 扩展 API** | 子类、自定义策略/渲染器、少见场景 | 冻结同上，但允许在**次版本**里调整 protected 契约（须记进 `docs/roadmap.md` 的决策记录 + CHANGELOG） |
| **C 诊断 API** | 测试、基准、调试面板 | 不保证名字与返回结构；只保证"要么能用、要么在 CHANGELOG 里写明改了" |
| **D 私有实现** | `src/**` 的一切（容器、缓存、`PaneClipHost`、`ColumnHost` 的布局规则等） | 无承诺，随时可改 |

判断方法：**能不能只用 A 层写一个业务页面**。能，就说明 A 层边界完整；A 层里出现
"为了测试才存在"的入口，就是分级没做对（本次复核删掉的两个正是这种）。

## 2. 冻结规则

1. **只加不删**（A/B 层）：新增 API 随时可以；删除或改签名只能进主版本，并且要在
   CHANGELOG 的 `Breaking` 段落里写清"改了什么、怎么迁移"。
2. **不改默认值语义**：`setX()` 的默认参数、`X()` 的默认返回值属于契约的一部分，
   例如 `setPanes({})` 回到默认三段布局、`moveColumn()` 程序化换序默认不动画。
3. **不新增"接受但忽略"的入口**：一个公开入口要么完整实现，要么明确拒绝（返回
   `bool`/`nullptr`/文档写明"不支持"）。`LayoutPolicy::setSizeIndex()` 的默认实现就是这条
   规则的具体化：它没有索引模型，但仍然履行 `takeOwnership` 的所有权约定。
4. **所有权必须在签名里可见**：`takeOwnership = false` 是默认值；`takeOwnership = true`
   表示"我接管、我会删"。有所有权的重载不能悄悄把它交给别人。
5. **诊断接口不写进业务代码**：C 层随时可能改名，业务依赖它们就等于自愿承担破坏性变更。
6. **Qt 版本差异不外泄**：公开签名不出现 `QList<int>`/`QVector<int>` 这类在两代 Qt 里
   不同的类型（`onDataChanged` 那种私有槽除外），见 README 的兼容约定表。

## 3. 复核清单（2026-09-25，roadmap 3a）

23 个公开头文件全部过了一遍，逐项结论如下。

| 头文件 | 层 | 结论 |
| --- | --- | --- |
| `virtuallistview.h` | A | 冻结。`setRootIndex()` + `ListLayout` 访问器；条目少、语义清楚 |
| `virtualtreeview.h` | A | 冻结。展开/折叠、缩进、分支装饰、`expanded()`/`collapsed()` 信号 |
| `virtualtableview.h` | A | 冻结。表头/列状态/冻结列与 pane/行高/span/排序/持久化/两种 materialization 模式 |
| `widgetadapter.h` | A | 冻结。4 个虚函数是最小契约 |
| `tablewidgetadapter.h` | A | 冻结。`TableWidgetAdapter`/`CellWidgetAdapter`/`ColumnHost`/`TableRowLayoutContext`/`TableSpanContext`；`layoutRowWidget()` 有默认空实现（可选项，不是"接受但忽略"） |
| `headergeometry.h` | A | 冻结。列宽/顺序/隐藏/排序/偏移的唯一事实来源 + `saveState()/restoreState()` |
| `nativeheaderview.h` | A（`HeaderViewInterface` 为 B） | 冻结。`HeaderViewInterface` 里带默认实现的可选钩子（`setPaneFilter()`/`setPaneOffset()`/动画三件套）**允许被渲染器忽略**，这是刻意的渲染器能力约定，已在文档写明 |
| `virtualheaderview.h` | B | 冻结。section 动画与拖动重排的视觉几何是公开契约（[header-animation.md](header-animation.md)） |
| `headerwidgetadapter.h` | B | 冻结。3 个虚函数 |
| `accessibility.h` | A（节点类为 C） | `installAccessibilityFactory()`/`removeAccessibilityFactory()` 冻结；`AccessibleVirtualItem`/`AccessibleVirtualItemView` 是实现细节（C 层），只通过 Qt 的 `QAccessibleInterface` 暴露 |
| `tablespan.h` | A | 冻结。`TableSpanProvider` 的锚点契约（只有锚点报 span）是语义契约 |
| `types.h` | A | 冻结。`WidgetType` 别名、`PaneSeparatorStyle`、`VisibleRange` |
| `itempane.h` | A | 冻结。`itemPanes()` 的返回值；与 `TablePane` 对称 |
| `branchindicator.h` | B | 冻结。`BranchIndicatorState` 的字段对应 `QTreeView::branch` 的状态词汇 |
| `virtualitemview.h` | A + B | 公开部分冻结；protected 部分是子类契约（B 层），`viewItemCount()`/`viewIndex()`/`isLayoutParent()` 是三个必须实现的钩子 |
| `layoutpolicy.h` | B | 冻结。见第 5 节第 1 条 |
| `listlayout.h` | B | 冻结（本次删掉了一个测试用入口，见第 5 节第 2 条） |
| `sizeindex.h` | B | 冻结。`SizeIndex` 是稳定接口、实现可换（[performance.md](performance.md)）；`sizes()`/`blockCount()`/`explicitSizeCount()`/`estimatedSize()` 是 C 层诊断 |
| `treevisibilityindex.h` | B | 冻结。性能契约（expand/collapse 不重走整棵树）是文档化契约；`modelQueryCount()` 是 C 层诊断 |
| `tablepane.h` | B | 冻结。`TablePaneSpec` 是输入、`TablePane` 是输出、`TablePaneLayout` 是引擎 |
| `scrollmapper.h` | B | 冻结（本次删掉两个重复别名，见第 5 节第 3 条） |
| `materializeditem.h` | C | `MaterializedItem` 与 `materializedItems()` 属于诊断；说明"行号不存、只存 identity"的注释是设计约束 |
| `widgetrecycler.h` | B + C | `acquire()`/`recycle()`/`setFactory()` 是 B 层；计数与 `isPooled()`/`trim()` 是 C 层诊断 |

## 4. 命名与约定（一并冻结）

* 命名空间 `viv`，公开类不加 `Q` 前缀；类 `PascalCase`，方法 `camelCase`。
* 读写成对：`setX()/x()`，布尔读用 `isX()`/`hasX()`，信号用过去式
  （`sectionResized()`、`itemDropped()`）。
* 枚举一律 `enum class`，QObject 派生类里的枚举带 `Q_ENUM`。
* "panes"：列方向是 `Column`/`TablePane`，行方向是 `ItemPane`；两者共用
  `PaneSeparatorStyle`，视觉与语义都对称（[row-freezing.md](row-freezing.md)）。
* 结果结构（`ColumnGeometry`、`TablePane`、`ItemPane`、`MaterializedItem`）只读，不承担
  第二份几何事实来源。

## 5. 本次复核发现并处理的

| # | 问题 | 处理 |
| --- | --- | --- |
| 1 | `LayoutPolicy::setSizeIndex()` 的默认实现"接受但忽略"：既不使用索引，也在 `takeOwnership = true` 时把它泄漏掉 | 默认实现改为履行所有权（`takeOwnership` 为真就删除），并在头文件写明"没有索引模型的策略也要释放"。这正是"不允许接受但忽略"的第一条落地 |
| 2 | `ListLayout::setOwnsSizeIndex(bool)`：没有任何调用者，唯一用途是测试脚手架；所有权已由 `setSizeIndex(index, takeOwnership)` 表达 | 公开 API 与其实现一起删除 |
| 3 | `ScrollMapper::toScrollbar()` / `toLogical()` 与 `toScrollBarValue()` / `toLogicalOffset()` 完全重复（只为迁就架构文档的旧名字） | 删除别名，改在类注释里注明"架构文档的旧名对应哪个 API" |
| 4 | `SelectionBehavior` 的注释写着"SelectColumns 会随 VirtualTableView 到来"，但行是物化单位、不存在列选择 | 注释改为明确"刻意没有 SelectColumns" |
| 5 | `CMakeLists.txt` 的 `VIRTUALITEMVIEWS_PUBLIC_HEADERS` 漏了 `itempane.h`（安装规则按目录复制，所以没漏装，但 target 源列表与 IDE 里缺一个公开头文件） | 补上；现在列表与 `include/virtualitemviews/` 的 23 个文件一一对应 |

## 6. 已知欠账（交接给后续步骤）

| 项 | 归属 |
| --- | --- |
| `VIRTUALITEMVIEWS_BUILD_SHARED=ON` 在 Windows 上没有导出宏（DLL 不会导出任何符号），目前实际只支持静态库 | 3b：ABI 策略里定"加 `VIRTUALITEMVIEWS_EXPORT` 还是写明只支持静态" |
| `PROJECT_VERSION` 还是 `0.1.0`、`SOVERSION = PROJECT_VERSION_MAJOR` | 3b：与 v1.0 一起收口 |
| `find_package(VirtualItemViews)` 的消费端实跑（安装 + 最小工程编译） | 3c |
| CHANGELOG 的维护节奏（每次破坏性变更必须写） | 已建 `CHANGELOG.md`，从 v1.0 起按本文第 2 条维护 |
