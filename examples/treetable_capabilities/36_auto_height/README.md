# 36 自适应高度与滚动上限

修改根与子节点数量并展开收起；高度包含表头、展开行、间距和必要滚动条，达到最大高度或父布局预算后才滚动。Designer 页面显示实际和建议高度及滚动范围。

入口：[example.cpp](example.cpp)。主要接口：`setAutoHeightEnabled`、`setMaximumHeight`、`setTrailingRowGridLineAutoHideEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 36`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。布局见 [auto_height.ui](auto_height.ui)，需要启用 CMake `AUTOUIC`。
