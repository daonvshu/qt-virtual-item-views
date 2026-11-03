# 31 异步加载子节点

展开待加载分组后由业务定时器模拟异步完成，加载完整的多列子节点。请求以持久列 0 索引定位，完成前检查删除、重置；模型只能在 GUI 线程更新。

入口：[example.cpp](example.cpp)。主要接口：`expanded`、`expand`、`QPersistentModelIndex`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 31`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
