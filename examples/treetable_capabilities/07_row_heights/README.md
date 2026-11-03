# 07 固定与动态行高

展开分组并调整列宽，比较固定高度、自动换行测量、显式优先与测量优先。行几何接口使用可见序号，先从节点索引转换。

入口：[example.cpp](example.cpp)。主要接口：`setItemHeightMode`、`setAutoMeasureItemHeight`、`setRowHeight`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 7`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
