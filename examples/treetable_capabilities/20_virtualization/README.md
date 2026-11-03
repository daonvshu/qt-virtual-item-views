# 20 二维虚拟化与复用

按需生成百万节点与 200 列，调节行列 overscan、池上限并清空空闲池。折叠节点不物化；统计不代表模型已分配同样数量的业务对象。

入口：[example.cpp](example.cpp)。主要接口：`setOverscan`、`setColumnOverscan`、`recycler`、`materializedCellIndexes`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 20`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
