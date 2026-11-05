# 02 尺寸与自动换列

配置目标列宽、固定高度或宽高比；最小列宽是期望值，窄窗口优先铺满。

源码：`examples/flow_capabilities/02_cell_sizes/example.cpp`；布局：`cell_sizes.ui`。

主要接口：setCellHeightMode()、setCellAspectRatio()、flowLayoutChanged()。

固定高度和宽高比为两种模式；拖动窗口观察列数、列宽和项目高度一起变化。最小宽度是目标值，不能据此假定一定出现水平滚动条。

运行 `flow_capabilities --example 2`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_cell_sizes::createExample(parent)` 返回的 QWidget 加入布局。
