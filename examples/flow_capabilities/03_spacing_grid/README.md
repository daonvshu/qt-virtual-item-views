# 03 间距、边距与分割线

比较间距中心和两侧模式；减少项目数量观察末行边界。

源码：`examples/flow_capabilities/03_spacing_grid/example.cpp`；布局：`spacing_grid.ui`。

主要接口：setRowSpacing()、setColumnSpacing()、setContentMargins()、setGridLinePlacement()、setSpacingBackgroundColor()、setTrailingRowGridLineAutoHideEnabled()。

中心模式始终穿过交叉间距；两侧模式才允许控制穿隙。间距是逻辑像素，分割线宽度是物理像素。零间距只有一条共享边界，末行最后一个真实项目补右侧内部边界。

中心模式在内容四周补半间距：左右使用列间距，上下使用行间距。奇数间距的左／上侧向下取整，右／下侧保留余数，例如 19 像素分为 9 和 10 像素。半间距位于 `setContentMargins()` 设置的边距内侧，参与自动列宽、换行、滚动范围和整体自适应高度计算。两侧模式不增加这些半间距，零间距也不增加。

勾选“自定义间距背景”并选择颜色，可观察行、列间距、交叉处及四周半间距的范围。设置的内容边距和末行空缺仍使用视口背景；传入无效 `QColor()` 恢复默认背景。两侧模式中，真实项目旁的间距会绘制两侧边界，即使相邻显示格没有项目。

底部内容边距大于零时，末行下边线始终保留（仍受“横线”总开关控制）。底部边距为零时，内容可滚动或启用整体自适应高度会默认隐藏末行下边线；勾选“始终绘制末行下边线”可保留。视图位于顶部时调整间距、边距或窗口尺寸，会保持滚动位置为零，从而完整显示配置的上边距。

运行 `flow_capabilities --example 3`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_spacing_grid::createExample(parent)` 返回的 QWidget 加入布局。
