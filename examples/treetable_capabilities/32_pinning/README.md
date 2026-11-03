# 32 屏外保留与释放

保留当前格后滚出视口，再查询控件并释放。保留只改变物化生命周期；折叠、删除、模型重置仍需业务结束编辑或异步活动，避免长期保留过多控件。

入口：[example.cpp](example.cpp)。主要接口：`setItemPinned`、`setMaxPinnedItems`、`cellWidget`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 32`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
