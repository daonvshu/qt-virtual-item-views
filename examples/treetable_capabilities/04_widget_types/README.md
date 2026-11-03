# 04 层级与列控件类型

根节点使用文本，子节点的第 2 列使用进度条。复用池按类型隔离；绑定和解绑处理原来控件的完整状态。

入口：[example.cpp](example.cpp)。主要接口：`CellWidgetAdapter::cellWidgetType`、`createCellWidget`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 4`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
