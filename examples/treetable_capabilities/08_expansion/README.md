# 08 展开收起与淡入淡出

选择节点后单步或递归展开，切换动画、淡入淡出和时长。collapseAll 是即时批量收起；节点身份不随动画改变。

入口：[example.cpp](example.cpp)。主要接口：`expand`、`collapse`、`toggleExpanded`、`expandRecursively`、`setExpansionFadeEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 8`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
