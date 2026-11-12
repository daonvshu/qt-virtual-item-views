"""Curated capability routes for the generated API reference and agent guide.

Paths are repository-relative. Keep behavioral notes grounded in public headers
and implementation; the generator separately extracts exact public declarations.
"""

VIEWS = {
    'virtualitemview': ('VirtualItemView', '共享内核（抽象基类）', 'QAbstractScrollArea', '',
        '模型、选择、滚动、复用、状态动画、编辑、拖放、背景边框和自动高度的共同入口。先查这里，再查具体视图。'),
    'virtuallistview': ('VirtualListView', '纵向列表', 'VirtualItemView', 'list',
        '每个模型项一个 QWidget；支持可变行高、状态范围、冻结行和拖动让位。'),
    'virtualtableview': ('VirtualTableView', '多列表格', 'VirtualItemView', 'table',
        '列宽、表头、冻结区域、合并和排序；可选择一行一个控件或一个单元格一个控件。'),
    'virtualtreeview': ('VirtualTreeView', '单列树', 'VirtualItemView', 'tree',
        '真实节点展开动画、缩进、branch、层级拖放；不支持冻结行。'),
    'virtualtreetableview': ('VirtualTreeTableView', '多列树表', 'VirtualTableView', 'treetable',
        '组合表格列布局与树节点展开；支持冻结列，不支持冻结行。展开后的视图行号不是 QModelIndex::row()。'),
    'virtualflowtableview': ('VirtualFlowTableView', '等宽响应式网格', 'VirtualItemView', 'flow',
        '模型行是项目；显示列由视口宽度决定。统一格高或宽高比，大量等尺寸卡片优先选择此视图。'),
    'virtualmasonryview': ('VirtualMasonryView', '独立高度瀑布流', 'VirtualItemView', 'masonry',
        '按最短列排布，项目高度独立；适用于图片卡片。固定高度或统一比例且项目很多时优先考虑 Flow。'),
    'virtualwrapview': ('VirtualWrapView', '不等宽换行', 'VirtualItemView', 'wrap',
        '项目宽度独立、统一高度、按模型顺序换行；虚拟化显示，初始布局与宽度变化重排为 O(N)。'),
    'wrapwidget': ('WrapWidget', '少量真实控件的换行容器', 'QScrollArea', 'wrap',
        '直接 addWidget，不需要模型或 adapter，不复用。默认自动高度开启，配合最大高度内部滚动。'),
    'wraplayout': ('WrapLayout', '可直接使用的换行布局', 'QLayout', 'wrap',
        '实现 heightForWidth、统一高度和行对齐；自身没有滚动条，限高滚动使用 WrapWidget。'),
    'virtualheaderview': ('VirtualHeaderView', '可复用 section 控件的表头', 'QWidget + HeaderViewInterface', '',
        '表格默认表头。常规样式经 horizontalHeader()/verticalHeader() 配置，自定义内容换 HeaderWidgetAdapter。'),
}

# key, title, relevant headers, public API names, combination rules,
# implementation paths, example paths (globs), related narrative document
FEATURES = []
def feature(key, title, headers, api, rules, impl, examples='', doc=''):
    FEATURES.append(dict(id=key, title=title, headers=headers.split(), api=api.split(),
                         rules=rules, implementation=impl.split(), examples=examples.split(), document=doc))

feature('model', '模型 / 根节点 / 代理模型 / 更新通知',
    'virtualitemview virtuallistview virtualtableview virtualtreeview virtualtreetableview virtualflowtableview virtualmasonryview virtualwrapview',
    'setModel model setRootIndex rootIndex',
    '使用 QAbstractItemModel 与正常的 dataChanged、begin/endInsertRows、removeRows、moveRows、layoutChanged/reset 通知驱动更新；视图不持有业务数据。简单模型直接填充 QStandardItemModel。代理模型索引属于代理，不可把源模型索引直接传入视图；setRootIndex 显示其子项，Table 没有该入口。切换模型后重新核对 root 和 selectionModel。',
    'src/core/virtualitemview.cpp src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/*_capabilities/*model*/example.cpp examples/*_capabilities/*root*/example.cpp examples/tree_capabilities/21_sorting_filtering/example.cpp', 'docs/models.md')
feature('adapter', '控件复用 / 多控件类型 / 异步清理',
    'virtualitemview widgetadapter tablewidgetadapter widgetrecycler',
    'setAdapter setTableAdapter setCellAdapter widgetType createWidget bindWidget unbindWidget estimatedSize visualStateChanged',
    '先实现 adapter，框架负责创建、复用和回收。createWidget 中连接一次信号；bindWidget 必须完整覆盖旧显示状态，事件中使用当前绑定索引。unbindWidget 停止旧索引的定时器、订阅、动画和异步图片请求；异步回调核对持久索引或任务代次。按 widgetType 分池，不要按滚动自行销毁重建整表。takeOwnership 默认 false，要保证 adapter 生命周期覆盖视图使用期间。',
    'src/recycler/widgetrecycler.cpp src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp',
    'examples/*_capabilities/*widget_types/example.cpp examples/list_capabilities/02_row_widgets/example.cpp', 'docs/getting-started.md')
feature('binding', '整行业务 payload / 深度 / 列 / branch 上下文',
    'virtualitemview types widgetadapter tablewidgetadapter itemeditoradapter',
    'setItemDataRole ItemBindingContext indentationWidth branchWidth',
    'ItemBindingContext 提供 index、rowData、logicalColumn、depth；cell 的整行业务数据从列 0 读取。负的 itemDataRole 禁用 payload 读取。树的 indentationWidth 是祖先缩进，branchWidth 是当前节点 branch 格占位，不是图标宽度，均为未裁剪的逻辑像素；TreeTable 仅逻辑列 0 有这两个宽度。',
    'src/core/virtualitemview.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/*_capabilities/*binding_context/example.cpp', 'docs/interaction.md')
feature('materialization', 'RowWidgets / CellWidgets / 物化成本',
    'virtualtableview virtualtreetableview tablewidgetadapter',
    'setMaterializationMode setTableAdapter setCellAdapter layoutRowWidget estimatedSize cellWidget materializedCellCount',
    'RowWidgets 配 TableWidgetAdapter，通过 layoutRowWidget/TableRowLayoutContext 布置行内列控件；使用 ColumnHost 子控件时框架自动定位，可省略布局回调。CellWidgets 配 CellWidgetAdapter，由框架定位每个 cell。行模式不能只传普通 WidgetAdapter。物化模式与 SelectRows/SelectItems 是独立维度，整行高亮不要求整行物化。CellWidgets 的 QWidget 数随可见行数×可见列数增长；先衡量控件数量和 adapter 工作量再考虑定制布局。',
    'src/widgets/virtualtableview.cpp src/widgets/columnhost.cpp',
    'examples/table_capabilities/01_row_widgets/example.cpp examples/table_capabilities/02_cell_widgets/example.cpp examples/treetable_capabilities/02_row_widgets/example.cpp examples/treetable_capabilities/03_cell_widgets/example.cpp', 'docs/table.md')
feature('height', '动态行高 / 测量 / 显式覆盖',
    'virtualitemview virtuallistview virtualtreeview virtualtableview virtualtreetableview',
    'setItemHeightMode setUniformItemHeight setEstimatedItemHeight setAutoMeasureItemHeight setRowSizePolicy setRowHeight clearRowHeight',
    'Uniform 统一高度；Uniform 高度为 0 且开启测量时按首个物化项适配。Variable 使用估值并在物化后测量，不能为了 auto height 预创建全部项目。Table 的显式 setRowHeight 默认优先于测量，拖动行高也会形成显式值；恢复动态行为用 clearRowHeight。列宽变化会影响换行高度，选择合适 RowSizePolicy。Flow/Masonry/Wrap 使用各自专用尺寸 API，不可套用内核行高配置。',
    'src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/index/sizeindex.cpp',
    'examples/*_capabilities/*item_heights/example.cpp examples/*_capabilities/*row_heights/example.cpp', 'docs/auto-height.md')
feature('auto-height', '自适应高度 / 嵌套滚动 / 最大高度',
    'virtualitemview wrapwidget',
    'setAutoHeightEnabled setAutoHeightMaximumHeight',
    '虚拟视图默认关闭，WrapWidget 默认开启。开启后内部 setFixedHeight，垂直 QSizePolicy::Fixed；上限 0 表示不限，不受父布局可用高度压缩。父容器放不下由业务安排外层滚动。开启时用 setAutoHeightMaximumHeight，不与 QWidget 的 setFixedHeight/setMaximumHeight 混用；关闭恢复原约束。高度包括内容、间距、适用半间距、表头和实际占位边框/横向滚动条；分割线不额外占高。没有内部滚动范围时滚轮交给外层，表头也同样处理。',
    'src/core/virtualitemview.cpp src/widgets/wrapwidget.cpp src/core/wheeleventforwarding_p.h',
    'examples/*_capabilities/*auto_height/example.cpp examples/wrap_capabilities/10_wrap_widget/example.cpp', 'docs/auto-height.md')
feature('selection', '选择 / 多选 / 当前项 / 表头选择',
    'virtualitemview virtualtableview virtualtreetableview',
    'setSelectionMode setSelectionBehavior setSelectionModel setCurrentIndex currentIndex selectionModel setHorizontalHeaderSelectionEnabled setVerticalHeaderSelectionEnabled',
    '默认 ExtendedSelection + SelectRows。SelectionMode 控制用户选择手势，程序批量选择通过 QItemSelectionModel::select(QItemSelection, flags)，不要循环 setCurrentIndex 代替范围选择。ExtendedSelection 使用 Ctrl/Shift。SelectRows 的 hover/selected 整行，SelectItems 按 cell，和物化模式无关。当前项不等于所有选中项；屏外选择保留在 selectionModel，widgetForIndex 可为空。默认行表头点击选行开启、列表头点击选列关闭；列选择不会把所有行号当作整行已选中。',
    'src/core/virtualitemview.cpp src/core/rowselection_p.h src/core/columnselectioncache_p.h src/widgets/virtualtableview.cpp',
    'examples/list_capabilities/10_selection/example.cpp examples/table_capabilities/12_selection/example.cpp examples/treetable_capabilities/17_selection/example.cpp', 'docs/interaction.md')
feature('states', 'hover / selected 默认绘制和动画',
    'virtualitemview virtuallistview virtualtableview virtualtreeview virtualtreetableview virtualflowtableview virtualmasonryview virtualwrapview',
    'visualState setVisualStateBackgroundVisible setHoverBackgroundColor setSelectedBackgroundColor setVisualStateAnimationDuration setVisualStateAnimationEasingCurve',
    '内置状态背景默认开启，动画默认 180ms OutCubic；业务控件保持透明并在 visualStateChanged 中更新按钮/复选框的显示即可。除非完全自定义外观，不要自己维护 hoveredIndex、动画或重画默认背景。原生拖动期间框架暂停普通 hover，结束后重新计算；不改变业务勾选数据。Flow/Masonry/Wrap 的状态圆角入口是 setStateBackgroundCornerRadius，List/Tree/Table 是 setVisualStateCornerRadius。',
    'src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp',
    'examples/*_capabilities/*visual_states/example.cpp examples/*_capabilities/*state_content/example.cpp', 'docs/interaction.md')
feature('state-extent', '状态范围 / 命中范围 / 固定高度 / padding',
    'virtuallistview virtualtreeview virtualtreetableview',
    'setVisualStateBackgroundExtent setVisualStateBackgroundMargins setVisualStateBackgroundHeight setRowGridLineExtent',
    'List: FullWidth/WidgetOnly。Tree: NodeOnly/NodeAndIcon/FullWidth/WidgetOnly/WidgetAndIcon；含 Icon 模式只有实际显示 branch 图标时才扩展。Margins 和 Height 只改变状态绘制及鼠标命中，不改变布局、键盘和程序选择；branch 展开点击单独处理。TreeTable 固定按表格行区域绘制，不提供 Tree 的这些 extent 设置，不能复制 Tree 的节点裁剪策略到树表。',
    'src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/tree_capabilities/13_visual_states/example.cpp examples/list_capabilities/08_visual_states/example.cpp', 'docs/tree.md')
feature('background', '背景 / 项底色 / 透明 / QSS',
    'virtualitemview headerview virtualheaderview',
    'setBackgroundVisible setBackgroundColor backgroundBrush setItemBackgroundColor setSectionBackgroundColor setSpacingBackgroundColor',
    '整体默认 QPalette::Base，项底色默认透明。View 底色→viewport QSS 背景→项与状态层；setBackgroundVisible(false) 关闭主体两层，不控制表头和显式间距色。QSS 使用 vivItemView/vivViewport/vivHorizontalHeader/vivVerticalHeader/vivHeaderSection/vivHeaderCorner 对象名，不使用 QHeaderView::section。QSS 背景由 Qt 画刷驱动，不解析 stylesheet 字符串。框架边框/圆角使用专用接口；不要假定自绘部件支持所有 QSS border/image 属性。 编辑器、冻结区域和动画的纯裁剪宿主，以及 ColumnHost/Wrap 内容层保持透明，屏蔽外层 QSS 的背景和边框；宿主透明规则不匹配业务子控件。',
    'src/core/virtualitemviewframe.cpp src/core/stylesheetbackground_p.h src/core/viewbackground_p.h src/widgets/styledheadersection.cpp',
    'examples/*_capabilities/*colors/example.cpp', 'docs/stylesheets.md')
feature('frame', '外边框 / 四边开关 / 抗锯齿圆角',
    'virtualitemview',
    'setFrameCornerRadius setFrameBorderWidth setFrameBorderColor setFrameBorderEdges setFrameBorderVisible',
    '框架提供外边框与内容圆角合成，不必对子控件 setMask 或截图遮罩。隐藏的边不预留占位；外框圆角和 hover 圆角是两种不同配置。末行/末列分割线会与边框相邻，先检查自动隐藏与实际滚动范围，不要重复绘制外框补线。',
    'src/core/virtualitemviewframe.cpp src/core/pixelalignedlines_p.h',
    'examples/*_capabilities/*borders/example.cpp examples/masonry_capabilities/13_borders_colors/example.cpp', 'docs/stylesheets.md')
feature('spacing', '间距 / 中心分割线 / 双侧分割线 / 半间距',
    'virtualitemview virtualtableview virtualflowtableview virtualmasonryview',
    'setRowSpacing setColumnSpacing setGridLinePlacement setSpacingBackgroundColor setVerticalSpacingLineThroughRowSpacing setHorizontalSpacingLineThroughColumnSpacing',
    '默认 SpacingCenter，在间距中心画线，并在支持的网格布局外围补半间距；SpacingEdges 在两侧画线。间距 0 都回退到单一共享边界。Center 始终穿过交叉间距，不能关闭；Edges 才允许配置穿过与否。内容 margins 与项目 spacing 不是同一层。冻结区域交界将间距分成两半，由冻结分割线标识边界，不应业务重复补线。Wrap 的水平间距叫 setItemSpacing，没有表格网格线 API。',
    'src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/layout/flowtablelayout.cpp src/layout/masonrylayout.cpp src/core/pixelalignedlines_p.h',
    'examples/table_capabilities/09_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp examples/flow_capabilities/03_spacing_grid/example.cpp examples/masonry_capabilities/03_spacing_grid/example.cpp', 'docs/table-layout-guide.md')
feature('header-gap', '表头与内容间距 / 穿过表头 / 状态穿过间距',
    'virtualtableview virtualtreetableview',
    'setHeaderContentSpacingEnabled setVerticalGridLinesThroughHeader setHorizontalGridLinesThroughHeader setHeaderSpacingBackgroundColor setVisualStateThroughColumnSpacing setVisualStateThroughRowSpacing',
    'header-content 开关默认 false，Horizontal 对应水平表头下方 rowSpacing，Vertical 对应垂直表头右侧 columnSpacing。开关不创建非零 spacing；启用使用全间距，未启用时 Center 保留原外侧半间距、Edges 为 0。穿过表头的行/列分割线默认开启，和穿过交叉间距是不同开关。整行状态跨列间距默认开启；跨上下半间距默认开启且只在 Center 生效，SelectItems 仍按 cell。',
    'src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp',
    'examples/table_capabilities/09_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp', 'docs/headers.md')
feature('grid', '分割线样式 / 最后一行 / 冻结分隔线',
    'virtualitemview virtuallistview virtualtreeview virtualtableview virtualflowtableview virtualmasonryview',
    'setRowGridLinesVisible setRowGridLineColor setRowGridLineWidth setHorizontalGridLinesVisible setVerticalGridLinesVisible setTrailingRowGridLineAutoHideEnabled setItemPaneSeparatorStyle setPaneSeparatorStyle',
    'List/Tree 用 RowGridLine 接口；Table/Flow/Masonry 用 Horizontal/VerticalGridLine 接口。末行自动隐藏默认 true：自动高度或实际竖向溢出时按末行边界规则处理，内容底边距存在时仍有例外；false 请求保留可见末行线，不增加高度。强制显示但 range=0 的滚动条不等于可滚动。普通分割线与冻结线分别配置，PaneSeparatorStyle 支持颜色、粗细、线型；查看具体 API 单位，线宽不可一律再乘 DPR。',
    'src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp src/widgets/virtuallistview.cpp src/widgets/virtualtreeview.cpp',
    'examples/table_capabilities/16_frozen_areas/example.cpp examples/*_capabilities/*auto_height/example.cpp', 'docs/auto-height.md')
feature('gap-widget', '间距内自定义控件',
    'virtualitemview virtualtableview',
    'setRowSpacingFactory setColumnSpacingFactory setHeaderColumnSpacingFactory',
    '工厂与 binder 为可见间距创建/刷新可复用控件；行间距按行、列间距按列管理，不是每个交叉空格一个 QWidget。普通颜色用 setSpacingBackgroundColor 更轻。Flow/Masonry/Wrap 不支持内核行间距控件；不要因继承了 setter 就假定可用。',
    'src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp',
    'examples/table_capabilities/09_spacing/example.cpp examples/list_capabilities/07_row_spacing/example.cpp', 'docs/table-layout-guide.md')
feature('columns', '列宽 / 比例 / FitViewport / 隐藏换序',
    'virtualtableview virtualtreetableview headergeometry',
    'setColumnWidth setColumnMinimumWidth setColumnMaximumWidth setColumnStretchFactor setStretchLastColumn setColumnResizeMode setColumnHidden moveColumn setColumnResizeEnabled',
    '默认 Independent。比例分配的是扣除固定列和间距后的剩余宽度，factor=0 保持自身宽度；此模式下拖动任意列或 setColumnWidth 会清除全部比例和末列拉伸，故先设固定宽度再设 stretch factors。FitViewport 根据当前比例让列宽总和适配视口，拖动边缘在相应侧按比例补偿；受最小/最大限制。拖动改宽默认关闭；模型列号、视觉顺序和隐藏状态不要混用。',
    'src/layout/headergeometry.cpp src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp',
    'examples/table_capabilities/07_columns/example.cpp examples/table_capabilities/08_stretch_columns/example.cpp examples/treetable_capabilities/13_stretch_columns/example.cpp', 'docs/table-layout-guide.md')
feature('headers', '默认表头 / 样式 / section / corner',
    'virtualtableview virtualtreetableview virtualheaderview headerview headerwidgetadapter headersection styledheadersection labelheaderview headercornerwidget',
    'horizontalHeader verticalHeader setAdapter setBackgroundColor setSectionBackgroundColor setForegroundColor setFont setTextAlignment setContentPadding setHeaderCornerWidget',
    '默认 VirtualHeaderView，样式优先通过 HeaderViewInterface 配置。容器 backgroundColor 改整条底色，sectionBackgroundColor 只改 section，spacingBackgroundColor 改间距；无效 QColor 恢复继承，透明色是显式透明。模型 headerData 的 ForegroundRole/FontRole/TextAlignmentRole 可覆盖默认文字样式。HeaderSection 管结构，StyledHeaderSection 管外观，LabelHeaderSection 管文字；复杂内容换 HeaderWidgetAdapter，无需替换整个表头。默认 corner 跟水平表头，替换 corner 的 QWidget 由 view 接管。',
    'src/widgets/virtualheaderview.cpp src/widgets/headersection.cpp src/widgets/styledheadersection.cpp src/widgets/labelheaderview.cpp src/widgets/headercornerwidget.cpp',
    'examples/table_capabilities/19_custom_header/example.cpp examples/treetable_capabilities/24_custom_header/example.cpp examples/table_capabilities/32_colors/example.cpp', 'docs/headers.md')
feature('header-state', '表头同步 hover / selected / 圆角',
    'virtualtableview virtualtreetableview virtualheaderview headerview',
    'setHeaderVisualStateBackgroundVisible headerVisualState setVisualStateCornerRadiusIncludesHeaders',
    '表头状态背景可以按轴独立开关，和点击选行/选列是不同配置。默认跟内容动画进度同步。状态圆角默认扩展到表头：SelectRows 行号仅左圆角、内容仅右圆角；SelectItems 行号和单元格各自四角，列头四角。自定义 section 应使用框架状态进度，不能只看 selected 布尔值立即换背景。',
    'src/widgets/virtualtableview.cpp src/widgets/virtualheaderview.cpp src/widgets/styledheadersection.cpp',
    'examples/table_capabilities/12_selection/example.cpp examples/table_capabilities/19_custom_header/example.cpp', 'docs/headers.md')
feature('header-reorder', '表头拖动换序 / 行号拖动 / 动画',
    'virtualtableview virtualtreetableview virtualheaderview',
    'setColumnDragEnabled setVerticalHeaderDragEnabled setVerticalHeaderResizeEnabled setHeaderAnimationEnabled setHeaderAnimationDuration setColumnFollowsHeaderVisual setRowFollowsHeaderVisual rowMoveRequested',
    '列换序改变 HeaderGeometry 的视觉顺序；行号换序需要模型 moveRows。行高/列宽拖动默认关闭，按需显式开启。表头换序动画与内容原生拖放让位是两套配置。verticalHeaderDrag 在无模型时可创建 ReorderableTableModel；业务已有模型时必须实现对应移动能力。树表只允许符合层级约束的移动。',
    'src/widgets/virtualheaderview.cpp src/widgets/virtualtableview.cpp src/core/reorderabletablemodel.cpp',
    'examples/table_capabilities/22_row_reordering/example.cpp examples/treetable_capabilities/27_row_reordering/example.cpp', 'docs/headers.md')
feature('frozen', '冻结行列 / 多 pane / 多滚动组',
    'virtualitemview virtualtableview virtualtreetableview tablepane itempane',
    'setFrozenRows setFrozenBottomRows setFrozenColumns setFrozenRightColumns setPanes setHorizontalOffset primaryScrollGroup',
    'List/Table 支持上下冻结行；Tree/TreeTable 不支持冻结行，TreeTable 保留冻结列。Flow/Masonry/Wrap 不支持行 pane。表格冻结列共用 HeaderGeometry，不复制宽度/顺序；左右集合冲突时左侧优先。高级 setPanes 指定列集合和滚动组，同组共享 offset，首个可滚动 pane 的组是主组；非主组用带 group 参数的接口。不要为冻结区再创建一个独立表格同步数据。',
    'src/layout/tablepane.cpp src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp',
    'examples/list_capabilities/14_frozen_rows/example.cpp examples/table_capabilities/16_frozen_areas/example.cpp examples/table_capabilities/17_panes/example.cpp examples/treetable_capabilities/21_frozen_columns/example.cpp examples/treetable_capabilities/22_panes/example.cpp', 'docs/table-layout-guide.md')
feature('spans', '合并单元格',
    'virtualtableview virtualtreetableview tablespan tablewidgetadapter',
    'setSpanProvider setSpan removeSpan clearSpans anchorIndex isSpanCovered spanRect cellRect',
    '内置 TableSpanMap 或自定义 TableSpanProvider 提供跨度；查询/绘制/命中使用 anchorIndex 与 cellRect。跨度裁剪到模型和 anchor 所在 pane，不跨 pane。被覆盖 cell 的 cellRect 为空。RowWidgets 通过 TableRowLayoutContext 的 spans 布局业务控件；表格拖动让位在 span provider 存在等不支持条件下退回指示器。',
    'src/layout/tablespan.cpp src/widgets/virtualtableview.cpp',
    'examples/table_capabilities/23_spans/example.cpp examples/treetable_capabilities/28_spans/example.cpp', 'docs/table-layout-guide.md')
feature('sort-persist', '排序 / 过滤 / 布局保存恢复',
    'virtualtableview virtualtreetableview headergeometry',
    'setSortingEnabled sortByColumn setSortIndicator saveHeaderState restoreHeaderState',
    '排序需要模型 sort 实现或 QSortFilterProxyModel；仅画 sort indicator 不等于数据已排序。saveHeaderState 保存列宽、顺序、隐藏、比例以及 pane/冻结配置；不保存业务数据或任意视图开关，ColumnResizeMode 是独立配置。restore 先验证再应用，检查 bool 返回值。树的展开状态与业务持久化需求单独处理，不要假定 header state 保存树展开。',
    'src/widgets/virtualtableview.cpp src/layout/headergeometry.cpp',
    'examples/table_capabilities/20_header_persistence/example.cpp examples/table_capabilities/21_sorting/example.cpp examples/treetable_capabilities/25_header_persistence/example.cpp examples/treetable_capabilities/26_sorting_filtering/example.cpp', 'docs/header-persistence.md')
feature('tree-expansion', '树展开 / 收起 / 渐变 / 懒加载',
    'virtualtreeview virtualtreetableview treevisibilityindex',
    'expand collapse setExpanded toggleExpanded expandRecursively collapseAll setExpansionAnimationEnabled setExpansionAnimationDuration setExpansionFadeEnabled visibleRowCount',
    '框架使用真实布局动画并可渐变，fade 默认开启；无需截图遮罩。collapseAll 是批量收起入口，不承诺逐节点动画。visibleRowCount 是展开后的扁平序列，模型 row 仍相对父节点。懒加载由模型 canFetchMore/fetchMore 配合展开；使用标准结构通知，不手工重建可见节点 QWidget。切换根、模型变化由框架维护可见映射。',
    'src/index/treevisibilityindex.cpp src/widgets/treeexpansiontransition_p.h src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/tree_capabilities/07_expansion/example.cpp examples/tree_capabilities/22_lazy_loading/example.cpp examples/treetable_capabilities/08_expansion/example.cpp examples/treetable_capabilities/31_lazy_loading/example.cpp', 'docs/tree.md')
feature('branch', 'branch 图标 / 旋转 / 缩进 / 自绘分支',
    'virtualtreeview virtualtreetableview branchindicator',
    'setIndentation setBranchAreaVisible setBranchIndicatorsVisible setExpandedBranchIcon setCollapsedBranchIcon setBranchIconSize setBranchIconColor setBranchIndicatorAlignment setBranchIndicatorPadding setBranchIndicatorRenderer',
    '默认圆角等边三角形 10 逻辑像素。只设置展开/收起其中一个图标时随展开动画旋转 90°；两个都设置时直接切换，自定义 renderer 自行处理。隐藏 branchArea 去掉自身 branch 格但保留祖先缩进；仅隐藏 indicators 保留占位。Tree 的 setIndentLeafItems(false) 可省略全叶子兄弟组占位，TreeTable 没有该开关。常规大小/颜色/对齐/padding 用便捷接口，只有完整自绘分支/连接线才实现 BranchIndicatorRenderer。',
    'src/widgets/branchindicator.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/tree_capabilities/09_indentation/example.cpp examples/tree_capabilities/10_branch_icons/example.cpp examples/tree_capabilities/11_branch_renderer/example.cpp examples/treetable_capabilities/10_indentation/example.cpp', 'docs/tree.md')
feature('node-spacing', '节点独立上下间距',
    'virtualtreeview virtualtreetableview',
    'NodeRowSpacingRole NodeRowSpacingBelowRole NodeRowSpacingAboveRole setRowSpacing',
    '模型列 0 的 BelowRole 覆盖节点下方间距，缺失/负值回退 rowSpacing；AboveRole 增加节点上方间距，缺失/负值为 0。相邻节点的 below+above 可能叠加，末个可见节点没有尾部普通 gap，首节点没有普通 above gap；不要把两份 role 当作同一间距。',
    'src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/tree_capabilities/12_row_spacing/example.cpp examples/treetable_capabilities/14_spacing/example.cpp', 'docs/tree.md')
feature('drag', '原生拖放 / 手柄起拖 / 预览 / Move',
    'virtualitemview types virtuallistview virtualtableview virtualtreeview virtualtreetableview virtualwrapview virtualflowtableview virtualmasonryview',
    'setDragEnabled setAcceptDrops setDragStartMode canStartDrag startDrag DragOptions setDragDropActions setDefaultDropAction setMoveRemovesSourceRows',
    'setDragEnabled 开启拖出，QWidget::setAcceptDrops 开启接收；模型需 flags、mimeData、mimeTypes、canDropMimeData/dropMimeData 与支持的 actions。Automatic 按阈值启动；Manual 由手柄调用 startDrag(index[, options])，不需框架注册按钮。默认预览按 SelectRows/SelectItems，DragOptions 可改来源/区域/热点，默认热点在鼠标处。Move 是否删除源行必须与模型协议一致：模型已移动不得再删；默认不删除。框架负责拖动 pin、hover 暂停和结束恢复。',
    'src/core/virtualitemview.cpp src/core/dragpreview_p.h src/core/dragmoveselection_p.h',
    'examples/list_capabilities/15_drag_drop/example.cpp examples/table_capabilities/24_drag_indicator/example.cpp examples/wrap_capabilities/07_drag_drop/example.cpp examples/masonry_capabilities/11_drag_drop/example.cpp', 'docs/interaction.md')
feature('drag-feedback', '拖动指示器 / 让位动画 / 树末级歧义',
    'virtualitemview virtuallistview virtualtableview virtualtreeview virtualtreetableview',
    'setDropIndicatorShown setDropIndicatorColor setDropIndicatorWidth setDropIndicatorPenStyle setDragFeedbackMode setDragReorderAnimationDuration setDragReorderAnimationEasingCurve',
    '支持的列表/树/表格默认 RowMoveAnimation；不满足内部单项 Move 等条件时自动退回指示器。动画只预览，成功 drop 后模型才改顺序。树单节点拖动先收起源节点，仅留单行占位；父同级/子同级末尾目标由缩进和允许目标描边区分，模型拒绝的目标不会为了画框而变成可投放。Flow/Masonry/Wrap 无专用行让位模式，不要套用 list 的枚举。',
    'src/core/rowdragpreview_p.h src/widgets/treedragtransition_p.h src/widgets/treedragplaceholder_p.h src/widgets/virtualtableview.cpp',
    'examples/list_capabilities/16_drag_animation/example.cpp examples/tree_capabilities/20_drag_animation/example.cpp examples/table_capabilities/25_drag_animation/example.cpp examples/treetable_capabilities/30_drag_animation/example.cpp', 'docs/interaction.md')
feature('cell-drop', 'cell 空白格放下 / 同时行间插入',
    'virtualtableview',
    'setDropTargetMode DropTargetMode',
    'SelectItems 下选择 OnCell 或 BetweenRowsOrOnCell；组合模式 cell 上下四分之一做行间插入，中部投到 cell。OnCell 调模型时 parent=cell、row=column=-1，由模型决定填充/替换/拒绝。SelectRows 始终行间插入，TreeTable 保留层级节点投放规则，不能靠此模式把树节点当普通二维空格。',
    'src/widgets/virtualtableview.cpp',
    'examples/table_capabilities/26_cell_drop/example.cpp', 'docs/interaction.md')
feature('editor', '覆盖编辑器 / 单双击 / F2 / 按钮 / 自定义 role',
    'virtualitemview itemeditoradapter widgetadapter tablewidgetadapter',
    'setEditTriggers editTriggers edit createEditor setEditorData updateEditorGeometry commitEditorData destroyEditor commitEditor closeEditor editingIndex editorWidget',
    '所有入口要求 ItemIsEnabled + ItemIsEditable。默认 DoubleClicked|EditKeyPressed(F2)，adapter 可按 index 重载触发方式；按钮/外部直接 edit(index)。默认 editor 为 null，业务实现 createEditor；默认数据经 Qt USER property↔EditRole，可重载提交到 UserRole。默认几何覆盖整格/项；无需 stack layout。框架处理焦点、Enter/Esc/Tab、校验和销毁；提交返回 false 保持编辑。编辑器不进入显示控件复用池，不应滚动即丢草稿。',
    'src/core/virtualitemviewediting.cpp src/core/itemeditoradapter.cpp src/widgets/virtualtableview.cpp',
    'examples/list_capabilities/23_editors/example.cpp examples/table_capabilities/33_editors/example.cpp examples/tree_capabilities/29_editors/example.cpp examples/treetable_capabilities/38_editors/example.cpp', 'docs/interaction.md')
feature('popup', 'Popup 编辑 / 坐标 / 统一取消 / 编辑覆盖范围',
    'itemeditoradapter virtualitemview types',
    'editRequest EditRequestContext cancelEditRequest closeEditor indentationWidth branchWidth',
    'editRequest 返回 true 表示自行处理 Popup，false 回退 createEditor。context 含 binding、view、viewport、三个坐标系的完整 rect 和 finish 回调。正常确认/关闭后调用 request.finish，仅结束对应会话，不提交模型；业务自行写数据。框架在滚动、目标失效、模型/adapter 变化、隐藏等取消条件下调用 cancelEditRequest，业务在此关闭 Popup；外部 closeEditor 同样转发。Popup 时 editingIndex 有效、editorWidget=null、commitEditor=false。跨事件保留 QPointer/QPersistentModelIndex。树编辑可用 indentationWidth+branchWidth 裁掉装饰占位并 clamp 到 rect.width。',
    'src/core/virtualitemviewediting.cpp src/widgets/virtualtreeview.cpp src/widgets/virtualtreetableview.cpp',
    'examples/*_capabilities/*editors/example.cpp', 'docs/interaction.md')
feature('scroll', '滚动 / 定位 / 几何 / 命中 / 平滑滚动',
    'virtualitemview virtualtableview scrollmapper',
    'scrollTo setVerticalOffset scrollByPixels setWheelScrollMode setWheelScrollPixels setWheelScrollItems indexAt visualRect widgetForIndex indexForWidget setHorizontalOffset',
    'indexAt 输入 viewport 坐标，visualRect/cellRect 可在屏外仍有效；widgetForIndex/cellWidget 仅查询物化控件，空指针不表示模型项或选择丢失。长期保存项目用 QPersistentModelIndex。64 位 offset 通过 ScrollMapper 映射到滚动条；不要自己用 scrollbar int 作为巨大模型的逻辑偏移。无内部范围的滚轮交外层。smoothscroll 是可选依赖，仅检测到该库时编译/显示对应章节，不是核心内置 timer 动画接口。',
    'src/core/scrollmapper.cpp src/core/virtualitemview.cpp src/core/wheeleventforwarding_p.h src/widgets/virtualheaderview.cpp',
    'examples/*_capabilities/*scrolling/example.cpp examples/list_capabilities/11_item_events/example.cpp', 'docs/interaction.md')
feature('pin', '屏外保留 / 预加载 / 焦点 / 诊断',
    'virtualitemview widgetrecycler virtualtableview',
    'setOverscan setColumnOverscan setItemPinned pinWidget unpinWidget setMaxPinnedItems stats materializedItemCount createdWidgetCount pooledWidgetCount',
    'overscan 增大意味着更多真实控件，CellWidgets 还乘以列数；先测再调。显式 pin 会保留屏外控件直到解除；拖动临时 pin 与业务 pin 独立，maxPinnedItems 是警告阈值而非自动回收上限。焦点/输入法等活动可能需要保留，不能为了降低统计数强删。状态和选择以模型为真源，不以物化控件数量为真源。',
    'src/core/virtualitemview.cpp src/recycler/widgetrecycler.cpp src/widgets/virtualtableview.cpp',
    'examples/*_capabilities/*virtualization/example.cpp examples/*_capabilities/*pinning/example.cpp', 'docs/accessibility-guide.md')
feature('performance', '性能日志 / 卡顿 / 统计而非逐帧打印',
    'virtualitemview',
    'setPerformanceLoggingEnabled setLifecycleLoggingEnabled lifecycleLog clearLifecycleLog stats',
    '性能探测默认关闭；启用开关并通过 Qt 日志类别筛选 viv.performance.refresh、viv.performance.paint，详细阶段表用 viv.performance.details。按视图/类别每秒汇总，计入同步 adapter 回调；禁用开关或过滤类别时跳过对应计时。阶段可能包含子阶段，不能把嵌套耗时相加当总时间。先区分布局/物化/回收/业务 callback/paint，再优化；生命周期日志是另一开关，最多保留 400 条，不要在 bind 内每项输出日志。',
    'src/core/performanceprobe.cpp src/core/performanceprobe_p.h src/core/virtualitemview.cpp src/widgets/virtualtableview.cpp',
    'examples/*_capabilities/*diagnostics/example.cpp', 'docs/accessibility-guide.md')
feature('flow', 'Flow 等宽换列 / 固定高度 / 比例',
    'virtualflowtableview',
    'setCellWidthRange setCellHeightMode setFixedCellHeight setCellAspectRatio columnCount displayRowCount displayRowForIndex displayColumnForIndex',
    '显示列数随可用宽度计算，列均分铺满；不是多列模型的表格，不具备表头/冻结列 API。模型每行代表一个项目，换行不改索引身份。宽高比为 width/height；内核 setItemHeightMode 与自动 sizeHint 测量不支持，使用本视图 CellHeightMode。支持共享 auto height、状态、选择、拖放、分割线。',
    'src/widgets/virtualflowtableview.cpp src/layout/flowtablelayout.cpp',
    'examples/flow_capabilities/02_cell_sizes/example.cpp examples/flow_capabilities/09_images/example.cpp', 'docs/flow-table.md')
feature('masonry', 'Masonry 最短列 / 图片估值 / 高度更新',
    'virtualmasonryview',
    'setColumnCount setCellWidthRange setItemHeightMode setFixedItemHeight setItemAspectRatio setItemEstimatedSize setItemEstimatedSizes setItemHeight invalidateItemSize setItemHeightAnimationEnabled',
    '本视图 ItemHeightMode 是 Fixed/AspectRatio/AdapterEstimate，与基类枚举不同；禁止套用内核自动测高。图片加载前给估值，结果到达后用 setItemHeight 或 invalidateItemSize 更新；批量估值用 setItemEstimatedSizes 减少重排。高度动画默认关闭。空间中的前后邻居不一定是模型相邻行，拖放仍服从模型目标与 flags，不是任意视觉位置都能接受。',
    'src/widgets/virtualmasonryview.cpp src/layout/masonrylayout.cpp',
    'examples/masonry_capabilities/04_height_modes/example.cpp examples/masonry_capabilities/05_height_updates/example.cpp examples/masonry_capabilities/12_images/example.cpp', 'docs/masonry.md')
feature('wrap', 'Wrap 不等宽标签 / 自然尺寸 / 行对齐',
    'virtualwrapview wraplayout wrapwidget',
    'setFixedItemHeight setItemWidthRange setItemWidth setItemWidths clearItemWidth invalidateItemWidths setItemSpacing setRowSpacing setLineAlignment',
    'VirtualWrapView 宽度优先级：显式覆盖→SizeHintRole→adapter estimate→100px，再按范围/行宽裁剪；批量 setItemWidths 避免逐项重排。水平 itemSpacing 与垂直 rowSpacing 可分别设置，统一 item height；SelectRows 仍选择模型项，不会选整条显示行。WrapLayout 按真实子控件 sizeHint/约束排布，隐藏控件通常不占位；WrapWidget 接管控件所有权，takeWidget 交回调用者，clear 删除。',
    'src/widgets/virtualwrapview.cpp src/layout/wrapitemlayout.cpp src/layout/wraplayout.cpp src/widgets/wrapwidget.cpp',
    'examples/wrap_capabilities/02_geometry/example.cpp examples/wrap_capabilities/09_wrap_layout/example.cpp examples/wrap_capabilities/10_wrap_widget/example.cpp', 'docs/wrap.md')
feature('accessibility', '无障碍 / 虚拟索引映射',
    'accessibility virtualitemview',
    'installAccessibilityFactory removeAccessibilityFactory',
    'QApplication 创建后调用 installAccessibilityFactory 安装虚拟项目的无障碍桥接；业务控件仍需正确 accessibleName、角色和可操作语义。先阅读 accessibility.h 与专门指南，不要为每个逻辑项目常驻一个隐藏 QWidget 来暴露语义。',
    'src/widgets/accessibility.cpp', '', 'docs/accessibility-guide.md')

feature('styled-table', '扩展自绘状态形状 / StyledTableView',
    'styledtableview virtualtableview',
    'StyledTableView StyledTableRowWidget StyledTableCellHost visualStateBackgroundPath paintVisualStateBackground setBackgroundCoversGridLines',
    '普通 hover/selection、颜色和圆角直接用 VirtualTableView 的函数即可。只有自定义背景形状/绘制才考虑 StyledTableView 和配套 row/cell host；默认状态圆角为 6 逻辑像素。不要为了常规背景重复引入一套业务状态动画。',
    'src/widgets/styledtableview.cpp', '', 'docs/table.md')
feature('layout-kernel', '布局与尺寸索引扩展',
    'layoutpolicy listlayout sizeindex scrollmapper treevisibilityindex virtualitemview',
    'LayoutPolicy ListLayout SizeIndex FixedSizeIndex BlockSizeIndex ScrollMapper TreeVisibilityIndex',
    '这些公开基础类型服务于布局扩展和定位查询。正常业务接入通过具体视图和模型通知；不要同时手工操作 sizeIndex/resetItems 和模型结构来维护两份状态。ListLayout 持有 SizeIndex，内部行布局区分固定/动态尺寸；TreeVisibilityIndex 管理展开后的映射。此层没有“任意改内部布局后自动保持所有业务状态”的契约，扩展时逐项阅读源码。',
    'src/layout/listlayout.cpp src/index/sizeindex.cpp src/index/treevisibilityindex.cpp src/core/scrollmapper.cpp', '', 'docs/models.md')
