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

两种树视图均支持展开/收起的高度、位置和淡入淡出过渡：`setExpansionAnimationEnabled(true)` 开启，`setExpansionAnimationDuration(300)` 设置毫秒时长（默认 300 ms，0 关闭过渡）。组件默认关闭，示例开启。子节点画面连同节点间距逐渐展开或收拢，后续行同步上下移动；节点映射、逻辑几何和信号立即更新，视觉动画按节点身份插值当前视图大小的前后画面，不修改模型、显式行高或虚拟化布局。连续操作替换旧过渡；滚动、窗口缩放、模型变化、更换显示根或鼠标/键盘操作取消过渡，输入按最终布局处理。启用冻结行时使用淡入淡出，避免行画面跨越冻结边界。

## 悬停与选中背景

树可以直接绘制带过渡动画的状态背景。覆盖范围按当前节点深度计算，和分割线范围独立设置：

```cpp
tree.setVisualStateBackgroundVisible(true);
tree.setVisualStateCornerRadius(6); // 默认 0（直角），负数按 0 处理
tree.setVisualStateBackgroundExtent(
    viv::VirtualTreeView::VisualStateBackgroundExtent::NodeAndIcon);
tree.setHoverBackgroundColor(QColor("#e7f4ed"));
tree.setSelectedBackgroundColor(QColor("#b9d9f1"));
tree.setVisualStateAnimationDuration(150);
tree.setHoverBackgroundThroughRowSpacing(true);
tree.setSelectedBackgroundThroughRowSpacing(false);
```

`NodeOnly` 从节点内容左侧开始，`NodeAndIcon` 从当前节点图标格左侧开始，`FullWidth` 覆盖整行。背景在分支图标下方绘制，不遮住图标；行控件需要保持透明，才能看到其下方的状态背景。默认不启用内置背景。

`setVisualStateCornerRadius()` 同时设置 hover 和 selected 背景圆角，滚动裁剪不会改变完整背景的圆角形状。`tree_view` 示例默认演示 6 像素圆角，可用 `--corner-radius 0` 切回直角，或在“状态”工具栏调整半径。

也可由节点控件自行绘制背景。树与列表使用同一接口：在 `WidgetAdapter::visualStateChanged(widget, index)` 中读取 `tree.visualState(index)`，把独立的 `hovered`、`selected` 标志交给节点控件，然后调用 `update()`。节点控件在 `paintEvent()` 中画自己的背景；回调也会在控件复用并绑定新节点后触发。两种状态可以同时为真，颜色优先级由业务决定。完整的自绘控件写法见 [列表的状态示例](list.md#自绘悬停与选中背景)。

`setHoverBackgroundThroughRowSpacing()` 和 `setSelectedBackgroundThroughRowSpacing()` 分别控制内置背景是否覆盖节点拥有的行间距，默认均为 false。`NodeRowSpacingBelowRole` 的部分跟随上方节点，`NodeRowSpacingAboveRole` 的部分跟随下方节点；下间距 role 缺失、无效或负值时，默认行间距归上方节点。两部分各自使用所属节点的状态和覆盖范围，首行前、末行后不延伸，不跨越冻结行边界。分隔线仍显示，自定义间距控件仍可绘制自己的内容。

节点控件按 `hoverProgress`、`selectedProgress` 混合背景颜色即可得到进入和离开动画；`setVisualStateAnimationDuration()` 控制过渡时间。

`tree.setHoverBackgroundColor(QColor("#e7f4ed"))` 和 `tree.setSelectedBackgroundColor(QColor("#b9d9f1"))` 分别设置业务控件可读取的颜色；更改后可见节点会收到重绘通知。

## 节点间距

创建节点时由应用根据深度或业务属性计算间距，写入第 0 列的 `NodeRowSpacingRole`（两种树视图均为 `Qt::UserRole + 0x100`）：

```cpp
const int spacing = depth == 0 ? 18 : depth == 1 ? 4 : 0;
item->setData(spacing, viv::VirtualTreeView::NodeRowSpacingRole);
item->setData(8, viv::VirtualTreeView::NodeRowSpacingAboveRole); // 节点上方
// 自定义模型在 data(index, NodeRowSpacingRole) 中返回节点保存的间距。
tree.setRowSpacing(6); // 节点未提供间距时的回退值
```

`NodeRowSpacingBelowRole` 是原 `NodeRowSpacingRole` 的明确别名（下方间距），`NodeRowSpacingAboveRole` 为 `Qt::UserRole + 0x101`（上方间距）。相邻可见节点间距为前一节点下方值加后一节点上方值；下方缺失/无效/负值回退到 `rowSpacing()`，上方缺失/无效/负值为 0。第一可见节点上方和末节点下方不额外留白。修改任一 role 并发送 dataChanged 后更新布局。

仅在第二个及后续根节点上方留白，创建节点时设置 `depth == 0 && rootRow > 0 ? 12 : 0` 到 `NodeRowSpacingAboveRole`，并将默认及节点下方间距设为 0。展开后，留白仍位于根节点之前。节点移动或更换显示根不会改写模型保存的间距。旧的按深度、同级和层级统一设置接口已移除。

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

`setIndentLeafItems(bool)` 默认 `true`，所有行保留自己的 branch 格。设置为 `false` 时，同一父项下如果所有子项都没有子节点，它们省略自己的 branch 格，内容直接对齐父项内容；如果同级中任意一项有子节点，所有同级项仍保留 branch 格并相互对齐。判断依据是能否展开，而不是当前展开/收起状态；根层全部为叶子时内容从左侧开始。隐藏 branch 图标不会改变这个规则。背景、分割线和行间距分割线随内容对齐，自定义 renderer 不会收到被省略的本行 branch 格。示例工具栏的“缩进叶子项”可以切换此设置。

默认分支指示器由视图绘制，行控件无需自己画缩进。只替换展开/收起图标时，可以直接设置：

```cpp
tree.setExpandedBranchIcon(QIcon(":/icons/expanded.svg"));
tree.setCollapsedBranchIcon(QIcon(":/icons/collapsed.svg"));
tree.setBranchIconSize(12);
tree.setBranchIconColor(QColor("#64748b"));
```

两种状态独立设置，传入空 `QIcon()` 恢复对应的默认三角。大小以逻辑像素计，默认 8，最小为 1；绘制时限制在当前 branch 格和行高内，左侧保留 4 像素边距，点击区域仍为整个 branch 格。颜色同时作用于默认三角和替换图标：替换图标按透明度染色，传入无效 `QColor()` 恢复图标原色，默认三角则使用调色板的 `WindowText`。

这些设置仅作用于未安装自定义 renderer 的 `VirtualTreeView`。连接线、叶子标记、按节点改变装饰等其他类型仍由用户实现 `BranchIndicatorRenderer`；安装 renderer 后它完全接管分支绘制。下面用文本绘制展开和折叠标记：

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
