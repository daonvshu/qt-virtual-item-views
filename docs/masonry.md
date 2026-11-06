# VirtualMasonryView

`VirtualMasonryView` 将每个项目放入当前最短的列；项目高度独立，窗口变宽或变窄会重新计算列数和卡片位置。源模型行是项目身份，布局列不是模型列。

完整演示由 [masonry_capabilities](../examples/masonry_capabilities/README.md) 运行。以下源码路径均相对于仓库根目录；每章都有独立的 model、adapter、控件和 `.ui`，可以直接阅读与复制。

| 需求 | 源码位置 |
| --- | --- |
| 基础接入 | [examples/masonry_capabilities/01_basic/example.cpp](../examples/masonry_capabilities/01_basic/example.cpp) |
| 列数与列宽 | [examples/masonry_capabilities/02_columns/example.cpp](../examples/masonry_capabilities/02_columns/example.cpp) |
| 间距与分割线 | [examples/masonry_capabilities/03_spacing_grid/example.cpp](../examples/masonry_capabilities/03_spacing_grid/example.cpp) |
| 三种高度模式 | [examples/masonry_capabilities/04_height_modes/example.cpp](../examples/masonry_capabilities/04_height_modes/example.cpp) |
| 高度更新与动画 | [examples/masonry_capabilities/05_height_updates/example.cpp](../examples/masonry_capabilities/05_height_updates/example.cpp) |
| 状态背景与内容 | [examples/masonry_capabilities/06_visual_states/example.cpp](../examples/masonry_capabilities/06_visual_states/example.cpp) |
| 结构体、多类型与事件 | [examples/masonry_capabilities/07_binding_events/example.cpp](../examples/masonry_capabilities/07_binding_events/example.cpp) |
| 选择与几何查询 | [examples/masonry_capabilities/08_selection/example.cpp](../examples/masonry_capabilities/08_selection/example.cpp) |
| 模型、代理与显示根 | [examples/masonry_capabilities/09_model_root/example.cpp](../examples/masonry_capabilities/09_model_root/example.cpp) |
| 滚动、物化与诊断 | [examples/masonry_capabilities/10_scrolling/example.cpp](../examples/masonry_capabilities/10_scrolling/example.cpp) |
| 自动与手动拖放 | [examples/masonry_capabilities/11_drag_drop/example.cpp](../examples/masonry_capabilities/11_drag_drop/example.cpp) |
| 异步图片卡片 | [examples/masonry_capabilities/12_images/example.cpp](../examples/masonry_capabilities/12_images/example.cpp) |
| 圆角、边框与颜色 | [examples/masonry_capabilities/13_borders_colors/example.cpp](../examples/masonry_capabilities/13_borders_colors/example.cpp) |
| 自适应高度 | [examples/masonry_capabilities/14_auto_height/example.cpp](../examples/masonry_capabilities/14_auto_height/example.cpp) |

## 从最小接入开始

第 01 节演示 `QStandardItemModel`、`WidgetAdapter`、`setModel()` 和 `AdapterEstimate`。adapter 仅负责创建、绑定、解绑卡片和提供估算；背景、选择、命中、滚动和复用由视图处理。

第 02 节的 `setColumnCount(0)` 自动决定列数；正值请求固定列数，仍需满足最小列宽。`setCellWidthRange()` 控制列宽范围，不改变模型结构。

## 高度与更新

第 04、05 节分别演示高度模式与更新入口：

- `Fixed` 使用固定高度，`AspectRatio` 使用全局宽高比，`AdapterEstimate` 使用每项估算比例；缺失或无效估算回退到固定高度。
- `setItemEstimatedSize()` 提交单项估算，`setItemEstimatedSizes()` 一次提交清单。`invalidateItemSize()` 合并读取 adapter 的新估算。
- `setItemHeight()` 设置权威像素高度，优先于高度模式与估算；可使用 `setItemHeightAnimationEnabled()` 启用高度变化动画。
- 不使用继承自列表的统一/逐行高度设置，也不启用自动 sizeHint 测量。
- 自适应控件高度见第 14 节：`setAutoHeightEnabled(true)` 配合 `setMaximumHeight()`，以所有列的最大底部计算高度，超限后内部滚动。

## 复用与业务事件

第 07 节通过 `ItemBindingContext::rowData` 接收完整结构体，不在业务事件中捕获首次绑定的 index；事件发生时使用 `indexForWidget()` 获取当前身份。`QPersistentModelIndex` 能追踪模型变化，但保存在控件中的索引仍需每次绑定替换、解绑清空。

第 12 节包含独立的目录扫描、图片后端和缓存。先在后台取得原图尺寸，之后按需解码；解绑取消旧回调，缩略图完成不覆盖原图估算。可选 aster 未启用时使用 Qt 缩略图后端。

瀑布流不提供树展开、规则表头、冻结行列、合并单元格或按显示行选择；这些需求分别使用树或表格视图。
