# 11 自适应高度

源码：`examples/flow_capabilities/11_auto_height/example.cpp`；布局：`auto_height.ui`。

调用 `setAutoHeightEnabled(true)`，将视图放入垂直布局；可选 `setMaximumHeight()` 限制整个控件高度。不要再设置固定高度。少量项目使用内容高度，多余空间留给下方控件；内容超出上限或父布局可用高度时滚动。

Flow 的高度取决于显示行数、项目高度、行间距、内容上下边距及边框。窗口换列、增删项目和切换宽高比会重新计算。整体自适应高度与项目控件的自动测高不同，Flow 项目仍使用 `setFixedCellHeight()` 或 `setCellAspectRatio()`。

框架按不显示竖向滚动条时的列宽计算建议高度，避免已有滚动条限制视图继续增高；待父布局分配空间后再稳定滚动条。比例布局在临界宽度可能出现“无滚动条时超高，有滚动条时内容反而变矮”的情况，此时保留滚动条以避免两种布局反复切换，滚动范围按实际内容计算。

运行 `flow_capabilities --example 11`。可修改项目数、视图上限、父区域上限、列宽、间距与高度模式；0 项、刚好容纳和超出上限均可观察。复制本目录并启用 CMake `AUTOUIC`，链接 VirtualItemViews 和 Qt Widgets，调用 `flow_auto_height::createExample(parent)` 加入布局。
