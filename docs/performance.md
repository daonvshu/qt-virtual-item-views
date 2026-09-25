# 性能模型与基准

## 1. 复杂度

| 操作 | 复杂度 | 说明 |
| --- | --- | --- |
| `FixedSizeIndex::offsetOf/indexAt` | O(1) | 除乘 |
| `BlockSizeIndex::offsetOf/indexAt` | O(log B + capacity) | 两张前缀表二分 + 块内线性 |
| `BlockSizeIndex::setSize` | O(1) + 下次查询 O(B) | 懒重建前缀表 |
| `BlockSizeIndex::insert` | O(capacity) | 块内插入 + 必要时分块 |
| `BlockSizeIndex::remove` | O(capacity + B) | 逐块删除 |
| `TreeVisibilityIndex::indexAtVisibleRow` | O(1) | 可见行数组下标 |
| `TreeVisibilityIndex::visibleRowForIndex` / `depth` | O(1) / O(depth) | 查询表按 index 值建立，结构变更后整体重建 |
| `TreeVisibilityIndex::expand` / `collapse` | O(被展开子树) + O(可见行) | 只遍历被展开的子树，**不重走整棵树**；可见行数组的插入/删除是 O(可见行) |
| `relayout()` | O(W) | W = 可见 + overscan + pin |
| 稳态滚动 | O(进入窗口的项数) | 以复用为主，通常 1~2 项 rebind |

其中 B 为块数（约 N / 1024），capacity 为块容量（1024）：一百万行时前缀重建约 10^3 次加法，
远低于一次全量 O(N) 重算。

## 2. 规模特性与校验

| 指标 | 目标 | 校验方式 |
| --- | --- | --- |
| 逻辑行数初始化 | 与行数弱相关，不创建全量控件 | `tst_virtualitemview::materializesOnlyVisibleAndOverscan`（1,000,000 行） |
| 可见控件数量 | visible + overscan + pinned | 同上以及 `materializedItemCount()` |
| 稳态滚动 | 不 new/delete | `tst_virtualitemview::scrollingReusesWidgetsWithoutAllocating`、`tst_modelmutationfuzz::widgetCountStaysBounded` |
| 内存 | 随可见项复杂度增长，不随逻辑行数线性增长 | 只持有可见项控件 + SizeIndex 的 int 数组 |
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

`--tree` 追加两个场景（同样以非 0 退出码报告不变量违规）：

* **宽树**：默认 `--tree-roots 1000000`（1,000,000 个顶层节点 x 10 子节点、深度 4，逻辑节点
  11,111,100,000 个）。折叠状态就有 1,000,000 个可见行，用来验证实例化集合只随视口变化、
  稳态滚动零分配、以及"在视口上方展开/折叠时锚点行不动"。
* **展开与变更**：堆树（`--tree-roots`/`--tree-branching` 之外的小树）全量展开，再在视口上方
  insert/remove 行，验证展开状态、可见行映射与锚点。

Debug 构建（Qt 6.8.3 / msvc2022_64，本机参考值，用于观察趋势而非横向比较）：

| 指标 | 1M 顶层节点宽树 | 4200 节点堆树（全展开） |
| --- | --- | --- |
| 打开（setModel + show + layout） | 约 1.2 s（1,000,000 可见行） | - |
| 展开一条 4 层路径 | 约 0.88 s（4 次 expand），模型查询 84 次 | 全部展开 0.12 s，模型查询 8200 次（≈ 2 x 节点数） |
| 折叠根节点 | 约 0.24 s | - |
| 稳态滚动每步 | 0.54 ms，新建/销毁 0 个 | 100 步 0.05 s，新建 0 个 |
| 视口上方 insert/remove 各 200 行 | 锚点行不动（偏移 24000 -> 28800 -> 24000 px） | 锚点行不动，新建 0 个 |
| 进程 working set 增量 | +80 MB（1,000,000 可见行） | - |

可见行数极大的时候，一次 expand/collapse 的成本由"可见行查询表重建"主导（O(可见行)），而不是
由树遍历主导（84 次模型查询）；上面这组数字里 4 次 expand 的 0.88 s 中绝大部分是这张表。

## 4. 已知取舍

* `BlockSizeIndex` 目前按行保存 int 尺寸（一千万元素约 40 MB）。若需要更低内存，可改成只记录
  "与估计值不同"的行，公开接口不变。
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
  同一基准（`bench_listview --tree --rows 1000000`，100 万同时可见行）：

  | 指标 | 旧（整表反向哈希） | 新（分块前缀和） |
  | --- | --- | --- |
  | 展开一条 4 层深路径 | 969.5 ms | **115.9 ms** |
  | 折叠根节点 | 229.8 ms | **4.3 ms** |
  | 打开树后的工作集 | 114.5 MB（+80.5） | **76.0 MB（+41.9）** |

  结构变更（insert/remove/move/layoutChanged/reset）仍然整体重建可见行列表 —— 那是 O(可见行) 的
  冷路径，也是文档里"先正确后优化"的边界：热路径（展开/折叠 + 滚动 + 锚点）不再依赖它。
  不变量由 `tests/unit/tst_treevisibilityindex` 与 `tst_virtualtreeview` 守住：展开/折叠不遍历
  整棵树（模型查询次数）、以及"每一行都能映射回它自己在可见行列表里的位置"（在很宽的树上反复
  展开/折叠/再展开后逐一校验）。
* 单元测试与基准运行在 offscreen 平台，不能替代真实合成器下的绘制耗时测量；本库的目标也不是
  击败 `QStyledItemDelegate` 的纯绘制性能（见 README 的定位）。
