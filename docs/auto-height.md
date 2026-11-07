# 自适应高度

`VirtualListView`、`VirtualTreeView`、`VirtualTableView`、`VirtualTreeTableView`、`VirtualFlowTableView`、`VirtualMasonryView` 与 `VirtualWrapView` 都继承以下接口：

```cpp
view->setAutoHeightMaximumHeight(320); // Optional total-height limit; 0 means unlimited.
view->setAutoHeightEnabled(true);
view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
verticalLayout->addWidget(view);
verticalLayout->addStretch();
```

`autoHeightEnabled()` 查询配置，虚拟视图默认关闭。开启后，框架根据内容高度调用 `setFixedHeight()`，并使用 `QSizePolicy::Fixed` 垂直策略。内容、宽度、布局参数和树展开状态变化后重新计算；父布局不会将视图压缩到更小的高度。

`setAutoHeightMaximumHeight(height)` 设置整个控件的高度上限，`autoHeightMaximumHeight()` 查询。默认 `0` 表示不限高，负值按 `0` 处理；不设上限时始终采用完整内容高度。父容器空间不足由业务安排外层滚动或设置合适的上限。

上限独立于 QWidget 的最小、最大高度，避免内部 `setFixedHeight()` 将上一帧的结果当成下一帧的上限。开启期间不要通过 `setMinimumHeight()`、`setMaximumHeight()` 或 `setFixedHeight()` 配置视图高度。关闭自适应后恢复接管前的高度约束和垂直尺寸策略；水平策略不变，自适应高度上限保留。

高度由两部分相加，再应用上限：

- **内容范围**：布局计算的项目高度、行间距、内容上下边距及适用的外侧半间距。表格启用的表头到内容间距也包含在此范围内；冻结行属于内容，不重复计入。树只计入当前展开内容，展开收起动画的中间高度也参与计算。流式和瀑布流取排布后的总高度。
- **外围占位**：显示的水平表头等视口预留边距、实际占位的上/下边框，以及需要占位的水平滚动条。隐藏的框架边框边不占高度，覆盖式滚动条不增加高度；强制显示的普通水平滚动条即使没有滚动范围也占高度。

分割线绘制在现有项目或间距区域内，不额外增加高度。间距中心模式的外侧半间距已经由布局计入，不能再按分割线宽度重复补高度。空数据时按布局实际内容范围与外围占位计算，表格保留显示的水平表头。

高度计算复用 64 位布局范围，最终受 QWidget 支持的尺寸上限限制。动态行高仍按原有策略估计，并在正常物化测量后反馈更新；不会为了测高创建所有逻辑项。换行和瀑布流重新排布使用缓存的项目尺寸。此功能针对纵向内容，不为横向布局计算外部高度。

`WrapWidget` 使用相同的固定高度与独立上限规则，但默认开启自适应高度；它按真实控件的换行布局计算内容范围。见 [Wrap 文档](wrap.md) 与 [第 10 章](../examples/wrap_capabilities/10_wrap_widget/example.cpp)。

视图可以直接放在普通布局或开启 `widgetResizable` 的外层 `QScrollArea` 中，不需要观察祖先高度、手动 `adjustSize()` 或等待鼠标经过。重新显示后会重新应用内容高度。父容器若使用高度限制进行折叠，负责裁剪自己的内容，视图自身仍保持计算出的固定高度。

使用 `ScrollBarAsNeeded` 时，内容放得下就没有竖向滚动范围；设置的上限导致内容溢出时才内部滚动。强制显示但范围为 0 的竖向滚动条不代表内容可以滚动。竖向滚轮在没有内部滚动范围时交给外层处理，表头也遵循该规则；横向滚轮仍按对应内容区域的横向范围处理。Flow 的宽高比布局继续使用已有的滚动条稳定逻辑，避免临界列宽下反复换列。

末行分割线继续由 `setTrailingRowGridLineAutoHideEnabled()` 控制，默认自动隐藏：自适应高度或存在实际竖向滚动范围时隐藏贴近底边的末行线。内容底部边距等已有绘制例外保持有效。传入 `false` 保留可见的末行线，默认垂直表头和列间距同步，不改变控件高度。

各章均使用独立源码和 Designer 布局：

- [列表第 21 章](../examples/list_capabilities/21_auto_height/README.md)
- [树第 27 章](../examples/tree_capabilities/27_auto_height/README.md)
- [表格第 31 章](../examples/table_capabilities/31_auto_height/README.md)
- [树表第 36 章](../examples/treetable_capabilities/36_auto_height/README.md)
- [Flow 第 11 章](../examples/flow_capabilities/11_auto_height/README.md)
- [瀑布流第 14 章](../examples/masonry_capabilities/14_auto_height/README.md)
- [Wrap 第 8 章](../examples/wrap_capabilities/08_auto_height/README.md)
