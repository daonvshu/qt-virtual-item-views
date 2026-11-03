# 23 表头交互与调整

配置显示、尺寸、文本对齐、padding、字体和颜色。拖动调整显式启用；垂直表头显示可见树行序号，颜色或 model header role 不能作为节点身份。

入口：[example.cpp](example.cpp)。主要接口：`horizontalHeader`、`verticalHeader`、`setColumnResizeEnabled`、`setVerticalHeaderResizeEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 23`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
