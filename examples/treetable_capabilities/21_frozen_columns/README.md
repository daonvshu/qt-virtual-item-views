# 21 左右冻结列

分别启用左右冻结列，横向滚动并展开子树，检查树装饰与内容裁剪。树表不支持冻结行；自定义 pane 见下一章。

入口：[example.cpp](example.cpp)。主要接口：`setFrozenColumns`、`setFrozenRightColumns`、`setPaneSeparatorStyle`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 21`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
