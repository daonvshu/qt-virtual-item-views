# 22 独立横向滚动分组

首列冻结，其余列拆成两个独立横向滚动组；通过两个输入框或鼠标所在区域的横向滚动分别移动。纵向所有树节点仍作为整体滚动。

入口：[example.cpp](example.cpp)。主要接口：`setPanes`、`setHorizontalOffset`、`scrollGroups`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 22`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
