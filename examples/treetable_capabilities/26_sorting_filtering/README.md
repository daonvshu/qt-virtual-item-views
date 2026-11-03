# 26 同级排序与递归过滤

点击列头排序或过滤名称含 1 的节点，递归过滤保留匹配节点的祖先。视图只接收 proxy 索引，业务通过 mapToSource 回到原层级模型。

入口：[example.cpp](example.cpp)。主要接口：`setSortingEnabled`、`sortByColumn`、`QSortFilterProxyModel`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 26`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
