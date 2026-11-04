# 14 节点间距与分割线

切换间距中心或两侧分割线、状态跨间距、表头穿线、间距背景与自定义间距控件；仅支持冻结列。

中心模式默认在内容四周保留半间距。可分别勾选“水平表头与首行之间保留全间距”和“垂直表头与首列之间保留全间距”，把对应交界改为全间距；两侧模式下则从 0 改为全间距。树表直接使用继承的公共接口：

```cpp
view->setHeaderContentSpacingEnabled(Qt::Horizontal, true); // Full rowSpacing() below the horizontal header.
view->setHeaderContentSpacingEnabled(Qt::Vertical, true);   // Full columnSpacing() beside the vertical header.
```

方向参数表示表头方向；两项默认均为 `false`，`headerContentSpacingEnabled()` 可分别查询。开关控制内容起点的完整间距，随内容滚动，行号和列头同步对齐。内容末端、冻结列之间及节点之间的间距不受影响；关闭开关、隐藏表头或将其尺寸设为 0 后恢复普通外侧规则：中心模式为半间距，两侧模式为 0。零间距不增加留白。

全间距的分割线随模式切换：中心模式补中心线；两侧模式只补间距下方（水平表头）或右侧（垂直表头）的内容边线，另一侧沿用表头已有边线。关闭开关后保留中心模式的半间距，不在半间距内部额外补线。补线与普通网格共用显隐、颜色、物理像素线宽和贯通设置，零间距不增加额外线条。

默认所有节点使用“行间距”的统一值。勾选“节点独立间距（18 + 6）”后，节点 1.3 的列 0 设置 `NodeRowSpacingBelowRole = 18`，节点 1.4 的列 0 设置 `NodeRowSpacingAboveRole = 6`，两者之间的间距合计为 24，覆盖统一行间距。取消勾选会清除这两个 role，恢复统一值；切换数据量时保留开关选择。

行分割线固定覆盖实际列的整行范围，包括分支与缩进区域；不提供按树层级裁剪线段的模式。

入口：[example.cpp](example.cpp)。主要接口：`setRowSpacing`、`setColumnSpacing`、`setGridLinePlacement`、`setHeaderContentSpacingEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 14`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
