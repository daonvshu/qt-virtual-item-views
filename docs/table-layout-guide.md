# 表格布局：列宽、冻结、分区与合并

本页适用于 `VirtualTableView` 的行控件和单元格控件模式。列的尺寸、顺序、隐藏和排序指示统一写入 `HeaderGeometry`；表头与内容读取同一份已提交几何。以下片段假定表格已设置模型与 adapter，模型至少有 7 列、6 行；具体建表方式见 [表格与单元格](table.md)。

## 列宽与比例分配

```cpp
table.setDefaultColumnWidth(150);
table.setColumnWidth(0, 60);          // 固定宽度
table.setColumnHidden(4, true);
table.moveColumn(2, 1);              // 调整视觉顺序，不修改模型列顺序
table.setColumnStretchFactor(1, 2.0);
table.setColumnStretchFactor(2, 1.0); // 其余宽度按 2:1 分给这两列
```

因子为 0 的列保持自身宽度。用户拖动参与比例分配的列边界后，该列变为固定宽度。只需要最后一列填满剩余空间时可用 `setStretchLastColumn(true)`。

## 冻结列和冻结行

```cpp
table.setFrozenColumns({0, 1});
table.setFrozenRightColumns({5});
table.setFrozenRows(2);
table.setFrozenBottomRows(1);
```

列集合使用逻辑列号，显示时按视觉顺序排列；同时位于左右集合的列留在左侧。行参数是顶部/底部的行数，而不是行号。冻结区域不随对应方向滚动，也不会额外增加滚动空间。行号条跟随冻结行分带，且仍与内容行对齐。

## 多个水平滚动组

简单布局用冻结列 API 即可。需要多个独立水平偏移时，显式给出按视觉顺序排列的 pane：

```cpp
viv::TablePaneSpec frozen;
frozen.logicalColumns = {0};
frozen.scroll = viv::PaneScroll::Frozen;

viv::TablePaneSpec primary;
primary.logicalColumns = {1, 2, 3};
primary.scrollGroup = 0;

viv::TablePaneSpec secondary;
secondary.logicalColumns = {4, 5, 6};
secondary.scrollGroup = 1;

table.setPanes({frozen, primary, secondary});
table.setHorizontalOffset(1, 120); // 第二个滚动组
```

第一个可滚动 pane 的组是主组，由表头几何和水平滚动条驱动；其他组通过 `setHorizontalOffset(group, offset)` 调整。调用 `setPanes({})` 可恢复默认布局。每列只应出现在一个 pane 中，同组可滚动 pane 应相邻。

## 合并单元格

```cpp
table.setSpan(0, 0, 1, 3); // 第 0 行第 0 列开始，跨 1 行 3 列
table.setSpan(4, 1, 2, 2);
// 需要取消时：table.removeSpan(0, 0) 或 table.clearSpans();
```

规则来自业务数据时，实现 `TableSpanProvider`，只在锚点返回跨度：

```cpp
class GroupSpans : public viv::TableSpanProvider
{
public:
    viv::TableSpan spanAt(const QModelIndex &index) const override
    {
        return index.column() == 0 && index.row() % 5 == 0
            ? viv::TableSpan{1, 3} : viv::TableSpan{};
    }
    viv::TableSpan maximumSpan() const override { return {1, 3}; }
};

GroupSpans spans; // 生存期长于 table
table.setSpanProvider(&spans);
```

`setSpanProvider()` 默认由业务持有 provider。可用 `anchorIndex(index)` 找到被覆盖单元格的锚点。合并区域不能跨 pane，实际矩形会裁剪到锚点 pane。单元格模式只物化锚点控件；行控件模式可从 `TableRowLayoutContext::spans()` 读取覆盖关系。

合并跨度只描述模型坐标中的覆盖关系；列宽、行高、隐藏和冻结变化时，合并区域的显示矩形由视图重新计算。
