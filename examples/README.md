# 示例导航

`VirtualListView` 按功能查找源码见 [docs/list.md](../docs/list.md)，路径均相对于仓库根目录。基础接入直接阅读 [examples/list_capabilities/01_basic/example.cpp](list_capabilities/01_basic/example.cpp)。一个 `list_capabilities` 进程演示各项功能，每个目录包含自己的 model、adapter 和行控件；构建、启动与复制方式见 [examples/list_capabilities/README.md](list_capabilities/README.md)。

`VirtualTableView` 使用同样的独立目录结构，按功能查找见 [docs/table.md](../docs/table.md)，构建与启动见 [table_capabilities/README.md](table_capabilities/README.md)。

树、树表、流式表格和瀑布流保留最小参考程序和综合演示。最小参考程序的配置直接写在各自的 `main()` 中；综合演示用于参数比较和功能组合。

## 按视图入门

| 视图 | 首先阅读 | Adapter / 控件 | 后续参考 |
| --- | --- | --- | --- |
| `VirtualListView` | [独立能力示例](list_capabilities/README.md)、[基础列表](list_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [行高](list_capabilities/06_item_heights/README.md)、[状态背景](list_capabilities/08_visual_states/README.md)、[行内事件](list_capabilities/11_item_events/README.md)、[可选平滑滚动](list_capabilities/19_smooth_scrolling/README.md) |
| `VirtualTableView` 行物化 | [行控件](table_capabilities/01_row_widgets/example.cpp) | 各能力目录自行定义 | [整行结构体](table_capabilities/04_binding_context/example.cpp)、[行表头换序](table_capabilities/22_row_reordering/example.cpp) |
| `VirtualTableView` cell 物化 | [单元格控件](table_capabilities/02_cell_widgets/example.cpp) | 各能力目录自行定义 | [圆角行 hover](table_capabilities/10_visual_states/example.cpp)、[冻结与间距](table_capabilities/16_frozen_areas/example.cpp) |
| `VirtualTreeView` | [展开与收起](reference/tree/basic.cpp) | [文本 adapter](reference/list/textadapter.h) | [branch 设置](reference/tree/branch.cpp)、[节点类型与结构体](reference/tree/binding_context.cpp) |
| `VirtualTreeTableView` | [行控件](reference/tree_table/row_widgets.cpp)、[cell 控件](reference/tree_table/cell_widgets.cpp) | [文本 adapter](reference/table/textadapters.h) | [节点结构体、深度与逻辑列](reference/tree_table/binding_context.cpp) |
| `VirtualFlowTableView` | [自适应列数](reference/flow/basic.cpp) | [文本 adapter](reference/list/textadapter.h) | [完整图片演示](flow_table_view/main.cpp) |
| `VirtualMasonryView` | [按数据估算卡片尺寸](reference/masonry/basic.cpp) | [CardAdapter](reference/masonry/basic.cpp) | [完整图片演示](masonry_view/main.cpp) |

## 按交互查找

| 需求 | 最小参考 |
| --- | --- |
| hover / selected 时显示操作控件；事件处理拿当前正确 index | [状态关联内容](list_capabilities/09_state_content/README.md)、[行内事件](list_capabilities/11_item_events/README.md) |
| 表格 cell 物化时显示整行 hover 和圆角 | [状态背景](table_capabilities/10_visual_states/example.cpp) |
| 每行只存一份结构体 | [整行结构体绑定](table_capabilities/04_binding_context/example.cpp) |
| 不同层级节点使用不同控件类型和数据类型 | [tree/binding_context.cpp](reference/tree/binding_context.cpp) |
| 树表每个 cell 直接拿整行结构体 | [tree_table/binding_context.cpp](reference/tree_table/binding_context.cpp) |
| 改变数据后更新行高 | [独立行高示例](list_capabilities/06_item_heights/README.md) |
| 拖行表头排序 | [行表头换序](table_capabilities/22_row_reordering/example.cpp) |
| 原生整行拖放、Ctrl 复制 | [整行拖放](table_capabilities/24_drag_indicator/example.cpp) |
| cell 行间插入和空白格放下 | [单格投放](table_capabilities/26_cell_drop/example.cpp) |
| 仅从手柄启动拖动、自定义预览 | [列表手柄起拖](list_capabilities/15_drag_drop/example.cpp)、[预览参数](../docs/interaction.md#手动触发与自定义预览) |

完整的目标名称、构建方式、文件依赖和模型责任见 [reference/README.md](reference/README.md)。

## 综合演示

以下程序保留可交互的工具栏和参数，用于比较配置、观察边界和性能。它们也是更完整的实现参考，但不作为第一次接入的入口。

| 目标 | 用途 / 源码 |
| --- | --- |
| `tree_view` | [展开动画、branch renderer、行间距与状态](tree_view/main.cpp) |
| `tree_table_view` | [树表两种物化模式和整行数据上下文](tree_table_view/main.cpp) |
| `flow_table_view` / `masonry_view` | [流式图片](flow_table_view/main.cpp) / [瀑布流图片](masonry_view/main.cpp) |
| `drag_drop` | [多视图拖放、多个 cell、行高保留及诊断](drag_drop/README.md) |

`list_capabilities` 构建列表功能示例，`table_capabilities` 构建表格功能示例，`viv_reference_examples` 构建其他视图的最小参考程序，`viv_examples` 构建全部示例。
