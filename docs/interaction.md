# 选择、拖放与控件焦点

列表、表格和树共用 `VirtualItemView` 的选择、滚动和拖放接口。交互仍以模型的 `QModelIndex` 为身份，不以当前复用的 `QWidget *` 为身份。

## 选择与定位

```cpp
view.setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
const QModelIndex first = view.model()->index(0, 0);
view.setCurrentIndex(first);
view.scrollTo(first);
```

`selectionModel()` 返回 Qt 的 `QItemSelectionModel`；也可通过 `setSelectionModel()` 共享一个与当前模型匹配的选择模型。视图发出 `clicked`、`doubleClicked` 和 `activated`。表格按行还是按单元格选择由 `SelectionBehavior` 控制。

表格的行号点击选择默认开启，列头点击选择默认关闭，分别通过 `setVerticalHeaderSelectionEnabled()` 和 `setHorizontalHeaderSelectionEnabled()` 配置。它们与主体选择行为独立，支持当前选择模式和 Ctrl/Shift，结果仍写入同一个选择模型。启用列头选择后，点击列头优先选择而不排序。参考 [表头选择说明](headers.md#点击表头选择) 和 [第 12 节源码](../examples/table_capabilities/12_selection/example.cpp)。

## Hover、选中背景与圆角

`VirtualTableView` 可以直接绘制完整的 hover 和选中背景，无需为常用圆角样式继承视图：

```cpp
table.setVisualStateBackgroundVisible(true);
table.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
table.setVisualStateCornerRadius(6); // 逻辑像素；0 为直角，负数按 0 处理
table.setVisualStateCornerRadiusIncludesHeaders(true); // Default: true.
table.setHoverBackgroundColor(QColor(235, 240, 247));
table.setSelectedBackgroundColor(QColor(190, 215, 245));
table.setVisualStateAnimationDuration(120); // 0 关闭过渡动画
table.setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic); // 快速起步
```

`SelectionBehavior::SelectRows` 下，hover 和 selected 都按整行显示；`SelectItems` 下都按单元格显示。只需设置选择行为，无需单独设置背景范围；`visualState()` 和 adapter 收到的状态也遵循相同规则，与整行或单元格物化方式独立。行和单元格控件应保持背景透明；自绘时可调用 `setVisualStateBackgroundVisible(false)` 关闭框架背景。

圆角默认扩展到内置表头：按行高亮时，行号条绘制左侧圆角，内容区绘制右侧圆角；列表头选中背景绘制四角圆角。行号条隐藏或关闭状态背景时，内容区恢复自身的左右圆角。调用 `setVisualStateCornerRadiusIncludesHeaders(false)` 可让表头状态背景保持直角，内容区仍使用配置的圆角。按格高亮时，行号条和各单元格分别绘制完整四角圆角。参考 [第 19 节表头样式源码](../examples/table_capabilities/19_custom_header/styled_headers.cpp) 的圆角与列头选择开关。

hover 身份变化会立即驱动动画，不依赖后续绘制或 adapter 主动查询状态；快速离开时从实际经过时间对应的进度继续淡出。所有视图共用 `setVisualStateAnimationEasingCurve(const QEasingCurve &)` 与 `visualStateAnimationEasingCurve()`，控制 hover 和 selected 的进入、离开曲线，默认使用 `OutCubic`、时长 180 ms。修改曲线会重置当前过渡，与修改时长的行为一致。

所有视图默认启用框架 hover/selected 背景，普通视图的圆角默认为 0，`StyledTableView` 保留默认 6 像素圆角。表格按行选择时，状态背景默认贯通横向列间距，可通过 `setVisualStateThroughColumnSpacing(false)` 关闭，参考 [第 09 节源码](../examples/table_capabilities/09_spacing/example.cpp)。按单元格选择时只绘制单元格背景；纵向行间距和表头间距保持自身外观。其他视图仍只在 item 内绘制状态背景。颜色和动画继续使用基类现有接口。表格需要完全自定义形状或绘制方式时，可重写 `paintStateBackgroundLayer()`。

`VirtualTreeTableView` 同样支持上述背景开关、圆角和颜色接口，树的背景范围继续由 `setVisualStateBackgroundExtent()` 控制。`VirtualTreeView` 已提供同名的背景开关、圆角和颜色接口。最小参考代码见 [表格状态示例](../examples/table_capabilities/10_visual_states/example.cpp)。

## 拖放

模型必须提供 `flags()` 中的 `ItemIsDragEnabled` / `ItemIsDropEnabled`、MIME 数据和 `dropMimeData()`；视图只负责手势、目标命中、指示器和边缘自动滚动。最简单的复制拖放可直接使用 Qt 已实现 MIME 处理的 `QStandardItemModel`：

```cpp
#include <QStandardItemModel>

auto *model = new QStandardItemModel(&view); // view 管理模型生命周期
model->appendRow(new QStandardItem(QStringLiteral("第一项")));
model->appendRow(new QStandardItem(QStringLiteral("第二项")));
view.setModel(model); // view 已按入门页设置 WidgetAdapter
view.setDragEnabled(true);
view.setDropIndicatorShown(true);
view.setDragDropActions(Qt::CopyAction);
view.setDefaultDropAction(Qt::CopyAction);
```

拖动指示器的常见样式可直接配置，所有视图共用这些接口，同时作用于插入线和落点边框：

```cpp
view.setDropIndicatorColor(QColor("#409eff"));
view.setDropIndicatorWidth(2);
view.setDropIndicatorPenStyle(Qt::DashLine);
```

默认颜色为 `QPalette::Highlight`，传入无效 `QColor()` 可恢复跟随调色板；`dropIndicatorColor()` 返回配置值，因此默认返回无效颜色。默认宽度为 2 个逻辑像素，默认线型为 `Qt::SolidLine`。宽度为 0（负数归零）或 `Qt::NoPen` 时不绘制，落点接收和自动滚动照常工作。支持 Qt 内置实线、虚线、点线、点划线和双点划线；`Qt::CustomDashLine` 或其他不支持的值回退为实线。插入线的实际区域随宽度调整，落点边框向内绘制；颜色、宽度、线型的修改也会更新当前有效落点的指示器。

目标规则：列表按行上/下半段插入；表格按当前选择行为按行或单元格定位；树既可插在节点之间，也可放到节点上成为子节点。自定义模型需在 `canDropMimeData()` 中校验目标，并在 `dropMimeData()` 中修改数据。成功后视图发出 `itemDropped(parent, row, column, action)`。`MoveAction` 的源行通常由模型移动；跨视图拖放若需视图删除源行，可启用 `setMoveRemovesSourceRows(true)`，但模型已自行移动行时不要启用，以免重复删除。

启用源行删除后，两个 `VirtualItemView` 之间（含同一视图）的成功 Move 若将同级源行按顺序插入为一个新行块，框架会在源行删除后把原有选择及当前项转移到新条目。记录的是实际插入行的持续索引，源行删除引起的行号变化不会影响对应关系；其他选中项保留。Automatic、Manual 和两个 `startDrag()` 重载共用此逻辑。复制、取消、拒绝、源行删除失败不会转移选择；覆盖已有单元格、多父节点、模型重置或无法确定映射的自定义落点不自动推断。`itemDropped()` 在源清理前发出，最终选择以 `startDrag()` 返回后为准。

### 表格的单元格落点

`VirtualTableView` 提供落点模式，业务无需重写落点命中、指示框绘制或自投放判断：

```cpp
table.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
table.setDropTargetMode(viv::VirtualTableView::DropTargetMode::BetweenRowsOrOnCell);
// 单元格移动由 model 更新并清空源格，不删除整个源行。
table.setMoveRemovesSourceRows(false);
```

| `DropTargetMode` | 命中与指示器 |
| --- | --- |
| `BetweenRows`（默认） | 按行的上下半段插入，显示目标列的插入线 |
| `OnCell` | 放入已有单元格，显示单格边框；格外区域没有落点 |
| `BetweenRowsOrOnCell` | 单元格上下四分之一区域插入，中间区域放入该格 |

`SelectRows` 始终使用整行插入；上述设置适用于 `VirtualTableView` 的单元格选择，与 `RowWidgets` / `CellWidgets` 物化方式无关。`VirtualTreeTableView` 保持树节点之间插入、投放到节点成为子节点的规则。框架处理合并格锚点以及冻结列、滚动 pane 的指示器裁剪。

放入单元格时，`DropTarget` 的 `parent` 是目标格，`row = column = -1`，`ontoItem = cellDrop = true`；该目标的 `isValid()` 为 true。框架以同样的参数调用 model 的 `canDropMimeData()` / `dropMimeData()`，并在成功后发出 `itemDropped()`。model 可通过 `parent.isValid() && row == -1 && column == -1` 区分填格；行间插入仍传入插入位置和目标列。框架拒绝内部 Move 投放到源格或其后代，但允许单元格插入到源行附近，因为源行可能保留；整行移动仍拒绝无效的相邻位置。

数据规则由 model 决定，包括空白的定义、允许覆盖或交换、权限、插入和源格清理。仅配置落点模式不会让默认 model 自动支持单元格移动，仍需实现对应的 MIME 与数据处理。

`drag_drop` 示例使用组合模式：中间区域仅接受空白格，目标有数据或越界时拒绝；边缘投放插入新行并放入单元格。两种方式移动后都只清空源格，Ctrl 复制保留源格，多格选择保持相对位置；插入后的源位置通过持续索引确定。此模式使用本表内部的专用 MIME，并关闭 `setMoveRemovesSourceRows()`。示例的表格子类只保留背景绘制；勾选“整行拖动”后仍使用框架的行插入和源行清理。

### 列表与表格的行移动动画

`VirtualListView` 和 `VirtualTableView` 默认使用行移动动画，让经过的行滑动让位，留下源行大小的空位。表格需要使用整行选择；也可显式配置动画参数：

```cpp
table.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
table.setDragFeedbackMode(viv::VirtualTableView::DragFeedbackMode::RowMoveAnimation);
table.setDragReorderAnimationDuration(180); // 默认 180ms；0 表示立即让位
table.setDragReorderAnimationEasingCurve(QEasingCurve::OutCubic); // 默认曲线
// 切回指示器：setDragFeedbackMode(DragFeedbackMode::Indicator)
```

动画用于同一表格内的单行 Move，支持 `RowWidgets`、`CellWidgets` 及冻结列；源行和落点须位于滚动行区域。复制、多行、跨视图、单元格投放、跨冻结行区域、有 span provider 的表格及树表沿用指示器。模式独立于 `setDropIndicatorShown()` 和指示器颜色、线宽、线型设置；这些设置只作用于指示器。

拖动过程中只改变控件、状态背景和分隔线的显示位置，不提前修改 model 顺序。放下仍调用 model 的 `dropMimeData()`；取消、离开或拒绝落点后恢复原位置。`Automatic` 与 `Manual` 共用同一逻辑。动画配置不会改变 MIME、选择或源行删除规则。表格默认预览跟随选择行为：`SelectRows` 截取整行可见内容，`SelectItems` 截取起拖单元格，与物化方式无关；单元格物化的整行预览由框架截取 viewport 中该行所属区域，包含冻结列和当前可见的滚动列。

`drag_drop` 工具栏可切换反馈模式、物化方式、时长与曲线；勾选“整行拖动”后拖动表格的一行即可对比。启动参数 `--row-animation` 会同时启用整行语义与行移动动画。

`VirtualListView` 提供同名接口，默认模式同样为 `VirtualListView::DragFeedbackMode::RowMoveAnimation`，可用 `Indicator` 切回插入指示器。列表无需设置 `SelectRows`；动画适用于本视图当前 `rootIndex()` 下滚动区域内的单行 Move，复制、多行、跨视图及跨冻结行区域沿用指示器。根节点、模型或模型结构改变时清理当前动画。`list_capabilities --example 15` 单独演示指示器拖动与样式配置，`--example 16` 单独演示行移动动画及时长、曲线配置。

### 手动触发与自定义预览

`DragStartMode::Automatic` 是默认模式，鼠标按下后移动超过 Qt 的拖动距离就开始拖动。只允许从业务手柄启动时，保留 `setDragEnabled(true)`，再设置手动模式：

```cpp
view.setDragEnabled(true);
view.setDragStartMode(viv::VirtualItemView::DragStartMode::Manual);
```

`Manual` 只关闭框架根据鼠标移动自动启动的入口。业务自行处理手柄按下、拖动距离及权限，在满足条件后调用 `startDrag()`。模型的 `ItemIsDragEnabled`、MIME、落点检查、指示器和边缘自动滚动继续生效；无需通过临时 role 切换拖动权限。切换触发模式会清除尚未开始的鼠标按下记录。

起拖项已选中时，框架传递选择集中允许拖动的索引；未选中时，`SelectRows` 传递源行所有允许拖动的列（包括隐藏和屏外列），`SelectItems` 只传递起拖单元格。手动手柄无需先选中源行，框架也不会为收集 MIME 修改选择集。树节点使用该节点父项下的列范围。

表格默认预览包含对应区域实际显示的网格线、间距边界和冻结区域分隔线，适用于行与单元格物化模式；超出 viewport、所属 pane 或控件遮罩的部分保持透明。显式指定 `previewWidget` 时仍直接截取业务提供的控件。

`startDrag(index)` 使用默认的行／单元格预览。`DragOptions` 未指定截图控件和区域时也使用同一默认规则，只传 `globalPressPosition` 即可保留手柄的抓取点。热点默认位于鼠标按下的位置，让预览跟随原有抓取点移动。自动模式记录按下时的全局位置；手动调用有匹配的按下记录时使用该记录，否则使用起拖入口处的鼠标位置。需要覆盖默认截图或提供组合预览时，使用 `viv::DragOptions` 指定来源和区域：

```cpp
// 业务手柄超过拖动距离后调用；以下变量由业务保存/计算。
const QPersistentModelIndex source = currentBoundIndex;
if (source.isValid()) {
    // 如有平滑滚动控制器，在这里先停止滚动。
    viv::DragOptions options;
    options.previewWidget = view.viewport();
    options.previewRect = rowPreviewRect(source); // 业务函数，返回 viewport 局部坐标
    options.usePressPosition = true; // 默认开启
    options.globalPressPosition = globalPressPosition; // 手柄按下时保存的全局位置
    const Qt::DropAction action = view.startDrag(source, options);
    // action 是最终投放结果；取消或请求无效时为 Qt::IgnoreAction。
}
```

| 参数 | 规则 |
| --- | --- |
| `previewWidget` | 不接管所有权；为空时使用视图默认的 `dragSourceWidget(index)` |
| `previewRect` | 截图控件的局部逻辑像素坐标；未指定控件且矩形为空时使用默认行／单元格预览，显式指定控件且矩形为空时截取整个控件；非空矩形与控件边界求交，完全无交集时不设置预览图 |
| `usePressPosition` | 默认 true，使用鼠标抓取点；设为 false 切回居中热点 |
| `globalPressPosition` | `OptionalPoint` 类型的可选全局按下位置，赋值 `QPoint` 即可，调用 `reset()` 可清除；不传时使用匹配的按下记录或起拖时的鼠标位置，`QPoint(0, 0)` 也是有效值。转为截图控件局部坐标后减去实际裁剪区域左上角，无需乘以缩放比例，位置在截图外时保留该偏移 |

显式指定 `previewWidget` 或非空 `previewRect` 时，以业务提供的截图配置为准。预览只影响图像和热点，不改变选择集、MIME 或落点的行/单元格语义；若要改变这些语义，仍使用选择行为及模型接口。

框架按源索引临时 pin 对应的物化项：行模式使用整行身份，单元格模式使用合并格锚点。即使截图来自 viewport，也不会把 viewport 当作 pin 对象。其他行或单元格不会因为被截入预览而固定。业务已有的显式 pin 会保留，拖动结束只释放本次临时 pin，同时清除指示器并停止边缘自动滚动。准备 MIME、截图和执行原生拖动期间的重复 `startDrag()` 请求会返回 `IgnoreAction`。

普通 hover 更新从起拖准备阶段暂停，预览仍按当时的正常外观截图。截图完成后，框架立即清空普通 hover 及其动画进度，并通过 `refreshVisualStates()` 通知所有物化行或单元格（包括树表的 `CellWidgets` 模式）；选择状态和业务勾选数据不变。子控件事件、物化刷新和拖动自动滚动不会重新激活普通 hover，落点和插入指示线继续正常更新。放下、取消、拒绝或起拖失败后，框架在落点处理和源项清理结束后恢复 hover，按当前鼠标位置重新计算并通知控件；鼠标保持静止也能恢复。业务无需维护额外的拖动 hover 状态或在 `startDrag()` 前后刷新 hover。

手柄事件、整行区域计算、权限、撤销及排序保存由业务实现。框架不注册按钮，也不管理业务编辑生命周期。

## 焦点、编辑器与弹窗

滚动离开可见区的控件会解绑并回收。行内编辑器、弹窗或尚未结束的异步活动需要保持原控件时，在任务期间 pin 住对应模型项：

```cpp
const QModelIndex editing = view.model()->index(0, 0);
view.setItemPinned(editing, true);
// 编辑结束或异步回调完成后：
view.setItemPinned(editing, false);
```

也可用 `pinWidget()` / `unpinWidget()`。`setMaxPinnedItems()` 可帮助发现忘记解除的 pin。控件被复用时可能仍有未结束的输入法、弹窗或异步活动；adapter 应在 `unbindWidget()` 中清理与旧模型项关联的状态。
