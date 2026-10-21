# VirtualItemViews

面向 Qt Widgets 的虚拟化列表、表格与树视图。数据使用 `QAbstractItemModel`，行或单元格由业务提供真正的 `QWidget`。视图只创建可见区、预加载区和显式保留的控件，滚动时复用。

## 功能与用法

| 功能 | 能做什么 | 文档 |
| --- | --- | --- |
| 入门与集成 | 模型、adapter、控件生命周期、构建和链接 | [快速开始](docs/getting-started.md) |
| 模型接入 | 数据变更通知、代理模型、排序、行移动 | [模型与更新](docs/models.md) |
| 列表 | 固定/动态行高、行间距、像素滚动、屏外 pin、超大逻辑行数 | [列表与行高](docs/list.md) |
| 表格 | 行控件与单元格控件两种模式、行列间距、行高与选择 | [表格与单元格](docs/table.md) |
| 流式表格 | 随窗口宽度动态换列、均分铺满列宽、固定高度或宽高比 | [流式表格](docs/flow-table.md) |
| 瀑布流 | 最短列布局、独立项目高度、图片估算和动态列数 | [瀑布流](docs/masonry.md) |
| 表格布局 | 列宽比例分配、左右冻结列、上下冻结行、多滚动组、合并单元格 | [布局与合并](docs/table-layout-guide.md) |
| 表头 | 排序、自定义控件、行列拖动换序、动画与状态恢复 | [表头与排序](docs/headers.md)、[列布局持久化](docs/header-persistence.md) |
| 树 | 展开/折叠、按深度设置行间距、键盘导航、缩进和分支图标 | [树视图](docs/tree.md) |
| 多列表格树 | 树节点展开、表格列布局、表头、冻结 pane 与单元格控件 | [多列表格树](docs/tree-table.md) |
| 通用交互 | 选择、拖放、编辑器焦点与弹窗 | [选择与拖放](docs/interaction.md) |
| 可访问性与诊断 | 可见项接口、物化统计和生命周期日志 | [可访问性与诊断](docs/accessibility-guide.md) |

每篇文档直接给出最小用法和适用边界；从[快速开始](docs/getting-started.md)进入即可，不需要阅读演示程序。

需要可编译、可直接参考的代码时，从 [examples 导航](examples/README.md)进入。列表提供 [17 项独立能力示例](examples/list_capabilities/README.md)，在同一进程切换演示，每个能力目录自带全部实现；其他视图的[最小参考示例](examples/reference/README.md)按视图和功能组织。原有综合演示用于调整参数和观察多种功能组合。

## 适用场景

适合在长列表、表格或树中使用按钮、开关、编辑器、异步图片等真实控件，尤其适合内容高度会变化的业务视图。控件数量主要随可见范围变化，而非随模型总行数变化。

如果只需要绘制文本和图形，Qt 原生 Item View 配合 `QStyledItemDelegate` 通常更轻量。本库使用 QWidget 与 QPainter，不提供 GPU 渲染；同时显示大量重型控件仍有相应成本。

需要 CMake 3.16+、Qt 6.2+ 或 Qt 5.12.3+。Qt5 构建使用 C++14，Qt6 构建使用 C++17。安装与消费端的 `find_package()` 示例见 [快速开始](docs/getting-started.md)。
