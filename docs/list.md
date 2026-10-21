# VirtualListView 功能示例

按功能直接阅读下面的示例源码。表中的路径均相对于仓库根目录，每个 `example.cpp` 包含该功能使用的 model、adapter、行控件和视图配置，可单独参考或复制所在目录。

| 编号 | 功能 | 源码相对路径 |
| --- | --- | --- |
| 01 | 基础列表：model、adapter 接入和 rootIndex 切换 | [examples/list_capabilities/01_basic/example.cpp](../examples/list_capabilities/01_basic/example.cpp) |
| 02 | 自定义行控件：组合控件、数据回写和信号阻断 | [examples/list_capabilities/02_row_widgets/example.cpp](../examples/list_capabilities/02_row_widgets/example.cpp) |
| 03 | 多种行类型：WidgetType 分类和独立复用池 | [examples/list_capabilities/03_widget_types/example.cpp](../examples/list_capabilities/03_widget_types/example.cpp) |
| 04 | 整行数据绑定：结构体 role 和 ItemBindingContext | [examples/list_capabilities/04_binding_context/example.cpp](../examples/list_capabilities/04_binding_context/example.cpp) |
| 05 | 数据更新：修改、增删、移动、重置、排序和过滤 | [examples/list_capabilities/05_model_updates/example.cpp](../examples/list_capabilities/05_model_updates/example.cpp) |
| 06 | 行高：固定、首项测量、动态更新和锚点补偿 | [examples/list_capabilities/06_item_heights/example.cpp](../examples/list_capabilities/06_item_heights/example.cpp) |
| 07 | 行间距和分割线：间距、线宽、颜色和自定义间距控件 | [examples/list_capabilities/07_row_spacing/example.cpp](../examples/list_capabilities/07_row_spacing/example.cpp) |
| 08 | Hover 和选中背景：圆角、颜色、动画时长和曲线 | [examples/list_capabilities/08_visual_states/example.cpp](../examples/list_capabilities/08_visual_states/example.cpp) |
| 09 | 状态关联内容：根据 hover / selected 显示操作区 | [examples/list_capabilities/09_state_content/example.cpp](../examples/list_capabilities/09_state_content/example.cpp) |
| 10 | 选择与当前项：选择模式、程序选择和共享选择模型 | [examples/list_capabilities/10_selection/example.cpp](../examples/list_capabilities/10_selection/example.cpp) |
| 11 | 行内事件处理：行信号、键盘、当前绑定索引和选中项查询 | [examples/list_capabilities/11_item_events/example.cpp](../examples/list_capabilities/11_item_events/example.cpp) |
| 12 | 滚动与定位：百万行、自动滚动、定位和 64 位偏移 | [examples/list_capabilities/12_scrolling/example.cpp](../examples/list_capabilities/12_scrolling/example.cpp) |
| 13 | 虚拟化与复用：overscan、可见范围、复用池限制与清理 | [examples/list_capabilities/13_virtualization/example.cpp](../examples/list_capabilities/13_virtualization/example.cpp) |
| 14 | 冻结行：顶部、底部冻结和区域分隔线 | [examples/list_capabilities/14_frozen_rows/example.cpp](../examples/list_capabilities/14_frozen_rows/example.cpp) |
| 15 | 指示器拖动：自动 / 手动起拖、Move / Copy 和指示器样式 | [examples/list_capabilities/15_drag_drop/example.cpp](../examples/list_capabilities/15_drag_drop/example.cpp) |
| 16 | 行移动动画拖动：自动 / 手动起拖、让位时长和曲线 | [examples/list_capabilities/16_drag_animation/example.cpp](../examples/list_capabilities/16_drag_animation/example.cpp) |
| 17 | 屏外保留：按 index / widget 保留和释放控件 | [examples/list_capabilities/17_pinning/example.cpp](../examples/list_capabilities/17_pinning/example.cpp) |
| 18 | 运行诊断：统计快照和生命周期日志 | [examples/list_capabilities/18_diagnostics/example.cpp](../examples/list_capabilities/18_diagnostics/example.cpp) |
| 19（可选） | 平滑滚动：SmoothScrollbar 接入、启停和滚轮步长 | [examples/list_capabilities/19_smooth_scrolling/example.cpp](../examples/list_capabilities/19_smooth_scrolling/example.cpp) |

阅读时先看 `Example::setupExample()` 中的 `VirtualListView` 配置和信号连接，再看本文件的 adapter 与行控件。`setupUi()` 负责演示页面布局，各目录的 `README.md` 提供该功能的说明和独立运行入口。

Hover 和选中背景默认由框架绘制，普通圆角、颜色和动画直接配置视图接口，行控件保持透明。状态关联控件的显隐与行内事件处理分别参考第 09、11 项。

所有示例由一个 `list_capabilities` 程序演示，启动器位于 [examples/list_capabilities/main.cpp](../examples/list_capabilities/main.cpp)。构建、启动和复制方式见 [examples/list_capabilities/README.md](../examples/list_capabilities/README.md)。第 19 项仅在检测到与当前 Qt 主版本匹配的 SmoothScrollbar 时编译和显示。
