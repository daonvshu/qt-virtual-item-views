# 列表、行高与滚动

`VirtualListView` 接受普通 `QAbstractItemModel` 和 `WidgetAdapter`；入门代码见 [入门](getting-started.md)。默认只显示根节点下的行，可用 `setRootIndex()` 指向某个子树。

## 固定与动态行高

行高一致时设置 `setUniformItemHeight()`。行高由内容决定时，行控件必须根据当前内容返回尺寸。下面的文本控件按 300 px 文本宽度计算高度；代码放在已有 `QApplication` 的程序中：

```cpp
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QColor>
#include <QLabel>
#include <QPainter>
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

## 自绘悬停与选中背景

`view.visualState(index)` 返回独立的 `hovered` 和 `selected` 目标标志，以及 0–1 的 `hoverProgress`、`selectedProgress`。两种状态可以同时存在，进度从当前值平滑过渡到目标值，离开和取消选中时也会淡出。默认过渡时间为 180 ms；`setVisualStateAnimationDuration(0)` 可关闭动画。`setHoverBackgroundColor()` 与 `setSelectedBackgroundColor()` 分别设置提供给业务控件的颜色；视图不代替业务控件绘制背景。适配器绑定新行、状态、颜色或动画帧变化时会收到 `visualStateChanged()`；默认实现调用 `widget->update()`，若控件直接保存状态，可重写它：

```cpp
class StateRow : public QWidget {
public:
    using QWidget::QWidget;
    void setState(viv::VirtualItemView::VisualState state,
                  const QColor &hoverColor, const QColor &selectedColor) {
        state_ = state;
        hoverColor_ = hoverColor;
        selectedColor_ = selectedColor;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        painter.setOpacity(state_.hoverProgress);
        painter.fillRect(rect(), hoverColor_);
        painter.setOpacity(state_.selectedProgress);
        painter.fillRect(rect(), selectedColor_);
    }
private:
    viv::VirtualItemView::VisualState state_;
    QColor hoverColor_;
    QColor selectedColor_;
};

// 在 WidgetAdapter 子类中保存指向当前 view 的指针：
void visualStateChanged(QWidget *widget, const QModelIndex &index) override {
    static_cast<StateRow *>(widget)->setState(view->visualState(index),
        view->hoverBackgroundColor(), view->selectedBackgroundColor());
}
```

```cpp
view.setHoverBackgroundColor(QColor("#e7f4ed"));
view.setSelectedBackgroundColor(QColor("#b9d9f1"));
view.setVisualStateAnimationDuration(180);
```

控件复用时会重新绑定并重新通知状态。若自定义行控件包含不透明子控件，背景只会显示在未被子控件覆盖的区域；需要整行着色时，也要处理这些子控件的背景。

## 行间距

`setRowSpacing()` 在相邻行之间留白，默认间距为 0，末行后不加间距。行控件高度不包含间距，点击留白不会命中行。默认显示分割线：间距为 0 时绘制在相邻行的边界；有间距时绘制在留白的上下边缘。

```cpp
view.setRowSpacing(12);
view.setRowGridLinesVisible(true);
view.setRowGridLineWidth(2);
view.setRowGridLineColor(QColor("#5b7280"));
```

留白默认没有内容。需要在可见留白中放控件时，提供创建和重新绑定回调；控件离屏后会复用，绑定回调必须完整刷新它的内容。下面的 `view` 和 `model` 沿用本页前面的最小示例：

```cpp
view.setRowSpacingFactory(
    [](const QModelIndex &, QWidget *parent) -> QWidget * {
        return new QLabel(parent);
    },
    [](QWidget *widget, const QModelIndex &above) {
        static_cast<QLabel *>(widget)->setText(above.data().toString());
    });
```

工厂收到的是留白上方的行索引；控件被放在两条分割线之间，需要至少 `2 * 线宽 + 1` px 的间距才有可用高度。`setRowGridLinesVisible(false)` 可同时隐藏零间距行线和留白边线。

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
