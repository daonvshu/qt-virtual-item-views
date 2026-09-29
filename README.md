# VirtualItemViews

面向 Qt Widgets 的虚拟化列表、表格与树视图。数据使用 `QAbstractItemModel`，行或单元格由业务提供真正的 `QWidget`。视图只创建可见区、预加载区和显式保留的控件，滚动时复用。

## 功能与用法

| 功能 | 能做什么 | 文档 |
| --- | --- | --- |
| 入门与集成 | 模型、adapter、控件生命周期、构建和链接 | [快速开始](docs/getting-started.md) |
| 模型接入 | 数据变更通知、代理模型、排序、行移动 | [模型与更新](docs/models.md) |
| 列表 | 固定/动态行高、行间距、像素滚动、屏外 pin、超大逻辑行数 | [列表与行高](docs/list.md) |
| 表格 | 行控件与单元格控件两种模式、行列间距、行高与选择 | [表格与单元格](docs/table.md) |
| 表格布局 | 列宽比例分配、左右冻结列、上下冻结行、多滚动组、合并单元格 | [布局与合并](docs/table-layout-guide.md) |
| 表头 | 排序、自定义控件、行列拖动换序、动画与状态恢复 | [表头与排序](docs/headers.md)、[列布局持久化](docs/header-persistence.md) |
| 树 | 展开/折叠、按深度设置行间距、键盘导航、缩进和分支图标 | [树视图](docs/tree.md) |
| 通用交互 | 选择、拖放、编辑器焦点与弹窗 | [选择与拖放](docs/interaction.md) |
| 可访问性与诊断 | 可见项接口、物化统计和生命周期日志 | [可访问性与诊断](docs/accessibility-guide.md) |

每篇文档直接给出最小用法和适用边界；从[快速开始](docs/getting-started.md)进入即可，不需要阅读演示程序。

## 适用场景

适合在长列表、表格或树中使用按钮、开关、编辑器、异步图片等真实控件，尤其适合内容高度会变化的业务视图。控件数量主要随可见范围变化，而非随模型总行数变化。

如果只需要绘制文本和图形，Qt 原生 Item View 配合 `QStyledItemDelegate` 通常更轻量。本库使用 QWidget 与 QPainter，不提供 GPU 渲染；同时显示大量重型控件仍有相应成本。

需要 C++17、CMake 3.16+、Qt 6.2+ 或 Qt 5.15+。安装与消费端的 `find_package()` 示例见 [快速开始](docs/getting-started.md)。
