# 29 节点拖放与指示器

Automatic 直接拖动，Manual 从首列手柄触发。边缘插入同级、中部投放子节点，Ctrl 复制；示例模型提供 MIME，成功 Move 后框架删除源节点并继承选择。

入口：[example.cpp](example.cpp)。主要接口：`setDragEnabled`、`setDragStartMode`、`startDrag`、`setDragFeedbackMode`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 29`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
