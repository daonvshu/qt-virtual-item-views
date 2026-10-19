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

目标规则：列表按行上/下半段插入；表格按当前选择行为按行或单元格定位；树既可插在节点之间，也可放到节点上成为子节点。自定义模型需在 `canDropMimeData()` 中校验目标，并在 `dropMimeData()` 中修改数据。成功后视图发出 `itemDropped(parent, row, column, action)`。`MoveAction` 的源行通常由模型移动；跨视图拖放若需视图删除源行，可启用 `setMoveRemovesSourceRows(true)`，但模型已自行移动行时不要启用，以免重复删除。

### 手动触发与自定义预览

`DragStartMode::Automatic` 是默认模式，鼠标按下后移动超过 Qt 的拖动距离就开始拖动。只允许从业务手柄启动时，保留 `setDragEnabled(true)`，再设置手动模式：

```cpp
view.setDragEnabled(true);
view.setDragStartMode(viv::VirtualItemView::DragStartMode::Manual);
```

`Manual` 只关闭框架根据鼠标移动自动启动的入口。业务自行处理手柄按下、拖动距离及权限，在满足条件后调用 `startDrag()`。模型的 `ItemIsDragEnabled`、MIME、落点检查、指示器和边缘自动滚动继续生效；无需通过临时 role 切换拖动权限。切换触发模式会清除尚未开始的鼠标按下记录。

`startDrag(index)` 沿用原来的截图控件及行/单元格裁剪规则，热点默认位于鼠标按下的位置，让预览跟随原有抓取点移动。自动模式记录按下时的全局位置；手动调用有匹配的按下记录时使用该记录，否则使用起拖入口处的鼠标位置。需要整行截图或组合预览时，使用 `viv::DragOptions` 重载：

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
| `previewWidget` | 不接管所有权；为空时使用原有 `dragSourceWidget(index)` |
| `previewRect` | 截图控件的局部逻辑像素坐标；空矩形截整个控件，非空矩形与控件边界求交，完全无交集时不设置预览图 |
| `usePressPosition` | 默认 true，使用鼠标抓取点；设为 false 切回居中热点 |
| `globalPressPosition` | 可选的全局按下位置，赋值 `QPoint` 即可；不传时使用匹配的按下记录或起拖时的鼠标位置，`QPoint(0, 0)` 也是有效值。转为截图控件局部坐标后减去实际裁剪区域左上角，无需乘以缩放比例，位置在截图外时保留该偏移 |

新重载的空 `previewRect` 始终表示整个所选控件，不自动套用旧入口的单元格裁剪。预览只影响图像和热点，不改变选择集、MIME 或落点的行/单元格语义；若要改变这些语义，仍使用选择行为及模型接口。

框架按源索引临时 pin 对应的物化项：行模式使用整行身份，单元格模式使用合并格锚点。即使截图来自 viewport，也不会把 viewport 当作 pin 对象。其他行或单元格不会因为被截入预览而固定。业务已有的显式 pin 会保留，拖动结束只释放本次临时 pin，同时清除指示器并停止边缘自动滚动。准备 MIME、截图和执行原生拖动期间的重复 `startDrag()` 请求会返回 `IgnoreAction`。

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
