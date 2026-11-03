# 17 共享选择与键盘导航

两张树表共享选择模型但独立维护展开和 hover。程序选择第一组中的 2～5 个子节点，比较四种选择模式、行格模式与表头选择。行模式左/右操作树，单元格模式可用 Alt+左/右操作树。

入口：[example.cpp](example.cpp)。主要接口：`setSelectionModel`、`setSelectionMode`、`setSelectionBehavior`、`setHorizontalHeaderSelectionEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 17`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
