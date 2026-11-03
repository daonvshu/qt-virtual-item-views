# 06 层级模型更新

修改当前格、增加子节点、删除当前节点、为全部层级补列和重置数据。row() 必须与 parent() 一起使用；无需重新 setModel 刷新。

入口：[example.cpp](example.cpp)。主要接口：`setModel`、`currentIndex`、`expand`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 6`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
