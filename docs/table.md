# 表格：行控件与单元格控件

`VirtualTableView` 使用 `QAbstractItemModel` 的行列数据。表格有两种物化模式；模型、adapter 与视图的生命周期约定见 [入门](getting-started.md)。

## 一行传递一个结构体

可以把整行数据放在第 0 逻辑列的一个 role 中。框架读取它并通过 `ItemBindingContext` 交给 adapter，不需要为每个字段分配 role，也不需要在绑定回调里逐列查询模型。默认 role 为 `viv::kDefaultItemDataRole`（`Qt::UserRole + 0x200`），列表和两种树视图也支持该接口。

下面使用 `QStandardItemModel` 保存数据；自定义模型只需在 `data(index, role)` 中为第 0 列的相应 role 返回整行的 `QVariant`。

```cpp
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>

struct RowData
{
    QString title;
    QString owner;
    int progress = 0;
};
Q_DECLARE_METATYPE(RowData) // 放在类型定义之后、命名空间之外

QStandardItemModel model;
model.setColumnCount(3);
model.insertRow(0);
model.setData(model.index(0, 0),
              QVariant::fromValue(RowData{QStringLiteral("条目"), QStringLiteral("负责人"), 60}),
              viv::kDefaultItemDataRole);
```

行控件模式的 adapter 可以只重写新的绑定回调，行控件的 `bind()` 接收业务结构体：

```cpp
void bindWidget(viv::WidgetType, QWidget *widget,
                const viv::ItemBindingContext &context) override
{
    static_cast<RowWidget *>(widget)->bind(context.rowData.value<RowData>());
}
```

单元格模式同样收到完整结构体，再按逻辑列选择要显示的字段。列换序不会改变 `logicalColumn`：

```cpp
void bindCellWidget(QWidget *widget, const viv::ItemBindingContext &context) override
{
    const RowData data = context.rowData.value<RowData>();
    auto *label = static_cast<QLabel *>(widget); // 本例的所有格都由 QLabel 承载
    switch (context.logicalColumn) {
    case 0: label->setText(data.title); break;
    case 1: label->setText(data.owner); break;
    case 2: label->setText(QString::number(data.progress)); break;
    default: label->clear(); break;
    }
}
```

| 上下文字段 | 含义 |
| --- | --- |
| `index` | 当前绑定的模型索引；单元格模式保留该格自己的行列及父节点 |
| `rowData` | 整行或当前节点的数据；表格总从同一父节点、同一行的第 0 逻辑列读取，隐藏或换序第 0 列也不改变来源 |
| `logicalColumn` | 表格中的逻辑列号，行控件通常为 0；列表及单列树为 -1 |
| `depth` | 平面行是 0；树节点相对当前显示根的深度，根下第一层为 0 |

已有 role 可通过 `view.setItemDataRole(MyRowRole)` 指定；切换 role 会重新绑定当前物化的控件。负数关闭 payload 读取；role 缺失时 `rowData` 是无效 `QVariant`，业务应自行处理，必要时先检查 `canConvert<RowData>()`。框架不修改模型的 `DisplayRole`、排序、编辑或可访问性数据；这些能力需要时仍由模型提供。

更新数据时仍用模型的正常通知机制。`QStandardItemModel::setData()` 自动发出 `dataChanged`；自定义模型更新内部结构体后发出包含该 role 的 `dataChanged`。即使通知范围只有第 0 列，框架也会重新绑定该行所有已物化的单元格。空 roles 表示所有 role 改变，同样刷新整行；其他 role 保持原通知范围。

`visualStateChanged()` 和 `unbindWidget()` / `unbindCellWidget()` 也有接收 `ItemBindingContext` 的重载。旧的 index 回调继续可用，框架默认把新回调转发给旧回调；同一回调同时重写两个版本时，以 context 版本为入口，由它决定是否调用旧版。上下文只在回调期间有效，不要保存其引用。行内事件处理需要在每次绑定时保存业务数据及 `QPersistentModelIndex(context.index)`，解绑时清空；索引失效后 `rowData` 也可能为空，不能把解绑回调当作最后一次读取业务数据的机会。

较大的结构体可用 `QSharedPointer<const RowData>` 作为 payload，在命名空间外为该指针类型声明 `Q_DECLARE_METATYPE`，再通过 `context.rowData.value<QSharedPointer<const RowData>>()` 取出并检查非空。更新时换成新的不可变数据指针并发出通知，避免每格解包都复制大对象。`Q_DECLARE_METATYPE` 足以用于这里的 `QVariant` 存取；若业务还通过排队信号传递该类型，另按 Qt 要求注册元类型。

## 如何选择控件模式

| | `TableWidgetAdapter`：行控件模式（默认） | `CellWidgetAdapter`：单元格控件模式 |
| --- | --- | --- |
| 创建与复用单位 | 一个可见行对应一个行控件；整行一起绑定和回收 | 一个可见单元格对应一个控件；各单元格独立绑定和回收 |
| 横向滚动 | 行控件仍在；示例中预先创建的所有列控件也仍在，只调整位置和可见性 | 只物化可见列及预加载范围中的单元格，移出范围后进入复用池 |
| 业务代码 | 自己组织一行的子控件；可用 `ColumnHost` 让视图自动处理列几何 | 按 `QModelIndex` 创建、绑定单元格；不同控件类型用 `cellWidgetType()` 分池 |
| 更适合 | 列数较少、一行内的控件需要共同布局或共享状态 | 列数多、单元格控件较重，或希望横向滚动时也限制控件数量 |

例如 20 个可见行、24 列：如果每个行控件像下例一样预先创建全部列控件，行控件模式会持有约 20 个行控件和 480 组列子控件；单元格模式若同时只显示 6 列，则主要持有这 20 行与可见列交集中的控件，另加预加载和复用池中的控件。两种模式都只物化可见范围附近的行，不会因模型有 5 万行就创建 5 万行控件。

先按控件所有权选：需要把整行当成一个整体管理，用行控件模式；需要让每格独立回收，尤其是宽表，用单元格模式。两种模式都支持普通的纵向、横向滚动；滚动库的接入方式不决定这里的选型。

## 自绘悬停与选中背景

`table.visualState(index)` 返回 `hovered`、`selected` 目标标志和各自的 0–1 动画进度。默认 180 ms 平滑过渡；`setVisualStateAnimationDuration(0)` 可关闭。默认 `VisualStateScope::Row`：同一行只要有一个格被选中，整行的 `selected` 为真；鼠标位于行中任意格时，整行的 `hovered` 为真。切到 `Cell` 后，仅命中的格或选中的格返回对应标志。此设置与行控件/单元格控件的物化模式独立。

选区本身仍由 `setSelectionBehavior()` 控制。若需要点击后只选中一个单元格，同时调用 `table.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems)`；仅修改绘制范围不会改变已有选区。

```cpp
table.setVisualStateScope(viv::VirtualTableView::VisualStateScope::Cell);
table.setHoverBackgroundColor(QColor("#e7f4ed"));
table.setSelectedBackgroundColor(QColor("#b9d9f1"));
table.setVisualStateAnimationDuration(180);
const auto state = table.visualState(model.index(2, 1));
```

在行控件模式中，`TableWidgetAdapter::visualStateChanged(type, widget, rowIndex)` 通知整行控件重绘。若按格着色，在行控件的 `paintEvent()` 中逐列查询 `table.visualState(QModelIndex(rowIndex).siblingAtColumn(column))`，再按该列当前的 `columnGeometry(column)` 画背景；列顺序、宽度和冻结位置都应取当前几何。下面是关键绘制部分，`rowIndex` 是绑定时保存的 `QPersistentModelIndex`，`table` 是当前视图：

```cpp
QPainter painter(this);
for (int column = 0; column < table->model()->columnCount(); ++column) {
    const auto geometry = table->columnGeometry(column);
    if (!geometry.isValid() || geometry.hidden) continue;
    const auto state = table->visualState(QModelIndex(rowIndex).siblingAtColumn(column));
    const QRect cellRect(geometry.viewportX - x(), 0, geometry.width, height());
    painter.setOpacity(state.hoverProgress);
    painter.fillRect(cellRect, table->hoverBackgroundColor());
    painter.setOpacity(state.selectedProgress);
    painter.fillRect(cellRect, table->selectedBackgroundColor());
    painter.setOpacity(1.0);
}
```

单元格控件模式则由 `CellWidgetAdapter::visualStateChanged(widget, index)` 通知对应控件。控件可保存 `table.visualState(index)` 并在自己的 `paintEvent()` 中画背景。两个适配器的默认回调都会调用 `widget->update()`；如果控件在绘制时直接查询视图状态，不必重写回调。回收后重新绑定时也会通知状态，避免旧控件的选中色残留。不透明子控件会遮住父控件背景，需要同步处理其背景。

## 行列间距

表格默认绘制 hover 和选中背景，只在 item 范围着色，行列间距始终保持自身外观。两种物化模式及整行/单元格状态范围都遵循此规则。

普通圆角和颜色直接使用 `setVisualStateCornerRadius()`、`setHoverBackgroundColor()`、`setSelectedBackgroundColor()`。仅完全自定义时重写 `paintStateBackgroundLayer(painter, rect, clip, color)`；`clip` 已排除间距并按 pane 裁剪。自绘行控件应关闭框架背景，避免重复绘制。

覆写中可调用 `paintRoundedStateBackgroundLayer(painter, rect, clip, color, radius)`；它同时对齐圆角形状和裁剪区域的物理像素边界，避免 125% 等缩放下抗锯齿颜色进入下一行。

```cpp
table.setVisualStateCornerRadius(6);
```

两种控件模式都可设置行间距和列间距，默认均为 0；末行、末列之后不加间距。空白区域不命中行或列，行间距上下、列间距左右都有分割线。列间距跟随列的显示顺序、隐藏状态和冻结 pane。间距设为 0 时仍显示单元格分割线。

```cpp
table.setRowSpacing(8);
table.setColumnSpacing(12);
table.setVerticalSpacingLineThroughRowSpacing(false);      // 竖线在行间距处中断
table.setHorizontalSpacingLineThroughColumnSpacing(false); // 横线在列间距处中断
table.setVerticalGridLinesVisible(false);                  // 隐藏竖线及默认列表头分割线
table.setHorizontalGridLinesVisible(false);                // 隐藏横线及默认行表头分割线
```

横、竖分割线默认都显示，可以分别关闭；列表头底边、行表头右边及其间隙边框也遵循对应方向的开关。竖线穿过行间距、横线穿过列间距默认都开启；这两个开关只决定已显示的线在间隙内是否连续，不改变分割线的显隐。自定义表头控件若自己绘制边框，应由该控件同步处理边框显隐。

横线和竖线可以分别设置颜色与宽度，默认跟随 Qt 样式颜色、宽度为 1 个物理像素。线宽按物理像素绘制并对齐屏幕像素网格，在 125% 或 150% 等缩放下保持一致；行列尺寸与间距仍使用逻辑像素。颜色传入无效的 `QColor()` 可恢复样式颜色；宽度最小为 1 个物理像素。设置作用于表体分割线、间隙边缘和默认表头分割线，较宽的线向已有单元格或间隙内部绘制，不改变行列尺寸。pane 边界线仍由 `setPaneSeparatorStyle()` 单独控制。

```cpp
table.setVerticalGridLineColor(QColor(QStringLiteral("#3983a8")));
table.setVerticalGridLineWidth(2);
table.setHorizontalGridLineColor(QColor(QStringLiteral("#c76552")));
table.setHorizontalGridLineWidth(3);
```

需要在间距里显示控件时，分别提供创建与绑定回调。下面可接在本页的 `table` 和 `model` 建立之后；绑定回调会在控件复用时收到当前间距左侧的逻辑列号。

```cpp
table.setColumnSpacingFactory(
    [](int, QWidget *parent) -> QWidget * { return new QLabel(parent); },
    [](QWidget *widget, int leftColumn) {
        static_cast<QLabel *>(widget)->setText(QString::number(leftColumn));
    });
```

行间距控件使用 `setRowSpacingFactory()`，接口和列表相同。分割线占据间距的上下边缘；要容纳控件内容，间距须大于横线宽度的两倍。间距属于视图显示设置，未写入表头状态文件，重启后可从应用配置重新设置。

表头列间距和表体列间距可以分别放控件。两个工厂都只为可见间距创建控件；如果控件内容依赖左侧列，传入对应 binder 在复用时刷新。

```cpp
table.setHeaderColumnSpacingFactory([](int, QWidget *parent) -> QWidget * {
    auto *stripe = new QWidget(parent);
    stripe->setStyleSheet(QStringLiteral("background: #f4d35e;"));
    return stripe;
});
table.setColumnSpacingFactory([](int, QWidget *parent) -> QWidget * {
    auto *stripe = new QWidget(parent);
    stripe->setStyleSheet(QStringLiteral("background: #83c5be;"));
    return stripe;
});
```

## 一行一个控件（默认）

使用 `TableWidgetAdapter` 创建行控件。以下第 0 列放名称标签，第 1 列放进度条。`ColumnHost(列号, 行控件)` 只是列布局容器：视图会找到它，随列宽、顺序、隐藏、冻结和横向滚动调整其几何与裁剪；里面的业务控件由行控件持有和绑定，不会作为独立单元格回收。代码放在已创建 `QApplication` 的程序中。

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
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<RowWidget *>(widget);
        row->name->setText(index.siblingAtColumn(0).data().toString());
        row->progress->setValue(index.siblingAtColumn(1).data().toInt());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(300, 40); }
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

## 状态背景

需要带动画的悬停、选中背景时，可用 `StyledTableView` 替代 `VirtualTableView`，并让行控件继承 `StyledTableRowWidget`、单元格容器使用 `StyledTableCellHost`。这些基类负责背景绘制、圆角、冻结列边缘以及分隔线覆盖；业务控件只需绑定内容。

```cpp
#include <virtualitemviews/styledtableview.h>
#include <QLabel>
#include <QStandardItemModel>

class Row : public viv::StyledTableRowWidget
{
public:
    explicit Row(viv::StyledTableView *view, QWidget *parent)
        : viv::StyledTableRowWidget(view, parent)
    {
        auto *cell = new viv::StyledTableCellHost(0, view, this);
        label = new QLabel(cell);
        label->setGeometry(4, 0, 120, 28);
    }

    void bind(const QModelIndex &index)
    {
        bindRow(index);
        label->setText(index.data().toString());
    }

    QLabel *label = nullptr;
};

class Adapter : public viv::TableWidgetAdapter
{
public:
    explicit Adapter(viv::StyledTableView *table) : view(table) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new Row(view, parent);
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<Row *>(widget)->bind(index);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<Row *>(widget)->unbindRow();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(120, 28); }

    viv::StyledTableView *view;
};

QStandardItemModel model(2, 1);
model.setData(model.index(0, 0), QStringLiteral("第一行"));
model.setData(model.index(1, 0), QStringLiteral("第二行"));
viv::StyledTableView table;
Adapter adapter(&table);
table.setTableAdapter(&adapter);
table.setUniformItemHeight(28);
table.setModel(&model);
table.setVisualStateScope(viv::VirtualTableView::VisualStateScope::Cell);
table.setHoverBackgroundColor(QColor("#dceef4"));
table.setSelectedBackgroundColor(QColor("#b8dce9"));
table.setVisualStateAnimationDuration(150);
table.setVisualStateCornerRadius(6);
table.setBackgroundCoversGridLines(false); // 默认：分隔线显示在背景上方
table.show();
```

`VisualStateScope::Row` 把背景画在整行，`Cell` 按单元格绘制；两种模式都能设置圆角。`setBackgroundCoversGridLines(true)` 让高亮背景覆盖其范围内的分隔线，默认 `false`。需要自定义形状或绘制时，继承 `StyledTableView` 并覆写 `visualStateBackgroundPath()` 与 `paintVisualStateBackground()`；两处绘制会使用相同的钩子。这个样式基类适用于 `TableWidgetAdapter` 的行控件模式；不影响普通 `VirtualTableView` 的自定义绘制。

## 行高与选择

`setRowHeight(row, height)` 是显式行高，`clearRowHeight(row)` 恢复自动策略。默认 `RowSizePolicy::ExplicitWins` 让用户设置的高度优先；需要测量覆盖它时可切换为 `MeasuredWins`。动态行高的通用设置见 [列表与行高](list.md)。表格选择可用 `setSelectionMode()` 与 `setSelectionBehavior()` 控制；`selectionModel()` 是标准 Qt 选择模型。

列宽、冻结、合并和表头操作分别见 [布局与合并](table-layout-guide.md)、[表头与排序](headers.md)。
