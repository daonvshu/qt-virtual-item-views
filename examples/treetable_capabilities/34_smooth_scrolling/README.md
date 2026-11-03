# 34 可选平滑滚动

匹配的 SmoothScrollbar 可用时才编译并显示此页。一个视图一个控制器，切换启用、步长和停止，观察展开与冻结首列配合横纵滚动。

入口：[example.cpp](example.cpp)。主要接口：`setWheelScrollPixels`、`setHorizontalWheelPixels`、`SmoothScrollController`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 34`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。

需要 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR=ON`，并提供匹配当前 Qt 主版本的 SmoothScrollbar 包；不可用时其他章节编号保持不变。
