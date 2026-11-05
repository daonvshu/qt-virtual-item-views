# 10 颜色与样式表

分别设置主体、hover / selected 与 viewport 颜色；业务控件底色会覆盖状态背景。

源码：`examples/flow_capabilities/10_colors/example.cpp`；布局：`colors.ui`。

主要接口：setBackgroundColor()、setBackgroundVisible()、状态颜色与 setStyleSheet()。

主体默认 Base，可设透明；viewport 背景叠加在主体之上。给业务 QLabel 设置不透明底色会遮挡 hover / selected。使用对象名限定样式表作用范围。

运行 `flow_capabilities --example 10`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_colors::createExample(parent)` 返回的 QWidget 加入布局。
