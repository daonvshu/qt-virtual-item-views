# 多视图拖放综合演示

只想接入一个功能时，先读 [表格整行拖放](../reference/drag_drop/rows.cpp)、[单元格投放](../reference/drag_drop/cells.cpp) 或 [列表手柄起拖](../list_capabilities/15_drag_drop/example.cpp)。本目录保留多视图组合、多个 cell、行高保留、工具栏和诊断参数，目标仍为 `drag_drop`。

## 文件职责

| 文件 | 内容 |
| --- | --- |
| [main.cpp](main.cpp) | 参数、窗口装配、视图配置、信号接线及截图 / hover 诊断 |
| [dragwidgets.h](dragwidgets.h) | 行控件、adapter、表格拖动预览区域，不操作模型中的拖放数据 |
| [dragmodels.h](dragmodels.h) | MIME、接收校验、插入 / 移动 / 清空、行高载荷与信号 |
| [sampledata.cpp](sampledata.cpp) | 创建表格和层级树的样例数据 |

表格使用 `DropTargetMode::BetweenRowsOrOnCell`：单元格上下边缘插入，中间投放到目标格。框架处理落点、指示器、pin、预览、hover 和自动滚动；模型决定能否填格，以及移动后怎样清理源数据。

三个视图的 hover / selected 背景和过渡动画均由框架绘制，行控件只展示内容。背景开关及圆角直接通过 `setVisualStateBackgroundVisible()`、`setVisualStateCornerRadius()` 配置；表格的派生类只负责选择整行拖动预览区域。

勾选“整行拖动”切换为整行语义。模型与视图的清理设置同步，避免把单元格移动误处理为删除整行。Ctrl 复制保留源数据。

工具栏的“表格：指示器 / 行移动动画”可切换拖动反馈，默认选择行移动动画。勾选“整行拖动”，拖动本表的一行时，其他行滑动让位，放下才更新模型。也可直接用 `drag_drop --row-animation` 启动。默认时长为 180ms、曲线 OutCubic，右侧控件可调整；0ms 表示立即让位。

“单元格物化”切换 `RowWidgets` / `CellWidgets`，两者使用相同的整行排序动画，冻结列随整行移动。单元格物化时，示例通过 `dragSourceWidget()` / `dragPixmapRect()` 选择 viewport 的整行区域作为预览，框架负责动画。复制、多行、跨视图、单元格投放、跨冻结行及合并格等不符合动画条件的场景沿用指示器。“显示插入指示器”只控制指示器，不关闭行移动动画。

模型的整行 MIME 同时保存源模型与持续索引。内部移动在插入后按持续索引删除源行，向上和向下移动都不会误删；跨模型移动由源视图在拖动结束后清理。
