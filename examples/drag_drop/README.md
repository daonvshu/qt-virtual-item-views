# 多视图拖放综合演示

只想接入一个功能时，先读 [整行拖放](../reference/drag_drop/rows.cpp)、[单元格投放](../reference/drag_drop/cells.cpp) 或 [手柄起拖](../reference/drag_drop/manual.cpp)。本目录保留多视图组合、多个 cell、行高保留、工具栏和诊断参数，目标仍为 `drag_drop`。

## 文件职责

| 文件 | 内容 |
| --- | --- |
| [main.cpp](main.cpp) | 参数、窗口装配、视图配置、信号接线及截图 / hover 诊断 |
| [dragwidgets.h](dragwidgets.h) | 行控件、adapter、背景绘制，不操作模型中的拖放数据 |
| [dragmodels.h](dragmodels.h) | MIME、接收校验、插入 / 移动 / 清空、行高载荷与信号 |
| [sampledata.cpp](sampledata.cpp) | 创建表格和层级树的样例数据 |

表格使用 `DropTargetMode::BetweenRowsOrOnCell`：单元格上下边缘插入，中间投放到目标格。框架处理落点、指示器、pin、预览、hover 和自动滚动；模型决定能否填格，以及移动后怎样清理源数据。

勾选“整行拖动”切换为整行语义。模型与视图的清理设置同步，避免把单元格移动误处理为删除整行。Ctrl 复制保留源数据。
