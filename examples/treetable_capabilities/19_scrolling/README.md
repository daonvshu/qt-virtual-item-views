# 19 大树定位与滚动

按需生成 5 组、每组 20 万子节点与 24 列。定位前先展开父路径，比较像素和按项滚动；64 位偏移不依赖 Qt 滚动条的整数上限。

入口：[example.cpp](example.cpp)。主要接口：`scrollTo`、`setVerticalOffset`、`setWheelScrollItems`、`visibilityIndex`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 19`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
