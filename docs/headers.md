# 表头、排序与行列换序

`VirtualTableView` 默认提供水平列头和垂直行号条。两者可显示/隐藏，列头读取模型的 `headerData()`。默认表头使用当前 Qt 样式；需要按钮、筛选器等真实控件时，可换成 `VirtualHeaderView` 与 `HeaderWidgetAdapter`。

## 排序、列宽与状态

```cpp
table.setSortingEnabled(true);
table.setSortIndicator(0, Qt::AscendingOrder);
table.setColumnResizeEnabled(true);         // 拖边界改列宽，默认开启
table.setVerticalHeaderResizeEnabled(true); // 拖行号边界改行高，默认开启

QByteArray saved = table.saveHeaderState();
table.restoreHeaderState(saved);
```

排序交给模型的 `sort()`；若模型不实现排序，显示排序指示器并不会自行重排数据。状态保存/恢复包含列宽、顺序、隐藏、排序和滚动偏移等表格状态；恢复时表格结构应与保存时匹配。

## 拖动换序

```cpp
table.setColumnDragEnabled(true);         // 默认关闭；改的是视觉列顺序
table.setVerticalHeaderDragEnabled(true); // 默认关闭；改的是模型行顺序
```

列顺序写入表头几何，不要求模型移动列。行号条换序会调用 `model()->moveRows()`；普通模型若不实现它，拖动预览会复位。最小模型如下，`sourceRow()` 保证拖动后整行内容跟着身份移动：

```cpp
#include <virtualitemviews/reorderabletablemodel.h>
#include <QStringList>

class OrderModel : public viv::ReorderableTableModel
{
public:
    explicit OrderModel(QObject *parent = nullptr)
        : viv::ReorderableTableModel(3, 1, parent),
                   values{QStringLiteral("甲"), QStringLiteral("乙"), QStringLiteral("丙")} {}

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return values.at(sourceRow(index.row()));
    }

private:
    QStringList values;
};

auto *orderModel = new OrderModel(&table);
table.setModel(orderModel);
table.setVerticalHeaderDragEnabled(true);
```

`ReorderableTableModel` 只保存行身份顺序，不保存业务数据；插入/删除时仍须同步业务数据。视图尚无模型时开启行拖动会创建一个空的内部模型，随后设置业务模型会覆盖它。

## 换序动画

程序调用 `moveColumn()` 默认立即完成。需要滑动过渡时显式指定：

```cpp
table.setHeaderAnimationDuration(300); // 0 关闭动画
table.setColumnFollowsHeaderVisual(true);
table.moveColumn(2, 0, viv::VirtualTableView::MoveAnimation::Animate);
```

动画只影响显示位置；`columnGeometry()`、命中测试和滚动范围始终读取已提交几何。只有能报告视觉位置的 widget 表头参与逐帧跟随；resize、滚动和 pane 变化即时生效。

## 自定义 section 控件

实现 `HeaderWidgetAdapter::createSection()` / `bindSection()`，再创建对应方向的 `VirtualHeaderView`。下面的 adapter 从当前模型读取列标题，模型被替换时会收到新的指针：

```cpp
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/virtualheaderview.h>
#include <QAbstractItemModel>
#include <QLabel>
#include <QPointer>

class ColumnHeaderAdapter : public viv::HeaderWidgetAdapter
{
public:
    void setLabelModel(QAbstractItemModel *model) override { labels = model; }
    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindSection(QWidget *widget, int column) override
    {
        static_cast<QLabel *>(widget)->setText(labels
            ? labels->headerData(column, Qt::Horizontal).toString() : QString());
    }
private:
    QPointer<QAbstractItemModel> labels;
};

auto *header = new viv::VirtualHeaderView(Qt::Horizontal);
header->setAdapter(new ColumnHeaderAdapter, true); // header 接管 adapter
table.setHorizontalHeader(header); // 表格接管 header 所有权
```

adapter 若读取排序几何，还可覆盖 `setGeometryModel()` 获取当前对象。`bindSection()` 可能反复调用，应完整重建控件显示状态；`unbindSection()` 负责清理旧 section 的异步活动。
