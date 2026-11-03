# 28 同级单元格合并

在第一组的连续叶子间合并或按 provider 规则合并；查询覆盖格的锚点。不能跨父节点、展开后代或 pane；显式 setSpan 的行号从真实节点转换为可见序号。

入口：[example.cpp](example.cpp)。主要接口：`setSpan`、`setSpanProvider`、`anchorIndex`、`spanRect`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 28`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
