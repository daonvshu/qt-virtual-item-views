# 03 单元格物化

滚动 80 列并定位到末格，观察独立单元格物化。子节点与根节点使用相同列模式，首列仍显示树分支。

入口：[example.cpp](example.cpp)。主要接口：`setMaterializationMode`、`setCellAdapter`、`setColumnOverscan`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 3`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
