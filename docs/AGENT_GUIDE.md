# VIV agent capability routing

Use this index to integrate VIV capabilities into an application before writing equivalent custom behavior.
All paths are relative to the VIV dependency source root. Start with a requirement keyword, then follow its API and example links.

## Retrieval protocol
1. Classify the layout; consult the view matrix below. Do not assume VIV implements QAbstractItemView APIs.
2. Search this file by requirement or exact symbol. Read the listed header, then the smallest independent example.
3. Read base APIs too: every virtual content view derives from VirtualItemView; TreeTable also inherits Table.
4. Before custom hover/selection paint, timer animation, editor stacks, popup cancellation filters, wheel forwarding, pooling, frozen clones, manual height or column geometry: check the routes below.
5. Reuse a public setter/adapter first. Use custom drawing/layout only for behavior not covered by the public configuration or adapter callbacks.
6. Apply the documented layout and lifecycle constraints. Implementation links help clarify behavior; do not call private *_p.h helpers from application code.
7. Query `docs/api/data/public-api.json` by header/type: entries provide signature, symbols, access, line and documentation (zh/en, defaults, examples, related capabilities). Struct documentation is on the type; nested structs/enums carry documentation on their entry. documentation.members explains each field/enumerator. Read only the types relevant to the requirement.
8. Offline reference: `docs/api/index.html`, with Chinese/English switching, keyword search and per-overload function links.

## View matrix
- VirtualItemView | base=QAbstractScrollArea | header=include/virtualitemviews/virtualitemview.h | 模型、选择、滚动、复用、状态动画、编辑、拖放、背景边框和自动高度的共同入口。先查这里，再查具体视图。
- VirtualListView | base=VirtualItemView | header=include/virtualitemviews/virtuallistview.h | 每个模型项一个 QWidget；支持可变行高、状态范围、冻结行和拖动让位。
- VirtualTableView | base=VirtualItemView | header=include/virtualitemviews/virtualtableview.h | 列宽、表头、冻结区域、合并和排序；可选择一行一个控件或一个单元格一个控件。
- VirtualTreeView | base=VirtualItemView | header=include/virtualitemviews/virtualtreeview.h | 真实节点展开动画、缩进、branch、层级拖放；不支持冻结行。
- VirtualTreeTableView | base=VirtualTableView | header=include/virtualitemviews/virtualtreetableview.h | 组合表格列布局与树节点展开；支持冻结列，不支持冻结行。展开后的视图行号不是 QModelIndex::row()。
- VirtualFlowTableView | base=VirtualItemView | header=include/virtualitemviews/virtualflowtableview.h | 模型行是项目；显示列由视口宽度决定。统一格高或宽高比，大量等尺寸卡片优先选择此视图。
- VirtualMasonryView | base=VirtualItemView | header=include/virtualitemviews/virtualmasonryview.h | 按最短列排布，项目高度独立；适用于图片卡片。固定高度或统一比例且项目很多时优先考虑 Flow。
- VirtualWrapView | base=VirtualItemView | header=include/virtualitemviews/virtualwrapview.h | 项目宽度独立、统一高度、按模型顺序换行；虚拟化显示，初始布局与宽度变化重排为 O(N)。
- WrapWidget | base=QScrollArea | header=include/virtualitemviews/wrapwidget.h | 直接 addWidget，不需要模型或 adapter，不复用。默认自动高度开启，配合最大高度内部滚动。
- WrapLayout | base=QLayout | header=include/virtualitemviews/wraplayout.h | 实现 heightForWidth、统一高度和行对齐；自身没有滚动条，限高滚动使用 WrapWidget。
- VirtualHeaderView | base=QWidget + HeaderViewInterface | header=include/virtualitemviews/virtualheaderview.h | 表格默认表头。常规样式经 horizontalHeader()/verticalHeader() 配置，自定义内容换 HeaderWidgetAdapter。

## Capability routes

### model | 模型 / 根节点 / 代理模型 / 更新通知
api: setModel model setRootIndex rootIndex
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualflowtableview.h include/virtualitemviews/virtualmasonryview.h include/virtualitemviews/virtualwrapview.h
rules: 使用 QAbstractItemModel 与正常的 dataChanged、begin/endInsertRows、removeRows、moveRows、layoutChanged/reset 通知驱动更新；视图不持有业务数据。简单模型直接填充 QStandardItemModel。代理模型索引属于代理，不可把源模型索引直接传入视图；setRootIndex 显示其子项，Table 没有该入口。切换模型后重新核对 root 和 selectionModel。
impl: src/core/virtualitemview.cpp src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/models.md
examples: examples/flow_capabilities/06_model_root/example.cpp examples/list_capabilities/05_model_updates/example.cpp examples/masonry_capabilities/09_model_root/example.cpp examples/table_capabilities/05_model_updates/example.cpp examples/tree_capabilities/05_model_updates/example.cpp examples/treetable_capabilities/06_model_updates/example.cpp examples/wrap_capabilities/05_model_root/example.cpp examples/tree_capabilities/08_root_index/example.cpp examples/treetable_capabilities/09_root_index/example.cpp examples/tree_capabilities/21_sorting_filtering/example.cpp

### adapter | 控件复用 / 多控件类型 / 异步清理
api: setAdapter setTableAdapter setCellAdapter widgetType createWidget bindWidget unbindWidget estimatedSize visualStateChanged
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/widgetadapter.h include/virtualitemviews/tablewidgetadapter.h include/virtualitemviews/widgetrecycler.h
rules: 先实现 adapter，框架负责创建、复用和回收。createWidget 中连接一次信号；bindWidget 必须完整覆盖旧显示状态，事件中使用当前绑定索引。unbindWidget 停止旧索引的定时器、订阅、动画和异步图片请求；异步回调核对持久索引或任务代次。按 widgetType 分池，不要按滚动自行销毁重建整表。takeOwnership 默认 false，要保证 adapter 生命周期覆盖视图使用期间。
impl: src/recycler/widgetrecycler.cpp src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp
doc: docs/getting-started.md
examples: examples/list_capabilities/03_widget_types/example.cpp examples/table_capabilities/03_widget_types/example.cpp examples/tree_capabilities/03_widget_types/example.cpp examples/treetable_capabilities/04_widget_types/example.cpp examples/list_capabilities/02_row_widgets/example.cpp

### binding | 整行业务 payload / 深度 / 列 / branch 上下文
api: setItemDataRole ItemBindingContext indentationWidth branchWidth
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/types.h include/virtualitemviews/widgetadapter.h include/virtualitemviews/tablewidgetadapter.h include/virtualitemviews/itemeditoradapter.h
rules: ItemBindingContext 提供 index、rowData、logicalColumn、depth；cell 的整行业务数据从列 0 读取。负的 itemDataRole 禁用 payload 读取。树的 indentationWidth 是祖先缩进，branchWidth 是当前节点 branch 格占位，不是图标宽度，均为未裁剪的逻辑像素；TreeTable 仅逻辑列 0 有这两个宽度。
impl: src/core/virtualitemview.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/interaction.md
examples: examples/list_capabilities/04_binding_context/example.cpp examples/table_capabilities/04_binding_context/example.cpp examples/tree_capabilities/04_binding_context/example.cpp examples/treetable_capabilities/05_binding_context/example.cpp

### materialization | RowWidgets / CellWidgets / 物化成本
api: setMaterializationMode setTableAdapter setCellAdapter layoutRowWidget estimatedSize cellWidget materializedCellCount
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/tablewidgetadapter.h
rules: RowWidgets 配 TableWidgetAdapter，通过 layoutRowWidget/TableRowLayoutContext 布置行内列控件；使用 ColumnHost 子控件时框架自动定位，可省略布局回调。CellWidgets 配 CellWidgetAdapter，由框架定位每个 cell。行模式不能只传普通 WidgetAdapter。物化模式与 SelectRows/SelectItems 是独立维度，整行高亮不要求整行物化。CellWidgets 的 QWidget 数随可见行数×可见列数增长；先衡量控件数量和 adapter 工作量再考虑定制布局。
impl: src/widgets/virtualtableview.cpp src/widgets/columnhost.cpp
doc: docs/table.md
examples: examples/table_capabilities/01_row_widgets/example.cpp examples/table_capabilities/02_cell_widgets/example.cpp examples/treetable_capabilities/02_row_widgets/example.cpp examples/treetable_capabilities/03_cell_widgets/example.cpp

### height | 动态行高 / 测量 / 显式覆盖
api: setItemHeightMode setUniformItemHeight setEstimatedItemHeight setAutoMeasureItemHeight setRowSizePolicy setRowHeight clearRowHeight
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h
rules: Uniform 统一高度；Uniform 高度为 0 且开启测量时按首个物化项适配。Variable 使用估值并在物化后测量，不能为了 auto height 预创建全部项目。Table 的显式 setRowHeight 默认优先于测量，拖动行高也会形成显式值；恢复动态行为用 clearRowHeight。列宽变化会影响换行高度，选择合适 RowSizePolicy。Flow/Masonry/Wrap 使用各自专用尺寸 API，不可套用内核行高配置。
impl: src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/index/sizeindex.cpp
doc: docs/auto-height.md
examples: examples/list_capabilities/06_item_heights/example.cpp examples/tree_capabilities/06_item_heights/example.cpp examples/table_capabilities/06_row_heights/example.cpp examples/treetable_capabilities/07_row_heights/example.cpp

### auto-height | 自适应高度 / 嵌套滚动 / 最大高度
api: setAutoHeightEnabled setAutoHeightMaximumHeight
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/wrapwidget.h
rules: 虚拟视图默认关闭，WrapWidget 默认开启。开启后内部 setFixedHeight，垂直 QSizePolicy::Fixed；上限 0 表示不限，不受父布局可用高度压缩。父容器放不下由业务安排外层滚动。开启时用 setAutoHeightMaximumHeight，不与 QWidget 的 setFixedHeight/setMaximumHeight 混用；关闭恢复原约束。高度包括内容、间距、适用半间距、表头和实际占位边框/横向滚动条；分割线不额外占高。没有内部滚动范围时滚轮交给外层，表头也同样处理。
impl: src/core/virtualitemview.cpp src/widgets/wrapwidget.cpp src/core/wheeleventforwarding_p.h
doc: docs/auto-height.md
examples: examples/flow_capabilities/11_auto_height/example.cpp examples/list_capabilities/21_auto_height/example.cpp examples/masonry_capabilities/14_auto_height/example.cpp examples/table_capabilities/31_auto_height/example.cpp examples/tree_capabilities/27_auto_height/example.cpp examples/treetable_capabilities/36_auto_height/example.cpp examples/wrap_capabilities/08_auto_height/example.cpp examples/wrap_capabilities/10_wrap_widget/example.cpp

### selection | 选择 / 多选 / 当前项 / 表头选择
api: setSelectionMode setSelectionBehavior setSelectionModel setCurrentIndex currentIndex selectionModel setHorizontalHeaderSelectionEnabled setVerticalHeaderSelectionEnabled
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h
rules: 默认 ExtendedSelection + SelectRows。SelectionMode 控制用户选择手势，程序批量选择通过 QItemSelectionModel::select(QItemSelection, flags)，不要循环 setCurrentIndex 代替范围选择。ExtendedSelection 使用 Ctrl/Shift。SelectRows 的 hover/selected 整行，SelectItems 按 cell，和物化模式无关。当前项不等于所有选中项；屏外选择保留在 selectionModel，widgetForIndex 可为空。默认行表头点击选行开启、列表头点击选列关闭；列选择不会把所有行号当作整行已选中。
impl: src/core/virtualitemview.cpp src/core/rowselection_p.h src/core/columnselectioncache_p.h src/widgets/virtualtableview.cpp
doc: docs/interaction.md
examples: examples/list_capabilities/10_selection/example.cpp examples/table_capabilities/12_selection/example.cpp examples/treetable_capabilities/17_selection/example.cpp

### states | hover / selected 默认绘制和动画
api: visualState setVisualStateBackgroundVisible setHoverBackgroundColor setSelectedBackgroundColor setVisualStateAnimationDuration setVisualStateAnimationEasingCurve
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualflowtableview.h include/virtualitemviews/virtualmasonryview.h include/virtualitemviews/virtualwrapview.h
rules: 内置状态背景默认开启，动画默认 180ms OutCubic；业务控件保持透明并在 visualStateChanged 中更新按钮/复选框的显示即可。除非完全自定义外观，不要自己维护 hoveredIndex、动画或重画默认背景。原生拖动期间框架暂停普通 hover，结束后重新计算；不改变业务勾选数据。Flow/Masonry/Wrap 的状态圆角入口是 setStateBackgroundCornerRadius，List/Tree/Table 是 setVisualStateCornerRadius。
impl: src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp
doc: docs/interaction.md
examples: examples/flow_capabilities/04_visual_states/example.cpp examples/list_capabilities/08_visual_states/example.cpp examples/masonry_capabilities/06_visual_states/example.cpp examples/table_capabilities/10_visual_states/example.cpp examples/tree_capabilities/13_visual_states/example.cpp examples/treetable_capabilities/15_visual_states/example.cpp examples/wrap_capabilities/03_visual_states/example.cpp examples/list_capabilities/09_state_content/example.cpp examples/table_capabilities/11_state_content/example.cpp examples/tree_capabilities/14_state_content/example.cpp examples/treetable_capabilities/16_state_content/example.cpp

### state-extent | 状态范围 / 命中范围 / 固定高度 / padding
api: setVisualStateBackgroundExtent setVisualStateBackgroundMargins setVisualStateBackgroundHeight setRowGridLineExtent
headers: include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h
rules: List: FullWidth/WidgetOnly。Tree: NodeOnly/NodeAndIcon/FullWidth/WidgetOnly/WidgetAndIcon；含 Icon 模式只有实际显示 branch 图标时才扩展。Margins 和 Height 只改变状态绘制及鼠标命中，不改变布局、键盘和程序选择；branch 展开点击单独处理。TreeTable 固定按表格行区域绘制，不提供 Tree 的这些 extent 设置，不能复制 Tree 的节点裁剪策略到树表。
impl: src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/tree.md
examples: examples/tree_capabilities/13_visual_states/example.cpp examples/list_capabilities/08_visual_states/example.cpp

### background | 背景 / 项底色 / 透明 / QSS
api: setBackgroundVisible setBackgroundColor backgroundBrush setItemBackgroundColor setSectionBackgroundColor setSpacingBackgroundColor
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/headerview.h include/virtualitemviews/virtualheaderview.h
rules: 整体默认 QPalette::Base，项底色默认透明。View 底色→viewport QSS 背景→项与状态层；setBackgroundVisible(false) 关闭主体两层，不控制表头和显式间距色。QSS 使用 vivItemView/vivViewport/vivHorizontalHeader/vivVerticalHeader/vivHeaderSection/vivHeaderCorner 对象名，不使用 QHeaderView::section。QSS 背景由 Qt 画刷驱动，不解析 stylesheet 字符串。框架边框/圆角使用专用接口；不要假定自绘部件支持所有 QSS border/image 属性。 编辑器、冻结区域和动画的纯裁剪宿主，以及 ColumnHost/Wrap 内容层保持透明，屏蔽外层 QSS 的背景和边框；宿主透明规则不匹配业务子控件。
impl: src/core/virtualitemviewframe.cpp src/core/stylesheetbackground_p.h src/core/viewbackground_p.h src/widgets/styledheadersection.cpp
doc: docs/stylesheets.md
examples: examples/flow_capabilities/10_colors/example.cpp examples/list_capabilities/22_colors/example.cpp examples/masonry_capabilities/13_borders_colors/example.cpp examples/table_capabilities/32_colors/example.cpp examples/tree_capabilities/28_colors/example.cpp examples/treetable_capabilities/37_colors/example.cpp

### frame | 外边框 / 四边开关 / 抗锯齿圆角
api: setFrameCornerRadius setFrameBorderWidth setFrameBorderColor setFrameBorderEdges setFrameBorderVisible
headers: include/virtualitemviews/virtualitemview.h
rules: 框架提供外边框与内容圆角合成，不必对子控件 setMask 或截图遮罩。隐藏的边不预留占位；外框圆角和 hover 圆角是两种不同配置。末行/末列分割线会与边框相邻，先检查自动隐藏与实际滚动范围，不要重复绘制外框补线。
impl: src/core/virtualitemviewframe.cpp src/core/pixelalignedlines_p.h
doc: docs/stylesheets.md
examples: examples/list_capabilities/20_borders/example.cpp examples/table_capabilities/30_borders/example.cpp examples/tree_capabilities/26_borders/example.cpp examples/treetable_capabilities/35_borders/example.cpp examples/masonry_capabilities/13_borders_colors/example.cpp

### spacing | 间距 / 中心分割线 / 双侧分割线 / 半间距
api: setRowSpacing setColumnSpacing setGridLinePlacement setSpacingBackgroundColor setVerticalSpacingLineThroughRowSpacing setHorizontalSpacingLineThroughColumnSpacing
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualflowtableview.h include/virtualitemviews/virtualmasonryview.h
rules: 默认 SpacingCenter，在间距中心画线，并在支持的网格布局外围补半间距；SpacingEdges 在两侧画线。间距 0 都回退到单一共享边界。Center 始终穿过交叉间距，不能关闭；Edges 才允许配置穿过与否。内容 margins 与项目 spacing 不是同一层。冻结区域交界将间距分成两半，由冻结分割线标识边界，不应业务重复补线。Wrap 的水平间距叫 setItemSpacing，没有表格网格线 API。
impl: src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/layout/flowtablelayout.cpp src/layout/masonrylayout.cpp src/core/pixelalignedlines_p.h
doc: docs/table-layout-guide.md
examples: examples/table_capabilities/09_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp examples/flow_capabilities/03_spacing_grid/example.cpp examples/masonry_capabilities/03_spacing_grid/example.cpp

### grid-line-range | 分割线深度过滤 / 整线四向边距
api: setGridLineMargins gridLineMargins setGridLineMarginReference gridLineMarginReference setRowGridLineDepths rowGridLineDepths setRowVisualStateMargins rowVisualStateMargins
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualflowtableview.h include/virtualitemviews/virtualmasonryview.h
rules: gridLineMarginReference 默认 Viewport，Content 使用完整内容端点，未滚到末尾不截短屏幕边缘。List/Tree/Table/TreeTable 的 setRowVisualStateMargins(left,right) 默认 0，仅调整行 hover/select 绘制，不改变命中；表格仅 SelectRows 生效，显示且开启状态背景的垂直表头与内容共享端点，边距只扣一次；隐藏或关闭表头状态时退回内容范围。gridLineMargins 默认全零，横线使用 left/right，竖线使用 top/bottom；计算完整线范围后应用，正数缩短、负数伸长，不是每个 cell 的 padding。表头与内容共享范围；外框、冻结分隔线和拖放指示器独立。树和树表的深度集合默认空（全部），{0} 只画完整主节点子树之后的边界，{0,1} 还画一级子节点边界。深度相对于显示根；负值忽略，末行自动隐藏仍生效。
impl: src/core/virtualitemview.cpp src/core/pixelalignedlines_p.h src/widgets/virtualtreeview.cpp src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp
doc: docs/table-layout-guide.md
examples: examples/tree_capabilities/12_row_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp

### header-gap | 表头与内容间距 / 穿过表头 / 状态穿过间距
api: setHeaderContentSpacingEnabled setVerticalGridLinesThroughHeader setHorizontalGridLinesThroughHeader setHeaderSpacingBackgroundColor setVisualStateThroughColumnSpacing setVisualStateThroughRowSpacing
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h
rules: header-content 开关默认 false，Horizontal 对应水平表头下方 rowSpacing，Vertical 对应垂直表头右侧 columnSpacing。开关不创建非零 spacing；启用使用全间距，未启用时 Center 保留原外侧半间距、Edges 为 0。穿过表头的行/列分割线默认开启，和穿过交叉间距是不同开关。整行状态跨列间距默认开启；跨上下半间距默认开启且只在 Center 生效，SelectItems 仍按 cell。
impl: src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp
doc: docs/headers.md
examples: examples/table_capabilities/09_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp

### grid | 分割线样式 / 最后一行 / 冻结分隔线
api: setRowGridLinesVisible setRowGridLineColor setRowGridLineWidth setHorizontalGridLinesVisible setVerticalGridLinesVisible setTrailingRowGridLineAutoHideEnabled setItemPaneSeparatorStyle setPaneSeparatorStyle
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualflowtableview.h include/virtualitemviews/virtualmasonryview.h
rules: List/Tree 用 RowGridLine 接口；Table/Flow/Masonry 用 Horizontal/VerticalGridLine 接口。末行自动隐藏默认 true：自动高度或实际竖向溢出时按末行边界规则处理，内容底边距存在时仍有例外；false 请求保留可见末行线，不增加高度。强制显示但 range=0 的滚动条不等于可滚动。普通分割线与冻结线分别配置，PaneSeparatorStyle 支持颜色、粗细、线型；查看具体 API 单位，线宽不可一律再乘 DPR。
impl: src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp
doc: docs/auto-height.md
examples: examples/table_capabilities/16_frozen_areas/example.cpp examples/flow_capabilities/11_auto_height/example.cpp examples/list_capabilities/21_auto_height/example.cpp examples/masonry_capabilities/14_auto_height/example.cpp examples/table_capabilities/31_auto_height/example.cpp examples/tree_capabilities/27_auto_height/example.cpp examples/treetable_capabilities/36_auto_height/example.cpp examples/wrap_capabilities/08_auto_height/example.cpp

### gap-widget | 间距内自定义控件
api: setRowSpacingFactory setColumnSpacingFactory setHeaderColumnSpacingFactory
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtualtableview.h
rules: 工厂与 binder 为可见间距创建/刷新可复用控件；行间距按行、列间距按列管理，不是每个交叉空格一个 QWidget。普通颜色用 setSpacingBackgroundColor 更轻。Flow/Masonry/Wrap 不支持内核行间距控件；不要因继承了 setter 就假定可用。
impl: src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp
doc: docs/table-layout-guide.md
examples: examples/table_capabilities/09_spacing/example.cpp examples/list_capabilities/07_row_spacing/example.cpp

### columns | 列宽 / 比例 / FitViewport / 隐藏换序
api: setColumnWidth setColumnMinimumWidth setColumnMaximumWidth setColumnStretchFactor setStretchLastColumn setColumnResizeMode setColumnHidden moveColumn setColumnResizeEnabled
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/headergeometry.h
rules: 默认 Independent。比例分配的是扣除固定列和间距后的剩余宽度，factor=0 保持自身宽度；此模式下拖动任意列或 setColumnWidth 会清除全部比例和末列拉伸，故先设固定宽度再设 stretch factors。FitViewport 根据当前比例让列宽总和适配视口，拖动边缘在相应侧按比例补偿；受最小/最大限制。拖动改宽默认关闭；模型列号、视觉顺序和隐藏状态不要混用。
impl: src/layout/headergeometry.cpp src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp
doc: docs/table-layout-guide.md
examples: examples/table_capabilities/07_columns/example.cpp examples/table_capabilities/08_stretch_columns/example.cpp examples/treetable_capabilities/13_stretch_columns/example.cpp

### headers | 默认表头 / 样式 / section / corner
api: horizontalHeader verticalHeader setAdapter setBackgroundColor setSectionBackgroundColor setForegroundColor setFont setTextAlignment setContentPadding setHeaderCornerWidget
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualheaderview.h include/virtualitemviews/headerview.h include/virtualitemviews/headerwidgetadapter.h include/virtualitemviews/headersection.h include/virtualitemviews/styledheadersection.h include/virtualitemviews/labelheaderview.h include/virtualitemviews/headercornerwidget.h
rules: 默认 VirtualHeaderView，样式优先通过 HeaderViewInterface 配置。容器 backgroundColor 改整条底色，sectionBackgroundColor 只改 section，spacingBackgroundColor 改间距；无效 QColor 恢复继承，透明色是显式透明。模型 headerData 的 ForegroundRole/FontRole/TextAlignmentRole 可覆盖默认文字样式。HeaderSection 管结构，StyledHeaderSection 管外观，LabelHeaderSection 管文字；复杂内容换 HeaderWidgetAdapter，无需替换整个表头。默认 corner 跟水平表头，替换 corner 的 QWidget 由 view 接管。
impl: src/widgets/virtualheaderview.cpp src/widgets/headersection.cpp src/widgets/styledheadersection.cpp src/widgets/labelheaderview.cpp src/widgets/headercornerwidget.cpp
doc: docs/headers.md
examples: examples/table_capabilities/19_custom_header/example.cpp examples/treetable_capabilities/24_custom_header/example.cpp examples/table_capabilities/32_colors/example.cpp

### header-state | 表头同步 hover / selected / 圆角
api: setHeaderVisualStateBackgroundVisible headerVisualState setVisualStateCornerRadiusIncludesHeaders
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualheaderview.h include/virtualitemviews/headerview.h
rules: 表头状态背景可以按轴独立开关，和点击选行/选列是不同配置。默认跟内容动画进度同步。状态圆角默认扩展到表头：SelectRows 行号仅左圆角、内容仅右圆角；SelectItems 行号和单元格各自四角，列头四角。自定义 section 应使用框架状态进度，不能只看 selected 布尔值立即换背景。
impl: src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp src/widgets/styledheadersection.cpp
doc: docs/headers.md
examples: examples/table_capabilities/12_selection/example.cpp examples/table_capabilities/19_custom_header/example.cpp

### header-reorder | 表头拖动换序 / 行号拖动 / 动画
api: setColumnDragEnabled setVerticalHeaderDragEnabled setVerticalHeaderResizeEnabled setHeaderAnimationEnabled setHeaderAnimationDuration setColumnFollowsHeaderVisual setRowFollowsHeaderVisual rowMoveRequested
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualheaderview.h
rules: 列换序改变 HeaderGeometry 的视觉顺序；行号换序需要模型 moveRows。行高/列宽拖动默认关闭，按需显式开启。表头换序动画与内容原生拖放让位是两套配置。verticalHeaderDrag 在无模型时可创建 ReorderableTableModel；业务已有模型时必须实现对应移动能力。树表只允许符合层级约束的移动。
impl: src/widgets/virtualheaderview.cpp src/widgets/virtualtableview.cpp src/core/reorderabletablemodel.cpp
doc: docs/headers.md
examples: examples/table_capabilities/22_row_reordering/example.cpp examples/treetable_capabilities/27_row_reordering/example.cpp

### frozen | 冻结行列 / 多 pane / 多滚动组
api: setFrozenRows setFrozenBottomRows setFrozenColumns setFrozenRightColumns setPanes setHorizontalOffset primaryScrollGroup
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/tablepane.h include/virtualitemviews/itempane.h
rules: List/Table 支持上下冻结行；Tree/TreeTable 不支持冻结行，TreeTable 保留冻结列。Flow/Masonry/Wrap 不支持行 pane。表格冻结列共用 HeaderGeometry，不复制宽度/顺序；左右集合冲突时左侧优先。高级 setPanes 指定列集合和滚动组，同组共享 offset，首个可滚动 pane 的组是主组；非主组用带 group 参数的接口。不要为冻结区再创建一个独立表格同步数据。
impl: src/layout/tablepane.cpp src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp
doc: docs/table-layout-guide.md
examples: examples/list_capabilities/14_frozen_rows/example.cpp examples/table_capabilities/16_frozen_areas/example.cpp examples/table_capabilities/17_panes/example.cpp examples/treetable_capabilities/21_frozen_columns/example.cpp examples/treetable_capabilities/22_panes/example.cpp

### spans | 合并单元格
api: setSpanProvider setSpan removeSpan clearSpans anchorIndex isSpanCovered spanRect cellRect
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/tablespan.h include/virtualitemviews/tablewidgetadapter.h
rules: 内置 TableSpanMap 或自定义 TableSpanProvider 提供跨度；查询/绘制/命中使用 anchorIndex 与 cellRect。跨度裁剪到模型和 anchor 所在 pane，不跨 pane。被覆盖 cell 的 cellRect 为空。RowWidgets 通过 TableRowLayoutContext 的 spans 布局业务控件；表格拖动让位在 span provider 存在等不支持条件下退回指示器。
impl: src/layout/tablespan.cpp src/widgets/virtualtableview.cpp
doc: docs/table-layout-guide.md
examples: examples/table_capabilities/23_spans/example.cpp examples/treetable_capabilities/28_spans/example.cpp

### sort-persist | 排序 / 过滤 / 布局保存恢复
api: setSortingEnabled sortByColumn setSortIndicator saveHeaderState restoreHeaderState
headers: include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/headergeometry.h
rules: 排序需要模型 sort 实现或 QSortFilterProxyModel；仅画 sort indicator 不等于数据已排序。saveHeaderState 保存列宽、顺序、隐藏、比例以及 pane/冻结配置；不保存业务数据或任意视图开关，ColumnResizeMode 是独立配置。restore 先验证再应用，检查 bool 返回值。树的展开状态与业务持久化需求单独处理，不要假定 header state 保存树展开。
impl: src/widgets/virtualtableview.cpp src/layout/headergeometry.cpp
doc: docs/header-persistence.md
examples: examples/table_capabilities/20_header_persistence/example.cpp examples/table_capabilities/21_sorting/example.cpp examples/treetable_capabilities/25_header_persistence/example.cpp examples/treetable_capabilities/26_sorting_filtering/example.cpp

### tree-expansion | 树展开 / 收起 / 渐变 / 懒加载
api: expand collapse setExpanded toggleExpanded expandRecursively collapseAll setExpansionAnimationEnabled setExpansionAnimationDuration setExpansionFadeEnabled visibleRowCount
headers: include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/treevisibilityindex.h
rules: 框架使用真实布局动画并可渐变，fade 默认开启；无需截图遮罩。collapseAll 是批量收起入口，不承诺逐节点动画。visibleRowCount 是展开后的扁平序列，模型 row 仍相对父节点。懒加载由模型 canFetchMore/fetchMore 配合展开；使用标准结构通知，不手工重建可见节点 QWidget。切换根、模型变化由框架维护可见映射。
impl: src/index/treevisibilityindex.cpp src/widgets/treeexpansiontransition_p.h src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/tree.md
examples: examples/tree_capabilities/07_expansion/example.cpp examples/tree_capabilities/22_lazy_loading/example.cpp examples/treetable_capabilities/08_expansion/example.cpp examples/treetable_capabilities/31_lazy_loading/example.cpp

### branch | branch 图标 / 旋转 / 缩进 / 自绘分支
api: setIndentation setBranchAreaVisible setBranchIndicatorsVisible setExpandedBranchIcon setCollapsedBranchIcon setBranchIconSize setBranchIconColor setBranchIndicatorAlignment setBranchIndicatorPadding setBranchIndicatorRenderer
headers: include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/branchindicator.h
rules: 默认圆角等边三角形 10 逻辑像素。只设置展开/收起其中一个图标时随展开动画旋转 90°；两个都设置时直接切换，自定义 renderer 自行处理。隐藏 branchArea 去掉自身 branch 格但保留祖先缩进；仅隐藏 indicators 保留占位。Tree 的 setIndentLeafItems(false) 可省略全叶子兄弟组占位，TreeTable 没有该开关。常规大小/颜色/对齐/padding 用便捷接口，只有完整自绘分支/连接线才实现 BranchIndicatorRenderer。
impl: src/widgets/branchindicator.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/tree.md
examples: examples/tree_capabilities/09_indentation/example.cpp examples/tree_capabilities/10_branch_icons/example.cpp examples/tree_capabilities/11_branch_renderer/example.cpp examples/treetable_capabilities/10_indentation/example.cpp

### node-spacing | 节点独立上下间距
api: NodeRowSpacingRole NodeRowSpacingBelowRole NodeRowSpacingAboveRole setRowSpacing
headers: include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h
rules: 模型列 0 的 BelowRole 覆盖节点下方间距，缺失/负值回退 rowSpacing；AboveRole 增加节点上方间距，缺失/负值为 0。相邻节点的 below+above 可能叠加，末个可见节点没有尾部普通 gap，首节点没有普通 above gap；不要把两份 role 当作同一间距。
impl: src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/tree.md
examples: examples/tree_capabilities/12_row_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp

### drag | 原生拖放 / 手柄起拖 / 预览 / Move
api: setDragEnabled setAcceptDrops setDragStartMode canStartDrag startDrag DragOptions setDragDropActions setDefaultDropAction setMoveRemovesSourceRows
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/types.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h include/virtualitemviews/virtualwrapview.h include/virtualitemviews/virtualflowtableview.h include/virtualitemviews/virtualmasonryview.h
rules: setDragEnabled 开启拖出，QWidget::setAcceptDrops 开启接收；模型需 flags、mimeData、mimeTypes、canDropMimeData/dropMimeData 与支持的 actions。Automatic 按阈值启动；Manual 由手柄调用 startDrag(index[, options])，不需框架注册按钮。默认预览按 SelectRows/SelectItems，DragOptions 可改来源/区域/热点，默认热点在鼠标处。Move 是否删除源行必须与模型协议一致：模型已移动不得再删；默认不删除。框架负责拖动 pin、hover 暂停和结束恢复。
impl: src/core/virtualitemview.cpp src/core/dragpreview_p.h src/core/dragmoveselection_p.h
doc: docs/interaction.md
examples: examples/list_capabilities/15_drag_drop/example.cpp examples/table_capabilities/24_drag_indicator/example.cpp examples/wrap_capabilities/07_drag_drop/example.cpp examples/masonry_capabilities/11_drag_drop/example.cpp

### drag-feedback | 拖动指示器 / 让位动画 / 树末级歧义
api: setDropIndicatorShown setDropIndicatorColor setDropIndicatorWidth setDropIndicatorPenStyle setDragFeedbackMode setDragReorderAnimationDuration setDragReorderAnimationEasingCurve
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtuallistview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/virtualtreeview.h include/virtualitemviews/virtualtreetableview.h
rules: 支持的列表/树/表格默认 RowMoveAnimation；不满足内部单项 Move 等条件时自动退回指示器。动画只预览，成功 drop 后模型才改顺序。树单节点拖动先收起源节点，仅留单行占位；父同级/子同级末尾目标由缩进和允许目标描边区分，模型拒绝的目标不会为了画框而变成可投放。Flow/Masonry/Wrap 无专用行让位模式，不要套用 list 的枚举。
impl: src/core/rowdragpreview_p.h src/widgets/treedragtransition_p.h src/widgets/treedragplaceholder_p.h src/widgets/virtualtableview.cpp
doc: docs/interaction.md
examples: examples/list_capabilities/16_drag_animation/example.cpp examples/tree_capabilities/20_drag_animation/example.cpp examples/table_capabilities/25_drag_animation/example.cpp examples/treetable_capabilities/30_drag_animation/example.cpp

### cell-drop | cell 空白格放下 / 同时行间插入
api: setDropTargetMode DropTargetMode
headers: include/virtualitemviews/virtualtableview.h
rules: SelectItems 下选择 OnCell 或 BetweenRowsOrOnCell；组合模式 cell 上下四分之一做行间插入，中部投到 cell。OnCell 调模型时 parent=cell、row=column=-1，由模型决定填充/替换/拒绝。SelectRows 始终行间插入，TreeTable 保留层级节点投放规则，不能靠此模式把树节点当普通二维空格。
impl: src/widgets/virtualtableview.cpp
doc: docs/interaction.md
examples: examples/table_capabilities/26_cell_drop/example.cpp

### editor | 覆盖编辑器 / 单双击 / F2 / 按钮 / 自定义 role
api: setEditTriggers editTriggers edit createEditor setEditorData updateEditorGeometry commitEditorData destroyEditor commitEditor closeEditor editingIndex editorWidget
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/itemeditoradapter.h include/virtualitemviews/widgetadapter.h include/virtualitemviews/tablewidgetadapter.h
rules: 所有入口要求 ItemIsEnabled + ItemIsEditable。默认 DoubleClicked|EditKeyPressed(F2)，adapter 可按 index 重载触发方式；按钮/外部直接 edit(index)。默认 editor 为 null，业务实现 createEditor；默认数据经 Qt USER property↔EditRole，可重载提交到 UserRole。默认几何覆盖整格/项；无需 stack layout。框架处理焦点、Enter/Esc/Tab、校验和销毁；提交返回 false 保持编辑。编辑器不进入显示控件复用池，不应滚动即丢草稿。
impl: src/core/virtualitemviewediting.cpp src/core/itemeditoradapter.cpp src/widgets/virtualtableview.cpp
doc: docs/interaction.md
examples: examples/list_capabilities/23_editors/example.cpp examples/table_capabilities/33_editors/example.cpp examples/tree_capabilities/29_editors/example.cpp examples/treetable_capabilities/38_editors/example.cpp

### popup | Popup 编辑 / 坐标 / 统一取消 / 编辑覆盖范围
api: editRequest EditRequestContext cancelEditRequest closeEditor indentationWidth branchWidth
headers: include/virtualitemviews/itemeditoradapter.h include/virtualitemviews/virtualitemview.h include/virtualitemviews/types.h
rules: editRequest 返回 true 表示自行处理 Popup，false 回退 createEditor。context 含 binding、view、viewport、三个坐标系的完整 rect 和 finish 回调。正常确认/关闭后调用 request.finish，仅结束对应会话，不提交模型；业务自行写数据。框架在滚动、目标失效、模型/adapter 变化、隐藏等取消条件下调用 cancelEditRequest，业务在此关闭 Popup；外部 closeEditor 同样转发。Popup 时 editingIndex 有效、editorWidget=null、commitEditor=false。跨事件保留 QPointer/QPersistentModelIndex。树编辑可用 indentationWidth+branchWidth 裁掉装饰占位并 clamp 到 rect.width。
impl: src/core/virtualitemviewediting.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp
doc: docs/interaction.md
examples: examples/list_capabilities/23_editors/example.cpp examples/table_capabilities/33_editors/example.cpp examples/tree_capabilities/29_editors/example.cpp examples/treetable_capabilities/38_editors/example.cpp

### scroll | 滚动 / 定位 / 几何 / 命中 / 平滑滚动
api: scrollTo setVerticalOffset scrollByPixels setWheelScrollMode setWheelScrollPixels setWheelScrollItems indexAt visualRect widgetForIndex indexForWidget setHorizontalOffset
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/virtualtableview.h include/virtualitemviews/scrollmapper.h
rules: indexAt 输入 viewport 坐标，visualRect/cellRect 可在屏外仍有效；widgetForIndex/cellWidget 仅查询物化控件，空指针不表示模型项或选择丢失。长期保存项目用 QPersistentModelIndex。64 位 offset 通过 ScrollMapper 映射到滚动条；不要自己用 scrollbar int 作为巨大模型的逻辑偏移。无内部范围的滚轮交外层。smoothscroll 是可选依赖，仅检测到该库时编译/显示对应章节，不是核心内置 timer 动画接口。
impl: src/core/scrollmapper.cpp src/core/virtualitemview.cpp src/core/wheeleventforwarding_p.h src/widgets/virtualheaderview.cpp
doc: docs/interaction.md
examples: examples/flow_capabilities/07_scrolling/example.cpp examples/list_capabilities/12_scrolling/example.cpp examples/list_capabilities/19_smooth_scrolling/example.cpp examples/masonry_capabilities/10_scrolling/example.cpp examples/table_capabilities/14_scrolling/example.cpp examples/table_capabilities/29_smooth_scrolling/example.cpp examples/tree_capabilities/17_scrolling/example.cpp examples/tree_capabilities/25_smooth_scrolling/example.cpp examples/treetable_capabilities/19_scrolling/example.cpp examples/treetable_capabilities/34_smooth_scrolling/example.cpp examples/wrap_capabilities/06_scrolling/example.cpp examples/list_capabilities/11_item_events/example.cpp

### pin | 屏外保留 / 预加载 / 焦点 / 诊断
api: setOverscan setColumnOverscan setItemPinned pinWidget unpinWidget setMaxPinnedItems stats materializedItemCount createdWidgetCount pooledWidgetCount
headers: include/virtualitemviews/virtualitemview.h include/virtualitemviews/widgetrecycler.h include/virtualitemviews/virtualtableview.h
rules: overscan 增大意味着更多真实控件，CellWidgets 还乘以列数；先测再调。显式 pin 会保留屏外控件直到解除；拖动临时 pin 与业务 pin 独立，maxPinnedItems 是警告阈值而非自动回收上限。焦点/输入法等活动可能需要保留，不能为了降低统计数强删。状态和选择以模型为真源，不以物化控件数量为真源。
impl: src/core/virtualitemview.cpp src/recycler/widgetrecycler.cpp src/widgets/virtualtableview.cpp
doc: docs/accessibility-guide.md
examples: examples/list_capabilities/13_virtualization/example.cpp examples/table_capabilities/15_virtualization/example.cpp examples/tree_capabilities/18_virtualization/example.cpp examples/treetable_capabilities/20_virtualization/example.cpp examples/list_capabilities/17_pinning/example.cpp examples/table_capabilities/27_pinning/example.cpp examples/tree_capabilities/23_pinning/example.cpp examples/treetable_capabilities/32_pinning/example.cpp

### performance | 性能日志 / 卡顿 / 统计而非逐帧打印
api: setPerformanceLoggingEnabled setLifecycleLoggingEnabled lifecycleLog clearLifecycleLog stats
headers: include/virtualitemviews/virtualitemview.h
rules: 性能探测默认关闭；启用开关并通过 Qt 日志类别筛选 viv.performance.refresh、viv.performance.paint，详细阶段表用 viv.performance.details。按视图/类别每秒汇总，计入同步 adapter 回调；禁用开关或过滤类别时跳过对应计时。阶段可能包含子阶段，不能把嵌套耗时相加当总时间。先区分布局/物化/回收/业务 callback/paint，再优化；生命周期日志是另一开关，最多保留 400 条，不要在 bind 内每项输出日志。
impl: src/core/performanceprobe.cpp src/core/performanceprobe_p.h src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp
doc: docs/accessibility-guide.md
examples: examples/list_capabilities/18_diagnostics/example.cpp examples/table_capabilities/28_diagnostics/example.cpp examples/tree_capabilities/24_diagnostics/example.cpp examples/treetable_capabilities/33_diagnostics/example.cpp

### flow | Flow 等宽换列 / 固定高度 / 比例
api: setCellWidthRange setCellHeightMode setFixedCellHeight setCellAspectRatio columnCount displayRowCount displayRowForIndex displayColumnForIndex
headers: include/virtualitemviews/virtualflowtableview.h
rules: 显示列数随可用宽度计算，列均分铺满；不是多列模型的表格，不具备表头/冻结列 API。模型每行代表一个项目，换行不改索引身份。宽高比为 width/height；内核 setItemHeightMode 与自动 sizeHint 测量不支持，使用本视图 CellHeightMode。支持共享 auto height、状态、选择、拖放、分割线。
impl: src/widgets/virtualflowtableview.cpp src/layout/flowtablelayout.cpp
doc: docs/flow-table.md
examples: examples/flow_capabilities/02_cell_sizes/example.cpp examples/flow_capabilities/09_images/example.cpp

### masonry | Masonry 最短列 / 图片估值 / 高度更新
api: setColumnCount setCellWidthRange setItemHeightMode setFixedItemHeight setItemAspectRatio setItemEstimatedSize setItemEstimatedSizes setItemHeight invalidateItemSize setItemHeightAnimationEnabled
headers: include/virtualitemviews/virtualmasonryview.h
rules: 本视图 ItemHeightMode 是 Fixed/AspectRatio/AdapterEstimate，与基类枚举不同；禁止套用内核自动测高。图片加载前给估值，结果到达后用 setItemHeight 或 invalidateItemSize 更新；批量估值用 setItemEstimatedSizes 减少重排。高度动画默认关闭。空间中的前后邻居不一定是模型相邻行，拖放仍服从模型目标与 flags，不是任意视觉位置都能接受。
impl: src/widgets/virtualmasonryview.cpp src/layout/masonrylayout.cpp
doc: docs/masonry.md
examples: examples/masonry_capabilities/04_height_modes/example.cpp examples/masonry_capabilities/05_height_updates/example.cpp examples/masonry_capabilities/12_images/example.cpp

### wrap | Wrap 不等宽标签 / 自然尺寸 / 行对齐
api: setFixedItemHeight setItemWidthRange setItemWidth setItemWidths clearItemWidth invalidateItemWidths setItemSpacing setRowSpacing setLineAlignment
headers: include/virtualitemviews/virtualwrapview.h include/virtualitemviews/wraplayout.h include/virtualitemviews/wrapwidget.h
rules: VirtualWrapView 宽度优先级：显式覆盖→SizeHintRole→adapter estimate→100px，再按范围/行宽裁剪；批量 setItemWidths 避免逐项重排。水平 itemSpacing 与垂直 rowSpacing 可分别设置，统一 item height；SelectRows 仍选择模型项，不会选整条显示行。WrapLayout 按真实子控件 sizeHint/约束排布，隐藏控件通常不占位；WrapWidget 接管控件所有权，takeWidget 交回调用者，clear 删除。
impl: src/widgets/virtualwrapview.cpp src/layout/wrapitemlayout.cpp src/layout/wraplayout.cpp src/widgets/wrapwidget.cpp
doc: docs/wrap.md
examples: examples/wrap_capabilities/02_geometry/example.cpp examples/wrap_capabilities/09_wrap_layout/example.cpp examples/wrap_capabilities/10_wrap_widget/example.cpp

### accessibility | 无障碍 / 虚拟索引映射
api: installAccessibilityFactory removeAccessibilityFactory
headers: include/virtualitemviews/accessibility.h include/virtualitemviews/virtualitemview.h
rules: QApplication 创建后调用 installAccessibilityFactory 安装虚拟项目的无障碍桥接；业务控件仍需正确 accessibleName、角色和可操作语义。先阅读 accessibility.h 与专门指南，不要为每个逻辑项目常驻一个隐藏 QWidget 来暴露语义。
impl: src/widgets/accessibility.cpp
doc: docs/accessibility-guide.md
examples:

### styled-table | 扩展自绘状态形状 / StyledTableView
api: StyledTableView StyledTableRowWidget StyledTableCellHost visualStateBackgroundPath paintVisualStateBackground setBackgroundCoversGridLines
headers: include/virtualitemviews/styledtableview.h include/virtualitemviews/virtualtableview.h
rules: 普通 hover/selection、颜色和圆角直接用 VirtualTableView 的函数即可。只有自定义背景形状/绘制才考虑 StyledTableView 和配套 row/cell host；默认状态圆角为 6 逻辑像素。不要为了常规背景重复引入一套业务状态动画。
impl: src/widgets/styledtableview.cpp
doc: docs/table.md
examples:

### layout-kernel | 布局与尺寸索引扩展
api: LayoutPolicy ListLayout SizeIndex FixedSizeIndex BlockSizeIndex ScrollMapper TreeVisibilityIndex
headers: include/virtualitemviews/layoutpolicy.h include/virtualitemviews/listlayout.h include/virtualitemviews/sizeindex.h include/virtualitemviews/scrollmapper.h include/virtualitemviews/treevisibilityindex.h include/virtualitemviews/virtualitemview.h
rules: 这些公开基础类型服务于布局扩展和定位查询。正常业务接入通过具体视图和模型通知；不要同时手工操作 sizeIndex/resetItems 和模型结构来维护两份状态。ListLayout 持有 SizeIndex，内部行布局区分固定/动态尺寸；TreeVisibilityIndex 管理展开后的映射。此层没有“任意改内部布局后自动保持所有业务状态”的契约，扩展时逐项阅读源码。
impl: src/layout/listlayout.cpp src/index/sizeindex.cpp src/index/treevisibilityindex.cpp src/core/scrollmapper.cpp
doc: docs/models.md
examples:

## Public symbol inventory by declaring type
These are declarations, not a guarantee that all inherited configurations work on all layouts. Apply the capability limits above.
Nested types and overloaded signatures are retained in public-api.json. Public data members are indexed alongside methods.

### include/virtualitemviews/accessibility.h
AccessibleVirtualItem: AccessibleVirtualItem isValid object window rect role text setText state parent child childCount indexOfChild childAt focusChild interface_cast actionNames doAction keyBindingsForAction isSelected columnHeaderCells rowHeaderCells columnIndex rowIndex columnExtent rowExtent table index column rowModelIndex
AccessibleVirtualItemView: AccessibleVirtualItemView isValid object window rect role text setText state parent child childCount indexOfChild childAt focusChild interface_cast actionNames doAction keyBindingsForAction caption summary cellAt selectedCellCount selectedCells columnDescription rowDescription selectedColumnCount selectedRowCount columnCount rowCount selectedColumns selectedRows isColumnSelected isRowSelected selectRow selectColumn unselectRow unselectColumn modelChange view itemFor visibleIndexes visiblePosition applySelection
viv namespace: createAccessibleItemViewInterface installAccessibilityFactory removeAccessibilityFactory

### include/virtualitemviews/branchindicator.h
BranchIndicatorState: hasChildren hasSiblings adjoinsItem isExpanded cellDepth itemDepth isOpen isClosed isLeaf
BranchIndicatorRenderer: BranchIndicatorRenderer branchIcon branchIndicatorTargetRect branchIndicatorTargetRectF paintBranch paintBuiltinBranch
viv namespace: kBranchIndicatorSize kBranchIndicatorMargin

### include/virtualitemviews/global.h

### include/virtualitemviews/headercornerwidget.h
HeaderCornerWidget: HeaderCornerWidget

### include/virtualitemviews/headergeometry.h
ColumnGeometry: logicalIndex visualIndex contentX viewportX width hidden isValid
HeaderGeometry: HeaderGeometry orientation sectionCount storedSectionStateCount isUniform setSectionCount insertLogicalSections removeLogicalSections visibleSectionCount hiddenSectionCount isEmpty sectionSize storedSectionSize resizeSection ResizeMode resizeMode setResizeMode ResizeSnapshot resizeSnapshot resizeSectionFromEdge isSectionSizeExplicit clearExplicitSectionSize clearExplicitSectionSizes defaultSectionSize setDefaultSectionSize sectionSpacing setSectionSpacing setSectionSpacingOverrides setSparseSectionSpacingOverrides sectionSpacingAfter setEdgeSpacing leadingSpacing trailingSpacing minimumSectionSize setMinimumSectionSize maximumSectionSize setMaximumSectionSize sectionPosition sectionViewportPosition sectionAtOffset visualSectionAtOffset totalExtent logicalIndex visualIndex moveSection moveLogicalSections moveLogicalSectionSizes isSectionHidden setSectionHidden setAllSectionsHidden viewportOffset setViewportOffset maximumViewportOffset columnGeometry visibleVisualRange visibleVisualRangeFor visibleSectionsInVisualOrder orderRevision sortIndicatorSection sortIndicatorOrder setSortIndicator stretchLastSection setStretchLastSection stretchExtent setStretchExtent sectionStretchFactor setSectionStretchFactor hasStretchSections kStateMagic kStateVersion saveState restoreState sectionResized sectionMoved sectionVisibilityChanged sectionCountChanged sortIndicatorChanged stretchLastSectionChanged resizeModeChanged sectionStretchFactorChanged offsetChanged geometryChanged bulkGeometryChanged

### include/virtualitemviews/headersection.h
HeaderSection: HeaderSection logicalIndex setLogicalIndex headerView tableView orientation visualState
HeaderSectionAdapter: createSection bindSection unbindSection

### include/virtualitemviews/headerview.h
HeaderViewInterface: HeaderViewInterface setGeometryModel geometryModel headerWidget orientation setAdapter adapter setBackgroundVisible backgroundVisible setBackgroundColor backgroundColor setSectionBackgroundColor sectionBackgroundColor setSpacingBackgroundColor spacingBackgroundColor setForegroundColor foregroundColor setTextAlignment textAlignment setContentPadding contentPadding setFont font setLabelModel setSortInteractionEnabled setViewportOrigin setPaneFilter clearPaneFilter kFollowGeometryOffset setPaneOffset setSectionAnimationEnabled setSectionAnimationDuration setSectionMoveAnimated sectionVisualX hasVisualSectionGeometry setVisualGeometryCallback void refreshSectionLabels
viv namespace: headerSectionSeparatorColor

### include/virtualitemviews/headerwidgetadapter.h
HeaderWidgetAdapter: HeaderWidgetAdapter sectionType createSection setLabelModel setGeometryModel bindSection unbindSection

### include/virtualitemviews/itemeditoradapter.h
EditRequestContext: binding view viewport viewportRect viewRect globalRect void
EditTrigger: EditTrigger
ItemEditorAdapter: ItemEditorAdapter editTriggers editRequest cancelEditRequest createEditor setEditorData updateEditorGeometry commitEditorData destroyEditor
viv namespace: Q_DECLARE_FLAGS Q_DECLARE_OPERATORS_FOR_FLAGS

### include/virtualitemviews/itempane.h
ItemPane: Type type viewportRect firstRow lastRow isEmpty rowCount containsRow isFrozen operator

### include/virtualitemviews/labelheaderview.h
LabelHeaderSection: LabelHeaderSection text sortOrder setText setSortOrder setTextAlignment setForegroundBrush
LabelHeaderAdapter: createSection setLabelModel setGeometryModel bindSection unbindSection

### include/virtualitemviews/layoutpolicy.h
LayoutPolicy: LayoutPolicy orientation itemCount itemSize offsetOf indexAtOffset lastIndexAtOffset contentExtent crossExtent setCrossExtent itemRect itemAtPoint isVariableSized needsInitialMeasurement filterVisibleRanges hasTransientGeometry itemOpacity sizeIndex setSizeIndex resetItems insertItems removeItems moveItems setItemSize

### include/virtualitemviews/listlayout.h
ListLayout: ListLayout orientation setOrientation sizeIndex setSizeIndex itemCount itemSize offsetOf indexAtOffset contentExtent filterVisibleRanges hasTransientGeometry itemOpacity crossExtent setCrossExtent itemRect itemAtPoint isVariableSized resetItems insertItems removeItems moveItems setItemSize setItemSpacing itemSpacing setSpacingAfter spacingAfter setEdgeSpacing leadingSpacing trailingSpacing estimate setEstimate viewportMargins setViewportMargins

### include/virtualitemviews/materializeditem.h
MaterializedItem: index widget type geometry pinned isValid

### include/virtualitemviews/reorderabletablemodel.h
ReorderableTableModel: ReorderableTableModel rowCount QModelIndex columnCount setColumnCount data headerData setRowCount insertRows removeRows sourceRow viewRow resetRowOrder isIdentityOrder moveRows rowOrderChanged

### include/virtualitemviews/scrollmapper.h
ScrollMapper: kMaxScrollRange setExtents contentExtent viewportExtent maximumOffset scrollRange isScaled toScrollBarValue toLogicalOffset setAnchor resetAnchor anchorOffset anchorValue

### include/virtualitemviews/sizeindex.h
SizeIndex: SizeIndex count totalSize offsetOf indexAt sizeOf reset setSize insert remove isUniform uniformSize
FixedSizeIndex: FixedSizeIndex setItemSize itemSize totalSize offsetOf indexAt sizeOf reset setSize insert remove isUniform uniformSize
BlockSizeIndex: kDefaultBlockCapacity BlockSizeIndex totalSize offsetOf indexAt sizeOf reset setSize insert remove isUniform blockCapacity blockCount maxBlockRowCount estimatedSize sizes explicitSizeCount

### include/virtualitemviews/styledheadersection.h
StyledHeaderSection: StyledHeaderSection

### include/virtualitemviews/styledtableview.h
StyledTableCellHost: StyledTableCellHost bindIndex clearIndex
StyledTableRowWidget: StyledTableRowWidget bindRow unbindRow refreshVisualState refreshVisualStates
StyledTableView: StyledTableView setVisualStateCornerRadius visualStateCornerRadius setBackgroundCoversGridLines backgroundCoversGridLines refreshRoundedEdges hasLeftRowEdge hasRightRowEdge visualStateBackgroundPath paintVisualStateBackground

### include/virtualitemviews/tablepane.h
PaneScroll: PaneScroll
TablePaneSpec: logicalColumns scroll scrollGroup isFrozen operator
TablePane: Type type scrollGroup viewportRect logicalColumns isEmpty isFrozen width operator
TablePaneLayout: setGeometry geometry setSplitBoundarySpacing setFrozenColumns setFrozenRightColumns frozenColumns frozenRightColumns hasFrozenColumns setPaneSpecs paneSpecs usesExplicitPanes insertLogicalColumns removeLogicalColumns moveLogicalColumns update refreshScrollWindows columnVisitsInLastUpdate viewportWidth panes pane paneRect columnViewportX columnSpacingAfter leadingSpacingForPane trailingSpacingForPane paneOfColumn paneIndexOfColumn paneAt isFrozenColumn scrollGroupOfColumn primaryPaneIndex primaryScrollGroup scrollGroups groupExtent groupWidth groupOffset setGroupOffset maximumGroupOffset frozenLeftWidth frozenRightWidth scrollableExtent scrollableWidth maximumOffset visibleScrollableRange columnsForLayout

### include/virtualitemviews/tablespan.h
TableSpan: rowSpan columnSpan isMerged operator
TableSpanProvider: TableSpanProvider spanAt maximumSpan anchorOf modelStructureChanged
TableSpanMap: setSpan removeSpan clearSpans isEmpty count spanAt modelStructureChanged maximumSpan

### include/virtualitemviews/tablewidgetadapter.h
TableSpanContext: isEmpty spanOf isCovered rect
TableRowLayoutContext: TableRowLayoutContext columnCount isEmpty column columnX viewportRect horizontalOffset visibleColumns columnsToLayout pane isColumnFrozen paneRect frozenLeftWidth frozenRightWidth scrollablePaneHost paneHost paneHostForColumn paneIndex paneAt paneRectAt spans
ColumnHost: ColumnHost logicalColumn setLogicalColumn
TableWidgetAdapter: layoutRowWidget
CellWidgetAdapter: CellWidgetAdapter cellWidgetType createCellWidget bindCellWidget visualStateChanged unbindCellWidget

### include/virtualitemviews/treevisibilityindex.h
TreeVisibilityIndex: TreeVisibilityIndex setModel model rootIndex setRootIndex rebuild handleModelChanged handleModelReset visibleRowCount visibleRowForIndex indexAtVisibleRow isVisible depth expand collapse expandRecursively setExpanded toggleExpanded isExpanded collapseAll expandedCount modelQueryCount resetModelQueryCount

### include/virtualitemviews/types.h
GridLinePlacement: GridLinePlacement
OptionalPoint: OptionalPoint operator has_value value_or reset
DragOptions: previewWidget previewRect usePressPosition globalPressPosition
ItemBindingContext: index rowData logicalColumn depth indentationWidth branchWidth
PaneSeparatorStyle: width color lineStyle isVisible effectiveColor operator
VisibleRange: first last isValid contains count expanded
viv namespace: kDefaultItemDataRole WidgetType kDefaultWidgetType

### include/virtualitemviews/virtualflowtableview.h
VirtualFlowTableView: CellHeightMode VirtualFlowTableView setModel setRootIndex rootIndex setCellWidthRange minimumCellWidth maximumCellWidth setCellHeightMode cellHeightMode setFixedCellHeight fixedCellHeight setCellAspectRatio cellAspectRatio setColumnSpacing columnSpacing setRowSpacing setGridLinePlacement setContentMargins contentMargins setSpacingBackgroundColor setVisualStateBackgroundVisible visualStateBackgroundVisible setStateBackgroundCornerRadius stateBackgroundCornerRadius setVerticalGridLinesVisible verticalGridLinesVisible setHorizontalGridLinesVisible horizontalGridLinesVisible setVerticalGridLineWidth verticalGridLineWidth setHorizontalGridLineWidth horizontalGridLineWidth setVerticalGridLineColor verticalGridLineColor setHorizontalGridLineColor horizontalGridLineColor setVerticalSpacingLineThroughRowSpacing verticalSpacingLineThroughRowSpacing setHorizontalSpacingLineThroughColumnSpacing horizontalSpacingLineThroughColumnSpacing columnCount displayRowCount columnWidth columnViewportX displayRowForIndex displayColumnForIndex flowLayoutChanged

### include/virtualitemviews/virtualheaderview.h
VirtualHeaderView: VirtualHeaderView setGeometryModel geometryModel headerWidget orientation setLabelModel labelModel setSortInteractionEnabled isSortInteractionEnabled setPaneFilter clearPaneFilter hasPaneFilter setPaneOffset setSectionAnimationEnabled sectionAnimationEnabled setSectionAnimationDuration sectionAnimationDuration setSectionMoveAnimated setSectionStretchFactor sectionStretchFactor setSectionDragEnabled isSectionDragEnabled setSectionResizeEnabled isSectionResizeEnabled setSectionSelectionEnabled isSectionSelectionEnabled setSectionOrderExternal hasExternalSectionOrder setViewportOrigin setPaneTerminalColumn setBackgroundVisible backgroundVisible setBackgroundColor backgroundColor setSectionBackgroundColor sectionBackgroundColor setForegroundColor foregroundColor setTextAlignment textAlignment setContentPadding contentPadding setFont font setSpacingBackgroundColor spacingBackgroundColor setSectionSeparatorsVisible sectionSeparatorsVisible setGridLinePlacement gridLinePlacement sectionSpacingAfter setCrossAxisSeparatorVisible crossAxisSeparatorVisible setSectionSeparatorColor sectionSeparatorColor setCrossAxisSeparatorColor crossAxisSeparatorColor setSectionSeparatorWidth sectionSeparatorWidth setCrossAxisSeparatorWidth crossAxisSeparatorWidth sectionVisualX hasVisualSectionGeometry setVisualGeometryCallback void refreshSectionLabels setAdapter adapter recycler materializedSections sectionWidget materializedSectionCount pooledSectionCount paneCacheRebuildCount materializationVisits setSectionOverscan sectionOverscan appearanceChanged sectionClicked sectionHoverChanged adapterAboutToChange adapterChanged sectionMoveRequested

### include/virtualitemviews/virtualitemview.h
VirtualViewStats: logicalItems materializedItems pooledWidgets pinnedWidgets createCount bindCount recycleCount
ScrollAnchor: index offsetInsideItem isValid
VirtualItemView: ScrollHint VirtualItemView setBackgroundVisible backgroundVisible setBackgroundColor backgroundColor backgroundBrush setItemBackgroundColor itemBackgroundColor setFrameCornerRadius frameCornerRadius setFrameBorderWidth frameBorderWidth setFrameBorderColor frameBorderColor setFrameBorderEdges frameBorderEdges setFrameBorderVisible frameBorderVisible setAutoHeightEnabled autoHeightEnabled setAutoHeightMaximumHeight autoHeightMaximumHeight sizeHint minimumSizeHint setModel model setSelectionModel selectionModel VisualState visualState hoveredIndex setHoverBackgroundColor hoverBackgroundColor setSelectedBackgroundColor selectedBackgroundColor setVisualStateAnimationDuration visualStateAnimationDuration setVisualStateAnimationEasingCurve visualStateAnimationEasingCurve currentIndex setCurrentIndex activateIndex SelectionMode SelectionBehavior setSelectionMode selectionMode setSelectionBehavior selectionBehavior DropTarget DropIndicatorStyle setDragEnabled isDragEnabled DragStartMode setDragStartMode dragStartMode canStartDrag startDrag QModelIndex setDropIndicatorShown isDropIndicatorShown setDropIndicatorColor dropIndicatorColor setDropIndicatorWidth dropIndicatorWidth setDropIndicatorPenStyle dropIndicatorPenStyle setDefaultDropAction defaultDropAction setMoveRemovesSourceRows moveRemovesSourceRows setDragDropActions dragDropActions dropTargetAt dropIndicatorRect dropIndicatorStyle setAdapter adapter recycler setEditTriggers editTriggers edit commitEditor closeEditor editingIndex editorWidget setItemDataRole itemDataRole setOverscan overscanBefore overscanAfter RowSpacingFactory void setRowSpacing rowSpacing setGridLinePlacement gridLinePlacement setGridLineMargins gridLineMargins GridLineMarginReference setGridLineMarginReference gridLineMarginReference setTrailingRowGridLineAutoHideEnabled trailingRowGridLineAutoHideEnabled setSpacingBackgroundColor spacingBackgroundColor setRowSpacingFactory scrollTo setItemPinned isItemPinned pinWidget unpinWidget setMaxPinnedItems maxPinnedItems stats setPerformanceLoggingEnabled isPerformanceLoggingEnabled kLifecycleLogCapacity setLifecycleLoggingEnabled isLifecycleLoggingEnabled lifecycleLog clearLifecycleLog indexAt visualRect usesItemWidgets verticalOffset contentExtent maximumVerticalOffset scrollMapper WheelScrollMode setWheelScrollMode wheelScrollMode setWheelScrollPixels wheelScrollPixels setWheelScrollItems wheelScrollItems setVerticalOffset scrollByPixels ItemHeightMode setItemHeightMode itemHeightMode setUniformItemHeight uniformItemHeight setEstimatedItemHeight estimatedItemHeight setAutoMeasureItemHeight autoMeasureItemHeight visibleItemRange setFrozenRows frozenRows setFrozenBottomRows frozenBottomRows isRowFrozen itemPanes itemPaneRect visibleItemRanges setItemPaneSeparatorStyle itemPaneSeparatorStyle itemPaneSeparatorLeftExtension itemPaneSeparatorColor itemPaneSeparatorRects scrollingPaneHost materializedItemCount pinnedItemCount widgetForIndex indexForWidget materializedItems pooledWidgetCount createdWidgetCount destroyedWidgetCount isRelayoutPending flushPendingRelayout clicked doubleClicked activated itemDropped selectionModelChanged virtualizationUpdated

### include/virtualitemviews/virtuallistview.h
VirtualListView: ItemHeightMode VirtualListView setModel setRootIndex rootIndex listLayout DragFeedbackMode setDragFeedbackMode dragFeedbackMode setDragReorderAnimationDuration dragReorderAnimationDuration setDragReorderAnimationEasingCurve dragReorderAnimationEasingCurve setRowGridLinesVisible rowGridLinesVisible setRowGridLineWidth rowGridLineWidth setRowGridLineColor rowGridLineColor setVisualStateBackgroundVisible visualStateBackgroundVisible setVisualStateCornerRadius visualStateCornerRadius setRowVisualStateMargins rowVisualStateMargins VisualStateBackgroundExtent setVisualStateBackgroundExtent visualStateBackgroundExtent setVisualStateBackgroundMargins visualStateBackgroundMargins setVisualStateBackgroundHeight visualStateBackgroundHeight indexAt itemPaneSeparatorColor

### include/virtualitemviews/virtualmasonryview.h
VirtualMasonryView: ItemHeightMode VirtualMasonryView setModel setAdapter setRootIndex rootIndex setCellWidthRange minimumCellWidth maximumCellWidth setColumnCount columnCount setColumnSpacing columnSpacing setRowSpacing rowSpacing setGridLinePlacement setContentMargins contentMargins setSpacingBackgroundColor setItemHeightMode itemHeightMode setFixedItemHeight fixedItemHeight setItemAspectRatio itemAspectRatio setItemEstimatedSize setItemEstimatedSizes setItemHeight invalidateItemSize setItemHeightAnimationEnabled itemHeightAnimationEnabled setItemHeightAnimationDuration itemHeightAnimationDuration itemColumn visualRect indexAt setVisualStateBackgroundVisible visualStateBackgroundVisible setStateBackgroundCornerRadius stateBackgroundCornerRadius setVerticalGridLinesVisible verticalGridLinesVisible setHorizontalGridLinesVisible horizontalGridLinesVisible setVerticalGridLineWidth verticalGridLineWidth setHorizontalGridLineWidth horizontalGridLineWidth setVerticalGridLineColor verticalGridLineColor setHorizontalGridLineColor horizontalGridLineColor setVerticalSpacingLineThroughRowSpacing verticalSpacingLineThroughRowSpacing setHorizontalSpacingLineThroughColumnSpacing horizontalSpacingLineThroughColumnSpacing masonryLayoutChanged

### include/virtualitemviews/virtualtableview.h
VirtualTableView: ItemHeightMode SelectionBehavior SelectionMode WheelScrollMode RowSizePolicy MaterializationMode DropTargetMode setDropTargetMode dropTargetMode DragFeedbackMode setDragFeedbackMode dragFeedbackMode setDragReorderAnimationDuration dragReorderAnimationDuration setDragReorderAnimationEasingCurve dragReorderAnimationEasingCurve VirtualTableView setModel horizontalHeaderGeometry verticalHeaderGeometry setHorizontalHeader horizontalHeader setVerticalHeader verticalHeader setHeaderCornerWidget headerCornerWidget setHorizontalHeaderVisible isHorizontalHeaderVisible setVerticalHeaderVisible isVerticalHeaderVisible isVerticalHeaderShown setHeaderHeight headerHeight setVerticalHeaderWidth verticalHeaderWidth setHeaderBackgroundVisible headerBackgroundVisible setHeaderBackgroundColor headerBackgroundColor setHorizontalHeaderSelectionEnabled isHorizontalHeaderSelectionEnabled setVerticalHeaderSelectionEnabled isVerticalHeaderSelectionEnabled setHeaderVisualStateBackgroundVisible headerVisualStateBackgroundVisible headerVisualState setColumnDragEnabled isColumnDragEnabled setVerticalHeaderDragEnabled isVerticalHeaderDragEnabled setColumnResizeEnabled isColumnResizeEnabled setVerticalHeaderResizeEnabled isVerticalHeaderResizeEnabled columnCount columnGeometry columnWidth ColumnSpacingFactory void setColumnSpacing columnSpacing setHeaderContentSpacingEnabled headerContentSpacingEnabled setSpacingBackgroundColor setVisualStateThroughColumnSpacing visualStateThroughColumnSpacing setVisualStateThroughRowSpacing visualStateThroughRowSpacing setHeaderSpacingBackgroundColor headerSpacingBackgroundColor setColumnSpacingFactory setHeaderColumnSpacingFactory setGridLinePlacement setVerticalSpacingLineThroughRowSpacing verticalSpacingLineThroughRowSpacing setHorizontalSpacingLineThroughColumnSpacing horizontalSpacingLineThroughColumnSpacing setVerticalGridLinesThroughHeader verticalGridLinesThroughHeader setHorizontalGridLinesThroughHeader horizontalGridLinesThroughHeader setVerticalGridLinesVisible verticalGridLinesVisible setHorizontalGridLinesVisible horizontalGridLinesVisible setVerticalGridLineColor verticalGridLineColor setHorizontalGridLineColor horizontalGridLineColor setVerticalGridLineWidth verticalGridLineWidth setHorizontalGridLineWidth horizontalGridLineWidth columnAtViewportX visibleColumns visibleRows visibleColumnLogicalIndexes setColumnOverscan columnOverscan setColumnWidth ColumnResizeMode setColumnResizeMode columnResizeMode setColumnHidden isColumnHidden MoveAnimation moveColumn setDefaultColumnWidth defaultColumnWidth setColumnMinimumWidth setColumnMaximumWidth setColumnStretchFactor columnStretchFactor setStretchLastColumn stretchLastColumn horizontalOffset setHorizontalOffset scrollByHorizontalPixels maximumHorizontalOffset horizontalContentExtent setHorizontalWheelPixels horizontalWheelPixels setFrozenColumns setFrozenRightColumns clearFrozenColumns frozenColumns frozenRightColumns isColumnFrozen panes paneTypeForColumn itemPaneSeparatorLeftExtension itemPaneSeparatorColor setPanes paneSpecs paneIndexOfColumn scrollGroups primaryScrollGroup paneSeparatorRects setPaneSeparatorStyle paneSeparatorStyle sectionSeparatorColor drawPaneSeparator horizontalLayoutColumnVisits setHeaderAnimationEnabled headerAnimationEnabled setHeaderAnimationDuration headerAnimationDuration setColumnFollowsHeaderVisual columnFollowsHeaderVisual setRowFollowsHeaderVisual rowFollowsHeaderVisual setRowSizePolicy rowSizePolicy rowHeight setRowHeight clearRowHeight hasExplicitRowHeight setSpanProvider spanProvider setSpan removeSpan clearSpans spanAt anchorIndex isSpanCovered spanRect cellRect setSortingEnabled isSortingEnabled sortByColumn setSortIndicator saveHeaderState restoreHeaderState setAdapter setTableAdapter tableAdapter setMaterializationMode materializationMode usesItemWidgets stats setCellAdapter cellAdapter materializedCellCount cellWidget setVisualStateBackgroundVisible visualStateBackgroundVisible setVisualStateCornerRadius visualStateCornerRadius setRowVisualStateMargins rowVisualStateMargins setVisualStateCornerRadiusIncludesHeaders visualStateCornerRadiusIncludesHeaders visualState indexForWidget cellIndexForWidget materializedCellIndexes sortIndicatorRequested rowMoveRequested horizontalOffsetChanged columnGeometryChanged rowHeightChanged

### include/virtualitemviews/virtualtreetableview.h
VirtualTreeTableView: VirtualTreeTableView setModel setRootIndex rootIndex expand collapse expandRecursively collapseAll setExpansionAnimationEnabled expansionAnimationEnabled setExpansionAnimationDuration expansionAnimationDuration setExpansionFadeEnabled expansionFadeEnabled setExpanded toggleExpanded isExpanded hasChildren visibleRowCount visibilityIndex itemDepth setIndentation indentation setBranchAreaVisible branchAreaVisible NodeRowSpacingRole NodeRowSpacingBelowRole NodeRowSpacingAboveRole setRowGridLineDepths rowGridLineDepths setRowGridLinesVisible rowGridLinesVisible setRowGridLineWidth rowGridLineWidth setRowGridLineColor rowGridLineColor setVisualStateBackgroundVisible visualStateBackgroundVisible setBranchIndicatorsVisible branchIndicatorsVisible setExpandedBranchIcon expandedBranchIcon setCollapsedBranchIcon collapsedBranchIcon setBranchIconSize branchIconSize setBranchIconColor branchIconColor setBranchIndicatorAlignment branchIndicatorAlignment setBranchIndicatorPadding branchIndicatorPadding setBranchIndicatorRenderer branchIndicatorRenderer branchState columnCount expanded collapsed visibleRowsChanged

### include/virtualitemviews/virtualtreeview.h
VirtualTreeView: VirtualTreeView setModel setRootIndex rootIndex expand collapse expandRecursively setExpanded toggleExpanded isExpanded hasChildren collapseAll setExpansionAnimationEnabled expansionAnimationEnabled setExpansionAnimationDuration expansionAnimationDuration setExpansionFadeEnabled expansionFadeEnabled DragFeedbackMode setDragFeedbackMode dragFeedbackMode setDragReorderAnimationDuration dragReorderAnimationDuration setDragReorderAnimationEasingCurve dragReorderAnimationEasingCurve setIndentation indentation setBranchAreaVisible branchAreaVisible setIndentLeafItems indentLeafItems NodeRowSpacingRole NodeRowSpacingBelowRole NodeRowSpacingAboveRole setRowGridLineDepths rowGridLineDepths setRowGridLinesVisible rowGridLinesVisible setRowGridLineWidth rowGridLineWidth setRowGridLineColor rowGridLineColor RowGridLineExtent setRowGridLineExtent rowGridLineExtent setVisualStateBackgroundVisible visualStateBackgroundVisible setVisualStateCornerRadius visualStateCornerRadius setRowVisualStateMargins rowVisualStateMargins setVisualStateBackgroundMargins visualStateBackgroundMargins setVisualStateBackgroundHeight visualStateBackgroundHeight VisualStateBackgroundExtent setVisualStateBackgroundExtent visualStateBackgroundExtent itemPaneSeparatorColor setBranchIndicatorsVisible branchIndicatorsVisible setExpandedBranchIcon expandedBranchIcon setCollapsedBranchIcon collapsedBranchIcon setBranchIconSize branchIconSize setBranchIconColor branchIconColor setBranchIndicatorAlignment branchIndicatorAlignment setBranchIndicatorPadding branchIndicatorPadding setBranchIndicatorRenderer branchIndicatorRenderer branchState visibilityIndex visibleRowCount indexAt itemDepth applyRowSpacingOverrides expanded collapsed

### include/virtualitemviews/virtualwrapview.h
VirtualWrapView: VirtualWrapView setModel setAdapter setRootIndex rootIndex setFixedItemHeight fixedItemHeight setItemWidthRange minimumItemWidth maximumItemWidth setItemWidth setItemWidths clearItemWidth clearItemWidths invalidateItemWidths setItemSpacing itemSpacing setRowSpacing setContentMargins contentMargins setLineAlignment lineAlignment displayRowCount displayRowForIndex setVisualStateBackgroundVisible visualStateBackgroundVisible setStateBackgroundCornerRadius stateBackgroundCornerRadius wrapLayoutChanged

### include/virtualitemviews/widgetadapter.h
WidgetAdapter: WidgetAdapter widgetType createWidget bindWidget visualStateChanged unbindWidget estimatedSize

### include/virtualitemviews/widgetrecycler.h
WidgetRecycler: Factory kDefaultMaxPoolSize WidgetRecycler operator setParentWidget parentWidget setFactory acquire recycle discard setMaxPoolSize maxPoolSize pooledCount activeCount createdCount destroyedCount pendingDestructionCount acquireCount reuseCount aliveCount isPooled trim clear

### include/virtualitemviews/wraplayout.h
WrapLayout: WrapLayout addItem insertWidget count itemAt takeAt sizeHint minimumSize expandingDirections hasHeightForWidth heightForWidth setGeometry invalidate setFixedItemHeight fixedItemHeight setItemSpacing itemSpacing setRowSpacing rowSpacing setLineAlignment lineAlignment layoutChanged

### include/virtualitemviews/wrapwidget.h
WrapWidget: WrapWidget wrapLayout addWidget insertWidget takeWidget clear setAutoHeightEnabled autoHeightEnabled setAutoHeightMaximumHeight autoHeightMaximumHeight sizeHint minimumSizeHint

## Complete independent example inventory
Each chapter owns its model/adapter/widgets/UI; copy the smallest chapter, not another view implementation. README explains setup.

### examples/flow_capabilities
- examples/flow_capabilities/01_basic/example.cpp | 01 基础接入
- examples/flow_capabilities/02_cell_sizes/example.cpp | 02 尺寸与自动换列
- examples/flow_capabilities/03_spacing_grid/example.cpp | 03 间距、边距与分割线
- examples/flow_capabilities/04_visual_states/example.cpp | 04 状态背景与动画
- examples/flow_capabilities/05_selection_events/example.cpp | 05 选择与项目事件
- examples/flow_capabilities/06_model_root/example.cpp | 06 模型更新与显示根
- examples/flow_capabilities/07_scrolling/example.cpp | 07 滚动、定位与物化
- examples/flow_capabilities/08_drag_drop/example.cpp | 08 项目拖放
- examples/flow_capabilities/09_images/example.cpp | 09 异步图片卡片
- examples/flow_capabilities/10_colors/example.cpp | 10 颜色与样式表
- examples/flow_capabilities/11_auto_height/example.cpp | 11 自适应高度

### examples/list_capabilities
- examples/list_capabilities/01_basic/example.cpp | 01 基础列表
- examples/list_capabilities/02_row_widgets/example.cpp | 02 自定义行控件
- examples/list_capabilities/03_widget_types/example.cpp | 03 多种行类型
- examples/list_capabilities/04_binding_context/example.cpp | 04 整行数据绑定
- examples/list_capabilities/05_model_updates/example.cpp | 05 数据更新
- examples/list_capabilities/06_item_heights/example.cpp | 06 行高
- examples/list_capabilities/07_row_spacing/example.cpp | 07 行间距和分割线
- examples/list_capabilities/08_visual_states/example.cpp | 08 Hover 和选中状态
- examples/list_capabilities/09_state_content/example.cpp | 09 状态关联内容
- examples/list_capabilities/10_selection/example.cpp | 10 选择与当前项
- examples/list_capabilities/11_item_events/example.cpp | 11 行事件与控件事件
- examples/list_capabilities/12_scrolling/example.cpp | 12 滚动与定位
- examples/list_capabilities/13_virtualization/example.cpp | 13 虚拟化与复用
- examples/list_capabilities/14_frozen_rows/example.cpp | 14 冻结行
- examples/list_capabilities/15_drag_drop/example.cpp | 15 指示器拖动
- examples/list_capabilities/16_drag_animation/example.cpp | 16 行移动动画拖动
- examples/list_capabilities/17_pinning/example.cpp | 17 屏外保留
- examples/list_capabilities/18_diagnostics/example.cpp | 18 运行诊断
- examples/list_capabilities/19_smooth_scrolling/example.cpp | 19 平滑滚动（可选）
- examples/list_capabilities/20_borders/example.cpp | 20 外边框与圆角
- examples/list_capabilities/21_auto_height/example.cpp | 21 自适应高度
- examples/list_capabilities/22_colors/example.cpp | 22 颜色与样式表
- examples/list_capabilities/23_editors/example.cpp | 23 覆盖编辑器与 Popup

### examples/masonry_capabilities
- examples/masonry_capabilities/01_basic/example.cpp | 基础接入
- examples/masonry_capabilities/02_columns/example.cpp | 列数与列宽
- examples/masonry_capabilities/03_spacing_grid/example.cpp | 间距与分割线
- examples/masonry_capabilities/04_height_modes/example.cpp | 三种高度模式
- examples/masonry_capabilities/05_height_updates/example.cpp | 高度更新与动画
- examples/masonry_capabilities/06_visual_states/example.cpp | 状态背景与内容
- examples/masonry_capabilities/07_binding_events/example.cpp | 结构体、多类型与事件
- examples/masonry_capabilities/08_selection/example.cpp | 选择与几何查询
- examples/masonry_capabilities/09_model_root/example.cpp | 模型、代理与显示根
- examples/masonry_capabilities/10_scrolling/example.cpp | 滚动、物化与诊断
- examples/masonry_capabilities/11_drag_drop/example.cpp | 自动与手动拖放
- examples/masonry_capabilities/12_images/example.cpp | 异步图片卡片
- examples/masonry_capabilities/13_borders_colors/example.cpp | 圆角、边框与颜色
- examples/masonry_capabilities/14_auto_height/example.cpp | 自适应高度

### examples/table_capabilities
- examples/table_capabilities/01_row_widgets/example.cpp | 01 行控件与 ColumnHost
- examples/table_capabilities/02_cell_widgets/example.cpp | 02 单元格物化
- examples/table_capabilities/03_widget_types/example.cpp | 03 多种控件类型
- examples/table_capabilities/04_binding_context/example.cpp | 04 整行结构体绑定
- examples/table_capabilities/05_model_updates/example.cpp | 05 模型更新
- examples/table_capabilities/06_row_heights/example.cpp | 06 固定与动态行高
- examples/table_capabilities/07_columns/example.cpp | 07 列宽、隐藏与换序
- examples/table_capabilities/08_stretch_columns/example.cpp | 08 比例列宽与填满视图
- examples/table_capabilities/09_spacing/example.cpp | 09 间距与分割线
- examples/table_capabilities/10_visual_states/example.cpp | 10 Hover 与选中背景
- examples/table_capabilities/11_state_content/example.cpp | 11 状态关联控件
- examples/table_capabilities/12_selection/example.cpp | 12 选择与当前项
- examples/table_capabilities/13_item_events/example.cpp | 13 行内事件与几何查询
- examples/table_capabilities/14_scrolling/example.cpp | 14 双向滚动与定位
- examples/table_capabilities/15_virtualization/example.cpp | 15 二维虚拟化与复用
- examples/table_capabilities/16_frozen_areas/example.cpp | 16 冻结行列与分隔线
- examples/table_capabilities/17_panes/example.cpp | 17 多区域与独立滚动组
- examples/table_capabilities/18_headers/example.cpp | 18 表头与换序动画
- examples/table_capabilities/19_custom_header/example.cpp | 19 自定义表头控件
- examples/table_capabilities/20_header_persistence/example.cpp | 20 列布局保存与恢复
- examples/table_capabilities/21_sorting/example.cpp | 21 排序与代理模型
- examples/table_capabilities/22_row_reordering/example.cpp | 22 行号拖动排序
- examples/table_capabilities/23_spans/example.cpp | 23 合并单元格
- examples/table_capabilities/24_drag_indicator/example.cpp | 24 整行指示器拖动
- examples/table_capabilities/25_drag_animation/example.cpp | 25 整行移动动画拖动
- examples/table_capabilities/26_cell_drop/example.cpp | 26 单格插入与空白格投放
- examples/table_capabilities/27_pinning/example.cpp | 27 屏外保留
- examples/table_capabilities/28_diagnostics/example.cpp | 28 运行统计与生命周期
- examples/table_capabilities/29_smooth_scrolling/example.cpp | 29 平滑滚动（可选）
- examples/table_capabilities/30_borders/example.cpp | 30 外边框与圆角
- examples/table_capabilities/31_auto_height/example.cpp | 31 自适应高度
- examples/table_capabilities/32_colors/example.cpp | 32 颜色与样式表
- examples/table_capabilities/33_editors/example.cpp | 33 覆盖编辑器与编辑触发

### examples/tree_capabilities
- examples/tree_capabilities/01_basic/example.cpp | 01 基础接入
- examples/tree_capabilities/02_row_widgets/example.cpp | 02 组合行控件与数据回写
- examples/tree_capabilities/03_widget_types/example.cpp | 03 不同节点类型与复用池
- examples/tree_capabilities/04_binding_context/example.cpp | 04 节点结构体与绑定上下文
- examples/tree_capabilities/05_model_updates/example.cpp | 05 层级模型更新
- examples/tree_capabilities/06_item_heights/example.cpp | 06 固定、首项测量与动态行高
- examples/tree_capabilities/07_expansion/example.cpp | 07 展开收起与过渡动画
- examples/tree_capabilities/08_root_index/example.cpp | 08 切换显示根与相对深度
- examples/tree_capabilities/09_indentation/example.cpp | 09 分支格宽与叶子对齐
- examples/tree_capabilities/10_branch_icons/example.cpp | 10 内置分支图标配置
- examples/tree_capabilities/11_branch_renderer/example.cpp | 11 自定义分支渲染器
- examples/tree_capabilities/12_row_spacing/example.cpp | 12 节点间距与分割线
- examples/tree_capabilities/13_visual_states/example.cpp | 13 状态背景、范围与圆角
- examples/tree_capabilities/14_state_content/example.cpp | 14 状态关联内容
- examples/tree_capabilities/15_selection/example.cpp | 15 选择、当前项与键盘导航
- examples/tree_capabilities/16_item_events/example.cpp | 16 行事件、正确索引与几何查询
- examples/tree_capabilities/17_scrolling/example.cpp | 17 大树滚动、定位与可见映射
- examples/tree_capabilities/18_virtualization/example.cpp | 18 可见行映射与控件复用
- examples/tree_capabilities/19_drag_drop/example.cpp | 19 节点拖放与手动起拖
- examples/tree_capabilities/20_drag_animation/example.cpp | 20 拖动位移动画
- examples/tree_capabilities/21_sorting_filtering/example.cpp | 21 层级排序与递归过滤
- examples/tree_capabilities/22_lazy_loading/example.cpp | 22 业务驱动的延迟加载
- examples/tree_capabilities/23_pinning/example.cpp | 23 屏外保留与编辑生命周期
- examples/tree_capabilities/24_diagnostics/example.cpp | 24 统计与生命周期日志
- examples/tree_capabilities/25_smooth_scrolling/example.cpp | 25 平滑滚动（可选）
- examples/tree_capabilities/26_borders/example.cpp | 26 外边框与圆角
- examples/tree_capabilities/27_auto_height/example.cpp | 27 自适应高度
- examples/tree_capabilities/28_colors/example.cpp | 28 颜色与样式表
- examples/tree_capabilities/29_editors/example.cpp | 29 覆盖编辑器与 Popup

### examples/treetable_capabilities
- examples/treetable_capabilities/01_basic/example.cpp | 01 基础多列树
- examples/treetable_capabilities/02_row_widgets/example.cpp | 02 整行物化与 ColumnHost
- examples/treetable_capabilities/03_cell_widgets/example.cpp | 03 单元格物化
- examples/treetable_capabilities/04_widget_types/example.cpp | 04 层级与列控件类型
- examples/treetable_capabilities/05_binding_context/example.cpp | 05 节点结构体与绑定上下文
- examples/treetable_capabilities/06_model_updates/example.cpp | 06 层级模型更新
- examples/treetable_capabilities/07_row_heights/example.cpp | 07 固定与动态行高
- examples/treetable_capabilities/08_expansion/example.cpp | 08 展开收起与淡入淡出
- examples/treetable_capabilities/09_root_index/example.cpp | 09 显示根与相对深度
- examples/treetable_capabilities/10_indentation/example.cpp | 10 缩进与分支配置
- examples/treetable_capabilities/11_branch_renderer/example.cpp | 11 自定义分支绘制
- examples/treetable_capabilities/12_columns/example.cpp | 12 列宽、隐藏与换序
- examples/treetable_capabilities/13_stretch_columns/example.cpp | 13 比例列宽与适应视口
- examples/treetable_capabilities/14_spacing/example.cpp | 14 节点间距与分割线
- examples/treetable_capabilities/15_visual_states/example.cpp | 15 状态背景与圆角
- examples/treetable_capabilities/16_state_content/example.cpp | 16 状态关联内容
- examples/treetable_capabilities/17_selection/example.cpp | 17 共享选择与键盘导航
- examples/treetable_capabilities/18_item_events/example.cpp | 18 行内事件与几何查询
- examples/treetable_capabilities/19_scrolling/example.cpp | 19 大树定位与滚动
- examples/treetable_capabilities/20_virtualization/example.cpp | 20 二维虚拟化与复用
- examples/treetable_capabilities/21_frozen_columns/example.cpp | 21 左右冻结列
- examples/treetable_capabilities/22_panes/example.cpp | 22 独立横向滚动分组
- examples/treetable_capabilities/23_headers/example.cpp | 23 表头交互与调整
- examples/treetable_capabilities/24_custom_header/example.cpp | 24 表头样式、Adapter 与 Corner
- examples/treetable_capabilities/25_header_persistence/example.cpp | 25 保存与恢复布局
- examples/treetable_capabilities/26_sorting_filtering/example.cpp | 26 同级排序与递归过滤
- examples/treetable_capabilities/27_row_reordering/example.cpp | 27 同级行号拖动排序
- examples/treetable_capabilities/28_spans/example.cpp | 28 同级单元格合并
- examples/treetable_capabilities/29_drag_indicator/example.cpp | 29 节点拖放与指示器
- examples/treetable_capabilities/30_drag_animation/example.cpp | 30 节点拖放与位移动画
- examples/treetable_capabilities/31_lazy_loading/example.cpp | 31 异步加载子节点
- examples/treetable_capabilities/32_pinning/example.cpp | 32 屏外保留与释放
- examples/treetable_capabilities/33_diagnostics/example.cpp | 33 统计与性能诊断
- examples/treetable_capabilities/34_smooth_scrolling/example.cpp | 34 可选平滑滚动
- examples/treetable_capabilities/35_borders/example.cpp | 35 圆角边框与透明视图
- examples/treetable_capabilities/36_auto_height/example.cpp | 36 自适应高度与滚动上限
- examples/treetable_capabilities/37_colors/example.cpp | 37 颜色与样式表
- examples/treetable_capabilities/38_editors/example.cpp | 38 覆盖编辑器与 Popup

### examples/wrap_capabilities
- examples/wrap_capabilities/01_basic/example.cpp | 01_basic / 基础接入
- examples/wrap_capabilities/02_geometry/example.cpp | 02_geometry / 宽度、间距与对齐
- examples/wrap_capabilities/03_visual_states/example.cpp | 03_visual_states / 状态、颜色与圆角
- examples/wrap_capabilities/04_binding_events/example.cpp | 04_binding_events / 结构体、多类型与事件
- examples/wrap_capabilities/05_model_root/example.cpp | 05_model_root / 模型、代理与显示根
- examples/wrap_capabilities/06_scrolling/example.cpp | 06_scrolling / 滚动、物化与诊断
- examples/wrap_capabilities/07_drag_drop/example.cpp | 07_drag_drop / 自动与手动拖放
- examples/wrap_capabilities/08_auto_height/example.cpp | 08 自适应高度
- examples/wrap_capabilities/09_wrap_layout/example.cpp | 09_wrap_layout / 直接使用 WrapLayout
- examples/wrap_capabilities/10_wrap_widget/example.cpp | 10_wrap_widget / 真实控件自适应高度
