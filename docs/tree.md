# VirtualTreeView 功能示例源码

`VirtualTreeView` 使用标准层级模型与 `WidgetAdapter`，每个节点物化一个真实行控件。只把展开路径中的节点映射为布局行，折叠的子树不创建控件。树节点不支持冻结行。以下路径均相对于仓库根目录，每个目录可独立复制，统一由 `tree_capabilities` 进程演示。

| 编号 | 能力 | 源码相对路径 |
| --- | --- | --- |
| 01 | 基础接入 | [examples/tree_capabilities/01_basic/example.cpp](../examples/tree_capabilities/01_basic/example.cpp) |
| 02 | 组合行控件与数据回写 | [examples/tree_capabilities/02_row_widgets/example.cpp](../examples/tree_capabilities/02_row_widgets/example.cpp) |
| 03 | 不同节点类型与复用池 | [examples/tree_capabilities/03_widget_types/example.cpp](../examples/tree_capabilities/03_widget_types/example.cpp) |
| 04 | 节点结构体与绑定上下文 | [examples/tree_capabilities/04_binding_context/example.cpp](../examples/tree_capabilities/04_binding_context/example.cpp) |
| 05 | 层级模型更新 | [examples/tree_capabilities/05_model_updates/example.cpp](../examples/tree_capabilities/05_model_updates/example.cpp) |
| 06 | 固定、首项测量与动态行高 | [examples/tree_capabilities/06_item_heights/example.cpp](../examples/tree_capabilities/06_item_heights/example.cpp) |
| 07 | 展开收起与过渡动画 | [examples/tree_capabilities/07_expansion/example.cpp](../examples/tree_capabilities/07_expansion/example.cpp) |
| 08 | 切换显示根与相对深度 | [examples/tree_capabilities/08_root_index/example.cpp](../examples/tree_capabilities/08_root_index/example.cpp) |
| 09 | 分支格宽与叶子对齐 | [examples/tree_capabilities/09_indentation/example.cpp](../examples/tree_capabilities/09_indentation/example.cpp) |
| 10 | 内置分支图标配置：大小、颜色、分支格宽、对齐、四向留白与显隐 | [examples/tree_capabilities/10_branch_icons/example.cpp](../examples/tree_capabilities/10_branch_icons/example.cpp) |
| 11 | 自定义分支渲染器 | [examples/tree_capabilities/11_branch_renderer/example.cpp](../examples/tree_capabilities/11_branch_renderer/example.cpp) |
| 12 | 节点间距与分割线 | [examples/tree_capabilities/12_row_spacing/example.cpp](../examples/tree_capabilities/12_row_spacing/example.cpp) |
| 13 | 状态背景、范围与圆角 | [examples/tree_capabilities/13_visual_states/example.cpp](../examples/tree_capabilities/13_visual_states/example.cpp) |
| 14 | 状态关联内容 | [examples/tree_capabilities/14_state_content/example.cpp](../examples/tree_capabilities/14_state_content/example.cpp) |
| 15 | 选择、当前项与键盘导航 | [examples/tree_capabilities/15_selection/example.cpp](../examples/tree_capabilities/15_selection/example.cpp) |
| 16 | 行事件、正确索引与几何查询 | [examples/tree_capabilities/16_item_events/example.cpp](../examples/tree_capabilities/16_item_events/example.cpp) |
| 17 | 大树滚动、定位与可见映射 | [examples/tree_capabilities/17_scrolling/example.cpp](../examples/tree_capabilities/17_scrolling/example.cpp) |
| 18 | 可见行映射与控件复用 | [examples/tree_capabilities/18_virtualization/example.cpp](../examples/tree_capabilities/18_virtualization/example.cpp) |
| 19 | 节点拖放与手动起拖 | [examples/tree_capabilities/19_drag_drop/example.cpp](../examples/tree_capabilities/19_drag_drop/example.cpp) |
| 20 | 拖动位移动画 | [examples/tree_capabilities/20_drag_animation/example.cpp](../examples/tree_capabilities/20_drag_animation/example.cpp) |
| 21 | 层级排序与递归过滤 | [examples/tree_capabilities/21_sorting_filtering/example.cpp](../examples/tree_capabilities/21_sorting_filtering/example.cpp) |
| 22 | 业务驱动的延迟加载 | [examples/tree_capabilities/22_lazy_loading/example.cpp](../examples/tree_capabilities/22_lazy_loading/example.cpp) |
| 23 | 屏外保留与编辑生命周期 | [examples/tree_capabilities/23_pinning/example.cpp](../examples/tree_capabilities/23_pinning/example.cpp) |
| 24 | 统计与生命周期日志 | [examples/tree_capabilities/24_diagnostics/example.cpp](../examples/tree_capabilities/24_diagnostics/example.cpp) |
| 25（可选） | 平滑滚动（可选） | [examples/tree_capabilities/25_smooth_scrolling/example.cpp](../examples/tree_capabilities/25_smooth_scrolling/example.cpp) |
| 26 | 外边框与圆角：框架接口、透明角、状态圆角与展开收起 | [examples/tree_capabilities/26_borders/example.cpp](../examples/tree_capabilities/26_borders/example.cpp) |

先看各章的 `Example::setupExample()`，再看本目录的 model、adapter 和节点控件；`setupUi()` 只创建页面布局。普通数据直接创建 `QStandardItemModel` 填充，不为简单数据额外派生模型类。每个目录都有英文代码注释和独立运行入口，构建与启动见 [examples/tree_capabilities/README.md](../examples/tree_capabilities/README.md)。

## 节点身份与生命周期

模型 `row()` 只在同一 parent 下有意义，完整 `QModelIndex` 才能定位层级节点；可见映射行和 `itemDepth()` 另有用途，见第 08、17、18 章。`setRootIndex()` 只改变显示范围，节点索引仍属于原模型。结构变化使用 Qt model 信号，普通更新无需重新设置模型。

节点可通过 `kDefaultItemDataRole` 存完整结构体，bind 回调直接取 `context.rowData`；不同层级可用不同结构体和 `WidgetType`，见第 04 章。持久索引不会自动跟随控件复用，必须每次绑定更新、解绑清空；行内事件的两种正确写法见第 14、16 章。

## 绘制与交互

默认 hover／selected 背景在透明行控件和 branch 图标下方绘制。圆角、颜色、范围和动画直接配置函数，见第 13 章；状态不覆盖行间距。分支格宽由 `setIndentation()` 控制，默认图标大小、颜色、替换和隐藏见第 10 章；只有连接线等完全自定义装饰才需要第 11 章的 renderer。

整体圆角和外边框见 [第 26 章](../examples/tree_capabilities/26_borders/README.md)。`setFrameCornerRadius()`、`setFrameBorderWidth()` 和 `setFrameBorderColor()` 统一处理节点、分支和滚动条的外框；`setVisualStateCornerRadius()` 独立控制行状态圆角。页面提供多层树、少量节点、空模型及展开收起操作。

内置图标和三角默认 `Qt::AlignCenter`，padding 全为 0。`setBranchIndicatorAlignment(Qt::Alignment)` 与 `setBranchIndicatorPadding(QMargins)`（或四参数重载）可以调整图标位置和四向留白；图标大小会限制在扣除 padding 后的区域内。点击区域仍是整个分支格，自定义 renderer 不受这些设置影响，可用 `BranchIndicatorRenderer::branchIndicatorTargetRect()` 复用同一套几何计算。

默认 10px 圆角等边三角形以展开朝下为基准，收起时逆时针旋转 90°；旋转和真实行展开使用同一条动画，反向操作保留当前角度。只设置 `setExpandedBranchIcon(icon)` 时同样复用该图标；只设置 `setCollapsedBranchIcon(icon)` 时，展开方向为该图标顺时针旋转 90°。设置两个非空图标时立即切换，清空两个图标恢复内置旋转。自定义 renderer 不自动旋转。关闭展开动画或无过渡的操作会立即应用最终方向；单独关闭淡入淡出不影响旋转。第 10 章提供四种图标模式的对比。

展开动画默认开启，可用 `setExpansionAnimationEnabled(false)` 关闭，`setExpansionAnimationDuration(ms)` 设置时长（默认 300 ms）。动画直接更新真实行布局，行控件、分支、分割线和命中区域随布局变化，不截取画面或使用截图覆盖层。部分露出的行由原生父控件进行矩形裁剪，业务控件保持正常高度。大子树只物化当前露出的范围，屏外尾部不逐行参与动画。

收起开始后 `isExpanded()` 返回 `false`；为保留正在退出的真实控件，子节点映射和 `visibleRowCount()` 到动画结束才移除，随后发送 `collapsed`。展开映射和 `expanded` 在展开开始时更新。连续反向展开收起从当前进度继续；模型结构变化会取消过渡并重建布局。`collapseAll()` 和已展开节点的递归展开直接更新，不使用截图回退。

子节点淡入淡出默认启用，`setExpansionFadeEnabled(false)` 可单独关闭渐变，配置查询为 `expansionFadeEnabled()`。透明度和高度共享动画进度；父节点和后续分组不渐变。第 07 章提供勾选演示。框架通过临时容器保留业务控件已有的图形效果；Qt 的实时透明度合成可能使用离屏缓冲，开启后有额外绘制成本。

树默认使用 `DragFeedbackMode::RowMoveAnimation`，单节点起拖时在截图前立即收起源节点，不执行收起动画；内部 Move 只按主节点的一行高度（含正常间距）让位，整棵子树的数据仍一起移动。取消或放下后保持收起。`setDragReorderAnimationDuration(ms)` 与 `setDragReorderAnimationEasingCurve(curve)` 控制位移，默认 180 ms / OutCubic，0 表示立即让位。复制、跨视图、多节点和投放为子项使用指示器；拖动中业务重新展开源节点时也回退到指示器。位移动画不提前修改模型顺序；取消、离开、拒绝落点或结构变化会清理显示偏移。指示器、手动起拖和模型移动责任见第 19 章，动画参数见第 20 章。业务异步加载、节点保留和诊断分别见第 22–24 章。

第 09、10 章演示 `setBranchAreaVisible(false)`：隐藏所有分支装饰并移除节点自身的图标占位，保留 `depth × indentation()` 的层级缩进。`branchAreaVisible()` 查询配置，默认 `true`。`setBranchIndicatorsVisible(false)` 只隐藏装饰、仍保留图标占位；`setIndentation(0)` 则将全部缩进归零。隐藏分支区域不会修改图标配置或展开状态，仍可通过双击、键盘和展开接口操作。
