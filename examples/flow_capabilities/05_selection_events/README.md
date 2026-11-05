# 05 选择与项目事件

方向键导航，Ctrl / Shift 扩展选择；项目身份使用模型索引。

源码：`examples/flow_capabilities/05_selection_events/example.cpp`；布局：`selection_events.ui`。

主要接口：setSelectionMode()、selectionModel()、currentIndex()、clicked()。

选择身份是模型索引；布局换列不改变身份。程序选择通过 QItemSelection 一次提交，SelectionMode 控制鼠标和键盘手势。显示坐标不能作为持久身份。

运行 `flow_capabilities --example 5`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_selection_events::createExample(parent)` 返回的 QWidget 加入布局。
