# 15 状态背景与圆角

按行选择时高亮实际列的整行范围，包含分支与缩进区域；按单元格选择时高亮完整单元格。配置颜色、圆角、时长、曲线和表头延伸；普通控件不重复绘制状态背景。

入口：[example.cpp](example.cpp)。主要接口：`setVisualStateCornerRadius`、`setSelectionBehavior`、`setHoverBackgroundColor`、`setSelectedBackgroundColor`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 15`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
