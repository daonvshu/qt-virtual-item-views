# 06 模型更新与显示根

Flow 只排列显示根的直接子项；插删数据通过模型信号更新。

源码：`examples/flow_capabilities/06_model_root/example.cpp`；布局：`model_root.ui`。

主要接口：setRootIndex()、QStandardItemModel 的插入、删除和 dataChanged 通知。

Flow 仅显示根的直接子项，不展开整棵树。示例保留两个分组根供导航；子项可修改、删除，也可追加新项。

运行 `flow_capabilities --example 6`。复制本目录中的源码、头文件和布局文件即可；在自己的 CMake 目标启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和对应版本的 Qt Widgets，然后将 `flow_model_root::createExample(parent)` 返回的 QWidget 加入布局。
