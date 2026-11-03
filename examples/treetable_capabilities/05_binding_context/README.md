# 05 节点结构体与绑定上下文

切换整行与单元格物化，更新当前节点结构体。每级节点的列 0 各存一份 payload，绑定直接读取 rowData、depth、logicalColumn。

入口：[example.cpp](example.cpp)。主要接口：`setItemDataRole`、`ItemBindingContext`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 5`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
