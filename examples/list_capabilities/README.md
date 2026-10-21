# VirtualListView 独立能力示例

构建目标：`list_capabilities`。一个进程通过左侧导航切换 17 个基础示例，按需创建页面；检测到 SmoothScrollbar 时额外加入第 18 项平滑滚动示例。不包含无障碍示例。

每个编号目录都有自己的 `example.h`、`example.cpp` 和说明。model、adapter、行控件、手柄等代码都在该目录内定义，不引用其他示例或 `examples/common`。重复的小类是有意保留的，便于复制整个目录。每个目录使用独立命名空间，统一编译时不会出现重名冲突。

## 索引

| 编号 | 能力 | 本目录说明与代码 |
| --- | --- | --- |
| 01 | 基础列表：model、adapter 接入和 rootIndex 切换 | [说明](01_basic/README.md) / [example.cpp](01_basic/example.cpp) |
| 02 | 自定义行控件：组合控件、数据回写和信号阻断 | [说明](02_row_widgets/README.md) / [example.cpp](02_row_widgets/example.cpp) |
| 03 | 多种行类型：WidgetType 分类和独立复用池 | [说明](03_widget_types/README.md) / [example.cpp](03_widget_types/example.cpp) |
| 04 | 整行数据绑定：结构体 role 和 ItemBindingContext | [说明](04_binding_context/README.md) / [example.cpp](04_binding_context/example.cpp) |
| 05 | 数据更新：修改、增删、移动、重置、排序和过滤 | [说明](05_model_updates/README.md) / [example.cpp](05_model_updates/example.cpp) |
| 06 | 行高：固定、首项测量、动态高度和锚点补偿 | [说明](06_item_heights/README.md) / [example.cpp](06_item_heights/example.cpp) |
| 07 | 行间距和分割线：间距、线宽、颜色和自定义间距控件 | [说明](07_row_spacing/README.md) / [example.cpp](07_row_spacing/example.cpp) |
| 08 | Hover 和选中状态：默认状态背景、圆角、颜色和动画 | [说明](08_visual_states/README.md) / [example.cpp](08_visual_states/example.cpp) |
| 09 | 状态关联内容：根据 hover / selected 显示操作区 | [说明](09_state_content/README.md) / [example.cpp](09_state_content/example.cpp) |
| 10 | 选择与当前项：选择模式、程序选择和共享选择模型 | [说明](10_selection/README.md) / [example.cpp](10_selection/example.cpp) |
| 11 | 行事件与控件事件：行信号、键盘和复用后的当前索引 | [说明](11_item_events/README.md) / [example.cpp](11_item_events/example.cpp) |
| 12 | 滚动与定位：滚轮模式、定位提示和 64 位偏移 | [说明](12_scrolling/README.md) / [example.cpp](12_scrolling/example.cpp) |
| 13 | 虚拟化与复用：overscan、可见范围和控件复用 | [说明](13_virtualization/README.md) / [example.cpp](13_virtualization/example.cpp) |
| 14 | 冻结行：顶部、底部冻结和区域分隔线 | [说明](14_frozen_rows/README.md) / [example.cpp](14_frozen_rows/example.cpp) |
| 15 | 拖放：自动 / 手动起拖、预览和 Move / Copy | [说明](15_drag_drop/README.md) / [example.cpp](15_drag_drop/example.cpp) |
| 16 | 屏外保留：按 index / widget 保留和释放控件 | [说明](16_pinning/README.md) / [example.cpp](16_pinning/example.cpp) |
| 17 | 运行诊断：统计快照和生命周期日志 | [说明](17_diagnostics/README.md) / [example.cpp](17_diagnostics/example.cpp) |
| 18（可选） | 平滑滚动：SmoothScrollbar 接入、启停和滚轮步长 | [说明](18_smooth_scrolling/README.md) / [example.cpp](18_smooth_scrolling/example.cpp) |

## 构建与启动

使用项目已有、启用了 `VIRTUALITEMVIEWS_BUILD_EXAMPLES` 的 CMake 配置：

```sh
cmake --build <BUILD_DIR> --target list_capabilities --config Debug
```

运行构建输出中的 `list_capabilities`，也可通过 `--example 15` 直接进入拖放页。运行前按当前 Qt kit 配置运行库和平台插件。前 17 项只依赖 VirtualItemViews 和 Qt Widgets，不依赖可选图片或滚动组件。

启用 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR`（默认开启），并通过 `SmoothScrollbar_DIR` 或 `CMAKE_PREFIX_PATH` 提供与当前 Qt 主版本匹配的 SmoothScrollbar 包后，才会编译并显示第 18 项，可用 `--example 18` 直接进入。未检测到匹配依赖时，不编译该页，导航及命令行范围自动保持为 17 项。

## 阅读和复制

每页按以下顺序阅读：

1. 本地行控件：布局、数据展示和必要的状态绘制。
2. 本地 adapter：create / bind / unbind，及该能力需要的回调。
3. `Example::setupExample()`：model 数据、view 配置、交互接线。
4. `Example::setupUi()`：演示用的少量参数控件和布局。

启动器 `main.cpp` 只负责导航和调用各目录的 `createExample()`，不参与 model、adapter 或行交互。每个目录的说明包含独立运行入口写法；复制单个目录时不需要复制启动器。

示例把 adapter 所有权交给 view，model 则由页面或 view 持有，确保解绑和销毁期间的生命周期有效。数据操作必须发出 Qt model 信号；业务操作和数据语义依然由 model / 行控件负责。每页只演示该主题需要的配置，不隐式给其他页面增加背景、拖动或编辑行为。

原有最小程序及其他视图示例继续保留，见 [examples 导航](../README.md)。
