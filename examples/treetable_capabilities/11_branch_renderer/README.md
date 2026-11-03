# 11 自定义分支绘制

在祖先连接线加减框与可调大小的蓝色三角之间切换。颜色与形状由本章 renderer 决定；true 将 renderer 所有权交给视图。

入口：[example.cpp](example.cpp)。主要接口：`setBranchIndicatorRenderer`、`branchState`、`BranchIndicatorRenderer`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 11`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
