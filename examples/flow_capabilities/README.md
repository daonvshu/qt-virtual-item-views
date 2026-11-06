# VirtualFlowTableView 独立能力示例

一个 `flow_capabilities` 进程通过左侧导航演示各章。页面首次访问时创建；每章自带模型、adapter、控件和 Qt Designer 布局，不引用其他章节的业务代码。注释使用英文。

## 功能索引

| 章节 | 主题 | 源码 |
| --- | --- | --- |
| 01 | 基础接入 | [01_basic/example.cpp](01_basic/example.cpp) / [说明](01_basic/README.md) |
| 02 | 尺寸与自动换列 | [02_cell_sizes/example.cpp](02_cell_sizes/example.cpp) / [说明](02_cell_sizes/README.md) |
| 03 | 间距、边距与分割线 | [03_spacing_grid/example.cpp](03_spacing_grid/example.cpp) / [说明](03_spacing_grid/README.md) |
| 04 | 状态背景与动画 | [04_visual_states/example.cpp](04_visual_states/example.cpp) / [说明](04_visual_states/README.md) |
| 05 | 选择与项目事件 | [05_selection_events/example.cpp](05_selection_events/example.cpp) / [说明](05_selection_events/README.md) |
| 06 | 模型更新与显示根 | [06_model_root/example.cpp](06_model_root/example.cpp) / [说明](06_model_root/README.md) |
| 07 | 滚动、定位与物化 | [07_scrolling/example.cpp](07_scrolling/example.cpp) / [说明](07_scrolling/README.md) |
| 08 | 项目拖放 | [08_drag_drop/example.cpp](08_drag_drop/example.cpp) / [说明](08_drag_drop/README.md) |
| 09 | 异步图片卡片 | [09_images/example.cpp](09_images/example.cpp) / [说明](09_images/README.md) |
| 10 | 颜色与样式表 | [10_colors/example.cpp](10_colors/example.cpp) / [说明](10_colors/README.md) |
| 11 | 整体自适应高度、最大高度与父布局限制 | [11_auto_height/example.cpp](11_auto_height/example.cpp) / [说明](11_auto_height/README.md) |

## 构建与启动

项目配置需要启用 `VIRTUALITEMVIEWS_BUILD_EXAMPLES=ON`。在已配置的构建目录执行：

```sh
cmake --build <BUILD_DIR> --target flow_capabilities --config Debug
```

启动 `flow_capabilities` 或 `flow_capabilities --example 3`。可用 `--exit-after <ms>` 指定退出时间；`--accessibility-trace` 开启辅助几何诊断。第 07 章固定使用 10000 项，第 03 章可以调整项目数量。

## 复制与接入

复制目标章节目录，编译其中的 `example.cpp` 和 `.ui`，启用 CMake `AUTOUIC`，链接 VirtualItemViews 和 Qt Widgets。调用该章节 `example.h` 声明的 `createExample(parent)` 并加入业务布局即可；无需复制启动程序或其他章节。

基础数据直接使用 `QStandardItemModel`。视图持有 adapter，模型由 QObject 父对象管理。Flow 使用专门的固定高度 / 宽高比接口，不使用继承的动态行高 API。

第 09 章另带 `imagebackend.h`、`thumbnailcache.h`，需要一起复制。默认 Qt 后备实现无需额外依赖；项目的可选 aster 配置可切换到 ImageBox。Flow 不提供固定业务列的表头、冻结列、合并格或树展开，相关示例见其他视图。
