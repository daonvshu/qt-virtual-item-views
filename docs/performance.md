# 性能模型与基准

## 1. 复杂度

| 操作 | 复杂度 | 说明 |
| --- | --- | --- |
| `FixedSizeIndex::offsetOf/indexAt` | O(1) | 除乘 |
| `BlockSizeIndex::offsetOf/indexAt` | O(log B + E_b) | 两张前缀表二分 + 例外表二分/游走（E_b = 该行之前的例外数，恒 <= 2 x capacity） |
| `BlockSizeIndex::setSize` | O(log B + E_b) | 例外表二分 + 一次内存移动，懒重建前缀表 |
| `BlockSizeIndex::insert` | O(log B + capacity) | 切一次块 + 建一个新块 + 必要时与邻块合并 |
| `BlockSizeIndex::remove` | O(B + capacity) | 按块边界切两刀后整块删除 + 归并邻块 |
| `TreeVisibilityIndex::indexAtVisibleRow` | O(1) | 可见行数组下标 |
| `TreeVisibilityIndex::visibleRowForIndex` / `depth` | O(1) / O(depth) | 查询表按 index 值建立，结构变更后整体重建 |
| `TreeVisibilityIndex::expand` / `collapse` | O(被展开子树) + O(可见行) | 只遍历被展开的子树，**不重走整棵树**；可见行数组的插入/删除是 O(可见行) |
| `relayout()` | O(W) | W = 可见 + overscan + pin |
| 稳态滚动 | O(进入窗口的项数) | 以复用为主，通常 1~2 项 rebind |

其中 B 为块数（约 N / 1024），capacity 为块容量（1024），E_b 为该行所在的块里排在该行之前的例外
数（`BlockSizeIndex` 只保存"实测过、且与估计值不同"的行，见 §4）。一百万行时前缀重建约 10^3 次
加法，远低于一次全量 O(N) 重算；没有被测量过的行不占任何额外存储。

## 2. 规模特性与校验

| 指标 | 目标 | 校验方式 |
| --- | --- | --- |
| 逻辑行数初始化 | 与行数弱相关，不创建全量控件 | `tst_virtualitemview::materializesOnlyVisibleAndOverscan`（1,000,000 行） |
| 可见控件数量 | visible + overscan + pinned | 同上以及 `materializedItemCount()` |
| 稳态滚动 | 不 new/delete | `tst_virtualitemview::scrollingReusesWidgetsWithoutAllocating`、`tst_modelmutationfuzz::widgetCountStaysBounded` |
| 内存 | 随可见项复杂度增长，不随逻辑行数线性增长 | 只持有可见项控件 + `SizeIndex` 的"块 + 稀疏例外"表（`tst_sizeindex::blockIndexStaysCompactWithoutMeasuredSizes` 守住"未测量的行不占存储"） |
| 单项 `dataChanged` | 只影响对应可见控件/尺寸 | `tst_virtualitemview::dataChangedRebindsOnlyAffectedWidgets`（bind 次数 +1） |
| resize | 不重建模型，不全量创建控件 | `tst_virtualitemview::resizeUpdatesVisibleRange` |
| Table 列 resize | 只更新 materialized 行（不 rebind、不重建、不遍历逻辑行） | `tst_virtualtableview::columnResizeTouchesMaterializedRowsOnly`、`hugeModelColumnResizeDoesNotWalkRows`（1,000,000 行） |
| Table 列 move/hide | 行控件列边界跟随 HeaderGeometry，无第二套列宽 | `tst_virtualtableview::columnMoveFollowsTheGeometry` / `hiddenColumnHidesHost` / `geometryIsTheSingleAuthority` |
| Table 横向滚动 | 表头与 body 共享同一像素偏移，不漂移 | `tst_virtualtableview::horizontalScrollKeepsHeaderAndRowsAligned`、GUI `tst_tableviewinteraction::horizontalWheelScrollsBodyAndHeader` |
| Table 表头状态 | saveState/restoreState 往返一致（列宽/顺序/隐藏/排序/偏移） | `tst_headergeometry::saveRestoreRoundTrip`、`tst_virtualtableview::headerStateRoundTrip` |
| Table Cell Widget Mode | 只 materialize `visibleRows x visibleColumns`（不是 rows x 全部列），双向滚动零分配 | `tst_tablecellmode::materializesVisibleCellsOnly` / `scrollingIsAllocationFree`、`bench_listview --table` |
| 大列数（100 列 x 1M 行） | 可见列区间 + 横向虚拟化；列 resize 不触碰行/其他 cell | `bench_listview --table --table-columns 100`、`tst_virtualtableview::hugeModelColumnResizeDoesNotWalkRows` |
| Tree 可见行映射 | 折叠的子树只付一次 `rowCount()`；expand/collapse 不遍历整棵树 | `tst_treevisibilityindex::expandDoesNotWalkTheWholeTree`（统计模型查询次数）、`tst_virtualtreeview::expandInsertsChildRows` |
| Tree 稳态滚动 | 与 List 相同：不 new/delete，实例化集合只随视口变化 | `tst_virtualtreeview::scrollingIsAllocationFree`、`bench_listview --tree` |
| Tree 结构变更 | 保留展开状态、保持滚动锚点、按身份回收被删子树 | `tst_virtualtreeview::rowsInsertedKeepsExpansionAndOrder` / `rowsRemovedRecyclesTheSubtree` / `anchorKeepsTheTopItemWhileExpandingAbove` |
| Tree 分支装饰 | 只按可见行的层级数绘制：一格 = 一次渲染器调用 + 少量 `rowCount()` 查询；失效区域只到"最深层可见行"的缩进宽度 | `tst_virtualtreeview::customRendererOwnsTheBranchDecoration` / `indicatorsFollowScrolling` |
| Table 冻结列（v0.7） | pane 布局只缓存"列 -> x"，几何/resize/偏移变化各重算一次 O(可见 section)；Row Mode 不增加控件，Cell Mode 只多实例化冻结列 | `tst_virtualtableview::frozenColumnsStayWhileTheScrollablePaneScrolls` / `frozenPanesDoNotAddScrollSpace`、`tst_tablecellmode::frozenColumnsStayMaterializedAndOnTop` |

最后一列是守住该性质的用例，它们都在 `scripts/validate.ps1` 的 CTest 一步里每次验证都会重跑；
基准里的"零分配滚动 / 实例化集合有界"断言在同脚本的第 4 步（见 §3）。

## 3. 手工基准

`benchmarks/bench_listview.cpp` 是独立程序（不注册进 CTest，因为数字与机器相关）：

```
bench_listview --rows 1000000 --steps 2000
bench_listview --rows 100000 --steps 500 --compare
bench_listview --table --table-columns 100
bench_listview --tree
```

`--compare` 会追加 `QListView` + 默认委托（绘制基线）与 `QListWidget + setItemWidget`（极端参考，
限制在 2000 行）的对比测量。程序覆盖方案文档 §39 的指标：

| 指标 | 场景 |
| --- | --- |
| startup | `open (setModel + show + layout)` |
| QWidget count | 初始可见控件数、稳态滚动后的 live/池数量 |
| create/recycle count | 连续滚动与随机跳转期间的新建/销毁计数 |
| scroll frame time | 每步（滚动条 + relayout）耗时 |
| relayout time | `60 x resize relayout` |
| model mutation latency | `2000 x dataChanged`、`20 x insert 500`、`20 x remove 500` |
| memory | Windows 进程 working set 增量（进程级，仅作趋势参考） |

输出包含：打开时的控件数、滚动 N 步的耗时与每步耗时、滚动期间新建/销毁控件数、池大小、
实例化项数，并在违反不变量时以非 0 退出码结束：

```
  widget allocation free scrolling : yes
  materialized count bounded       : yes
```

`--tree` 追加三个场景（同样以非 0 退出码报告不变量违规）：

* **宽树**：默认 `--tree-roots 1000000`（1,000,000 个顶层节点 x 10 子节点、深度 4，逻辑节点
  11,111,100,000 个）。折叠状态就有 1,000,000 个可见行，用来验证实例化集合只随视口变化、
  稳态滚动零分配、以及"在视口上方展开/折叠时锚点行不动"。
* **展开与变更**：堆树（`--tree-roots`/`--tree-branching` 之外的小树）全量展开，再在视口上方
  insert/remove 行，验证展开状态、可见行映射与锚点。
* **只看索引的 splice**（P2-10）：不带视图，直接量 `TreeVisibilityIndex` 在 100 万可见行上的
  expand/collapse —— "在**末尾**展开"（纯追加）与"在**开头**展开"（要搬尾部）的差值就是扁平
  向量的那次 memmove；场景自带"可见行数恢复、首/末行不变"的不变量检查。

### v1.0 基线（2026-09-25 实测）

下面三张表是 v1.0 收口时的基准数字（roadmap 3d）。环境：**Debug**、Qt 6.11.2 / msvc2022_64、
MSVC 19.50 x64、Windows 11、静态构建、`QT_QPA_PLATFORM=offscreen`。Debug 的绝对值偏悲观，
只用来看趋势与"有没有数量级退化"；Release 基线尚未采集（见本文 §4 末条）。

**列表 1,000,000 行**（`bench_listview --rows 1000000 --steps 200`）

| 指标 | 值 |
| --- | --- |
| 打开（setModel + show + layout） | 15.77 ms，初始控件 27 个 |
| 稳态滚动 | 0.89 ms/步（200 步共 177.72 ms），新建/销毁 0/0 |
| 随机跳转 | 新建/销毁 0/0，实例化 30 项 |
| `2000 x dataChanged` | 11.57 ms |
| `20 x insert 500` / `20 x remove 500` | 3.18 ms / 3.24 ms |
| `60 x resize relayout` | 24.56 ms |
| 进程 working set | 打开后 +9.6 MB，变更后 +10.8 MB |

**表格 200,000 行 x 100 列**（`bench_listview --table --table-columns 100 --rows 200000 --steps 100`）

| 指标 | Row Widget Mode | Cell Widget Mode |
| --- | --- | --- |
| 打开 | 42.27 ms，实例化 26 行 x 11 可见列 | 119.51 ms，实例化 312 个单元格 |
| 垂直滚动 | 0.85 ms/步，新建/销毁 0/0 | 8.04 ms/步，新建/销毁 0/0 |
| 横向滚动（100 步） | 19.34 ms | 1012.59 ms（新建 264：新列进入视口的单元格） |
| 列宽调整（60 次） | 21.89 ms | 1043.22 ms |
| 进程 working set | +2.6 MB | +3.6 MB |

两种模式的不变量都是"垂直滚动零分配 + 实例化集合有界"；Cell Widget Mode 的横向滚动与列宽变化
要重排 312 个单元格控件，那一栏的数字是**实例化代价**，不是泄漏（交互结束后控件回到池里）。

**树 1,000,000 顶层节点 x 10 子节点（深度 4，逻辑节点 1.1e10）**（`bench_listview --tree`）

| 指标 | 值 |
| --- | --- |
| 打开 | 4225.24 ms（1,000,000 个可见行），实例化 27 项 |
| 展开一条 4 层深路径 | 174.90 ms，模型查询 48 次 |
| 折叠根节点 | 10.83 ms |
| 稳态滚动 | 2.07 ms/步（2000 步共 1032.85 ms），新建/销毁 0/0 |
| 堆树（4200 节点）全量展开 | 127.82 ms，模型查询 4400 次（≈ 2 x 节点数） |
| 视口上方 insert/remove 各 200 行 | 459.00 ms / 502.01 ms，锚点行不动（偏移 24000 -> 28800 -> 24000 px） |
| 进程 working set | +41.9 MB（1,000,000 可见行） |

树的打开成本由"1,000,000 个可见行"决定（可见行列表本身是 O(可见行)），不是由树遍历决定；
展开/折叠已经是增量的（48 次模型查询、174.90 ms），**旧的"每次 expand 重建可见行索引表"（当时
0.88 s、84 次查询）在 2a 之后就没了**；结构性变更（insert/remove/move/reset）仍然重建可见行列表，
那是冷路径（上面 insert/remove 各 200 行约 0.5 s，含锚点校正）。

这些场景的**不变量**（零分配滚动、实例化集合有界、增量展开/折叠、锚点稳定）不是"看完就丢"的
一次性结论：`scripts/validate.ps1` 的第 4 步每次都会重跑这三档基准，并要求退出码为 0。

## 4. 已知取舍

* **`BlockSizeIndex` 只记录"与估计值不同"的行（v0.9 / roadmap 2c）**：每个块保存一个基值
  （`baseSize`）+ 一张按行号排序的稀疏例外表（`QVector<Exception>`，例外 = 实测过且不等于基值的行）。
  从来没有被测量过的行不占任何存储，一千万元素的均匀内容从约 40 MB 降到约 0.6 MB（9766 个块 x
  48 B + 两张前缀表），实测尺寸则一个不漏地保留。公开接口（`SizeIndex`）与 `sizes()` /
  `estimatedSize()` / `blockCount()` 的语义都没变，另加了一个只给测试和诊断用的
  `explicitSizeCount()`（当前例外总数）。
  语义要点：`setSize()` 写入的值恰好等于所在块的基值时不建例外（等于"回到估计值"），
  `insert()` 在中间插入只切块、不搬动其它块的例外，`remove()` 先按块边界切两刀再整块丢弃，
  残留的相邻同基值块在 `remove()` 后归并。
  块的行数超过 2 x capacity 就对半切，基值相同、合计不超过 capacity 的相邻块合并（`insert()`
  只并插入点两侧，`remove()` 后统一归并），因此 E_b 恒 <= 2 x capacity，最坏情况与旧的逐行实现
  同阶，常见情况（没有例外的块）退化成 O(log B)。
* 前缀表是懒重建的：一次 `insert/remove` 后第一次查询需要付 O(B)。批量变更（模型一次性插入 N 行）
  只重建一次，符合"先正确后优化"的原则。
* `materializedItems()` 返回的列表在每次 pass 后重建（W 很小），不是热路径瓶颈。
* **树的可见行映射是增量的（v0.8 / roadmap 2a）**：`TreeVisibilityIndex` 不再维护"整表反向哈希"。
  索引 → 可见行由**每个已展开父节点一棵 Fenwick 树**（`BranchBlock`：每个子节点存自己的可见子树
  大小）自底向上累加得到：`row(节点) = Σ 各层 (1 + 该层前序兄弟的可见子树大小)`。因此
  `visibleRowForIndex()` 是 O(depth × log(siblings))，`expand/collapse` 只沿路径更新
  （每层 O(log k)），既不重建任何表，也不扫描兄弟；`indexAtVisibleRow()` 仍是 O(1)（可见行列表
  本身就是唯一事实来源）。分块只在"已展开的父节点"上分配，所以一棵宽树（一百万同时可见行）的
  额外内存约 8 字节/行，而不是原来那一整张索引哈希表。
  同一基准（`bench_listview --tree --rows 1000000`，100 万同时可见行；**这组对照是 2a 当时在
  Qt 6.8.3 上测的**，Qt 6.11.2 下的新实现数字见 §3 的 v1.0 基线表）：

  | 指标 | 旧（整表反向哈希） | 新（分块前缀和） |
  | --- | --- | --- |
  | 展开一条 4 层深路径 | 969.5 ms | **115.9 ms** |
  | 折叠根节点 | 229.8 ms | **4.3 ms** |
  | 打开树后的工作集 | 114.5 MB（+80.5） | **76.0 MB（+41.9）** |

  结构变更（insert/remove/move/layoutChanged/reset）仍然整体重建可见行列表 —— 那是 O(可见行) 的
  冷路径，也是文档里"先正确后优化"的边界：热路径（展开/折叠 + 滚动 + 锚点）不再依赖它。

* **树的 expand/collapse 是"原地 splice + memmove"（v1.0 收口时按 P2-10 改）**：`expand()` 以前
  用 `mid(0, row+1) + 子树 + mid(row+1)` 重建整个可见行向量（两次整表拷贝 + 一次分配），之后
  改成原地 splice；但 `std::move_backward` / `std::copy` 在 MSVC 上并没有被折成 `memmove`，
  于是尾部搬移仍是逐元素拷贝。改成显式 `memmove` / `memcpy`（`QModelIndex` 是可平凡复制的值
  类型，源码里有 `static_assert` 守着）后，`bench_listview --tree --tree-roots 1000000` 的
  "只看索引"场景（新增，见 §3）在 100 万可见行上得到：

  | 操作 | 改前 | 改后 |
  | --- | --- | --- |
  | 在**末尾**展开一个根（纯追加，无尾部搬移） | 0.05 ms | 0.05 ms |
  | 在**开头**展开一个根（搬 1M 行 ≈ 8 MB） | 9.0 ms | **1.4 ms** |
  | 折叠同一个根（搬尾部） | 1.5 ms | 1.5 ms |
  | 稳态 expand+collapse 一对（20 次平均） | 10.6 ms | **2.9 ms** |

  剩下的 1.4 ms / 次就是那次尾部 memmove（≈8 MB 的带宽代价），`QVector` 在这里已经贴着内存
  带宽跑：要再往下只能把可见行表换成 rope / 分块 / 隐式树，而那会改变公开类
  `TreeVisibilityIndex` 的成员布局，按 [abi.md](abi.md) §4 第 3 条属于 ABI 破坏 —— 因此它是
  **下一个主版本**的议题，1.x 期间接受这个数字（决策见 [roadmap.md](roadmap.md) §5）。
* **Release 基线尚未采集（roadmap 3d 的遗留项）**：§3 的数字全部来自 Debug 构建，够用来看趋势与
  回归，但不能当"发布版性能"引用。采集 Release 基线需要另一棵 `-DCMAKE_BUILD_TYPE=Release` 的树
  （`scripts/validate.ps1` 目前固定 Debug），留到有实际性能诉求时再做。
  不变量由 `tests/unit/tst_treevisibilityindex` 与 `tst_virtualtreeview` 守住：展开/折叠不遍历
  整棵树（模型查询次数）、以及"每一行都能映射回它自己在可见行列表里的位置"（在很宽的树上反复
  展开/折叠/再展开后逐一校验）。
* 单元测试与基准运行在 offscreen 平台，不能替代真实合成器下的绘制耗时测量；本库的目标也不是
  击败 `QStyledItemDelegate` 的纯绘制性能（见 README 的定位）。
