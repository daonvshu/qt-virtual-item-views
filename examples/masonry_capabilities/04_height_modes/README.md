# 三种高度模式

Fixed 使用统一高度；AspectRatio 使用全局宽高比；AdapterEstimate 使用每项 QSize 比例，非法估算回退到固定高度。

使用 `Fixed` 或 `AspectRatio` 模式，尤其是项目数量较多且希望按行对齐时，优先考虑 `VirtualFlowTableView`。它通过算术计算规则网格的位置，无需保存每个项目的几何信息，窗口宽度变化时也无需重新计算全部项目的位置。参考 [FlowTableView 尺寸示例](../../flow_capabilities/02_cell_sizes/example.cpp)。需要每项独立高度、最短列排列或单项高度动画时，选择 `VirtualMasonryView`。

入口为 [example.cpp](example.cpp)，页面布局为 [height_modes.ui](height_modes.ui)。本目录独立定义 model、adapter 和控件，不依赖其他章节。复制时保留本目录的源码与 UI 文件，将 UI 加入启用 `AUTOUIC` 的目标。
