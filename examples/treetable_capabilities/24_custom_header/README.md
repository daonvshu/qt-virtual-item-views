# 24 表头样式、Adapter 与 Corner

两个页签分别演示状态渐变与选中图标、组合表头控件与全选 corner。普通样式直接配置 header；只有自定义内容才更换 adapter，冻结克隆复用对应设置。

组合表头继承 `StyledHeaderSection`，通过布局添加标题与按钮，同时保留默认背景、状态动画和分割线。adapter 继承 `HeaderSectionAdapter`，绑定与解绑时调用基类维护 `logicalIndex()`，排序按钮在点击时读取当前绑定的列号。

入口：[example.cpp](example.cpp)。主要接口：`VirtualHeaderView::setAdapter`、`setHeaderCornerWidget`、`setBackgroundVisible`。关键调用旁有英文注释。

在已有示例构建配置中构建 `treetable_capabilities`，运行 `treetable_capabilities --example 24`。

本目录包含自己的模型、adapter 和控件实现；复制本章时无需其他能力目录。同时复制 [styled_headers.cpp](styled_headers.cpp) 与 [styled_headers.h](styled_headers.h)。
