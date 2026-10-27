# 表头、排序与行列换序

`VirtualTableView` 默认提供水平列头和垂直行号条，两者都是自带 `LabelHeaderAdapter` 的 `VirtualHeaderView`，读取模型的 `headerData()` 并使用当前 Qt 样式。需要按钮、筛选器等真实控件时，通过 `horizontalHeader()->setAdapter()` 或 `verticalHeader()->setAdapter()` 更换 adapter 即可。

可运行源码：[表头设置与动画](../examples/table_capabilities/18_headers/example.cpp)、[自定义表头](../examples/table_capabilities/19_custom_header/example.cpp)、[保存与恢复](../examples/table_capabilities/20_header_persistence/example.cpp)、[代理排序](../examples/table_capabilities/21_sorting/example.cpp)、[行号拖动排序](../examples/table_capabilities/22_row_reordering/example.cpp)。路径均相对于仓库根目录，各目录实现独立。

## Section 的继承层次

```text
QWidget
  HeaderSection
    StyledHeaderSection
      LabelHeaderSection
      HeaderCornerWidget
```

| 类型 | 职责 | 头文件 |
| --- | --- | --- |
| `HeaderSection` | 绑定索引、所属表头和表格查询、动画状态查询、分层绘制入口 | `virtualitemviews/headersection.h` |
| `StyledHeaderSection` | 默认面板、hover/selected 背景、圆角和 section 分割线 | `virtualitemviews/styledheadersection.h` |
| `LabelHeaderSection` | 文本和排序箭头 | `virtualitemviews/labelheaderview.h` |
| `HeaderCornerWidget` | 空白 corner 面板和与表格一致的边线 | `virtualitemviews/headercornerwidget.h` |

section 间距的背景及间距内的分割线由 `VirtualHeaderView` 绘制，section 只绘制自己的矩形区域。表头手势和复用也由 `VirtualHeaderView` 管理。

`HeaderSection` 按 `paintBackground()`、`paintContent()`、`paintForeground()` 的顺序绘制，每层独立保存和恢复 painter 状态。需要保留默认外观、仅自绘图标等内容时，继承 `StyledHeaderSection` 并重写 `paintContent()`；需要文字上叠加图标时，继承 `LabelHeaderSection`，先调用其 `paintContent()` 再绘制图标，并为图标安排不与文字和排序箭头重叠的位置。需要完全自绘时继承 `HeaderSection`。

自定义内容通过 `logicalIndex()` 读取当前绑定身份，通过 `visualState()` 读取表头状态和共享的动画进度；图标使用 `selectedProgress` 作为透明度即可跟随选中和取消动画。`headerView()`、`tableView()` 和 `orientation()` 提供当前所属环境，无需业务反查父对象。

可直接参考 [第 19 节选中表头图标示例](../examples/table_capabilities/19_custom_header/styled_headers.cpp) 的 `SelectedIconHeaderSection::paintContent()`：固定预留图标位置，为文字与排序箭头安排剩余区域，再按选中进度绘制图标。页面勾选“选中表头显示图标”即可观察行号与列头的效果。

`HeaderSectionAdapter` 只负责绑定和清除 section 的索引，不要求文本内容。自定义无文本 section 时，可继承它并仅重写 `createSection()`；同时重写绑定函数时，需要调用基类以维护身份。`LabelHeaderAdapter` 继承这一层，再处理文本、模型和排序标记，因此原有标签适配器和渐变示例仍可使用。

## 左上角控件

横、纵表头交汇处默认使用 `HeaderCornerWidget`，与内置表头采用相同面板样式，分隔线沿用表格的颜色、粗细和显隐设置。可直接替换为按钮、复选框或组合控件：

```cpp
auto *button = new QPushButton(QStringLiteral("Select all"));
table.setHeaderCornerWidget(button);
QWidget *corner = table.headerCornerWidget();
table.setHeaderCornerWidget(nullptr); // Restore the default styled widget.
```

视图接管控件所有权；替换时隐藏旧控件并通过 `deleteLater()` 释放。控件的宽度跟随行号条、高度跟随列表头，只有横、纵表头都实际显示且尺寸非零时才显示，滚动时位置固定。控件事件由业务连接，默认 corner 不附带全选行为。`VirtualTreeTableView` 继承此接口。

可运行源码：[自定义表头与 corner 按钮](../examples/table_capabilities/19_custom_header/example.cpp)。页面可切换全选按钮和默认样式；[表头尺寸与显隐](../examples/table_capabilities/18_headers/example.cpp) 可观察 corner 随表头变化。

## 直接配置默认表头

`horizontalHeader()`、`verticalHeader()` 返回 `HeaderViewInterface*`，可直接配置各自的默认样式，无需转换类型、继承或替换 adapter。默认 `VirtualHeaderView` 实现以下设置，冻结区域的对应副本自动同步初始配置和运行时修改。

```cpp
auto *header = table.horizontalHeader();
header->setBackgroundColor(QColor("#fff3d6"));
header->setForegroundColor(QColor("#334155"));
header->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
header->setContentPadding(QMargins(12, 2, 12, 2));

QFont font = header->font();
font.setPointSize(11);
font.setBold(true);
font.setItalic(false);
header->setFont(font);

table.verticalHeader()->setTextAlignment(Qt::AlignCenter);
```

| 配置 | 默认值与作用 |
| --- | --- |
| `setBackgroundColor()` / `backgroundColor()` | 无效颜色使用原生面板；支持 alpha，hover/selected 继续叠加 |
| `setForegroundColor()` / `foregroundColor()` | 无效颜色使用 widget palette 的 `ButtonText` |
| `setTextAlignment()` / `textAlignment()` | 居中；空 alignment 恢复居中 |
| `setContentPadding()` / `contentPadding()` | 零附加内边距；负值归零，只调整文字与排序箭头 |
| `setFont()` / `font()` | 遵循 QWidget 字体继承；通过 `QFont` 设置字号、加粗、斜体等 |
| `setBackgroundVisible()` / `backgroundVisible()` | 默认开启，只控制基础背景 |
| `setSpacingBackgroundColor()` / `spacingBackgroundColor()` | 默认使用 `QPalette::Base`；无效颜色跟随显式配置的本轴基础背景 |

padding 添加在原生样式的内容留白之外，不缩小背景或分割线。留白耗尽 section 空间时不绘制内容。字体大小不自动改变表头高度或行号宽度，使用 `setHeaderHeight()`、`setVerticalHeaderWidth()` 为文字提供空间。自定义文本内容可调用 `LabelHeaderSection::initContentStyleOption()` 复用文字样式与 padding。

模型可通过 `headerData()` 的 `Qt::FontRole`、`Qt::ForegroundRole`、`Qt::TextAlignmentRole` 覆盖单个 section；`FontRole` 使用 `QFont`，`ForegroundRole` 使用 `QBrush` 或 `QColor`，对齐使用 Qt alignment 的整数值。未设置的 role 使用 header 默认配置，`QFont` 未显式指定的属性继续继承。通过 `setHeaderData(..., QVariant(), role)` 清除覆盖后立即回退，控件复用不会保留上一个 section 的样式。

可运行源码：[第 18 节默认表头文字样式](../examples/table_capabilities/18_headers/example.cpp)，包含文字配置、模型 role 覆盖和冻结首行首列开关。

## 表头底色与透明背景

横纵表头独立管理基础背景，默认 corner 跟随水平表头底色与显隐。表头间距默认使用 `QPalette::Base`，与内容区间距保持一致；显式配置本轴底色后，未指定颜色的间距跟随该底色，显式间距颜色优先。传入 `QColor()` 恢复原生面板；`setBackgroundVisible(false)` 暂时隐藏已配置底色，重新显示后继续生效。

```cpp
for (auto *header : {table.horizontalHeader(), table.verticalHeader()}) {
    header->setBackgroundVisible(false);
    header->setSpacingBackgroundColor(QColor()); // Let gaps follow the transparent base.
}
```

关闭基础背景后，表头、冻结区域的副本和默认 corner 显示父控件背景，文字、排序箭头、分割线及 hover/selected 动画继续绘制。基础背景开关与 `setHeaderVisualStateBackgroundVisible()` 独立，当前选择和动画进度不受影响。自定义 section、corner 和间距控件自行绘制的背景由业务控制。

原有 `VirtualTableView::setHeaderBackgroundColor()`、`setHeaderBackgroundVisible()`、`setHeaderSpacingBackgroundColor()` 保留为同时设置横纵表头的便捷入口，其 getter 返回水平表头配置。布局和分割线刷新不会覆盖直接配置在 header 上的样式。替换 header 后使用新 header 自己的配置。完全自定义的 `HeaderViewInterface` 可选择实现这些外观接口；默认文本层的样式和模型 role 不会替它绘制业务内容。

独立使用 `VirtualHeaderView` 时使用相同接口。第 19 节的“表头样式与绘制”页签提供“自定义表头底色”和“透明表头背景”开关，可与圆角、图标和渐变组合查看。

## 排序、列宽与状态

```cpp
table.setSortingEnabled(true);
table.setSortIndicator(0, Qt::AscendingOrder);
table.setColumnResizeEnabled(true);         // Opt in to dragging column edges.
table.setVerticalHeaderResizeEnabled(true); // Opt in to dragging row-number edges.

QByteArray saved = table.saveHeaderState();
table.restoreHeaderState(saved);
```

排序交给模型的 `sort()`；若模型不实现排序，显示排序指示器并不会自行重排数据。状态保存/恢复包含列宽、顺序、隐藏、排序和滚动偏移等表格状态；恢复时表格结构应与保存时匹配。写入本地 INI 文件的完整用法见 [列布局持久化](header-persistence.md)。

横纵表头的尺寸拖动均默认关闭，独立使用的 `VirtualHeaderView` 也默认关闭。按需调用上述接口，或通过 `setSectionResizeEnabled(true)` 开启独立表头的拖动。开关只控制鼠标手势和尺寸光标，`setColumnWidth()`、`setRowHeight()` 等程序设置接口始终可用。

## 点击表头选择

默认行号条支持点击选择整行，列头点击选择整列默认关闭：

```cpp
table.setVerticalHeaderSelectionEnabled(true);    // Default: true.
table.setHorizontalHeaderSelectionEnabled(false); // Default: false.

// Opt in to column selection and show the selected cells individually.
table.setHorizontalHeaderSelectionEnabled(true);
table.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
```

两项配置独立于主体的 `SelectionBehavior`。点击行号始终选整行，点击列头始终选整列，包括屏外单元格；选择写入同一个 `selectionModel()`，共享选择模型的视图也会同步。`NoSelection` 不响应，`SingleSelection` 只选择当前行或列，`MultiSelection` 普通点击切换选中，`ExtendedSelection` 普通点击替换、Ctrl 追加或取消；Shift 连续选择，Ctrl+Shift 保留已有选择并追加范围。列隐藏或换序后，Shift 按当前显示顺序选择。

启用点击选列时，列头点击优先用于选择，不触发排序；关闭后恢复已配置的点击排序，`sortByColumn()` 始终可用。拖边缘缩放和拖动表头换序仍是独立手势，不会在结束后额外触发选择。默认表头、`VirtualHeaderView` 自定义 section 及冻结区域表头使用相同规则；自定义 section 中主动消费事件的按钮继续处理自己的点击。

只有完整行选中才点亮行号条，列头也要求完整列选中；选中一列不会因此点亮每一行的行号。`SelectRows` 下增加列时，框架将新增单元格加入原本所有既有单元格均已选中的行，保留整行选择及行号高亮；部分单元格或列选择不会被扩展成整行。

颜色沿用表格的 `setSelectedBackgroundColor()`。行号条和列头的选中背景使用视图的 `setVisualStateAnimationDuration()` 和 `setVisualStateAnimationEasingCurve()` 配置，选中和取消选中都会渐变；冻结列的表头副本共享同一列的动画进度。`SelectRows` 下，主体 hover 和 selected 都按整行显示；行号条同步该行 hover 及其进度，鼠标放在行号条上也会悬停对应数据行。`SelectItems` 下，主体只高亮对应单元格。原生拖动期间遵守框架的 hover 暂停生命周期。需要看清列选择时使用 `SelectItems`；`SelectRows` 会将包含已选单元格的行显示为整行背景，行号选中背景仍要求完整行被选中。

表头状态背景可按方向独立关闭，两项默认均开启。只让主体 item 高亮时：

```cpp
table.setHeaderVisualStateBackgroundVisible(Qt::Vertical, false);
table.setHeaderVisualStateBackgroundVisible(Qt::Horizontal, false);
```

这只控制内置表头的 hover/selected 背景，不影响表头原样式、点击选择或选择数据；冻结区域表头副本同步生效。通过 `headerVisualStateBackgroundVisible(orientation)` 查询配置。`VirtualTreeTableView` 继承此接口。

`setVisualStateCornerRadiusIncludesHeaders(bool)` 控制状态圆角是否扩展到内置表头，默认开启，半径沿用 `setVisualStateCornerRadius()`。按行高亮时，行号条只绘制左上、左下圆角，内容区只绘制右上、右下圆角；列表头选中背景保留四角圆角。关闭后，表头状态背景恢复直角，内容区保留自身圆角。行号条隐藏、宽度为零或关闭状态背景时，内容区恢复左右圆角；按格高亮时，行号条和各单元格分别绘制完整四角圆角。此开关不启用列头选择，需要时仍调用 `setHorizontalHeaderSelectionEnabled(true)`。参考 [圆角与表头示例](../examples/table_capabilities/19_custom_header/styled_headers.cpp)。

自定义绘制可通过 `headerVisualState(orientation, logicalIndex)` 查询状态，即使状态背景关闭，仍返回实际状态；自定义 section 可检查 `headerVisualStateBackgroundVisible(orientation)` 决定是否绘制背景，并读取 `visualStateCornerRadiusIncludesHeaders()` 与 `visualStateCornerRadius()` 采用相同圆角设置。垂直 section 编号是视图行号，树表映射为当前可见节点，列选择覆盖树表当前可见节点的对应列。完全自行实现的 `HeaderViewInterface` 继续负责自己的点击和绘制。

仅替换表头状态填充时，重写 `StyledHeaderSection` 提供的受保护函数 `visualStateBackgroundBrush(bool selected, const QColor &defaultColor) const`，返回 `QBrush`。`LabelHeaderSection` 继承此重载点，因此文本表头也可直接使用。例如仅在 `selected` 为 `true` 时返回渐变画刷，其余调用基类，即可保留默认 hover；动画进度、圆角、文字和分割线继续由框架处理。通过继承 `LabelHeaderAdapter` 并仅重写 `createSection()` 创建该控件，仍可沿用标签绑定和复用逻辑。参考 [第 19 节彩色渐变表头](../examples/table_capabilities/19_custom_header/styled_headers.cpp)。

可运行源码：[选择与当前项](../examples/table_capabilities/12_selection/example.cpp)，页面提供按行／按格选择、行号选择、列头选择和横纵表头状态背景开关。

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

实现 `HeaderWidgetAdapter::createSection()` / `bindSection()`，再设置到已有表头上，无需新建或继承表头。下面的 adapter 从当前模型读取列标题，模型被替换时会收到新的指针：

```cpp
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>
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

table.horizontalHeader()->setAdapter(new ColumnHeaderAdapter, true); // Transfer ownership.
```

表头会向新 adapter 提供当前模型和几何。`takeOwnership` 为 `true` 时表头接管 adapter；为 `false` 时由业务保证其存活到表头及冻结副本释放之后。替换 adapter 时，框架先用旧 adapter 回收所有相关控件，再释放旧 adapter 并重建冻结副本，已有样式和列宽设置保留。`setAdapter(nullptr)` 停止创建 section 控件；需要恢复默认文本内容时使用 `setAdapter(new viv::LabelHeaderAdapter, true)`，并包含 `virtualitemviews/labelheaderview.h`。

adapter 若读取排序几何，还可覆盖 `setGeometryModel()` 获取当前对象。`bindSection()` 可能反复调用，应完整重建控件显示状态；`unbindSection()` 负责清理旧 section 的异步活动。
