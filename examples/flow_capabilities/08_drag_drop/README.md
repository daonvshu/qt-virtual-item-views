# 08 项目拖放

拖到卡片左 / 右半插入，拖到末行空缺追加；Ctrl 复制，默认移动。

源码：`examples/flow_capabilities/08_drag_drop/example.cpp`；布局：`drag_drop.ui`。

主要接口：setDragEnabled()、setDragStartMode()、setMoveRemovesSourceRows()、setDropIndicatorPenStyle()。

使用 QStandardItemModel 的 MIME 与插入语义，由视图在 Move 成功后删除源行。若业务模型自身实现真正移动，关闭 setMoveRemovesSourceRows()。Flow 使用插入指示器，不提供树和普通表格的行让位动画。

运行 `flow_capabilities --example 8`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_drag_drop::createExample(parent)` 返回的 QWidget 加入布局。
