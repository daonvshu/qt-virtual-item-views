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

## 焦点、编辑器与弹窗

滚动离开可见区的控件会解绑并回收。行内编辑器、弹窗或尚未结束的异步活动需要保持原控件时，在任务期间 pin 住对应模型项：

```cpp
const QModelIndex editing = view.model()->index(0, 0);
view.setItemPinned(editing, true);
// 编辑结束或异步回调完成后：
view.setItemPinned(editing, false);
```

也可用 `pinWidget()` / `unpinWidget()`。`setMaxPinnedItems()` 可帮助发现忘记解除的 pin。控件被复用时可能仍有未结束的输入法、弹窗或异步活动；adapter 应在 `unbindWidget()` 中清理与旧模型项关联的状态。
