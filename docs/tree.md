# 树视图

`VirtualTreeView` 使用标准树形 `QAbstractItemModel` 和与列表相同的 `WidgetAdapter`。只把当前展开路径中的可见节点映射为视图行，未展开的子树不会创建行控件。

在已创建 `QApplication` 的程序中，可直接用 `QStandardItemModel` 建一棵小树：

```cpp
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QColor>
#include <QLabel>
#include <QStandardItemModel>

class TreeAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(300, 26); }
};

QStandardItemModel model;
auto *group = new QStandardItem(QStringLiteral("分组"));
group->appendRow(new QStandardItem(QStringLiteral("子项")));
model.appendRow(group);
TreeAdapter adapter;
viv::VirtualTreeView tree;
tree.setAdapter(&adapter);
tree.setUniformItemHeight(26);
tree.setIndentation(20);
tree.setModel(&model);
tree.expand(model.index(0, 0));
tree.show();
```

还可用 `setRootIndex()` 只显示一棵子树，`isExpanded()` 查询状态，`visibleRowCount()` 查询展开后的行数。分支指示器可点击，双击和方向键也可展开/收起。

## 按深度设置行间距

树的间距跟在上方节点之后，按这个节点的深度计算；默认行间距为 0，各深度默认没有覆盖值，末个可见节点后没有间距。普通行间距和深度覆盖可以组合：

```cpp
tree.setRowSpacing(6);          // 默认深度
tree.setDepthRowSpacing(0, 18); // 顶层节点后
tree.setDepthRowSpacing(1, 4);  // 子节点后
tree.setDepthRowSpacing(1, -1); // 取消深度 1 的覆盖，恢复默认值
tree.setRowGridLinesVisible(true);
tree.setRowGridLineWidth(2);
tree.setRowGridLineColor(QColor("#5b7280"));
tree.setRowGridLineExtent(viv::VirtualTreeView::RowGridLineExtent::NodeAndIcon);
```

分割线默认显示，绘制范围默认为 `FullWidth`（整行）。`NodeOnly` 仅从上方节点的内容区域左侧开始，`NodeAndIcon` 从该节点的图标格左侧开始；三种模式都绘制连续横线，起点随上方节点深度变化。间距为 0 时绘制单条行线，有间距时在留白上下边缘绘制，范围设置对两者都生效。`setRowGridLinesVisible(false)` 可隐藏这些线。默认间距为空白；自定义间距控件使用列表相同的 `setRowSpacingFactory()`；创建与绑定回调收到的是上方节点的索引，可用 `tree.itemDepth(index)` 区分深度。控件按可见范围创建，并在同一深度内复用。

## 不同父节点使用不同控件

`widgetType(index)` 决定行控件进入哪个复用池。把父节点的类别存在模型角色中，就能让一组子节点显示文本、另一组子节点显示进度；父节点本身仍使用文本控件。下面沿用上面的 `model`，以 `MixedTreeAdapter` 替换 `TreeAdapter`：

```cpp
#include <QProgressBar>

class MixedTreeAdapter : public viv::WidgetAdapter
{
public:
    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.parent().data(Qt::UserRole).toInt() == 1 ? 1 : 0;
    }

    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *bar = new QProgressBar(parent);
            bar->setRange(0, 100);
            return bar;
        }
        return new QLabel(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (widgetType(index) == 1)
            static_cast<QProgressBar *>(widget)->setValue(index.data().toInt());
        else
            static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(300, 26); }
};

auto *tasks = new QStandardItem(QStringLiteral("任务"));
tasks->setData(1, Qt::UserRole);
tasks->appendRow(new QStandardItem(QStringLiteral("65")));
model.appendRow(tasks);

MixedTreeAdapter mixedAdapter;
viv::VirtualTreeView mixedTree;
mixedTree.setAdapter(&mixedAdapter);
mixedTree.setUniformItemHeight(26);
mixedTree.setModel(&model);
mixedTree.expand(model.index(1, 0));
mixedTree.show();
```

示例中根层行的 `parent()` 无效，因此仍分到文本池。类型应由稳定的模型数据决定；控件离屏后会在同类型节点之间复用，`bindWidget()` 必须完整刷新内容，与旧节点有关的任务在 `unbindWidget()` 中清理。

## 自定义分支图标

默认分支指示器由视图绘制，行控件无需自己画缩进。需要改变符号时实现 `BranchIndicatorRenderer`；下面用文本绘制展开和折叠标记：

```cpp
#include <virtualitemviews/branchindicator.h>
#include <QPainter>

class TextBranches : public viv::BranchIndicatorRenderer
{
public:
    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &, const QRect &rect) const override
    {
        if (state.adjoinsItem && state.hasChildren)
            painter->drawText(rect, Qt::AlignCenter, state.isExpanded ? "-" : "+");
    }
};

tree.setBranchIndicatorRenderer(new TextBranches, true); // tree 接管所有权
```

`branchIcon(state, index)` 也可按展开状态返回 `QIcon`；调用 `setBranchIndicatorRenderer(nullptr)` 恢复默认样式。树的拖放落点与普通列表不同，见 [选择与拖放](interaction.md)。
