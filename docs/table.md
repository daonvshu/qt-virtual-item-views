# VirtualTableView 功能示例

表中路径均相对于仓库根目录。每个编号目录独立包含 model、adapter、控件和页面配置，不引用其他示例实现；可直接复制当前目录。一个 `table_capabilities` 进程切换第 01–28、30 项，检测到与当前 Qt 主版本匹配的 SmoothScrollbar 时额外显示第 29 项。

| 编号 | 功能 | 源码相对路径 |
| --- | --- | --- |
| 01 | 行控件与 ColumnHost：固定八列，一行一个 QWidget，列几何由框架管理 | [examples/table_capabilities/01_row_widgets/example.cpp](../examples/table_capabilities/01_row_widgets/example.cpp) |
| 02 | 单元格物化：横向和纵向只创建可见格，适合多列表格 | [examples/table_capabilities/02_cell_widgets/example.cpp](../examples/table_capabilities/02_cell_widgets/example.cpp) |
| 03 | 多种控件类型：文本格与进度格按 WidgetType 分开复用，禁止不匹配的控件类型转换 | [examples/table_capabilities/03_widget_types/example.cpp](../examples/table_capabilities/03_widget_types/example.cpp) |
| 04 | 整行结构体绑定：每行第零列存一份结构体，单元格绑定直接使用 ItemBindingContext | [examples/table_capabilities/04_binding_context/example.cpp](../examples/table_capabilities/04_binding_context/example.cpp) |
| 05 | 模型更新：修改数据、插入删除行列和重置模型，依靠 Qt model 信号自动刷新 | [examples/table_capabilities/05_model_updates/example.cpp](../examples/table_capabilities/05_model_updates/example.cpp) |
| 06 | 固定与动态行高：内容换行后自动测量，显式行高可优先于测量，支持清除覆盖 | [examples/table_capabilities/06_row_heights/example.cpp](../examples/table_capabilities/06_row_heights/example.cpp) |
| 07 | 列宽、隐藏与换序：拖动列边界调整宽度，列换序保持模型逻辑列不变 | [examples/table_capabilities/07_columns/example.cpp](../examples/table_capabilities/07_columns/example.cpp) |
| 08 | 列宽模式：普通比例、末列拉伸、始终填满视图并由拖动侧按比例分摊 | [examples/table_capabilities/08_stretch_columns/example.cpp](../examples/table_capabilities/08_stretch_columns/example.cpp) |
| 09 | 间距与分割线：行列间距、状态背景贯通列间距开关、间距背景色、分割线与自定义间距控件 | [examples/table_capabilities/09_spacing/example.cpp](../examples/table_capabilities/09_spacing/example.cpp) |
| 10 | Hover 与选中背景：背景跟随按行或按格选择，可配置颜色、圆角、时长和曲线 | [examples/table_capabilities/10_visual_states/example.cpp](../examples/table_capabilities/10_visual_states/example.cpp) |
| 11 | 状态关联控件：鼠标悬停或选中行时显示第 3 列操作按钮，业务数据与视觉状态分开 | [examples/table_capabilities/11_state_content/example.cpp](../examples/table_capabilities/11_state_content/example.cpp) |
| 12 | 选择与当前项：行号选整行、可选列头选整列、Ctrl/Shift 及共享选择模型 | [examples/table_capabilities/12_selection/example.cpp](../examples/table_capabilities/12_selection/example.cpp) |
| 13 | 行内事件与几何查询：控件复用后在事件发生时取得正确索引，查询当前项、选中项与格子位置 | [examples/table_capabilities/13_item_events/example.cpp](../examples/table_capabilities/13_item_events/example.cpp) |
| 14 | 双向滚动与定位：一亿行按需生成数据，纵向 64 位像素偏移，横向像素偏移定位 | [examples/table_capabilities/14_scrolling/example.cpp](../examples/table_capabilities/14_scrolling/example.cpp) |
| 15 | 二维虚拟化与复用：同时限制行列物化范围，查看复用池和物化数量 | [examples/table_capabilities/15_virtualization/example.cpp](../examples/table_capabilities/15_virtualization/example.cpp) |
| 16 | 冻结行列与分隔线：左右冻结列与上下冻结行组合，滚动内容在各自区域裁剪 | [examples/table_capabilities/16_frozen_areas/example.cpp](../examples/table_capabilities/16_frozen_areas/example.cpp) |
| 17 | 多区域与独立滚动组：同一张表组合冻结区域和两个横向独立滚动区域 | [examples/table_capabilities/17_panes/example.cpp](../examples/table_capabilities/17_panes/example.cpp) |
| 18 | 表头与换序动画：显示隐藏表头、调整高度和行号宽度，列拖动时主体跟随动画 | [examples/table_capabilities/18_headers/example.cpp](../examples/table_capabilities/18_headers/example.cpp) |
| 19 | 自定义表头控件：底色与透明、圆角、选中渐变与图标、组合控件、冻结区与 corner | [examples/table_capabilities/19_custom_header/example.cpp](../examples/table_capabilities/19_custom_header/example.cpp) |
| 20 | 列布局保存与恢复：保存列宽、顺序、隐藏、冻结设置，恢复前检查结果 | [examples/table_capabilities/20_header_persistence/example.cpp](../examples/table_capabilities/20_header_persistence/example.cpp) |
| 21 | 排序与代理模型：通过代理模型排序过滤，视图只发起排序，数据顺序由 model 管理 | [examples/table_capabilities/21_sorting/example.cpp](../examples/table_capabilities/21_sorting/example.cpp) |
| 22 | 行号拖动排序：拖动左侧行号，模型通过 sourceRow 保存数据身份，主体跟随移动 | [examples/table_capabilities/22_row_reordering/example.cpp](../examples/table_capabilities/22_row_reordering/example.cpp) |
| 23 | 合并单元格：同列跨行或同行跨列合并，覆盖格命中折回锚点，合并不跨 pane | [examples/table_capabilities/23_spans/example.cpp](../examples/table_capabilities/23_spans/example.cpp) |
| 24 | 整行指示器拖动：整行 Move 与 Ctrl 复制，支持自动起拖或手柄手动起拖 | [examples/table_capabilities/24_drag_indicator/example.cpp](../examples/table_capabilities/24_drag_indicator/example.cpp) |
| 25 | 整行移动动画拖动：整行 Move 与 Ctrl 复制，支持自动起拖或手柄手动起拖 | [examples/table_capabilities/25_drag_animation/example.cpp](../examples/table_capabilities/25_drag_animation/example.cpp) |
| 26 | 单格插入与空白格投放：格子上下边缘插入新行，中间只接受空白格；Move 只清空源格 | [examples/table_capabilities/26_cell_drop/example.cpp](../examples/table_capabilities/26_cell_drop/example.cpp) |
| 27 | 屏外保留：编辑或异步活动需要控件时显式保留，用完释放，索引不随屏外回收失去身份 | [examples/table_capabilities/27_pinning/example.cpp](../examples/table_capabilities/27_pinning/example.cpp) |
| 28 | 运行统计与生命周期：读取统计快照和有界日志，区分累计创建次数与当前物化数量 | [examples/table_capabilities/28_diagnostics/example.cpp](../examples/table_capabilities/28_diagnostics/example.cpp) |
| 29 | 平滑滚动（可选）：检测到匹配的 SmoothScrollbar 后才编译和显示，开关对比原生双向滚动 | [examples/table_capabilities/29_smooth_scrolling/example.cpp](../examples/table_capabilities/29_smooth_scrolling/example.cpp) |
| 30 | 外边框与圆角：方案 2 四角覆盖与方案 3 整体 Alpha 合成对比，最终方案待确认 | [examples/table_capabilities/30_borders/README.md](../examples/table_capabilities/30_borders/README.md) |

先看 `Example::setupExample()` 中的视图配置与信号连接，再看本文件的 model、adapter 和控件。`setupUi()` 只处理页面布局。Hover 与 selected 背景由框架绘制，圆角、颜色、时长和曲线直接调用接口配置。

行控件模式适合整行一起管理状态与布局，使用 `ColumnHost` 自动跟随列几何；单元格模式会横向回收离屏格子，适合列数较多的表格。整行结构体绑定见第 04 项，事件处理中取得当前索引见第 13 项。

构建、启动和复制方式见 [examples/table_capabilities/README.md](../examples/table_capabilities/README.md)。
