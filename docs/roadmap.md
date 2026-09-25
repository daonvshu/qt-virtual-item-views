# 路线图与进度

> 这份文档是"已经做到哪、接下来做什么"的唯一事实来源。条款号（§N）对应方案文档
> `VirtualItemViews_Architecture_v0.3.md`（§43 是路线图原文）；每项能力的逐条状态见
> [features.md](features.md)（README 首页只放概览）。每收尾一步就更新这里，别让状态散落在
> README、`docs/*.md` 和提交信息三处。

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
| v0.8 | 表格收口（Wave 1）：多滚动组、表头动画（committed/visual 两层几何 + 拖动重排）、行冻结（内核 + 行号条按 pane 切分 + 状态持久化 v2 + 独立示例） | `6c09cd9` `2de4c2a` `5dff417` `b6fbd99` `e7e3769` `c5a86aa` `e73cc29` `f52649f` `659f456` `052a520` `4c1c74c` |
| v0.9 | 规模化与可访问性（Wave 2）：树的增量可见行映射（Fenwick 分块）、`QAccessibleTableInterface`/`QAccessibleTableCellInterface`、`BlockSizeIndex` 稀疏例外表 | `f9c7744` `0ba3ef0` `ab32e94` |
| v1.0 | 工程化交付（Wave 3）：API 四级冻结与实验性入口清理、导出宏（静态/动态）、安装消费端实跑、性能基线、一键验证脚本；细节见 §2 Wave 3 与 [CHANGELOG](../CHANGELOG.md) | `ca61c92` `9ce0055` `43d9dcd` `958a47a` `58b5696` |

v0.7 的逐项细节与实现决定记在 [spans.md](spans.md)、[accessibility.md](accessibility.md)、
[drag-and-drop.md](drag-and-drop.md)；v0.8 起逐项细节见本文 §2 的 Wave 段落与对应的
[header-animation.md](header-animation.md)、[row-freezing.md](row-freezing.md)、
[performance.md](performance.md)、[api-stability.md](api-stability.md)、[abi.md](abi.md)。

### 当前基线（2026-09-25 复验）

| 项 | 值 |
| --- | --- |
| 版本 | `1.0.0`（`SOVERSION 1`、`find_package` 兼容性 `SameMajorVersion`；tag 由仓库主人手动打） |
| Qt 6 | `D:\devlib\Qt\6.11.2\msvc2022_64` |
| Qt 5 | `D:\devlib\Qt\5.15.2\msvc2019_64` |
| 工具链 | MSVC 18 (14.50.35717) x64 + Ninja + CMake 4.3（CLion 自带） |
| 构建树 | `cmake-build-debug-qt6` / `cmake-build-debug-qt5`（静态）与 `cmake-build-debug-qt6-shared` / `cmake-build-debug-qt5-shared`（动态） |
| 验证 | 一条命令：`pwsh -File scripts/validate.ps1` —— 四种组合（Qt 6.11.2 / Qt 5.15.2 × 静态 / 动态）共 28 个步骤全绿：`all` 构建、20 个 CTest 目标（248 单元 + 4 变异 + 10 GUI 用例）、12 个示例退出码 0、`bench_listview` 不变量自检、`cmake --install` + 消费端冒烟测试 |

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

两块互相独立，也不改结构，可按实际需求调序。**Wave 2 已全部完成（2026-09-25），下一步进入
Wave 3（v1.0 工程化交付）。**

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
- [x] **2c `BlockSizeIndex` 内存优化**：块改成"一个基值 + 一张按行号排序的稀疏例外表"，即只记录
      "实测过、且与估计值不同"的行；没测量过的行不占存储（一千万元素均匀内容约 40 MB → 约
      0.6 MB）。语义：`setSize()` 写回基值即释放例外，`insert()` 在中间插入只切块、不搬动其它块的
      例外，`remove()` 按块边界切两刀再整块丢弃并归并残留邻块；块超过 2 x capacity 对半切、
      与邻块基值相同且合计不超过 capacity 就合并，因此最坏情况与旧的逐行实现同阶
      （[performance.md](performance.md) §4 第一条）。公开接口不变，另加只给测试/诊断用的
      `explicitSizeCount()`；新增 4 条用例（未测量行不占存储、估计值变化不改已测行、跨越删除与插入
      后实测值仍在、逐行追加不会把索引碎成 N 个块）。

### Wave 3（v1.0）：工程化交付

**Wave 3 已全部完成（2026-09-25），`PROJECT_VERSION` 收口到 `1.0.0`（`SOVERSION 1`、
`find_package` 兼容性 `SameMajorVersion`）。** 剩下的动作只有仓库主人手动打 tag；打完之后的
第一件事是更新 [abi.md](abi.md) §1 与 CHANGELOG，因为 1.x 之间的兼容承诺从这一版开始生效。

- [x] **3a API 稳定性审查**（[api-stability.md](api-stability.md)）：23 个公开头文件逐个复核并按
      "应用 / 扩展 / 诊断 / 私有"四级冻结，规则写进文档（只加不删、不改默认值语义、不新增
      "接受但忽略"的入口、所有权必须在签名里可见、Qt 版本差异不外泄）。复核发现并处理：
      `LayoutPolicy::setSizeIndex()` 的默认实现"接受但忽略"且会泄漏索引（改为履行所有权）、
      删掉死入口 `ListLayout::setOwnsSizeIndex()`、删掉重复的 `ScrollMapper::toScrollbar()` /
      `toLogical()`、修掉 `SelectionBehavior` 里"SelectColumns 会到来"的过期注释、
      补上 `CMakeLists.txt` 漏列的公开头 `itempane.h`；建 `CHANGELOG.md`（从 v1.0 起记录
      破坏性变更，本轮的三个改动已在里面）。顺带把 1a 已清掉的 `setPanes()` 忽略分支从
      "待办示例"降级为已完成（复核时确认无残余）。
- [x] **3b ABI 策略 + Qt 版本矩阵**（[abi.md](abi.md)，用户拍板"加导出宏，静态与动态都要"）：
      新增 `include/virtualitemviews/global.h` 定义 `VIRTUALITEMVIEWS_EXPORT`，套到 30 个公开类与
      3 个自由函数上；`VIRTUALITEMVIEWS_LIBRARY`（库自己，`PRIVATE`）与 `VIRTUALITEMVIEWS_STATIC`
      （静态消费者，`PUBLIC` 随导出目标传播）由 CMake 管理，业务代码不需要手工 define。
      产物布局统一成 `<build>/bin` + `<build>/lib`（Windows 只在自己所在目录找 DLL，同目录后
      CTest / 示例 / 基准都不需要 PATH 技巧）。版本号 `0.1.0 -> 0.9.0`，`find_package` 兼容性在
      0.x 期间用 `SameMinorVersion`、1.0 起切 `SameMajorVersion`，SOVERSION 跟主版本。
      文档写清"什么改动算 ABI 破坏"（7 条）、跨边界 Qt 容器的约束（同一套 Qt + 运行库设置）、
      以及实测矩阵（Qt 5.15.2 / 6.11.2 × 静态 / 动态，MSVC 19.50 x64）与未实测组合。
      实测证据：共享构建产出 `VirtualItemViews.dll`（约 1000 个导出符号）+ 导入库，
      四种组合各自跑通 20 个 CTest 目标与 12 个示例。
- [x] **3c 文档齐全度 + 安装消费端实跑**：公开类/结构体的文档注释做了一次机械核查（23 个头文件里
      每个 `class`/`struct` 都有 `///` 说明）；README 新增「安装与消费」一节（构建 → `cmake --install`
      → 消费端 `find_package` 的完整命令、Qt 大版本各装一个前缀、动态安装的 DLL 查找注意事项）；
      `docs/abi.md` 补上装出来的目录布局与消费端约定。新增
      `tests/install/consumer/`（**独立** CMake 工程，只认 `find_package(VirtualItemViews)`，不碰源码树
      或构建树），内含 28 项运行期自检：列表虚拟化与滚动、表格列几何/命中/隐藏列/表头状态往返、
      span 锚点与合并矩形、冻结列与显式 pane 列表（含两个滚动组）、树展开折叠、单元格模式、
      accessibility 工厂。四种组合（Qt 5.15.2 / 6.11.2 × 静态 / 动态）都做了
      install → configure → build → run，全部 28/28 通过、退出码 0。
- [x] **3d 性能基线固化**：[performance.md](performance.md) §3 新增 v1.0 基线三张表（列表 1M 行 /
      表格 20 万行 x 100 列 / 树 1M 顶层节点 x 深度 4），都带确切命令、环境（Debug、Qt 6.11.2、
      MSVC 19.50 x64、Windows 11）与进程 working set；§2 说明"守性质的用例都在一键脚本里重跑"，
      §3 说明三档基准的不变量由 `scripts/validate.ps1` 第 4 步每次断言。顺手修掉两处过期描述：
      §3 原来那组树数字是 Qt 6.8.3 时期测的却标成了 6.11.2（现在明确标注并换成实测值），
      "expand/collapse 由可见行查询表重建主导"这句在 2a 之后已经不对（现在 174.90 ms / 48 次
      模型查询，是增量的）。**Release 基线未采集**，作为遗留项写在 performance.md §4 末条。
- [x] **3e 一键验证脚本**：`scripts/validate.ps1` 把本轮一直在手工做的检查固化下来 —— 对每个
      Qt kit × 库形态组合依次做 configure → `all` 构建 → CTest → 12 个示例（`--exit-after`，
      退出码必须 0）→ benchmark 不变量自检 → `cmake --install` + 消费端冒烟测试；
      Qt/vcvars/cmake 路径都能用参数覆盖，失败步不中止（一次跑完看到全部问题），退出码 = 失败步数。
      实测：`pwsh -File scripts/validate.ps1` 对四种组合共 28 个步骤全绿、退出码 0（约 4.5 分钟）。
      **CI 未接入**：本机没有 CI 账号/网络，提交一个没跑过的 workflow 是不负责任的；脚本就是 CI 的
      入口（`pwsh -File scripts/validate.ps1 -QtBin <runner 上的 Qt bin>`），接入时只需要一个
      Windows + MSVC + Qt 的 job 包一层，见 [abi.md](abi.md) §5 的支持矩阵。

## 3. 代码审查与修复（2026-09-25）

外部全量代码审查（`VirtualItemViews_Full_Code_Review.md`，按仓库惯例本地保留、不入库）列出
3 个 P0、15 个 P1、10 个 P2，并给出"修完 P0 + 主要 P1 再打 1.0 tag"的结论。修复按审查建议的
顺序分批落地，每批都带回归测试与四组合验证：

| 批次 | 内容 | 状态 |
| --- | --- | --- |
| **Wave 1 崩溃 / 悬空指针** | Adapter 切换 UAF、Recycler 池的 adapter 身份、Model/SelectionModel 生命周期（`QPointer` + 不变量）、表头 pane 渲染器析构顺序、视图析构解绑、Cell 模式在行/列移除与 reset 前解绑 | 已完成 |
| **Wave 2 数据 / 状态正确性** | **已完成**：P1-1 列结构 remap（`tst_headerstructure`）、P1-2 动态高度锚点（`tst_dynamicanchor`）、P1-7 `RowSizePolicy` 与行号条一致（`tst_virtualtableview`）、P1-8 rootIndex 持久化 + 校验（`tst_virtuallistview`）、P1-9 选择语义统一（`tst_selection`）、P1-10 `scrollToColumn()` 的 pane 感知（`tst_tablepanes`）、P1-11 span 重叠校验与 `maximumSpan()` 重算（`tst_tablespan`）、P1-13 冻结行下的拖放坐标（`tst_dndfrozen`） | ✅ |
| **Wave 3 虚拟化性能** | **已完成**：P1-3 列宽上下限语义 + 批量信号（`tst_headergeometry`）、P1-4 pane 局部前缀和 + 滚动只刷新窗口 + 表头 orderRevision 快路径（`tst_tablepanes::scrollingDoesNotWalkEveryColumn`）、P1-5 横向 64 位偏移与 extent（`tst_tablepanes`）、P1-12 `BlockSizeIndex` 分块上界（`tst_sizeindex`）、P2-1 relayout 队列合并、P2-8 纯横向滚动不跑纵向 pass（`tst_relayoutqueue`） | ✅ |
| **Wave 4 API / 发布** | **进行中**：P1-14 vertical widget header 明确拒绝 + P2-4 表头 orientation 校验（`tst_virtualheaderview`）；剩余 P2-2 pin 的离屏语义、P2-3 事务化 `restoreHeaderState`、P2-5 动态子控件的事件过滤、P2-6 `TablePaneSpec` 结构校验、P2-9 vertical header 状态不粘滞、P2-10 树的 visible 结构优化、Linux CI + ASan/UBSan | 🚧 |

Wave 1 新增的回归测试：`tst_adapterreplacement`（8 例）、`tst_modellifetime`（6 例）、
`tst_celllifecycle`（5 例）；四种组合（Qt 5.15.2 / 6.11.2 × 静态 / 动态）28 步验证全绿。

**v1.0 tag 暂缓**：`PROJECT_VERSION` 保持 `1.0.0`（尚未打 tag）。Wave 1（P0）、Wave 2
（数据 / 状态正确性）与 Wave 3（虚拟化性能，含 P1-3/P1-4/P1-5）都已完成，Wave 4 里唯一的发布
阻塞项 P1-14（vertical widget header）连同 P2-4 orientation 校验已落地；剩下的 P2 与 CI
接入不影响 1.0 的对外契约，按同一节奏继续。

## 4. 每一步的完成定义

沿用既有节奏，走完才算完成：

1. 规格或决策写进对应 `docs/*.md`（语义、边界、为什么这样选）。
2. 实现。
3. 单元测试（必要时补 GUI 场景 / fuzz）。
4. Qt 6 与 Qt 5 双配置 `all` 构建 + CTest + 示例退出码。
5. 同步 [features.md](features.md) 能力表、README 概览与本文件。
6. 一个独立提交。

## 5. 决策记录

| 日期 | 决定 | 理由 |
| --- | --- | --- |
| 2026-09-25 | 不打 v0.7 tag，等 v1.0 后由用户手动打 | 用户要求 |
| 2026-09-25 | 先做 1a（多滚动组）再做 1b（表头动画） | pane 结构定稿在动画之前，避免 committed/visual 分离返工 |
| 2026-09-25 | 多滚动 pane 的宽度按 extent 比例分配（而不是"首个滚动 pane 拿全部剩余"） | 否则第二个组要么整列全露、要么把主 pane 压到 0，两个组都无法滚动；单滚动 pane 时逐像素不变 |
| 2026-09-25 | 主组 = 首个滚动 pane 所在的组（默认布局下是组 0），不写死组 0 | 显式列表可以任意编号；默认布局与既有测试逐像素一致 |
| 2026-09-25 | 构建必须在沙箱外运行 | 沙箱内 ninja 无法派生编译器子进程，构建会永久挂起（已用最小 ninja 工程复现） |
| 2026-09-25 | 1c 行冻结纳入范围 | 用户要求：做一个垂直方向的 pane 类比，而不是写进非目标 |
| 2026-09-25 | 表头动画只覆盖换序（resize/滚动/pane 变化保持逐帧同步） | §24 的判据是"body 是否逐帧跟随"；动画只做 body 不跟帧的那一种，避免表头与 body 撕裂 |
| 2026-09-25 | `BlockSizeIndex` 用"块基值 + 稀疏例外表"，块 > 2 x capacity 对半切、与同基值邻块合并到 capacity | 逐行存 int 让一千万元素白付 40 MB；基值化后未测量的行零开销，而"切/合并"把每块的例外数夹在 2 x capacity 内，最坏复杂度与旧实现同阶，常见情况退化成 O(log B) |
| 2026-09-25 | 公开 API 分四级冻结，而不是"全部一起冻" | 业务入口（A）和内核扩展点（B）的兼容成本完全不同：A 层要能挡住一切变化，B 层的 protected 契约需要留出次版本内的调整空间，诊断接口（C）跟着实现走才诚实 |
| 2026-09-25 | 公开入口不许"接受但忽略"：要么完整实现，要么明确拒绝；`LayoutPolicy::setSizeIndex()` 的默认实现改为"没有索引模型也履行 takeOwnership" | 一个声称接管所有权却把对象丢掉的入口是静默泄漏；把它写进冻结规则后，后续评审有可执行的判据 |
| 2026-09-25 | 静态库与动态库都支持（加 `VIRTUALITEMVIEWS_EXPORT`），不做"只支持静态" | 用户拍板；导出宏是 30 个类的一次性机械改动，而"共享构建产出空 DLL"是实质缺陷，留着迟早要还 |
| 2026-09-25 | 产物统一到 `<build>/bin` 与 `<build>/lib` | Windows 不会去隔壁目录找 DLL；可执行文件与库同目录后，CTest、示例、基准都不需要 PATH 技巧，静态/动态行为一致；代价只是示例路径从 `examples/xxx` 变成 `bin/xxx` |
| 2026-09-25 | `PROJECT_VERSION` 先落到 `0.9.0`（与已完成的 Wave 2 对齐），1.0 收尾再 bump 到 `1.0.0`；0.x 期间 `find_package` 用 `SameMinorVersion` | 版本号要如实反映进度：Wave 3 还没做完就不是 1.0；0.x 没有 ABI 承诺，不该让"要 0.9"的消费端匹配到 0.10 |
| 2026-09-25 | 安装冒烟测试是一个**独立工程**（`tests/install/consumer`），不进主构建树 | 只有独立 configure/build 才能真正证明"装出来的包能被人消费"；放在主构建里就只能证明"源码树里的头文件能被自己的 target 用到"，而那从来不是问题所在 |
| 2026-09-25 | 安装前缀按 Qt 大版本区分（`VirtualItemViewsConfig.cmake` 里写死 `find_dependency(Qt6 …)`） | 一个前缀里混两个 Qt 大版本需要包名/目标名去版本化，代价远大于收益；Qt 5 与 Qt 6 各装各的前缀更简单也更好理解 |
| 2026-09-25 | 一键验证写成项目自带脚本 `scripts/validate.ps1`，不提交未验证过的 CI workflow | 脚本能在本机真跑、能进发布清单；CI 配置在拿到 runner 之前无法验证，宁可先留一句"怎么接" |
| 2026-09-25 | 验证脚本遇错不中止（跑完全部组合再汇总，退出码 = 失败步数） | 四种组合跑一轮要几分钟，第一处失败就退出会让人反复重跑；一次拿到全部失败信息更省时间 |
| 2026-09-25 | 性能基线只固化 Debug + 本机工具链的数字，并显式标注"Release 未测" | 把 Debug 数字包装成"发布性能"比不写更糟；基线的价值是回归对比，不是横向吹牛，所以环境、命令、局限性都写在表旁边 |
| 2026-09-25 | 切换 Adapter / 切换 Row-Cell 模式 / 替换 cell adapter 时**清空控件池**，而不是给池的 key 加 adapter 身份 | 池只按 `WidgetType` 分池，而业务默认都返回 0：跨 adapter 复用等于把 A 的控件交给 B 去 `static_cast`。清池是最小且绝对安全的解法（代价是配置级切换要重建控件），"adapter 身份 + type" 的池 key 留到确有性能诉求时再做 |
| 2026-09-25 | v1.0 tag 暂缓，先修完代码审查的 Wave 1 + Wave 2 | 审查指出 Adapter/Recycler、Model 生命周期、列结构 remap 这三组问题若在 1.0 ABI 冻结后再修，会逼着改公开类的成员布局与 ownership 语义，与 1.x 的 `SameMajorVersion` 承诺冲突 |
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
