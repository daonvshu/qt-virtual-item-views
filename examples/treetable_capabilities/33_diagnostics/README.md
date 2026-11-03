# 33 统计与性能诊断

手动查询物化与复用统计、有界生命周期记录，并按需开启每秒性能摘要。用 viv.performance.refresh、viv.performance.paint 筛选日志，viv.performance.details 输出完整分项。

入口：[example.cpp](example.cpp)。主要接口：`stats`、`setLifecycleLoggingEnabled`、`setPerformanceLoggingEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 33`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
