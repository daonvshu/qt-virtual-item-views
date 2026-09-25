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
| 验证 | 两种配置 `all` 构建通过；19 个 CTest 目标全绿（211 单元 + 4 变异 + 10 GUI 用例）；11 个示例与 `bench_listview` 已产出 |

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
- [ ] **1b 表头动画**（§23/§24）：committed geometry 与 visual geometry 分离，动画只发生在
      渲染器层（`VirtualHeaderView`/`HeaderWidgetAdapter`），表格只提交一次。优先级：
      sort icon → section move transition → resize / frozen pane transition。
- [ ] **1c 行冻结**（§31 只定义了列方向）：待定项，要么实现（垂直方向的 pane 类比），
      要么明确写进 README 的"非目标"。

### Wave 2（v0.9）：规模化与可访问性补完

两块互相独立，也不改结构，可按实际需求调序。

- [ ] **2a 树的增量可见行映射**：[performance.md](performance.md) §4 记录了现状与实测
      （一百万可见行时 `QHash` 表重建，4 次 expand 从 0.88 s 涨到 12 s）；目标结构是分块 /
      前缀和的增量索引，公开接口不变，用 benchmark 的"宽树 + 频繁展开 + 锚点"守回归。
- [ ] **2b Accessibility 补 `QAccessibleTableInterface`**（屏幕阅读器按行列朗读），
      按需再考虑 `TextInterface`/`EditableTextInterface`（[accessibility.md](accessibility.md) §4）。
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
