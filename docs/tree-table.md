# 多列表格树

`VirtualTreeTableView` 使用树形 `QAbstractItemModel`，纵向只物化展开后的可见节点，横向沿用 `VirtualTableView` 的列布局、表头、控件模式和 pane。单列且不需要表格布局时使用 `VirtualTreeView`；没有父子节点时使用 `VirtualTableView`。

## 最小用法

每个节点由逻辑列 0 的索引标识。行控件用 `ColumnHost` 承载各列内容，视图负责列宽、滚动和换序；adapter 绑定时通过当前行索引的 `siblingAtColumn()` 读取其他列。

```cpp
#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < 3; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(6, 0, 6, 0);
            labels[column] = new QLabel(host);
            layout->addWidget(labels[column]);
        }
    }

    void bind(const QModelIndex &node)
    {
        for (int column = 0; column < 3; ++column)
            labels[column]->setText(node.siblingAtColumn(column).data().toString());
    }

private:
    QLabel *labels[3] = {nullptr, nullptr, nullptr};
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
        static_cast<RowWidget *>(widget)->bind(index);
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 32); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({"Name", "Type", "State"});
    QList<QStandardItem *> parent{new QStandardItem("Device"),
                                 new QStandardItem("Group"),
                                 new QStandardItem("Online")};
    parent[0]->appendRow({new QStandardItem("Channel"),
                          new QStandardItem("Input"),
                          new QStandardItem("Ready")});
    model.appendRow(parent);

    viv::VirtualTreeTableView view;
    view.setModel(&model);
    view.setTableAdapter(new RowAdapter, true);
    view.setUniformItemHeight(32);
    view.setColumnWidth(0, 220);
    view.setColumnWidth(1, 120);
    view.setColumnWidth(2, 120);
    view.expand(model.index(0, 0));
    view.resize(560, 320);
    view.show();
    return app.exec();
}
```

行控件和单元格控件模式的选择、adapter 生命周期以及焦点保留规则与[表格文档](table.md)相同。单元格模式使用 `setMaterializationMode(MaterializationMode::CellWidgets)` 和 `setCellAdapter()`；第 0 列内容会自动给分支图标留出缩进。

若不同父节点或列需要不同控件，在单元格 adapter 中用模型索引分类；每种类型有自己的复用池：

```cpp
#include <QLabel>
#include <QProgressBar>

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        const bool leafState = index.column() == 2 && index.parent().isValid()
            && index.parent().parent().isValid();
        return leafState ? 1 : 0;
    }

    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1)
            return new QProgressBar(parent);
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (cellWidgetType(index) == 1) {
            auto *progress = static_cast<QProgressBar *>(widget);
            progress->setRange(0, 100);
            progress->setValue(index.data().toString() == QStringLiteral("正常") ? 100 : 0);
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }
};

view.setCellAdapter(new CellAdapter, true);
view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
view.setSelectionBehavior(viv::VirtualTableView::SelectionBehavior::SelectItems);
```

控件再次绑定时必须覆盖旧索引的数据；第 0 列所属节点仍由树视图负责展开与分支绘制。

`setItemPinned(index)` 在行控件模式固定该索引所属的节点，在单元格模式固定指定单元格；视口外的固定项也会按需创建控件。`pinWidget()` 和 `unpinWidget()` 可对当前已物化的行控件或单元格控件操作。折叠节点时，已固定的控件保留但移出可见区域，展开后恢复显示。

## 树与列

- `setRootIndex()` 限定显示某个节点的子树；`expand()`、`collapse()`、`expandRecursively()`、`collapseAll()` 控制可见行。`visibleRowCount()` 返回展开后的节点数。第 0 逻辑列即使换到其他视觉位置，分支图标仍跟随该列。整行选择模式下 Left/Right 操作树节点；单元格选择模式下 Left/Right 在列间移动。两种模式都可用 `Alt+Left` / `Alt+Right` 操作树，隐藏第 0 列后也可用此快捷键或公开展开 API。
- 根索引下的列数决定全局表头。某个父节点缺少子列时，该格没有有效模型索引，不创建单元格控件；行控件模式下对应的 `ColumnHost` 会隐藏。
- 模型插入、删除或移动逻辑列 0 后，原第 0 列索引或其祖先若转到其他列，就不再代表当前树中的节点；相关展开、选择、当前项、显式行高和固定状态失效。限定根节点若失去第 0 列身份，视图恢复显示模型根。仅调整表头视觉顺序不会改变节点身份。
- 垂直表头显示当前可见节点的序号。表头拖动行只允许同一父节点的兄弟项，并交给模型的 `moveRows()`；模型拒绝时顺序不变。排序交给模型或代理模型，展开状态跟随节点索引。
- Shift 扩展选择按当前可见行顺序覆盖节点，因此跨父节点和已展开的子节点也在范围内。整行选择覆盖这些节点的现有列；单元格选择使用起点和终点的逻辑列范围，跳过某个父节点不存在的列。
- 首/尾冻结行按当前可见节点序列计算，展开或折叠后重新定位。左右冻结列、多 pane、比例列宽、列状态保存与恢复沿用表格接口。跨进程恢复展开状态需要应用自己的稳定节点 ID。

`saveHeaderState()` 保存当前根的列宽、视觉顺序、隐藏列、比例设置和冻结行列。先设置模型及根索引，再调用 `restoreHeaderState()`；保存时与恢复时的根列数必须相同，否则返回 `false` 且不修改现有列状态。切换到列数更少的根会截去高位列的临时状态，切回后这些列恢复默认值。需要分别保留多个根的列布局时，应用应按稳定根 ID 分别保存状态，并在每次切换根后恢复对应状态。

## 间距与状态

```cpp
view.setRowSpacing(4);               // 所有节点后的默认间距
view.setDepthRowSpacing(1, 10);      // 深度 1 节点后的间距
view.setColumnSpacing(6);
view.setHorizontalGridLinesVisible(true);
view.setVerticalGridLinesVisible(true);
view.setRowGridLineExtent(viv::VirtualTreeTableView::RowGridLineExtent::NodeAndIcon);
view.setHorizontalGridLineColor(QColor("#9aa8b2"));
view.setHorizontalGridLineWidth(1);
view.setVisualStateBackgroundVisible(true);
view.setVisualStateBackgroundExtent(
    viv::VirtualTreeTableView::VisualStateBackgroundExtent::NodeOnly);
view.setHoverBackgroundColor(QColor("#e7f4ed"));
view.setSelectedBackgroundColor(QColor("#b9d9f1"));
```

`RowGridLineExtent` 和 `VisualStateBackgroundExtent` 分别选择只覆盖节点内容（`NodeOnly`）、图标加内容（`NodeAndIcon`）或整行（`FullWidth`）。范围只扣除第 0 逻辑列中的树装饰区域，其他列仍正常绘制。分割线的颜色、宽度、显隐、是否穿过行列间距，以及自定义间距控件均沿用[表格设置](table.md)。状态背景默认关闭；行控件需要保持透明，才能看到视图在其下方绘制的背景。也可以使用 adapter 的状态回调自行绘制。

深度行间距跟随其上方节点，垂直表头的行号位置和间隙随可见节点同步更新。

## 合并与拖放

`TableSpanProvider` 与表格共用。行跨度仅在同一父节点的相邻行仍是连续可见节点时有效；不能跨折叠边界或上下冻结 pane。越过同级行或全局列末端的跨度会裁剪到现有范围；跨列 pane 的跨度裁剪到锚点所在 pane，不能覆盖该父节点缺失的列。无效跨度按普通单元格处理。`TableSpanMap` 在排序或模型结构变化后若发现两个跨度重叠，保留当前同级行列顺序中靠前的锚点并移除另一个。拖放支持节点前、节点后和放入节点；实际移动、复制仍由模型的拖放接口决定。
