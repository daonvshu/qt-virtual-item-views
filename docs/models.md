# 模型接入与数据更新

视图使用 `QAbstractItemModel` 的标准索引和信号。业务模型负责数据及其变化；视图负责更新可见控件和布局，不需要业务调用 `reload()`。

## 数据变化

单项内容改变后从模型发出 `dataChanged(topLeft, bottomRight, roles)`。已物化且落在范围内的控件会重新绑定；动态行高模式也会重新测量对应行。插入、删除、移动行时使用模型的 `begin...` / `end...` 成对通知；重建全部数据时使用模型 reset。Qt 的 `QSortFilterProxyModel` 可直接作为视图模型，保持 Qt 自己的索引映射和排序/筛选行为。

```cpp
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

auto *sourceModel = new QStandardItemModel(2, 1, &view);
sourceModel->setData(sourceModel->index(0, 0), QStringLiteral("初始内容"));
auto *proxy = new QSortFilterProxyModel(&view);
proxy->setSourceModel(sourceModel);
view.setModel(proxy); // view 已设置 WidgetAdapter
sourceModel->setData(sourceModel->index(0, 0), QStringLiteral("更新内容"));
```

模型、proxy 和 adapter 默认由调用方持有，生命周期应长于视图；示例把两个模型作为 `view` 的子对象管理。模型替换、行删除或重置时，旧索引及其控件绑定会失效；业务异步回调应检查当前索引是否仍有效。

## 表格排序和行移动

`VirtualTableView::setSortingEnabled(true)` 允许通过表头调用模型的 `sort()`。排序算法和数据重排属于模型；表头只表达排序列与方向。

行号条拖动换序同样属于模型。开启 `setVerticalHeaderDragEnabled(true)` 后，视图提交时调用 `moveRows()`。普通 `QAbstractItemModel` 默认拒绝该操作；可继承 `ReorderableTableModel` 保存“视图行到数据行”的映射，并在 `data()` 中使用 `sourceRow(index.row())` 读取业务数据。

```cpp
#include <virtualitemviews/reorderabletablemodel.h>
#include <QStringList>

class Names : public viv::ReorderableTableModel
{
public:
    explicit Names(QObject *parent = nullptr)
        : viv::ReorderableTableModel(2, 1, parent),
              names{QStringLiteral("甲"), QStringLiteral("乙")} {}
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return names.at(sourceRow(index.row()));
    }
private:
    QStringList names;
};

auto *orderModel = new Names(&table);
table.setModel(orderModel); // table 已设置行或单元格 adapter
table.setVerticalHeaderDragEnabled(true);
```

`ReorderableTableModel::resetRowOrder()` 可恢复数据插入顺序。模型的业务数据仍由子类管理；插入/删除行时须保持业务数据与模型结构一致。

## 拖放的数据操作

拖放能否开始、目标是否合法和最终如何修改数据，分别由模型的 `flags()`、`canDropMimeData()`、`dropMimeData()` 决定。视图负责命中与反馈；最小复制拖放配置见 [选择与拖放](interaction.md)。
