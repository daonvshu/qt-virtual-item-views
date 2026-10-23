# VirtualItemViews

面向 Qt Widgets 的虚拟化列表、表格与树视图。数据使用 `QAbstractItemModel`，行或单元格由业务提供真正的 `QWidget`。视图只创建可见区、预加载区和显式保留的控件，滚动时复用。

## 功能与用法

| 功能 | 能做什么 | 文档 |
| --- | --- | --- |
| 入门与集成 | 模型、adapter、控件生命周期、构建和链接 | [快速开始](docs/getting-started.md) |
| 模型接入 | 数据变更通知、代理模型、排序、行移动 | [模型与更新](docs/models.md) |
| 列表 | 行控件、数据绑定、行高、状态、选择、拖放、滚动与复用 | [VirtualListView 功能示例源码](docs/list.md) |
| 表格 | 行控件与单元格控件两种模式、行列间距、行高与选择 | [VirtualTableView 功能示例源码](docs/table.md) |
| 流式表格 | 随窗口宽度动态换列、均分铺满列宽、固定高度或宽高比 | [流式表格](docs/flow-table.md) |
| 瀑布流 | 最短列布局、独立项目高度、图片估算和动态列数 | [瀑布流](docs/masonry.md) |
| 表格布局 | 列宽比例分配、左右冻结列、上下冻结行、多滚动组、合并单元格 | [布局与合并](docs/table-layout-guide.md) |
| 表头 | 排序、自定义控件、行列拖动换序、动画与状态恢复 | [表头与排序](docs/headers.md)、[列布局持久化](docs/header-persistence.md) |
| 树 | 展开/折叠、节点上下间距、键盘导航、缩进和分支图标 | [VirtualTreeView 功能示例源码](docs/tree.md) |
| 多列表格树 | 树节点展开、表格列布局、表头、冻结 pane 与单元格控件 | [多列表格树](docs/tree-table.md) |
| 通用交互 | 选择、拖放、编辑器焦点与弹窗 | [选择与拖放](docs/interaction.md) |
| 可访问性与诊断 | 可见项接口、物化统计和生命周期日志 | [可访问性与诊断](docs/accessibility-guide.md) |

从[快速开始](docs/getting-started.md)完成构建与接入。列表、表格与树的用法按功能列在 [VirtualListView](docs/list.md)、[VirtualTableView](docs/table.md) 和 [VirtualTreeView](docs/tree.md) 源码路径索引中，所有路径均相对于仓库根目录。

列表从 [examples/list_capabilities/01_basic/example.cpp](examples/list_capabilities/01_basic/example.cpp) 开始阅读；每个功能单独一个目录，包含完整实现，由同一进程演示。构建和运行方式见 [examples/list_capabilities/README.md](examples/list_capabilities/README.md)。表格从 [行控件](examples/table_capabilities/01_row_widgets/example.cpp) 或 [单元格控件](examples/table_capabilities/02_cell_widgets/example.cpp) 开始阅读，统一程序为 [table_capabilities](examples/table_capabilities/README.md)。树从 [基础接入](examples/tree_capabilities/01_basic/example.cpp) 开始阅读，统一程序为 [tree_capabilities](examples/tree_capabilities/README.md)。其他视图见 [examples 导航](examples/README.md)。

## 适用场景

适合在长列表、表格或树中使用按钮、开关、编辑器、异步图片等真实控件，尤其适合内容高度会变化的业务视图。控件数量主要随可见范围变化，而非随模型总行数变化。

如果只需要绘制文本和图形，Qt 原生 Item View 配合 `QStyledItemDelegate` 通常更轻量。本库使用 QWidget 与 QPainter，不提供 GPU 渲染；同时显示大量重型控件仍有相应成本。

需要 CMake 3.16+、Qt 6.2+ 或 Qt 5.12.3+。Qt5 构建使用 C++14，Qt6 构建使用 C++17。安装与消费端的 `find_package()` 示例见 [快速开始](docs/getting-started.md)。
