# 列表、行高与滚动

`VirtualListView` 接受普通 `QAbstractItemModel` 和 `WidgetAdapter`；入门代码见 [入门](getting-started.md)。默认只显示根节点下的行，可用 `setRootIndex()` 指向某个子树。

## 固定与动态行高

行高一致时设置 `setUniformItemHeight()`。行高由内容决定时，行控件必须根据当前内容返回尺寸。下面的文本控件按 300 px 文本宽度计算高度；代码放在已有 `QApplication` 的程序中：

```cpp
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStringListModel>

class WrappingRow : public QLabel
{
public:
    explicit WrappingRow(QWidget *parent) : QLabel(parent) { setWordWrap(true); }
    QSize sizeHint() const override
    {
        const QRect textRect = fontMetrics().boundingRect(
            QRect(0, 0, 300, 100000), Qt::TextWordWrap, text());
        return QSize(320, qMax(32, textRect.height() + 12));
    }
};

class WrappingAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new WrappingRow(parent);
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<WrappingRow *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(320, 48); }
};

QStringListModel model({QStringLiteral("短内容"), QStringLiteral("另一行")});
WrappingAdapter adapter;
viv::VirtualListView view;
view.setAdapter(&adapter);
view.setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
view.setEstimatedItemHeight(48);
view.setAutoMeasureItemHeight(true);
view.setModel(&model);
view.show();
```

adapter 的 `estimatedSize(index)` 用于新插入行的初始估计。下面用 `QStringListModel::setData()` 改变一行的内容；它会发出 `dataChanged`，视图重绑并测量。自己的模型也应在数据变化后发出对应信号。

```cpp
const QModelIndex first = model.index(0, 0);
model.setData(first, QStringLiteral("更新后的多行内容，可以因文本变长而增加行高"),
              Qt::EditRole);
```

视口上方的高度变化会通过滚动锚点补偿，减少内容跳动。若高度来自异步图片或展开状态，先更新模型数据，再发出该行的 `dataChanged`；`bindWidget()` 应刷新控件内容和尺寸提示。

## 滚动与物化窗口

```cpp
view.setOverscan(2, 2); // 可见区上下各预加载两行
view.setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
view.setWheelScrollPixels(48);
view.scrollTo(model.index(1, 0), viv::VirtualItemView::ScrollHint::PositionAtCenter);
view.scrollByPixels(24);
```

滚轮默认按像素滚动；也可设置 `WheelScrollMode::Items`。`setVerticalOffset()` 使用像素偏移，逻辑滚动空间支持大于 32 位的内容高度。`visibleItemRange()` 返回当前滚动窗口，不包含 overscan。

## 屏外保留与诊断

编辑器、弹窗或异步任务仍需使用原控件时，可在离开视口前 pin；任务结束务必 unpin，否则每个 pin 都会持续占用一个控件。

```cpp
view.setItemPinned(model.index(0, 0), true);
view.setItemPinned(model.index(0, 0), false);
view.setMaxPinnedItems(50); // 超限时给出诊断警告
const viv::VirtualViewStats snapshot = view.stats();
```

也可用 `pinWidget()` / `unpinWidget()` 针对当前控件操作。`setLifecycleLoggingEnabled(true)` 可记录有界的 create/bind/unbind/recycle/pin 事件。解绑时 adapter 应停止与旧模型项关联的定时器和异步任务；正在编辑、持有焦点或打开弹窗的控件应在操作结束后及时解除 pin。

大量逻辑行仍使用相同接口；模型不必预先生成每一行的文本。例如自己的 `QAbstractListModel::rowCount()` 返回逻辑行数，而 `data()` 按 `index.row()` 即时生成显示内容。控件数量可通过 `view.stats()` 观察。
