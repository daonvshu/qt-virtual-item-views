# VirtualMasonryView 能力示例

一个 `masonry_capabilities` 进程按需打开各章。每个目录包含自己的 model、adapter、卡片和 Qt Designer 页面；不与其他章节共用业务代码。代码注释使用英文。

| 章节 | 能力 | 入口 |
| --- | --- | --- |
| 01 | 基础接入 | [01_basic/example.cpp](01_basic/example.cpp) |
| 02 | 列数与列宽 | [02_columns/example.cpp](02_columns/example.cpp) |
| 03 | 间距与分割线 | [03_spacing_grid/example.cpp](03_spacing_grid/example.cpp) |
| 04 | 三种高度模式 | [04_height_modes/example.cpp](04_height_modes/example.cpp) |
| 05 | 高度更新与动画 | [05_height_updates/example.cpp](05_height_updates/example.cpp) |
| 06 | 状态背景与内容 | [06_visual_states/example.cpp](06_visual_states/example.cpp) |
| 07 | 结构体、多类型与事件 | [07_binding_events/example.cpp](07_binding_events/example.cpp) |
| 08 | 选择与几何查询 | [08_selection/example.cpp](08_selection/example.cpp) |
| 09 | 模型、代理与显示根 | [09_model_root/example.cpp](09_model_root/example.cpp) |
| 10 | 滚动、物化与诊断 | [10_scrolling/example.cpp](10_scrolling/example.cpp) |
| 11 | 自动与手动拖放 | [11_drag_drop/example.cpp](11_drag_drop/example.cpp) |
| 12 | 异步图片卡片 | [12_images/example.cpp](12_images/example.cpp) |
| 13 | 圆角、边框与颜色 | [13_borders_colors/example.cpp](13_borders_colors/example.cpp) |
| 14 | 自适应高度 | [14_auto_height/example.cpp](14_auto_height/example.cpp) |

## 构建与启动

在已启用 `VIRTUALITEMVIEWS_BUILD_EXAMPLES=ON` 的构建目录中：

```sh
cmake --build <BUILD_DIR> --target masonry_capabilities --config Debug
```

运行生成的 `masonry_capabilities`，也可使用 `--example 12` 直接打开图片章节。Qt 运行库与平台插件按应用的通常方式部署。

## 复制示例

复制需要的整个章节目录，将 `example.cpp` 与该目录的 `.ui` 加入自己的目标并启用 `AUTOUIC`，链接 `VirtualItemViews::VirtualItemViews` 和 Qt Widgets。由自己的窗口调用该目录的 `createExample(parent)`；不需要复制浏览器或其他章节。第 07 节还需要 `card.ui`，第 12 节还需要目录内的图片辅助头文件。

简单数据直接使用 `QStandardItemModel`；第 10 节用惰性模型演示大量数据。模型、adapter 和异步后端必须覆盖绑定控件的生命周期。视图接管 adapter；图片页在销毁 backend 前销毁视图并解绑卡片。

## 使用边界

- 每个模型行是一张卡片，瀑布流列由最短列布局计算；列不是业务字段，也没有规则表格行或表头。
- `AdapterEstimate` 的 `QSize` 表示宽高比例；`setItemHeight()` 设置权威像素高度。不要使用继承的列表行高设置或自动 sizeHint 测量。
- 事件中通过 `indexForWidget()` 获取复用后身份；解绑停止旧请求。第 12 节先扫描原图尺寸，缩略图完成后不反复重排。
- 第 11 节使用标准 MIME 的复制插入和源行清理；自行实现 moveRows 的模型不要再启用源行删除。

性能探测需同时启用视图开关与日志分类，例如 `QT_LOGGING_RULES=viv.performance.*.debug=true`，输出按时间窗口统计。
