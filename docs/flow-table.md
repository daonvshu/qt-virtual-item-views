# VirtualFlowTableView 功能示例

`VirtualFlowTableView` 将一列模型的项目按从左到右、从上到下排列。显示列数由视口宽度决定，每个真实项目对应一个可复用 QWidget。模型索引是项目身份，显示行列仅是当前布局坐标。

各功能在同一个 `flow_capabilities` 程序中演示，每章独立可复制。构建、启动与接入见 [示例说明](../examples/flow_capabilities/README.md)。以下路径均相对于仓库根目录。

| 章节 | 功能 | 源码相对路径 |
| --- | --- | --- |
| 01 | 基础接入 | [examples/flow_capabilities/01_basic/example.cpp](../examples/flow_capabilities/01_basic/example.cpp) |
| 02 | 尺寸与自动换列 | [examples/flow_capabilities/02_cell_sizes/example.cpp](../examples/flow_capabilities/02_cell_sizes/example.cpp) |
| 03 | 间距、边距与分割线 | [examples/flow_capabilities/03_spacing_grid/example.cpp](../examples/flow_capabilities/03_spacing_grid/example.cpp) |
| 04 | 状态背景与动画 | [examples/flow_capabilities/04_visual_states/example.cpp](../examples/flow_capabilities/04_visual_states/example.cpp) |
| 05 | 选择与项目事件 | [examples/flow_capabilities/05_selection_events/example.cpp](../examples/flow_capabilities/05_selection_events/example.cpp) |
| 06 | 模型更新与显示根 | [examples/flow_capabilities/06_model_root/example.cpp](../examples/flow_capabilities/06_model_root/example.cpp) |
| 07 | 滚动、定位与物化 | [examples/flow_capabilities/07_scrolling/example.cpp](../examples/flow_capabilities/07_scrolling/example.cpp) |
| 08 | 项目拖放 | [examples/flow_capabilities/08_drag_drop/example.cpp](../examples/flow_capabilities/08_drag_drop/example.cpp) |
| 09 | 异步图片卡片 | [examples/flow_capabilities/09_images/example.cpp](../examples/flow_capabilities/09_images/example.cpp) |
| 10 | 颜色与样式表 | [examples/flow_capabilities/10_colors/example.cpp](../examples/flow_capabilities/10_colors/example.cpp) |
| 11 | 整体自适应高度 | [examples/flow_capabilities/11_auto_height/example.cpp](../examples/flow_capabilities/11_auto_height/example.cpp) |

布局使用各章节自己的 Qt Designer `.ui`。函数调用旁的英文注释说明配置目的、索引含义和生命周期；图片章节额外包含该目录内的异步加载与缓存代码。

Flow 的列宽范围为目标值，优先铺满可用宽度；末行已有项目保持正常列宽，空缺位置不物化、不命中。高度使用 `setFixedCellHeight()` 或 `setCellAspectRatio()`，不通过业务控件 sizeHint 动态测量。冻结行、固定业务列表头、冻结列、合并格和树展开不属于 Flow API。

调用 `setAutoHeightEnabled(true)` 可让整个视图随网格内容自适应高度，`setAutoHeightMaximumHeight()` 设置可选上限（0 表示不限）；超出指定上限后内部滚动，父布局空间不足由业务处理。窗口换列和宽高比变化也会更新建议高度，具体见第 11 章。

选择、滚动与事件使用真实模型索引，不能把显示行列当作持久 ID。拖放权限和数据变更由 model 决定，详见 [交互与拖放](interaction.md)。
