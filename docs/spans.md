# Span 与 advanced panes 规格（§43 v0.7 待实现项）

方案文档在 v0.7 里只写了名字（`spans`、`advanced table panes`），没有语义。这份文档把语义先定下来，
实现按第 6 节的顺序推进。两条不变量先摆在前面，它们决定了后面所有取舍：

1. **`HeaderGeometry` 仍然是列几何与列状态的唯一事实来源**（§45.10）。span 不产生第二份列宽/列序。
2. **行的几何仍然由 row layout 决定**（`ListLayout` + `SizeIndex`）。span 不产生第二份行高。

也就是说：span 是"某个锚点 cell 覆盖若干行列"的**投影信息**，而不是一套并行的几何。

## 1. Span 的模型表达

```cpp
/// 一个 span 的锚点与跨度，坐标全部是 model 坐标（逻辑行/逻辑列）。
struct TableSpan
{
    int rowSpan = 1;
    int columnSpan = 1;

    bool isMerged() const { return rowSpan > 1 || columnSpan > 1; }
};

/// Span 的来源：默认实现是"没有 span"，业务可以给 map、也可以按 data 现算。
class TableSpanProvider
{
public:
    virtual ~TableSpanProvider() = default;
    /// \a index 处的 span（仅当它是锚点时返回 >1 的跨度；被覆盖的 cell 返回 1x1）。
    virtual TableSpan spanAt(const QModelIndex &index) const = 0;
};
```

* 视图侧 API：`setSpanProvider(TableSpanProvider *, bool takeOwnership = false)`、`spanProvider()`，
  以及开箱即用的 `TableSpanMap`；表格上还有一层与 `QTableView` 同名的便捷入口
  `setSpan(row, column, rowSpan, columnSpan)` / `removeSpan(row, column)` / `clearSpans()`
  （第一次调用会自动建一个由视图持有的 map）。
* 查询入口：`spanAt(index)`、`anchorIndex(index)`、`isSpanCovered(index)`、
  `spanRect(anchor)`（合并矩形，viewport 坐标）、`cellRect(index)`（锚点 = 合并矩形，
  被覆盖 cell = 空矩形）。
* **锚点约定**：span 只在锚点登记；被覆盖的 cell 由 `anchorIndex(index)` 反查锚点。
  这避免"同一块区域有两份真相"，也让 model 不需要实现任何 span API。
* **合法性**：`rowSpan/columnSpan < 1` 视为 1；越界的 span 裁剪到 model 边界；**重叠的 span 视为非法**，
  后者被忽略并 `qWarning()` 一次（与 `setMaxPinnedItems` 的诊断风格一致）。
* **改动通知**：provider 变化后业务调用 `view.rebuildSpans()`（或直接 `setSpanProvider()` 重新安装），
  内部走 `markDirty()`；行/列结构变化时 span 表按"锚点身份 + 跨度"保留并重新裁剪。

## 2. 命中测试与几何

| 入口 | 语义 |
| --- | --- |
| `indexAt(pos)` | 先按现有列/行命中原 cell，再 `anchorIndex()` 折回锚点，返回**锚点** index |
| `cellRect(row, logicalColumn)` | 锚点返回**合并后**的矩形（含跨列宽度、跨行高度）；被覆盖 cell 返回空矩形 |
| `columnAtViewportX(x)` | 不变：列命中只依赖 `HeaderGeometry` + pane 布局 |
| `visualRect(index)` | 行矩形保持不变；cell 层只多一个 `cellRect()` 的 span 版本 |
| 拖放 `resolveDropTarget()` | 命中被覆盖的 cell 时解析到锚点（`column` = 锚点列），drop indicator 用合并矩形 |

跨行 span（`rowSpan > 1`）的几何是"锚点行起 N 行的高度之和"，与被覆盖行是否物化无关（尺寸来自
`ListLayout` 的已提交几何）。**渲染**则是另一回事：Cell Widget Mode 下框架自己把锚点 cell 控件
放大到合并矩形，跨行原生可用；Row Widget Mode 下每一行都有自己的行控件，被覆盖行的控件会盖住
合并区域，所以跨行 span 要么用 Cell Widget Mode，要么由业务用 `spanOf()/spanRect()` 自己在行
控件里留白（第 3 步的 API）。

## 3. 与列几何、冻结 pane 的关系

* **span 不跨 pane**：pane 边界是"列几何折叠"的硬边界（冻结列不参与横向滚动，可滚动列有偏移），
  一个跨边界的 span 会被**裁剪到锚点所在 pane**，被裁掉的部分仍然由本 pane 的列绘制。
  这样每个 pane 仍然可以独立裁剪子控件（§31 的 `PaneClipHost` 机制不变）。
* **列宽/列序变化**：span 不参与列宽计算。拖动列宽、移动列、隐藏列之后，span 的"合并宽度"是
  锚点 pane 内当前列几何的和；隐藏中间列时合并宽度自动变窄（不补偿，这是"几何唯一来源"的自然结果）。
* **行高变化**：`rowSpan = 1` 的 span 与行高无关；`rowSpan > 1` 的合并高度是这些行当前高度之和。
* **Cell Widget Mode**（§28）：只为锚点 materialize cell 控件，被覆盖的 cell 不创建控件；
  cell 的数量上限仍是 `可见行 x 可见列`（span 只会让它更少）。
* **Row Widget Mode**（§26/§27）：`ColumnHost` 的对齐规则扩展为"锚点 host 占合并宽度、
  被覆盖列的 host 隐藏"。为此 `TableRowLayoutContext` 增加：

  ```cpp
  TableSpan spanOf(int logicalColumn) const;   // 该列在本行的跨度（非锚点返回 1x1）
  QRect spanRect(int row, int logicalColumn) const;  // 合并后的 cell 矩形（viewport 坐标）
  ```

  业务代码用 `spanOf()/spanRect()` 就能做复杂自定义（例如"标题行跨整行"），
  不需要自己算列几何；不用 span 的业务完全不受影响（`spanOf()` 恒为 1x1）。

## 4. 绘制与交互

* 行控件的背景/边框仍由业务绘制；框架只负责 host 的位置与尺寸（span 只改变尺寸）。
* 选中：点击合并区域的任意位置选中**锚点**；`SelectRows` 语义下选中锚点所在行。
* 键盘：Left/Right 在合并区域内跳到下一个非覆盖 cell（跳过被覆盖列），与 `columnAtViewportX` 一致。
* 分支/树不受影响：树没有列 span（`VirtualTreeView` 不实现 span provider 时行为不变）。

## 5. Advanced panes（> 3 个 pane / 嵌套 pane）

当前 `TablePaneLayout` 固定三段：`FrozenLeft | Scrollable | FrozenRight`（§31）。扩展目标是
"任意数量的 pane"，语义如下：

```cpp
enum class PaneScroll { Frozen, Scrollable };   // 冻结 = 不参与横向偏移
struct PaneSpec
{
    QVector<int> logicalColumns;   // 顺序 = 视觉顺序；仍由 HeaderGeometry 提供宽/隐藏
    PaneScroll scroll = PaneScroll::Scrollable;
    int scrollGroup = 0;            // 同组共享一个横向偏移（同组内列必须相邻）
};
void setPanes(const QVector<PaneSpec> &specs);   // 空 = 回到默认三段
```

* **布局**：pane 从左到右依次排布，宽度 = 组内可见列宽之和（超出视口的 pane 被压缩到 0，
  与今天 `TablePaneLayout::update()` 的做法一致）；每个 pane 一个 `PaneClipHost` 负责裁剪。
* **滚动**：同 `scrollGroup` 的 scrollable pane 共享一个横向偏移（一个滚动条）；
  Frozen pane 不参与偏移。多组 = 多个横向偏移，默认只给第一组接滚动条，
  其余组通过 `setHorizontalOffset(group, offset)` 由业务驱动（例如"左右两侧各自独立滚动"的场景）。
* **pane 交界线**：仍然是一根覆盖控件（`vivPaneSeparatorLine`），数量 = pane 数 - 1。
* **向后兼容**：`setFrozenColumns()/setFrozenRightColumns()` 是 `setPanes()` 的语法糖
  （冻结列在左/右各成一组，其余为可滚动组），`panes()`/`paneTypeForColumn()` 语义不变。
* 表头同样 pane 化：每个 pane 一个表头实例 + `setPaneFilter()`（今天的机制直接复用）。

## 6. 实现顺序

1. `TableSpan` / `TableSpanProvider` / `TableSpanMap` + `anchorIndex()` + `indexAt()`/`cellRect()`
   折回锚点（这一层不需要碰绘制，先用单元测试钉住命中与几何语义）。
2. Cell Widget Mode：只物化锚点，行/列遍历跳过被覆盖 cell。
3. Row Widget Mode：`TableRowLayoutContext` 暴露 `spanOf()/spanRect()`，框架按要求摆放
   `ColumnHost`（锚点占合并宽度、被覆盖列隐藏）。
4. 拖放与选择：命中合并区域解析到锚点，插入指示器用合并矩形。
5. Advanced panes：把固定三段重构为 pane 列表（`PaneSpec`），`setFrozenColumns()` 变成糖。
6. 示例与文档：`examples/table_spans`（跨行标题 + 跨列小计 + 冻结 pane 组合），
   README 能力表与路线图同步。

每一项都按框架既有节奏收尾：先补规格/实现，再补单元测试，最后跑 Qt 5 / Qt 6 双配置构建、
CTest 与示例（见 README 的"验证"一节）。

## 7. 实现状态

已完成（`include/virtualitemviews/tablespan.h`、`src/layout/tablespan.cpp`、
`tests/unit/tst_tablespan`）：

| 步骤 | 状态 |
| --- | --- |
| 1. span 模型 + 锚点 + `indexAt()`/`cellRect()`/`spanRect()` 折回锚点 | 已完成 |
| 2. Cell Widget Mode 只物化锚点（锚点控件放大到合并矩形） | 已完成 |
| 4. 拖放：命中被覆盖单元格时列折回锚点，插入指示器用合并矩形 | 已完成（选择/键盘沿用同一套 `indexAt()` 折回） |
| 3. Row Widget Mode：`TableRowLayoutContext` 暴露 `spanOf()/spanRect()`，框架按 span 摆放 `ColumnHost` | 已完成 |
| 5. Advanced panes：把固定三段重构为 pane 列表（`PaneSpec`） | 待做 |
| 6. 示例 `examples/table_spans` | 已完成 |

顺带对齐的既有能力：accessibility 桥接（§37）也走同一套锚点语义 —— 合并区域只暴露一个
`Cell` 节点（被覆盖的行列不再是独立节点），它的 `rect()` 就是合并矩形。

第 3 步的落地方式：框架在 `layoutContext()` 里按"正在摆放的那一行"填一个
`TableSpanContext`（`spanOf(column)` / `isCovered(column)` / `rect(column)`，最后一个是
**行控件局部坐标**、且被裁剪到本行高度），`applyColumnLayout()` 据此隐藏被覆盖列的
`ColumnHost`、把锚点 host 摆到合并矩形上；业务在 `layoutRowWidget()` 里读同一个对象就能给
合并单元格换样式（`examples/table_spans` 的分组标题就是这么居中加粗的）。没有 span 时代码
路径完全不变（`spans().isEmpty()`）。
