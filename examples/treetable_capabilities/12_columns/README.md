# 12 列宽、隐藏与换序

调整列宽、隐藏第 2 列并换序。逻辑列 0 无论移动到哪里都保留树装饰，逻辑列身份与视觉位置分离。

入口：[example.cpp](example.cpp)。主要接口：`setColumnWidth`、`setColumnHidden`、`moveColumn`、`setColumnResizeEnabled`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 12`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。
