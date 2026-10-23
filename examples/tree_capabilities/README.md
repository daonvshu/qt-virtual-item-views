# VirtualTreeView 独立能力示例

一个 `tree_capabilities` 进程通过左侧导航演示 24 项基础功能。检测到匹配的 SmoothScrollbar 时才编译和显示第 25 项。本目录仅演示 `VirtualTreeView`，树表的独立示例继续保留。

每个编号目录自带 model、adapter、节点控件和 `example.h`／`example.cpp`／`README.md`，不引用其他能力目录或 `examples/common`。重复的小类有意保留，复制单章即可参考。普通数据直接使用 `QStandardItemModel`，第 17 章因按需生成百万节点才实现自定义模型。所有代码注释为英文。

| 编号 | 能力 | 源码相对路径 |
| --- | --- | --- |
| 01 | 基础接入 | [examples/tree_capabilities/01_basic/example.cpp](01_basic/example.cpp) |
| 02 | 组合行控件与数据回写 | [examples/tree_capabilities/02_row_widgets/example.cpp](02_row_widgets/example.cpp) |
| 03 | 不同节点类型与复用池 | [examples/tree_capabilities/03_widget_types/example.cpp](03_widget_types/example.cpp) |
| 04 | 节点结构体与绑定上下文 | [examples/tree_capabilities/04_binding_context/example.cpp](04_binding_context/example.cpp) |
| 05 | 层级模型更新 | [examples/tree_capabilities/05_model_updates/example.cpp](05_model_updates/example.cpp) |
| 06 | 固定、首项测量与动态行高 | [examples/tree_capabilities/06_item_heights/example.cpp](06_item_heights/example.cpp) |
| 07 | 展开收起与过渡动画 | [examples/tree_capabilities/07_expansion/example.cpp](07_expansion/example.cpp) |
| 08 | 切换显示根与相对深度 | [examples/tree_capabilities/08_root_index/example.cpp](08_root_index/example.cpp) |
| 09 | 分支格宽与叶子对齐 | [examples/tree_capabilities/09_indentation/example.cpp](09_indentation/example.cpp) |
| 10 | 内置分支图标配置 | [examples/tree_capabilities/10_branch_icons/example.cpp](10_branch_icons/example.cpp) |
| 11 | 自定义分支渲染器 | [examples/tree_capabilities/11_branch_renderer/example.cpp](11_branch_renderer/example.cpp) |
| 12 | 节点间距与分割线 | [examples/tree_capabilities/12_row_spacing/example.cpp](12_row_spacing/example.cpp) |
| 13 | 状态背景、范围与圆角 | [examples/tree_capabilities/13_visual_states/example.cpp](13_visual_states/example.cpp) |
| 14 | 状态关联内容 | [examples/tree_capabilities/14_state_content/example.cpp](14_state_content/example.cpp) |
| 15 | 选择、当前项与键盘导航 | [examples/tree_capabilities/15_selection/example.cpp](15_selection/example.cpp) |
| 16 | 行事件、正确索引与几何查询 | [examples/tree_capabilities/16_item_events/example.cpp](16_item_events/example.cpp) |
| 17 | 大树滚动、定位与可见映射 | [examples/tree_capabilities/17_scrolling/example.cpp](17_scrolling/example.cpp) |
| 18 | 可见行映射与控件复用 | [examples/tree_capabilities/18_virtualization/example.cpp](18_virtualization/example.cpp) |
| 19 | 顶部底部冻结与区域分隔线 | [examples/tree_capabilities/19_frozen_rows/example.cpp](19_frozen_rows/example.cpp) |
| 20 | 节点拖放与手动起拖 | [examples/tree_capabilities/20_drag_drop/example.cpp](20_drag_drop/example.cpp) |
| 21 | 层级排序与递归过滤 | [examples/tree_capabilities/21_sorting_filtering/example.cpp](21_sorting_filtering/example.cpp) |
| 22 | 业务驱动的延迟加载 | [examples/tree_capabilities/22_lazy_loading/example.cpp](22_lazy_loading/example.cpp) |
| 23 | 屏外保留与编辑生命周期 | [examples/tree_capabilities/23_pinning/example.cpp](23_pinning/example.cpp) |
| 24 | 统计与生命周期日志 | [examples/tree_capabilities/24_diagnostics/example.cpp](24_diagnostics/example.cpp) |
| 25（可选） | 平滑滚动（可选） | [examples/tree_capabilities/25_smooth_scrolling/example.cpp](25_smooth_scrolling/example.cpp) |

## 构建与启动

在项目已有配置中启用 `VIRTUALITEMVIEWS_BUILD_EXAMPLES=ON`，构建：

```sh
cmake --build <BUILD_DIR> --target tree_capabilities --config Debug
```

运行 `tree_capabilities` 或 `tree_capabilities --example 7`。`main.cpp` 仅负责导航和延迟创建页面；各章 `setupExample()` 明确列出视图调用和业务接线，`setupUi()` 管理少量参数控件及布局。独立运行入口见各章 README。

第 25 章要求 `VIRTUALITEMVIEWS_EXAMPLES_USE_SMOOTH_SCROLLBAR=ON`，并通过 `SmoothScrollbar_DIR` 或 `CMAKE_PREFIX_PATH` 提供匹配当前 Qt 主版本的包。依赖缺失时不编译该页，导航和命令行编号保持 1–24。

## 使用时关注的边界

树的模型 `row()` 是兄弟位置，可见映射行由展开路径决定，`itemDepth()` 相对于显示根计算，三者不要混用。行控件复用时必须完整绑定和解绑；事件发生时查询当前绑定，不能捕获首次绑定的索引。每页 model 归视图的 QObject 父子关系管理，adapter 通过 `takeOwnership=true` 交给视图，确保销毁顺序。

hover／selected 背景默认由框架绘制，节点控件保持透明。树拖放提供兄弟插入线与成为子项的目标框，业务模型管理权限、MIME 和数据移动。延迟加载页由业务监听展开信号后更新模型，不依赖自动 `fetchMore()`。
