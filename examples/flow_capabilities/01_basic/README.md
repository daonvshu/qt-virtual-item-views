# 01 基础接入

一列模型生成卡片网格；拖动窗口宽度观察自动换列。

源码：`examples/flow_capabilities/01_basic/example.cpp`；布局：`basic.ui`。

主要接口：setAdapter()、setModel()、setCellWidthRange()、setFixedCellHeight()。

模型只有一列，窗口宽度决定显示列数。adapter 的 estimatedSize() 是必需接口，但 Flow 高度由专用配置决定。

运行 `flow_capabilities --example 1`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_basic::createExample(parent)` 返回的 QWidget 加入布局。
