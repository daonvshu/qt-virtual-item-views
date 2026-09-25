# 变更记录

格式：每个版本一节，破坏性变更单独列在 `Breaking`。稳定性分级与冻结规则见
[docs/api-stability.md](docs/api-stability.md)。

## 未发布（v1.0 开发中）

这一节记录 v1.0 定稿前**还没有对外发布**的 API 收口。库此前没有打 tag、也没有下游使用者，
所以这些改动直接生效，不提供兼容层。

### Breaking

* `LayoutPolicy::setSizeIndex()` 的默认实现改为履行所有权：没有索引模型的策略在
  `takeOwnership = true` 时会删除传入的 `SizeIndex`（此前会静默忽略并泄漏）。自定义策略如果
  既不用索引又要接管所有权，行为变化就是"不再泄漏"。
* 删除 `ListLayout::setOwnsSizeIndex(bool)`：无调用者，所有权用
  `setSizeIndex(index, takeOwnership)` 表达。
* 删除 `ScrollMapper::toScrollbar()` / `toLogical()`：与 `toScrollBarValue()` /
  `toLogicalOffset()` 完全重复（架构文档里的旧名字）。

### Added

* `docs/api-stability.md`：公开 API 的四级分类（应用/扩展/诊断/私有）、冻结规则与复核清单。
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
