# 14 节点间距与分割线

切换间距中心或两侧分割线、状态跨间距、表头穿线、间距背景与自定义间距控件；仅支持冻结列。

默认所有节点使用“行间距”的统一值。勾选“节点独立间距（18 + 6）”后，节点 1.3 的列 0 设置 `NodeRowSpacingBelowRole = 18`，节点 1.4 的列 0 设置 `NodeRowSpacingAboveRole = 6`，两者之间的间距合计为 24，覆盖统一行间距。取消勾选会清除这两个 role，恢复统一值；切换数据量时保留开关选择。

行分割线固定覆盖实际列的整行范围，包括分支与缩进区域；不提供按树层级裁剪线段的模式。

入口：[example.cpp](example.cpp)。主要接口：`setRowSpacing`、`setColumnSpacing`、`setGridLinePlacement`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 14`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
