# 09 显示根与相对深度

切换整棵树或指定分组子树，点击节点查看相对深度与可见映射行。显示根本身不作为行，每层子数据都提供完整列。

入口：[example.cpp](example.cpp)。主要接口：`setRootIndex`、`rootIndex`、`itemDepth`、`visibilityIndex`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 9`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
