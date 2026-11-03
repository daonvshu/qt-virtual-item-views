# 25 保存与恢复布局

先保存，再改变宽度、隐藏列、顺序和冻结列，最后恢复。该状态不包含展开状态；先设置模型与显示根，恢复时列 schema 必须匹配。

入口：[example.cpp](example.cpp)。主要接口：`saveHeaderState`、`restoreHeaderState`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 25`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
