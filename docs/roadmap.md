# 路线图与进度

> 这份文档是"已经做到哪、接下来做什么"的唯一事实来源。条款号（§N）对应方案文档
> `VirtualItemViews_Architecture_v0.3.md`（§43 是路线图原文）；每项能力的详细说明见
> README 的"当前状态"表。每收尾一步就更新这里，别让状态散落在 README、`docs/*.md`
> 和提交信息三处。

## 1. 已完成

| 版本 | 内容 | 提交 |
| --- | --- | --- |
| v0.1 | List Kernel：`VirtualItemView`（QAbstractScrollArea 内核）+ `WidgetAdapter`/`WidgetRecycler` + `ScrollMapper`（64 位逻辑滚动空间）+ `SizeIndex` + `ListLayout` | `5523d56` |
| v0.2 | 动态高度列表：估计值 + 测量反馈 + 滚动锚点 | `5523d56` |
| v0.3 | 生产级列表：焦点 / IME / popup pin、Model 信号矩阵、`QSortFilterProxyModel` 直连、像素滚动 | `5523d56` |
| v0.4 | Table MVP：`HeaderGeometry` 作为列几何唯一事实来源（§14/§45.10）、`NativeHeaderView`、Row Widget Mode（§26/§27） | `5523d56` |
| v0.5 | Cell Widget Mode 二维虚拟化（§28）、`visibleRows()/visibleColumns()`、大列数基准 | `5523d56` |
| v0.6 | Tree MVP：`TreeVisibilityIndex` + 缩进 / 分支指示 / 键盘导航（§35） | `5523d56` |
| v0.7 | 冻结列（§31）、表头状态持久化（§32）、`VirtualHeaderView`+`HeaderWidgetAdapter`（§17–§19）、拖放（§38）、Accessibility（§37）、span + advanced panes（§43） | `d85d814` `edfb74e` `dfa9046` `f4bca16` `2ee729e` `257a086` `fbd6be3` `66e5509` |

v0.7 的逐项细节与实现决定记在 [spans.md](spans.md)、[accessibility.md](accessibility.md)、
[drag-and-drop.md](drag-and-drop.md)。

### 当前基线（2026-09-25 复验）

| 项 | 值 |
| --- | --- |
| Qt 6 | `D:\devlib\Qt\6.11.2\msvc2022_64` |
| Qt 5 | `D:\devlib\Qt\5.15.2\msvc2019_64` |
| 工具链 | MSVC 18 (14.50.35717) x64 + Ninja + CMake 4.3（CLion 自带） |
| 构建树 | `cmake-build-debug-qt6` / `cmake-build-debug-qt5` |
| 验证 | 两种配置 `all` 构建通过；19 个 CTest 目标全绿（214 单元 + 4 变异 + 10 GUI 用例）；11 个示例与 `bench_listview` 已产出 |

注意：构建与测试必须在沙箱外运行。沙箱内 ninja 无法派生编译器子进程，构建会永久挂起
（已用最小 ninja 工程复现）。

## 2. 待做

### Wave 1（v0.8）：表格收口

顺序不能反：多滚动组是最后一次动 pane / 表头几何结构的改动，表头动画要建在定稿的几何之上。

- [x] **1a 多滚动组**（[spans.md](spans.md) 第 5b 步）：每个滚动 pane 一个裁剪容器（row 与
      cell 两种模式），`setPanes()` 不再拒绝第二个滚动组；宽度按 extent 比例分给各滚动 pane，
      主组（首个滚动 pane 的组）仍由 `HeaderGeometry` + 滚动条驱动，其余组由
      `setHorizontalOffset(group, offset)` 驱动；表头新增 `setPaneOffset()`；命中测试按 pane
      裁剪。示例 `examples/table_panes`（`--check` 自检 / `--snapshot` 截图）。
- [x] **1b 表头动画**（§23/§24）：committed 与 visual 两层几何分离（[header-animation.md](header-animation.md)）。
      `VirtualHeaderView` 在换序时把 section 滑到新位置（默认 300 ms、OutCubic，可关、可调），body 只在 commit
      时重排一次；resize / 滚动 / pane 变化逐帧同步；native 表头渲染器忽略该设置（支持矩阵见文档）。
      顺带修掉"应用自己创建的表头控件从未 reparent、按屏幕坐标摆放"导致的表头/body 几像素错位。
      测试：`tst_virtualheaderview` 3 个新用例；示例：`table_custom_header --move-demo`。
- [ ] **1c 行冻结**（§31 只定义了列方向）：**在范围内**（用户 2026-09-25 决定），规格见
      [row-freezing.md](row-freezing.md)（不变量、API、布局与滚动、7 步实现顺序、3 个开放问题）。
      - [x] 第 1-4 步：内核行区间视图（`verticalOffset` 与最大偏移语义不变 = "冻结不产生额外滚动空间"）、
            `ItemPane`/`itemPanes()`/`isRowFrozen()`、行 pane 裁剪容器（Row Widget Mode 一个可滚动
            容器；Cell Widget Mode 用"行 pane × 列 pane"交集容器）、命中/键盘/`scrollTo()` 按 pane
            折回。测试 `tests/unit/tst_frozenrows` 14 个用例（含 100 万行仍只物化冻结行 + 窗口）。
      - [x] 第 5 步：纵向表头（行号条）按 pane 切分：每个行 pane 一条同类型渲染器、各持自己的
            偏移（`NativeHeaderView::setPaneOffset()` 的显式偏移现在优先于共享几何），位置用视口
            原点换算；测试覆盖"三条带子各自贴着自己的行"（未冻结、冻结、滚动、改行高、取消冻结
            五个状态）。顺带修掉两个既有缺陷：均匀行高下行号与行错位、行几何的最小 section 尺寸
            把行高夹到 24。示例 `table_many_columns --frozen-rows N` 可直接看效果。
      - [x] 第 6 步：行冻结状态进表格级持久化（§32）：状态版本 2 追加 `frozenTopRows` /
            `frozenBottomRows`，版本 1 仍可恢复（行数视为 0），更早的裸 HeaderGeometry 状态也照旧。
      - [x] 第 7 步：`examples/table_frozen_rows`（顶部/底部冻结行 + 冻结列可调、`--check` 自检
            "冻结行不动 / 滚动范围不变 / 行号条贴合 / 命中归属"、`--snapshot` 截图）与 README/roadmap
            收尾。1c 全部完成。

### Wave 2（v0.9）：规模化与可访问性补完

两块互相独立，也不改结构，可按实际需求调序。

- [x] **2a 树的增量可见行映射**：把"整表反向哈希 + 每次 expand/collapse 重建"换成**每个已展开
      父节点一棵 Fenwick 树**（`BranchBlock`），索引 → 可见行自底向上累加（O(depth × log siblings)），
      expand/collapse 只沿路径更新。公开接口不变，可见行列表仍是唯一事实来源。百万可见行实测：
      4 层展开 969.5 → **115.9 ms**、折叠 229.8 → **4.3 ms**、工作集 114.5 → **76.0 MB**
      （[performance.md](performance.md) §4 有对照表）；新增两条用例守住"不遍历整棵树"与
      "每一行都能映射回自己的可见行位置"（在很宽的树上反复展开/折叠/再展开）。
- [x] **2b Accessibility 补 `QAccessibleTableInterface`**：表格视图节点在 `TableInterface` 上返回
      自己，按**模型坐标**读出 `rowCount()`/`columnCount()`/`cellAt()`（合并区域折回锚点）、
      `columnDescription()`/`rowDescription()` 提供表头文字（UIA/AT-SPI 的"第几列/第几行"来源）、
      选择查询与整行整列的选择/取消（`NoSelection` 时返回 false）；单元格节点一并补上
      `QAccessibleTableCellInterface`（坐标、`table()`、`isSelected()`、合并单元格的 span extent）。
      `selectedCells()` 只给窗口内的节点（计数精确、列表有界）。顺带把节点的
      `rowIndex()`（返回 QModelIndex）改名为 `rowModelIndex()`，避免与 Qt 的 int 版重名。
      仍未实现：`TextInterface`/`EditableTextInterface`（[accessibility.md](accessibility.md) §4）。
- [ ] **2c `BlockSizeIndex` 内存优化**：只记录"与估计值不同"的行，公开接口不变
      （[performance.md](performance.md) §4 第一条）。

### Wave 3（v1.0）：工程化交付

- [ ] API 稳定性审查：清理实验性入口（例如 `setPanes()` 不再有"接受但忽略"的分支）、
      复核公开头文件的边界。
- [ ] ABI 策略文档 + Qt 版本支持矩阵（5.15 / 6.x 各自实测的组合）。
- [ ] 文档齐全度：公开类文档、README 快速开始、安装消费端示例（`find_package(VirtualItemViews)` 实跑）。
- [ ] 性能基线固化：把 bench 数字写进 [performance.md](performance.md)，保留稳态滚动零分配断言。
- [ ] 一键验证脚本 / CI：双 Qt 构建 + CTest + 示例退出码。

## 3. 每一步的完成定义

沿用既有节奏，走完才算完成：

1. 规格或决策写进对应 `docs/*.md`（语义、边界、为什么这样选）。
2. 实现。
3. 单元测试（必要时补 GUI 场景 / fuzz）。
4. Qt 6 与 Qt 5 双配置 `all` 构建 + CTest + 示例退出码。
5. 同步 README 能力表与本文件。
6. 一个独立提交。

## 4. 决策记录

| 日期 | 决定 | 理由 |
| --- | --- | --- |
| 2026-09-25 | 不打 v0.7 tag，等 v1.0 后由用户手动打 | 用户要求 |
| 2026-09-25 | 先做 1a（多滚动组）再做 1b（表头动画） | pane 结构定稿在动画之前，避免 committed/visual 分离返工 |
| 2026-09-25 | 多滚动 pane 的宽度按 extent 比例分配（而不是"首个滚动 pane 拿全部剩余"） | 否则第二个组要么整列全露、要么把主 pane 压到 0，两个组都无法滚动；单滚动 pane 时逐像素不变 |
| 2026-09-25 | 主组 = 首个滚动 pane 所在的组（默认布局下是组 0），不写死组 0 | 显式列表可以任意编号；默认布局与既有测试逐像素一致 |
| 2026-09-25 | 构建必须在沙箱外运行 | 沙箱内 ninja 无法派生编译器子进程，构建会永久挂起（已用最小 ninja 工程复现） |
| 2026-09-25 | 1c 行冻结纳入范围 | 用户要求：做一个垂直方向的 pane 类比，而不是写进非目标 |
| 2026-09-25 | 表头动画只覆盖换序（resize/滚动/pane 变化保持逐帧同步） | §24 的判据是"body 是否逐帧跟随"；动画只做 body 不跟帧的那一种，避免表头与 body 撕裂 |
| 2026-09-25 | 视图会 reparent 应用传入的表头控件 | 顶层窗口的位置是屏幕坐标（带窗口边框偏移），表头会与 body 差几像素；reparent 后统一用视图坐标 |
| 2026-09-25 | 行冻结里 `verticalOffset()` 的语义与范围保持不变（最大偏移仍 = 内容高 - 视口高） | 这正是"冻结不产生额外滚动空间"的算式：可滚动区少掉的像素数恰好等于冻结带高度；滚动到末尾时最后几行由底部冻结带绘制，内容仍然连续 |
| 2026-09-25 | Cell Widget Mode 的裁剪容器改成"行 pane × 列 pane"交集，Row Widget Mode 不变（内核裁纵向、行内的列容器裁横向） | 两个方向的边界互相独立；Row 模式的行控件本身被内核容器裁一次，天然正交，不需要第二层容器 |
| 2026-09-25 | 测试目标统一加 `/utf-8`（MSVC） | Qt5 配置下测试源文件的 UTF-8 中文注释按 936 代码页解析会吞掉行尾，导致整文件语法错误（C4819 → C2447） |
| 2026-09-25 | 行号条按 pane 切分时，每条带子持有自己的显式偏移（`setPaneOffset()` 优先于共享几何的 offset），位置用视口原点换算 | 冻结带上下的内容范围不同、一个几何偏移表达不了三段；而条子是视图的子控件（有横向表头时视口 y≠0），比较对齐时必须换算回同一坐标系 |
| 2026-09-25 | 表格级状态版本升到 2（追加冻结行数），版本 1 仍可恢复 | 冻结行与冻结列一样是用户状态（不是应用组成），必须跟着 `saveHeaderState()` 走；旧版本状态丢掉新增字段即可，不需要迁移代码 |
| 2026-09-25 | 行冻结的"底部冻结支持、默认 0""冻结行参与选择""排序后前 N 行"三个开放问题按最小意外默认落地，不加开关 | 顶/底在实现里本来就对称；冻结行就是普通行；排序语义跟随模型（与冻结列一致），加开关只会增加需要维护和解释的状态 |
| 2026-09-25 | 行 pane 交界线与列 pane 交界线共用一套样式与颜色（探到的样式分隔线颜色），横向线也跨过行号条 | 同一个视图里出现两种边界线（一个是探到的样式色、一个是调色板中灰；一条跨表头条、一条不跨行号条）看起来像两个特性各做一半；所以用 `setPaneSeparatorStyle()` 一个旋钮管两个方向，list/tree 的默认实现才回落到调色板中灰 |
| 2026-09-25 | 树的可见行映射用"每个已展开父节点一棵 Fenwick 树"而不是整表反向哈希 | 反向哈希必须整表重建（O(可见行)）才能在 expand/collapse 后保持正确；分块前缀和让每层只更新自己那一格、行号自底向上累加，热路径变成 O(depth × log k)，内存也少一个数量级的常数 |
| 2026-09-25 | 可访问性的表格接口用**模型坐标**（不是可见坐标），并且 `selectedCells()` 只返回窗口内的节点 | 屏幕阅读器问的是"第 3 行第 2 列"，而不是"当前窗口第几个"；同时必须守住"节点数不随逻辑行数增长"这条不变量，否则"全选"会瞬间物化一百万个接口 |
| 2026-09-25 | 节点的 `rowIndex()` 改名 `rowModelIndex()` | 实现 `QAccessibleTableCellInterface` 后 `rowIndex()` 必须是"第几行"（int），原来那个返回 QModelIndex 的同名方法在语义上本来也更容易误会 |
| 2026-09-25 | 表头过渡改成**按需触发**：程序化换序默认即时，只有显式 `MoveAnimation::Animate`（或渲染器自己的手势）才播 | 用户反馈"手动设置列顺序也被当成了拖动"；程序化/模型换序/状态恢复不该变出没人要求的动画。现有 `moveColumn()` 调用语义不变（默认即时），过渡从"任何顺序变化都播"收敛为"被请求才播" |
| 2026-09-25 | 拖动重排重做成"拖动距离阈值 + 视觉预览 + 松手一次提交"，并把单次过渡接回松手 | 旧实现每越过一个邻居就提交一次：既不能连续拖（只能一格一格挪），又会把点击/抖动误判成拖动，且逐格动画让 section 跟不上光标。现在 committed 几何只在松手时变一次，拖动期间只动渲染器的视觉几何 |
| 2026-09-25 | 拖动期间"邻居让位"也走缓动，并且与换序过渡**同一套曲线与时长**（OutCubic，默认 300 ms；关闭动画时即刻） | 瞬移的让位看起来像跳帧：拖动是被拖列跟随光标、其他列让出插入位的一次连续运动。让位与过渡用同一个旋钮，手感才会一致（要更快就整体调小时长） |
