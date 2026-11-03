# 01 基础多列树

用三列 QStandardItemModel 构造分组与子节点，默认展开第一组。列 0 拥有子节点，其他列属于同一节点的字段。

入口：[example.cpp](example.cpp)。主要接口：`setModel`、`setCellAdapter`、`expand`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 1`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
