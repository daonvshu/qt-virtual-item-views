# 自适应高度

`VirtualListView`、`VirtualTreeView`、`VirtualTableView` 与 `VirtualTreeTableView` 都继承以下接口：

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
