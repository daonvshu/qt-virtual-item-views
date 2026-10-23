# 入门：模型、Adapter 与视图

VirtualItemViews 使用 Qt 的 `QAbstractItemModel` 提供数据，用 adapter 创建和复用真实的 `QWidget`。视图只物化可见区、预加载区和显式保留的项；模型的数据与结构变化通过 Qt model 信号通知视图。

## 基础列表源码

直接阅读 [examples/list_capabilities/01_basic/example.cpp](../examples/list_capabilities/01_basic/example.cpp)。路径相对于仓库根目录，model、adapter 和页面装配都在同一个文件中。

先看 `Example::setupExample()` 中的 `setAdapter()`、`setUniformItemHeight()` 和 `setModel()`，再看 adapter 的创建、绑定和清理逻辑。复制该目录独立运行时，按同目录 `README.md` 添加 `QApplication` 入口。

其他列表功能按 [VirtualListView 功能示例源码](list.md)中的相对路径查找。表格可从 [行控件](../examples/table_capabilities/01_row_widgets/example.cpp) 或 [单元格控件](../examples/table_capabilities/02_cell_widgets/example.cpp) 入门，各项功能见 [VirtualTableView 源码索引](table.md)。树从 [基础接入](../examples/tree_capabilities/01_basic/example.cpp) 开始，各项功能见 [VirtualTreeView 源码索引](tree.md)。

`createWidget()` 只负责创建，事件连接通常也在这里完成。`bindWidget()` 必须在每次复用时覆盖旧内容；若控件持有定时器、动画、异步请求或订阅，在 `unbindWidget()` 中解除与旧索引的关联。adapter 默认由调用方持有；显式传入 `takeOwnership = true` 时交给视图管理。外部持有的模型和 adapter 必须比视图活得更久。

## 构建与链接

需要 CMake 3.16+、Qt 6.2+ 或 Qt 5.12.3+（Core、Gui、Widgets）。Qt5 构建使用 C++14，Qt6 构建使用 C++17，链接导出的 CMake target 会自动传递相应标准要求。仓库默认只构建静态库。

以下命令从仓库根目录执行，`<QT_PREFIX>` 和 `<INSTALL_PREFIX>` 替换为 Qt 与安装目录：

```sh
cmake -S . -B build "-DCMAKE_PREFIX_PATH=<QT_PREFIX>"
cmake --build build
cmake --install build --prefix "<INSTALL_PREFIX>"
```

需要动态库时传 `-DVIRTUALITEMVIEWS_BUILD_SHARED=ON`。需要运行列表示例时，在配置时启用 `-DVIRTUALITEMVIEWS_BUILD_EXAMPLES=ON`，再构建 `list_capabilities` 目标；具体方式见 [examples/list_capabilities/README.md](../examples/list_capabilities/README.md)。表格对应目标为 `table_capabilities`，构建方式见 [examples/table_capabilities/README.md](../examples/table_capabilities/README.md)。树对应目标为 `tree_capabilities`，见 [examples/tree_capabilities/README.md](../examples/tree_capabilities/README.md)。

安装后，在消费端的 `CMAKE_PREFIX_PATH` 中提供安装前缀和 Qt 路径：

```cmake
find_package(VirtualItemViews REQUIRED)
target_link_libraries(app PRIVATE VirtualItemViews::VirtualItemViews)
```

下一步：[VirtualListView 功能示例源码](list.md)、[VirtualTableView 功能示例源码](table.md)、[VirtualTreeView 功能示例源码](tree.md)。
