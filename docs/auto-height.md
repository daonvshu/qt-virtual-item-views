# 自适应高度

`VirtualListView`、`VirtualTreeView`、`VirtualTableView`、`VirtualTreeTableView`、`VirtualFlowTableView`、`VirtualMasonryView` 与 `VirtualWrapView` 都继承以下接口：

```cpp
view->setAutoHeightEnabled(true);
view->setMaximumHeight(320);
view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
verticalLayout->addWidget(view);
verticalLayout->addStretch();
```

`autoHeightEnabled()` 查询配置，默认关闭。开启后，内容较少时控件的建议高度等于内容、表头预留区域、边框和占用布局空间的水平滚动条的总高度。表格会计入显示的水平表头；冻结行属于内容，不重复计入。树只计算当前展开内容，展开收起过渡也会更新建议高度。

最大高度使用标准 QWidget 接口，`setMaximumHeight(QWIDGETSIZE_MAX)` 取消显式上限。垂直布局根据兄弟控件、布局边距和父区域的可用空间分配实际高度，空间不足时可压缩视口。这里的父布局应在有高度约束的容器中；如果外层本身允许无限增高，例如没有高度限制的外部滚动区，仍需设置最大高度才能限制内部视图。

框架在开启时将垂直尺寸策略设为 `QSizePolicy::Maximum`，让短视图不占据多余空间，同时允许父布局缩小它。关闭时恢复开启前的垂直策略；如果业务已经重新设置了其他垂直策略，则保留业务设置。水平策略不变。不要再用 `setFixedHeight()`、不可收缩的最小高度或 `Qt::AlignTop` 控制视图的垂直大小，这些 Qt 布局约束会影响收缩结果。

当视图嵌入开启 `widgetResizable` 的外层 `QScrollArea`，且中间容器没有显式最大高度时，框架同时用内容建议高度作为布局最小高度。这让外层滚动区能够为整张表格预留空间，避免多个表格在长表单中被压到只剩表头。该高度仍受视图自身的 `maximumHeight()` 限制，超过上限后由内部滚动。无需对外层内容手动调用 `adjustSize()` 或设置固定最小高度。中间容器存在显式高度上限（例如折叠动画）或最近的滚动祖先是其他虚拟视图时，保留原来的可收缩行为；框架不会修改外层滚动区或业务容器的属性。

自适应高度开启期间，框架会观察祖先的布局、尺寸、显示隐藏及父对象变化，并合并到下一轮事件循环重新检查高度建议。父容器通过 `maximumHeight` 执行折叠动画、结束后解除高度限制时，会使旧的布局高度缓存失效；视图重新显示时也会重新通知布局，无需鼠标经过或业务手动触发刷新。关闭自适应高度或销毁视图时移除观察，正常布局未变化时不重复通知。

管理少量真实控件的 `WrapWidget` 使用同样的嵌套高度规则，且默认开启自适应高度。它根据当前宽度下的完整换行高度和自身最大高度提供布局最小高度，与其他自适应视图放在同一个外层滚动容器时不会因只保留边框高度而被压缩。父容器折叠、重新展开或换父对象后也会更新高度建议；关闭自适应高度后恢复普通 `QScrollArea` 的高度建议。示例见 [Wrap 第 10 章](../examples/wrap_capabilities/10_wrap_widget/example.cpp)，布局由同目录的 `wrap_widget.ui` 定义。

滚动条策略不被此开关覆盖。使用默认 `ScrollBarAsNeeded` 时，内容放得下就没有竖向滚动范围；只有内容超出实际视口时才显示滚动条并允许滚动。`ScrollBarAlwaysOn`、`ScrollBarAlwaysOff` 仍遵循业务的显式选择。

内容没有竖向滚动范围时，视图不接受竖向滚轮事件，让事件继续传递给外层滚动区域；强制显示但范围为 0 的滚动条也不会拦截。该行为同样适用于未开启自适应高度的视图，以及 `VirtualFlowTableView`、`VirtualWrapView` 和 `WrapWidget`。判断依据是实际内容与视口的尺寸，不是是否达到 `maximumHeight()`：父布局分配的空间更小时，只要内容溢出仍由内部视图滚动。表格的横向滚轮按鼠标所在滚动组的横向范围判断；存在滚动范围时，到达边界仍保持原有的内部滚动行为。

启用自适应高度或内容超出视口、存在竖向滚动范围时，不绘制最后一行的底部分割线，避免与控件下边框叠加。判断使用实际内容范围，不依赖滚动条是否可见；固定高度下强制显示滚动条但滚动范围为 0 时，仍保留末行线。表格的列间距与默认垂直表头同步处理；内部行分割线和冻结区域分隔线保持原有行为。

末行线自动隐藏默认启用，可使用 `setTrailingRowGridLineAutoHideEnabled(false)` 关闭；此时只要行分割线已启用，就始终绘制可见的末行线。传入 `true` 恢复上述自动规则，`trailingRowGridLineAutoHideEnabled()` 查询配置。该接口由四种视图共享，无需分别配置表格的默认垂直表头和列间距。

高度计算复用现有的 64 位布局范围，包含行间距和表格外侧半间距。动态行高仍按原有策略估计，并在正常物化测量后反馈更新；不会为了求总高度创建所有逻辑项或遍历隐藏子树。此功能针对纵向内容，不为横向布局计算外部高度。

独立示例：

- [列表第 21 章](../examples/list_capabilities/21_auto_height/README.md)
- [树第 27 章](../examples/tree_capabilities/27_auto_height/README.md)
- [表格第 31 章](../examples/table_capabilities/31_auto_height/README.md)
- [树表第 36 章](../examples/treetable_capabilities/36_auto_height/README.md)
