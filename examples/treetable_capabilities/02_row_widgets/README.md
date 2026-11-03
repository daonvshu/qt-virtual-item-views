# 02 整行物化与 ColumnHost

每个节点一个 RowWidget；每列一个 ColumnHost。首列缩进、列换序和裁剪由框架处理，绑定通过 siblingAtColumn 保留父节点身份。

入口：[example.cpp](example.cpp)。主要接口：`setTableAdapter`、`ColumnHost`、`moveColumn`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 2`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
