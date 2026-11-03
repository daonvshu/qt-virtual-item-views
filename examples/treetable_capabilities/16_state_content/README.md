# 16 状态关联内容

移动鼠标或选中节点，只在状态激活时显示第 3 列操作控件。事件发生时查询当前绑定，解绑时清理隐藏状态。

入口：[example.cpp](example.cpp)。主要接口：`visualState`、`CellWidgetAdapter::visualStateChanged`、`cellIndexForWidget`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 16`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
