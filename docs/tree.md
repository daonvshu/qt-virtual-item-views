# VirtualTreeView 功能示例源码

`VirtualTreeView` 使用标准层级模型与 `WidgetAdapter`，每个节点物化一个真实行控件。只把展开路径中的节点映射为布局行，折叠的子树不创建控件。以下路径均相对于仓库根目录，每个目录可独立复制，统一由 `tree_capabilities` 进程演示。

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
| 10 | 内置分支图标配置 | [examples/tree_capabilities/10_branch_icons/example.cpp](../examples/tree_capabilities/10_branch_icons/example.cpp) |
| 11 | 自定义分支渲染器 | [examples/tree_capabilities/11_branch_renderer/example.cpp](../examples/tree_capabilities/11_branch_renderer/example.cpp) |
| 12 | 节点间距与分割线 | [examples/tree_capabilities/12_row_spacing/example.cpp](../examples/tree_capabilities/12_row_spacing/example.cpp) |
| 13 | 状态背景、范围与圆角 | [examples/tree_capabilities/13_visual_states/example.cpp](../examples/tree_capabilities/13_visual_states/example.cpp) |
| 14 | 状态关联内容 | [examples/tree_capabilities/14_state_content/example.cpp](../examples/tree_capabilities/14_state_content/example.cpp) |
| 15 | 选择、当前项与键盘导航 | [examples/tree_capabilities/15_selection/example.cpp](../examples/tree_capabilities/15_selection/example.cpp) |
| 16 | 行事件、正确索引与几何查询 | [examples/tree_capabilities/16_item_events/example.cpp](../examples/tree_capabilities/16_item_events/example.cpp) |
| 17 | 大树滚动、定位与可见映射 | [examples/tree_capabilities/17_scrolling/example.cpp](../examples/tree_capabilities/17_scrolling/example.cpp) |
| 18 | 可见行映射与控件复用 | [examples/tree_capabilities/18_virtualization/example.cpp](../examples/tree_capabilities/18_virtualization/example.cpp) |
| 19 | 顶部底部冻结与区域分隔线 | [examples/tree_capabilities/19_frozen_rows/example.cpp](../examples/tree_capabilities/19_frozen_rows/example.cpp) |
| 20 | 节点拖放与手动起拖 | [examples/tree_capabilities/20_drag_drop/example.cpp](../examples/tree_capabilities/20_drag_drop/example.cpp) |
| 21 | 层级排序与递归过滤 | [examples/tree_capabilities/21_sorting_filtering/example.cpp](../examples/tree_capabilities/21_sorting_filtering/example.cpp) |
| 22 | 业务驱动的延迟加载 | [examples/tree_capabilities/22_lazy_loading/example.cpp](../examples/tree_capabilities/22_lazy_loading/example.cpp) |
| 23 | 屏外保留与编辑生命周期 | [examples/tree_capabilities/23_pinning/example.cpp](../examples/tree_capabilities/23_pinning/example.cpp) |
| 24 | 统计与生命周期日志 | [examples/tree_capabilities/24_diagnostics/example.cpp](../examples/tree_capabilities/24_diagnostics/example.cpp) |
| 25（可选） | 平滑滚动（可选） | [examples/tree_capabilities/25_smooth_scrolling/example.cpp](../examples/tree_capabilities/25_smooth_scrolling/example.cpp) |

先看各章的 `Example::setupExample()`，再看本目录的 model、adapter 和节点控件；`setupUi()` 只创建页面布局。普通数据直接创建 `QStandardItemModel` 填充，不为简单数据额外派生模型类。每个目录都有英文代码注释和独立运行入口，构建与启动见 [examples/tree_capabilities/README.md](../examples/tree_capabilities/README.md)。

## 节点身份与生命周期

模型 `row()` 只在同一 parent 下有意义，完整 `QModelIndex` 才能定位层级节点；可见映射行和 `itemDepth()` 另有用途，见第 08、17、18 章。`setRootIndex()` 只改变显示范围，节点索引仍属于原模型。结构变化使用 Qt model 信号，普通更新无需重新设置模型。

节点可通过 `kDefaultItemDataRole` 存完整结构体，bind 回调直接取 `context.rowData`；不同层级可用不同结构体和 `WidgetType`，见第 04 章。持久索引不会自动跟随控件复用，必须每次绑定更新、解绑清空；行内事件的两种正确写法见第 14、16 章。

## 绘制与交互

默认 hover／selected 背景在透明行控件和 branch 图标下方绘制。圆角、颜色、范围和动画直接配置函数，见第 13 章；状态不覆盖行间距。分支格宽由 `setIndentation()` 控制，默认图标大小、颜色、替换和隐藏见第 10 章；只有连接线等完全自定义装饰才需要第 11 章的 renderer。

展开动画默认关闭，第 07 章显式启用；冻结行情况下使用淡入淡出。树只提供插入线／目标框拖放，手动起拖和模型移动责任见第 20 章。业务异步加载、节点保留和诊断分别见第 22–24 章。
