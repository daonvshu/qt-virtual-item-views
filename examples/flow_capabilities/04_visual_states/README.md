# 04 状态背景与动画

框架绘制 hover / selected，业务 QLabel 保持透明。

源码：`examples/flow_capabilities/04_visual_states/example.cpp`；布局：`visual_states.ui`。

主要接口：setVisualStateBackgroundVisible()、setStateBackgroundCornerRadius()、setVisualStateAnimationDuration()。

默认 OutCubic、180ms。状态仅覆盖真实卡片，透明业务控件才能显示框架背景。间距和不存在的项目位置不着色。

运行 `flow_capabilities --example 4`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_visual_states::createExample(parent)` 返回的 QWidget 加入布局。
