# 示例导航

列表能力从 [VirtualListView 独立能力示例](list_capabilities/README.md)进入：一个 `list_capabilities` 进程切换 17 项基础能力，检测到 SmoothScrollbar 时额外加入第 18 项平滑滚动。每项单独一个目录，完整包含自己的 model、adapter、行控件和说明，各目录不共用实现代码。

其他视图保留最小参考程序和综合演示。最小参考程序的配置直接写在各自的 `main()` 中；综合演示用于参数比较和功能组合。

## 按视图入门

| 视图 | 首先阅读 | Adapter / 控件 | 后续参考 |
| --- | --- | --- | --- |
| `VirtualListView` | [独立能力示例](list_capabilities/README.md)、[基础列表](list_capabilities/01_basic/example.cpp) | 各能力目录自行定义 | [行高](list_capabilities/06_item_heights/README.md)、[状态背景](list_capabilities/08_visual_states/README.md)、[行内事件](list_capabilities/11_item_events/README.md)、[可选平滑滚动](list_capabilities/18_smooth_scrolling/README.md) |
| `VirtualTableView` 行物化 | [行控件](reference/table/row_widgets.cpp) | [TextRow / TextRowAdapter](reference/table/textadapters.h) | [整行结构体](reference/table/binding_context.cpp)、[行表头换序](reference/table/reorder_rows.cpp) |
| `VirtualTableView` cell 物化 | [单元格控件](reference/table/cell_widgets.cpp) | [TextCellAdapter](reference/table/textadapters.h) | [圆角行 hover](reference/table/state.cpp)、[冻结与间距](reference/table/layout.cpp) |
| `VirtualTreeView` | [展开与收起](reference/tree/basic.cpp) | [文本 adapter](reference/list/textadapter.h) | [branch 设置](reference/tree/branch.cpp)、[节点类型与结构体](reference/tree/binding_context.cpp) |
| `VirtualTreeTableView` | [行控件](reference/tree_table/row_widgets.cpp)、[cell 控件](reference/tree_table/cell_widgets.cpp) | [文本 adapter](reference/table/textadapters.h) | [节点结构体、深度与逻辑列](reference/tree_table/binding_context.cpp) |
| `VirtualFlowTableView` | [自适应列数](reference/flow/basic.cpp) | [文本 adapter](reference/list/textadapter.h) | [完整图片演示](flow_table_view/main.cpp) |
| `VirtualMasonryView` | [按数据估算卡片尺寸](reference/masonry/basic.cpp) | [CardAdapter](reference/masonry/basic.cpp) | [完整图片演示](masonry_view/main.cpp) |

## 按交互查找

| 需求 | 最小参考 |
| --- | --- |
| hover / selected 时显示操作控件；事件处理拿当前正确 index | [状态关联内容](list_capabilities/09_state_content/README.md)、[行内事件](list_capabilities/11_item_events/README.md) |
| 表格 cell 物化时显示整行 hover 和圆角 | [table/state.cpp](reference/table/state.cpp) |
| 每行只存一份结构体 | [table/binding_context.cpp](reference/table/binding_context.cpp) |
| 不同层级节点使用不同控件类型和数据类型 | [tree/binding_context.cpp](reference/tree/binding_context.cpp) |
| 树表每个 cell 直接拿整行结构体 | [tree_table/binding_context.cpp](reference/tree_table/binding_context.cpp) |
| 改变数据后更新行高 | [独立行高示例](list_capabilities/06_item_heights/README.md) |
| 拖行表头排序 | [table/reorder_rows.cpp](reference/table/reorder_rows.cpp) |
| 原生整行拖放、Ctrl 复制 | [drag_drop/rows.cpp](reference/drag_drop/rows.cpp) |
| cell 行间插入和空白格放下 | [drag_drop/cells.cpp](reference/drag_drop/cells.cpp)、[数据模型](reference/drag_drop/cellmodel.h) |
| 仅从手柄启动拖动、自定义预览 | [drag_drop/manual.cpp](reference/drag_drop/manual.cpp)、[手柄事件](reference/drag_drop/draghandle.h) |

完整的目标名称、构建方式、文件依赖和模型责任见 [reference/README.md](reference/README.md)。

## 综合演示

以下程序保留可交互的工具栏和参数，用于比较配置、观察边界和性能。它们也是更完整的实现参考，但不作为第一次接入的入口。

| 目标 | 用途 / 源码 |
| --- | --- |
| `simple_list` | [列表状态、颜色、圆角及统计](simple_list/main.cpp) |
| `dynamic_height` | [定时数据变化、自动测高和滚动锚定](dynamic_height/main.cpp) |
| `million_rows` | [超大模型与物化统计](million_rows/main.cpp) |
| `order_cards` | [包含多种业务控件的卡片](order_cards/main.cpp) |
| `table_row_widgets` | [行物化、ColumnHost 和参数控制](table_row_widgets/main.cpp) |
| `table_many_columns` | [多列 cell 物化和复用](table_many_columns/main.cpp) |
| `tree_view` | [展开动画、branch renderer、行间距与状态](tree_view/main.cpp) |
| `tree_table_view` | [树表两种物化模式和整行数据上下文](tree_table_view/main.cpp) |
| `flow_table_view` / `masonry_view` | [流式图片](flow_table_view/main.cpp) / [瀑布流图片](masonry_view/main.cpp) |
| `drag_drop` | [多视图拖放、多个 cell、行高保留及诊断](drag_drop/README.md) |
| `table_custom_header` | [自定义表头控件及排序](table_custom_header/main.cpp) |
| `table_spans` | [合并单元格](table_spans/main.cpp) |
| `table_panes` | [多个 pane 和独立滚动组](table_panes/main.cpp) |
| `table_frozen_rows` | [冻结行及表头联动](table_frozen_rows/main.cpp) |
| `table_stretch_columns` | [比例列宽、拖动换序和布局](table_stretch_columns/main.cpp) |
| `table_header_persistence` | [保存 / 恢复列布局](table_header_persistence/main.cpp) |
| `smooth_scroll_table` | [可选平滑滚动集成](smooth_scroll_table/README.md)，仅依赖可用时构建 |

现有目标名称和启动参数保留。`viv_examples` 构建所有示例，`viv_reference_examples` 只构建原有最小参考程序，`list_capabilities` 单独构建列表能力演示。
