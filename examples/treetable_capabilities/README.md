# VirtualTreeTableView 独立能力示例

一个 `treetable_capabilities` 进程，通过左侧导航按需创建各章页面。包含 36 个常规章节，检测到匹配当前 Qt 主版本的 SmoothScrollbar 时额外编译和显示第 34 章，共 37 章。

每个编号目录独立包含自己的模型、adapter 和控件，不引用其他能力目录或 `examples/common`。重复的小类有意保留，复制一个章节即可作为接入参考；关键视图调用旁均使用英文注释。

每章页面布局、控件文字和初始值放在同目录的 `.ui` 文件中，可用 Qt Designer 编辑。`ui` 在堆上创建并由智能指针管理，`setupUi()` 加载界面；直接通过 `ui->控件名` 访问界面控件，`setupExample()` 展示视图配置和事件接线；阅读功能用法时可直接从后者开始。复制章节时一起复制 `.ui`，在自己的 CMake 目标启用 `AUTOUIC` 并列入该文件。ColumnHost 中按列创建的控件和 adapter 创建的简单组合仍保留在 C++ 中，用于说明物化和复用。

先阅读第 01 章最小多列树，再按需要选择第 02 章整行物化或第 03 章单元格物化。第 05 章演示每级节点只存一份结构体，绑定不必再次拆解索引数据。

| 编号 | 功能 | 独立源码（相对仓库根目录） |
| --- | --- | --- |
| 01 | 基础多列树 | [examples/treetable_capabilities/01_basic/example.cpp](01_basic/example.cpp) |
| 02 | 整行物化与 ColumnHost | [examples/treetable_capabilities/02_row_widgets/example.cpp](02_row_widgets/example.cpp) |
| 03 | 单元格物化 | [examples/treetable_capabilities/03_cell_widgets/example.cpp](03_cell_widgets/example.cpp) |
| 04 | 层级与列控件类型 | [examples/treetable_capabilities/04_widget_types/example.cpp](04_widget_types/example.cpp) |
| 05 | 节点结构体与绑定上下文 | [examples/treetable_capabilities/05_binding_context/example.cpp](05_binding_context/example.cpp) |
| 06 | 层级模型更新 | [examples/treetable_capabilities/06_model_updates/example.cpp](06_model_updates/example.cpp) |
| 07 | 固定与动态行高 | [examples/treetable_capabilities/07_row_heights/example.cpp](07_row_heights/example.cpp) |
| 08 | 展开收起与淡入淡出 | [examples/treetable_capabilities/08_expansion/example.cpp](08_expansion/example.cpp) |
| 09 | 显示根与相对深度 | [examples/treetable_capabilities/09_root_index/example.cpp](09_root_index/example.cpp) |
| 10 | 缩进与分支配置 | [examples/treetable_capabilities/10_indentation/example.cpp](10_indentation/example.cpp) |
| 11 | 自定义分支绘制 | [examples/treetable_capabilities/11_branch_renderer/example.cpp](11_branch_renderer/example.cpp) |
| 12 | 列宽、隐藏与换序 | [examples/treetable_capabilities/12_columns/example.cpp](12_columns/example.cpp) |
| 13 | 比例列宽与适应视口 | [examples/treetable_capabilities/13_stretch_columns/example.cpp](13_stretch_columns/example.cpp) |
| 14 | 节点间距与分割线 | [examples/treetable_capabilities/14_spacing/example.cpp](14_spacing/example.cpp) |
| 15 | 状态背景与圆角 | [examples/treetable_capabilities/15_visual_states/example.cpp](15_visual_states/example.cpp) |
| 16 | 状态关联内容 | [examples/treetable_capabilities/16_state_content/example.cpp](16_state_content/example.cpp) |
| 17 | 共享选择与键盘导航 | [examples/treetable_capabilities/17_selection/example.cpp](17_selection/example.cpp) |
| 18 | 行内事件与几何查询 | [examples/treetable_capabilities/18_item_events/example.cpp](18_item_events/example.cpp) |
| 19 | 大树定位与滚动 | [examples/treetable_capabilities/19_scrolling/example.cpp](19_scrolling/example.cpp) |
| 20 | 二维虚拟化与复用 | [examples/treetable_capabilities/20_virtualization/example.cpp](20_virtualization/example.cpp) |
| 21 | 左右冻结列 | [examples/treetable_capabilities/21_frozen_columns/example.cpp](21_frozen_columns/example.cpp) |
| 22 | 独立横向滚动分组 | [examples/treetable_capabilities/22_panes/example.cpp](22_panes/example.cpp) |
| 23 | 表头交互与调整 | [examples/treetable_capabilities/23_headers/example.cpp](23_headers/example.cpp) |
| 24 | 表头样式、Adapter 与 Corner | [examples/treetable_capabilities/24_custom_header/example.cpp](24_custom_header/example.cpp) |
| 25 | 保存与恢复布局 | [examples/treetable_capabilities/25_header_persistence/example.cpp](25_header_persistence/example.cpp) |
| 26 | 同级排序与递归过滤 | [examples/treetable_capabilities/26_sorting_filtering/example.cpp](26_sorting_filtering/example.cpp) |
| 27 | 同级行号拖动排序 | [examples/treetable_capabilities/27_row_reordering/example.cpp](27_row_reordering/example.cpp) |
| 28 | 同级单元格合并 | [examples/treetable_capabilities/28_spans/example.cpp](28_spans/example.cpp) |
| 29 | 节点拖放与指示器 | [examples/treetable_capabilities/29_drag_indicator/example.cpp](29_drag_indicator/example.cpp) |
| 30 | 节点拖放与位移动画 | [examples/treetable_capabilities/30_drag_animation/example.cpp](30_drag_animation/example.cpp) |
| 31 | 异步加载子节点 | [examples/treetable_capabilities/31_lazy_loading/example.cpp](31_lazy_loading/example.cpp) |
| 32 | 屏外保留与释放 | [examples/treetable_capabilities/32_pinning/example.cpp](32_pinning/example.cpp) |
| 33 | 统计与性能诊断 | [examples/treetable_capabilities/33_diagnostics/example.cpp](33_diagnostics/example.cpp) |
| 34（可选） | 可选平滑滚动 | [examples/treetable_capabilities/34_smooth_scrolling/example.cpp](34_smooth_scrolling/example.cpp) |
| 35 | 圆角边框与透明视图 | [examples/treetable_capabilities/35_borders/example.cpp](35_borders/example.cpp) |
| 36 | 自适应高度与滚动上限 | [examples/treetable_capabilities/36_auto_height/example.cpp](36_auto_height/example.cpp) |
| 37 | 颜色与样式表：主体、项、状态与 QSS 的作用范围 | [examples/treetable_capabilities/37_colors/example.cpp](37_colors/example.cpp) |

## 构建与运行

在项目配置中启用 `VIRTUALITEMVIEWS_BUILD_EXAMPLES=ON`，构建：

```sh
cmake --build <BUILD_DIR> --target treetable_capabilities --config Debug
```

运行 `treetable_capabilities`，也可用 `treetable_capabilities --example 8` 直接进入展开动画，`--example 30` 进入拖动位移动画，`--example 36` 进入自适应高度。`main.cpp` 只负责导航与页面延迟创建。

第 34 章需要 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR=ON`，通过 `SmoothScrollbar_DIR` 或 `CMAKE_PREFIX_PATH` 提供匹配 Qt 主版本的包。依赖缺失时该页不编译也不显示，其余章节保留原编号。

## 树表接入时的边界

- 节点用列 0 的完整 QModelIndex 标识；row() 是兄弟位置，不是可见树行。使用 `visibilityIndex()` 转换几何行序号。切换显示根后深度相对该根计算。
- 每个父节点下的行提供同样的逻辑列 schema。RowWidgets 用 ColumnHost 承载每列；CellWidgets 按列绑定。树装饰始终跟随逻辑列 0。
- hover 和 selected 背景由框架绘制，业务控件保持透明。事件查询当前绑定，或在每次 bind 更新、unbind 清空持久索引，不能捕获首次绑定索引。
- 支持左右冻结列及横向滚动分组，不提供冻结行。默认 branch 的图标、大小、颜色、对齐及 padding 与 VirtualTreeView 使用相同便捷接口；复杂装饰通过 branch renderer 定制。
- 行号拖动只排序同级节点，模型实现 moveRows；原生节点拖放可以按权限改变父节点，模型负责 MIME 和数据。树表投放区域表示同级插入或子节点投放，不使用平表的空白单元格投放语义。
- 同级连续节点才能跨行合并，不能跨展开后代或 pane。布局持久化保存列设置，展开状态需要业务另存稳定节点 ID。

详细接口与约束见 [docs/tree-table.md](../../docs/tree-table.md)。
