# 表格：行控件与单元格控件

`VirtualTableView` 使用 `QAbstractItemModel` 的行列数据。表格有两种物化模式；模型、adapter 与视图的生命周期约定见 [入门](getting-started.md)。

## 一行一个控件（默认）

使用 `TableWidgetAdapter` 创建行控件。以下第 0 列放名称标签，第 1 列放进度条；`ColumnHost` 会跟随列宽、顺序和 pane。代码放在已创建 `QApplication` 的程序中。

```cpp
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStandardItemModel>

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent) : QWidget(parent)
    {
        auto *first = new viv::ColumnHost(0, this);
        auto *second = new viv::ColumnHost(1, this);
        name = new QLabel(first);
        progress = new QProgressBar(second);
        progress->setRange(0, 100);
        auto *nameLayout = new QHBoxLayout(first);
        nameLayout->setContentsMargins(4, 0, 4, 0);
        nameLayout->addWidget(name);
        auto *progressLayout = new QHBoxLayout(second);
        progressLayout->setContentsMargins(4, 0, 4, 0);
        progressLayout->addWidget(progress);
    }
    QLabel *name;
    QProgressBar *progress;
};

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new RowWidget(parent);
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<RowWidget *>(widget);
        row->name->setText(index.siblingAtColumn(0).data().toString());
        row->progress->setValue(index.siblingAtColumn(1).data().toInt());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(300, 40); }
};

QStandardItemModel model(2, 2);
model.setData(model.index(0, 0), QStringLiteral("订单 A"));
model.setData(model.index(0, 1), 65);
RowAdapter rowAdapter;
viv::VirtualTableView table;
table.setTableAdapter(&rowAdapter);
table.setUniformItemHeight(40);
table.setDefaultColumnWidth(150);
table.setModel(&model);
table.show();
```

行控件自行定位子控件时，覆盖 adapter 的 `layoutRowWidget(widget, rowIndex, context)`：遍历 `context.columnsToLayout()`，通过 `context.columnX(column)` 和 `context.column(column)` 读取几何。有冻结列或多滚动组时，使用 `context.paneHostForColumn(column)` 对应的裁剪容器。

## 每个可见单元格一个控件

使用 `CellWidgetAdapter`，并明确切换模式。下面的 adapter 可与上面的 `model` 配合；横向、纵向滚动时只复用可见行与可见列交集中的控件。

```cpp
class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

CellAdapter cellAdapter;
viv::VirtualTableView cellTable;
cellTable.setCellAdapter(&cellAdapter);
cellTable.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
cellTable.setUniformItemHeight(28);
cellTable.setModel(&model);
cellTable.show();
```

不同列需要不同控件类型时覆盖 `cellWidgetType(index)`，将类型相同的控件分到同一个复用池。`table` 和 `cellTable` 展示两种独立的配置方式，实际使用时按需求选其一。

### 不同列使用不同控件

例如第 0 列显示名称，第 1 列显示进度。用列号选择池类型，并在创建和绑定时按同一个类型分支。下面的 adapter 可替换上面的 `CellAdapter`，沿用两列的 `model`；把第 1 列的数据设为 `0` 到 `100` 的整数即可。

```cpp
class MixedCellAdapter : public viv::CellWidgetAdapter
{
public:
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() == 1 ? 1 : 0;
    }

    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *bar = new QProgressBar(parent);
            bar->setRange(0, 100);
            return bar;
        }
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (index.column() == 1)
            static_cast<QProgressBar *>(widget)->setValue(index.data().toInt());
        else
            static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

MixedCellAdapter mixedAdapter;
viv::VirtualTableView mixedTable;
mixedTable.setCellAdapter(&mixedAdapter);
mixedTable.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
mixedTable.setUniformItemHeight(28);
mixedTable.setModel(&model);
mixedTable.show();
```

同一类型的控件可能绑定到不同的行，`bindCellWidget()` 每次都要写全当前状态；若控件持有与旧单元格有关的异步工作，还需在 `unbindCellWidget()` 中清理。行控件模式则在一个 `RowWidget` 的不同 `ColumnHost` 内放不同子控件，不使用 `cellWidgetType()`。

## 行高与选择

`setRowHeight(row, height)` 是显式行高，`clearRowHeight(row)` 恢复自动策略。默认 `RowSizePolicy::ExplicitWins` 让用户设置的高度优先；需要测量覆盖它时可切换为 `MeasuredWins`。动态行高的通用设置见 [列表与行高](list.md)。表格选择可用 `setSelectionMode()` 与 `setSelectionBehavior()` 控制；`selectionModel()` 是标准 Qt 选择模型。

列宽、冻结、合并和表头操作分别见 [布局与合并](table-layout-guide.md)、[表头与排序](headers.md)。
