# 13 比例列宽与适应视口

比较固定宽度、比例列和 FitViewport，拖动列边界观察比例取消或侧向分摊。树缩进始终在逻辑列 0 的可用宽度内。

入口：[example.cpp](example.cpp)。主要接口：`setColumnStretchFactor`、`setColumnResizeMode`、`setColumnWidth`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 13`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
