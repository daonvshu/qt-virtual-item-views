# 35 圆角边框与透明视图

框架 Alpha 合成统一处理表头、内容、分支和滚动条的透明圆角，切换背景、间距、冻结列及少量或空数据。页面使用 Designer，状态圆角独立配置。

入口：[example.cpp](example.cpp)。主要接口：`setFrameCornerRadius`、`setFrameBorderWidth`、`setFrameBorderColor`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 35`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。布局见 [borders.ui](borders.ui)，需要启用 CMake `AUTOUIC`。
