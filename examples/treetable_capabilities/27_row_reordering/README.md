# 27 同级行号拖动排序

拖动行号进行同级排序，也可将当前节点移到同级末尾。自定义模型维护稳定节点指针并使用 begin/endMoveRows；不使用仅适合平表的 ReorderableTableModel。

入口：[example.cpp](example.cpp)。主要接口：`setVerticalHeaderDragEnabled`、`setRowFollowsHeaderVisual`、`QAbstractItemModel::moveRows`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 27`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
