# 行冻结规格（§31 的行方向类比）

方案文档 §31 只定义了列方向（`setFrozenColumns()` / `setFrozenRightColumns()`）。这份文档把
行方向的语义先定下来，实现按第 6 节的顺序推进。

**状态（roadmap 1c）**：**7 步全部完成**。内核行区间视图、行 pane 布局、两种物化模式的裁剪、
命中/键盘/滚动 API、纵向表头按 pane 切分、表级状态持久化（版本 2）、独立示例
`examples/table_frozen_rows`（带 `--check` 自检与 `--snapshot`）；`tests/unit/tst_frozenrows`
15 个用例覆盖以上各项。

## 1. 目标与非目标

目标：像 Excel 的"冻结窗格"一样，把最上面 N 行（可选再加最下面 M 行）钉住，其余行滚动；
行冻结与列冻结必须能同时存在，形成"四角 + 中间"的矩形布局。

非目标：任意行集合的冻结（例如"冻结所有分组标题行"）；把冻结行做成独立的第二个视图；
改变现有"冻结列不产生额外横向滚动空间"的规则。

## 2. 不变量

1. **行的几何仍然只有一份**：`ListLayout` + `SizeIndex` 是行高与行位置的唯一事实来源，
   冻结行不产生第二份行高（与列方向同一条原则）。
2. **冻结不产生额外滚动空间**：横向已有"冻结列把可滚动区压窄、总范围不变"的性质
   （`frozenPanesDoNotAddScrollSpace` 用例）。纵向必须同样：滚动范围为
   `max(0, 可滚动行总高 - 可滚动区高度)`，冻结行高度不计入，也不改变逻辑行数。
3. **行 pane 是硬边界**：与列一样，行 pane 的边界是命中、合并、拖放落点的硬边界。

## 3. 模型与 API

```cpp
// VirtualItemView（列表 / 树 / 表格通用：行方向是内核概念）
void setFrozenRows(int count);            // 顶部冻结行数，0 = 关闭
void setFrozenBottomRows(int count);      // 底部冻结行数，0 = 关闭
int frozenRows() const;
int frozenBottomRows() const;
bool isRowFrozen(qsizetype row) const;

// 行 pane 的几何（诊断 / 测试 / 业务自定义布局）
QVector<ItemPane> itemPanes() const;      // 顶部冻结 | 可滚动 | 底部冻结，顺序即视觉顺序
QRect itemPaneRect(ItemPane::Type) const;
QVector<QRect> itemPaneSeparatorRects() const;
```

`ItemPane` 与列方向的 `TablePane` 对称：

```cpp
struct ItemPane
{
    enum class Type { FrozenTop, Scrollable, FrozenBottom };
    Type type = Type::Scrollable;
    QRect viewportRect;          // 视口坐标；宽 = 视口宽
    qsizetype firstRow = -1;     // 该 pane 覆盖的行区间（逻辑行号）
    qsizetype lastRow = -1;
};
```

重叠规则与列方向一致：同时出现在顶部与底部集合里的行只算顶部。

## 4. 布局与滚动

```text
┌───────────────────────────────┐  ← 顶部冻结 pane（高 = 冻结行高之和，offset = 0）
├───────────────────────────────┤  ← 交界线（1 px overlay，样式复用 PaneSeparatorStyle）
│                               │
│        可滚动 pane             │  ← 高 = 视口高 - 冻结高；offset = 纵向滚动偏移
│                               │
├───────────────────────────────┤  ← 交界线
└───────────────────────────────┘  ← 底部冻结 pane（offset = 0）
```

* **高度分配**：冻结 pane 取自己覆盖行的高度之和；放不下时按顺序压缩（与列方向同样的规则，
  但按行粒度，超出的行不显示而不是压成 0 高，因为 0 高的行没有意义）；可滚动 pane 拿剩下的高度。
* **滚动范围**：只由可滚动 pane 决定。最大纵向偏移变成
  `max(0, 可滚动行总高 - 可滚动 pane 高)`；横向偏移与它互不影响。
* **滚动锚点**（§10）：锚点始终指可滚动区域里的行。冻结行不需要锚点（它们不动）；
  `scrollTo(row)` 命中冻结行时直接返回"已可见"，不改变偏移。
* **`ScrollMapper`**：不新增第二个映射器。可滚动区是一个"行区间 + 前缀高度"的视图
  （`firstScrollableRow` / `scrollableRowCount` / `prefixHeight`），`SizeIndex` 仍按整表尺寸工作，
  纵向偏移在 `[prefixHeight, prefixHeight + 可滚动总高)` 空间里换算 —— 这样 `BlockSizeIndex`
  的分块与前缀和都不用为冻结行分叉。

### 4.1 实现后的精确定义（第 1-4 步落地时确认下来的）

* `verticalOffset()` 的语义与范围**完全不变**：仍是 `[0, contentExtent - viewportHeight]`，
  `maximumVerticalOffset()` 也不变 —— 这正是"冻结不产生额外滚动空间"的算式体现（可滚动区高度减少
  的像素数恰好等于冻结带高度）。
* 三块 pane 的内容映射（`itemPaneScrollOffset()`）：
  * 顶部冻结：0（内容 y = 屏幕 y）；
  * 可滚动：`verticalOffset()` —— 于是可滚动 pane 的**顶边**显示内容 `offset + 冻结顶高`，
    也就是 Excel 的行为：滚一行，内容就跟着走一行，被冻结带盖住的那些行不再出现在可滚动区顶部；
  * 底部冻结：`contentExtent - viewportHeight`（"滚到最底"），这样最后几行正好贴在视口底边；
    该值允许为负，内容比视口矮时同样成立。
* 物化窗口 = 可滚动窗口（`coreVisibleRange()` 已经把冻结顶高加进上下边界）∪ 顶部冻结行 ∪ 底部
  冻结行；overscan **不越过**冻结边界，否则冻结行会被物化两次。
* Cell Widget Mode 的裁剪容器是"行 pane × 列 pane"的**交集**：`(顶部|可滚动|底部) × (每个列 pane)`。
  Row Widget Mode 不需要新的表格侧容器 —— 行控件本身被内核的行 pane 容器裁一次，行控件内部的
  列 pane 容器再裁一次，两个方向正交。

## 5. 绘制、命中与交互

* **Row Widget Mode**：新增"行 pane 裁剪容器"（`ItemPaneClipHost`），与列方向"每个滚动 pane
  一个裁剪容器"对称 —— 可滚动行的行控件放进容器，冻结行的行控件直接挂在视口上（`raise()`）。
  一个行控件仍然横跨整个视口宽度，所以纵向裁剪与横向的列 pane 裁剪是正交的，两者各自负责一维。
* **Cell Widget Mode**：同样的规则，只是裁剪到 cell。
* **交界线与列冻结同款**：行 pane 的交界线用**和列 pane 完全一样**的样式与颜色——默认颜色就是
  "当前样式画 section 分隔线用的那个颜色"（列方向通过渲染一个 section 探到，行方向通过
  `itemPaneSeparatorColor()` 这个虚函数复用同一个探测结果），不是另猜一个调色板角色；宽度/线型/
  显式颜色也是同一个旋钮：`VirtualTableView::setPaneSeparatorStyle()` 会同时下发给列与行。
  **横向线还会跨过行号条**（`itemPaneSeparatorLeftExtension()`），就像竖向线会跨过横向表头条
  一样，两条边界在视觉上是对称的。list/tree 没有列方向，`itemPaneSeparatorColor()` 的默认实现
  回落到调色板中灰。
* **命中测试**：`indexAt()` 先把 y 折到"pane 局部坐标"，再按该 pane 的行区间解析行号；
  落在交界线上的点归属前一个 pane（与列方向一致）。
* **键盘/选择**：`Down` 从最后一个冻结行进入可滚动区第一行；`Up` 反向同理；
  跨越边界时若目标行在可滚动区且不在窗口内，先滚动再设 current。
* **拖放**（§38）：插入指示器与落点计算都按"行 pane 局部坐标"进行；落在冻结行上时，
  插入位置只能在冻结区内部或紧随其后（不允许把一个行插到冻结行之间而让冻结集合无声扩大）。
* **span**（§43）：跨行合并**不跨行 pane**，与"span 不跨 pane"的横向规则一致
  （跨 pane 的合并栽到锚点所在 pane 的边界）。
* **无障碍**（§37）：冻结行是普通的 `Row` 节点，`rect()` 用它们自己的 pane 坐标；
  可见行集合 = 冻结行 + 可滚动区可见行。

## 6. 实现顺序

### 第 5 步的落地方式（纵向表头按 pane 切分）

* **每个行 pane 一条行号条**：装的那条（`verticalHeader()`）覆盖可滚动带，冻结的上下两带各有
  一个同类型渲染器（`NativeHeaderView`；应用装的自定义渲染器无法克隆，此时按文档退化为单条，
  不猜测它的语义）。三条都放在自己的 pane 矩形上（`x = 视口左边界 - 行号条宽`）。
* **每条持有自己的偏移**：pane 的矩形是"视口相对"的，而条子是视图的子控件，所以位置要加上视口
  自身的 y（有横向表头时它不是 0 —— 这一点在实现时被测试抓到过一次）。偏移 = "这条带子顶边显示
  的内容 y"：顶部 0、可滚动 `verticalOffset() + 冻结顶高`、底部 `内容高 - 冻结底高`。
* **`NativeHeaderView::setPaneOffset()` 的优先级**：显式偏移现在**优先于** `HeaderGeometry` 的
  offset（不再要求同时有一个 pane filter）。这样三条带子各自持有偏移，共享的行几何在滚动、
  改行高、换模型时都不会把它们冲掉；没有冻结行时那条又会退回"跟随几何"。
* **顺带修掉两个既有缺陷**（都由这次的对齐测试暴露）：
  1. **均匀行高下行的行号与行错位**：行几何在均匀模式下刻意保持空（只有 default size），于是
     native 表头的镜像循环一个 section 都不遍历，条子沿用 Qt 自己的默认行高（24）——行高不是 24
     时行号就逐行漂移（例如 20 px 行高时第 3 行就差了 12 px）。现在镜像循环覆盖到模型的行数，
     几何里没有的 section 取几何的 default size。
  2. **行几何的最小 section 尺寸是 24**（列几何的默认值），于是 `resizeSection(20)` 被夹到 24 ✗。
     现在行几何的最小值跟随内核的限制（1 px，`setRowHeight()` 也是这么夹的）。

1. **内核**：行区间视图（`firstScrollableRow` / `scrollableRowCount` / `prefixHeight`）+
   纵向偏移换算；`SizeIndex` / `ListLayout` 不改接口；单元测试钉住"冻结不产生额外滚动空间"。
2. **行 pane 布局**：`ItemPaneLayout`（与 `TablePaneLayout` 对称）+ `itemPanes()` 查询 +
   交界线 overlay；测试：默认 0 冻结时逐像素等于现状。
3. **两种物化模式的裁剪**：Row Widget Mode 的行 pane 容器、Cell Widget Mode 的 cell 容器。
4. **命中 / 键盘 / 滚动 API**：`indexAt()`、`scrollTo()`、`ensureVisible()`、`PageUp/PageDown`、
   冻结集合与重叠规则。
5. **纵向表头**：行号条按同样的 pane 切分（冻结部分不动、可滚动部分跟着偏移）；
   拖动行号条改行高只在可滚动行上生效。
6. **表状态持久化**（§32）：把冻结行数写进表格级状态（版本号 +1，旧格式仍可恢复）。
7. **示例与文档** ✓：`examples/table_frozen_rows`（顶部/底部冻结行 + 冻结列组合、可切换、
   `--check` 自检、`--snapshot` 截图）、[features.md](features.md) 能力表与 roadmap 同步。

每一步按既有节奏收尾：规格/决策进文档 → 实现 → 单元测试（必要时 GUI 场景）→ Qt 6 与 Qt 5
双配置 `all` 构建 + CTest + 示例退出码 → 同步 README / roadmap → 一个独立提交。

## 7. 开放问题（实现前确认）

1. **是否需要底部冻结**？顶部冻结是绝大多数场景；底部冻结会让 `scrollTo()`、虚拟化与表头切分
   都多一倍分支。规格按"支持但默认 0"写，实现可以先只做顶部。
2. **冻结行是否参与选择**？规格按"参与"写 —— 它们就是普通行，只是不动。
3. **冻结行与 `QSortFilterProxyModel` 的排序**：排序后"前 N 行"指排序后的前 N 行，不跟随原始
   行号 —— 与 `QHeaderView` 冻结列的语义保持一致。

### 7.1 实现后的结论

三个开放问题都按"最不意外的默认"落地，没有额外开关：

1. **底部冻结支持、默认 0** ✓ 实现里顶/底是对称的（`frozenRows()` / `frozenBottomRows()`），
   一个行出现在两边时只算顶部。
2. **冻结行参与选择** ✓ 它们就是普通行（`QItemSelectionModel` 的 current/selection 与键盘导航都
   按行号工作），只是不随滚动移动。
3. **排序语义跟随模型** ✓ 冻结的是"当前模型顺序下的前 N 行"，不绑定原始行号。

## 8. 持久化格式（§32）

表格级状态（`saveHeaderState()` / `restoreHeaderState()`）版本升到 **2**：

```text
magic('VIVT') | version=2 | columnStateSize | columnState
              | frozenLeftSet | frozenRightSet
              | frozenTopRows | frozenBottomRows      ← v2 新增
```

版本 1（没有最后两个字段）仍然可以恢复：它的列状态与冻结列照常生效，冻结行数视为 0。
不是表格级状态时仍然按"裸的 `HeaderGeometry` 状态"处理，所以更早保存的状态也继续可用。

**已知边界**：状态里保存的是冻结集合，而不是显式 pane 列表（`setPanes()`）——应用自己组合的
pane 布局属于应用组成，不属于用户状态；恢复之后会回到默认三段。（这一点与冻结列现有行为一致。）
