# 07 滚动、定位与物化

10000 个项目仅物化可见区域及预加载区域；滚动后控件会复用。

源码：`examples/flow_capabilities/07_scrolling/example.cpp`；布局：`scrolling.ui`。

主要接口：scrollTo()、setOverscan()、stats()、visualRect()、indexAt()。

点击定位后滚出该项，再查询当前项：模型身份仍有效，屏外控件可能已回收。显示行列随窗口变化；几何查询不会强制创建控件。

运行 `flow_capabilities --example 7`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_scrolling::createExample(parent)` 返回的 QWidget 加入布局。
