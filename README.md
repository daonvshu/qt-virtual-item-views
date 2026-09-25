# VirtualItemViews

Qt Widgets 的虚拟化 Item View 框架：用 `QAbstractItemModel` 做数据源，只实例化可见区、overscan
和 pinned 范围内的**真实 QWidget**，并在滚动时复用它们。

## 适用场景

适合：

* 行内包含真实控件（按钮、开关、进度条、编辑器、异步图片）的企业列表、表格与树；
* 行高由业务内容决定、甚至会异步变化的长列表；
* 十万到千万级逻辑行，需要稳定内存与稳定滚动性能的场景。

不适合：

* 只需要纯绘制、追求极限吞吐的表格 —— 请用 `QTableView` + `QStyledItemDelegate`；
* 需要 GPU / scenegraph 渲染的场景（本库的绘制走 QPainter，与 QWidget 行同源）；
* 每行都是重型浏览器/视频控件、且要求十万级**同时可见**的场景（真控件始终有成本，
  虚拟化的收益来自"只看得到的那几十个"）。

它**不是** `QListView` 的替代品。复杂业务行用 Delegate 绘制会带来大量 `paint()`、`sizeHint()`、
`hitTest()`、`editorEvent()` 样板代码，而 `QListWidget + setItemWidget` 又会为每一行创建真实控件。
VirtualItemViews 走第三条路：**只创建看得见的行，且这些行是真正的 QWidget**。

## 基本架构

```
        VirtualListView / VirtualTableView / VirtualTreeView          业务 API
                                  ↓
        VirtualItemView（内核：滚动空间、可见区间、物化与回收、
                        无效化合并、选择 / current / 焦点 / 拖放）
             ↑                                      ↑
    LayoutPolicy / ListLayout              WidgetAdapter / WidgetRecycler
    SizeIndex（行高：固定 / 动态）          业务 QWidget 的创建、绑定与复用
```

* **内核**：`VirtualItemView`（继承 `QAbstractScrollArea`）持有滚动空间、可见区间、控件物化与
  回收、无效化合并、选择与 current；子类只提供"视图行 ↔ QModelIndex"的映射和布局策略。
* **布局**：`LayoutPolicy` / `ListLayout` 管几何，行高来自 `SizeIndex` —— `FixedSizeIndex` 固定
  行高全部 O(1)，`BlockSizeIndex` 动态行高只保存"与估计值不同"的行（没测量过的行不占存储）。
* **控件**：`WidgetAdapter` 定义"建控件 / 灌数据 / 解绑"三个函数，`WidgetRecycler` 按
  `WidgetType` 分池；滚动只做 bind / recycle，稳态滚动不 new / delete。
* **表格**在内核之上加 `HeaderGeometry`（列宽、顺序、隐藏、排序、横向偏移的唯一事实来源）与表头
  渲染器（`NativeHeaderView`，或每个可见 section 一个真实控件的 `VirtualHeaderView`）。
* **树**用 `TreeVisibilityIndex` 把模型树压平成"可见行"，再喂给同一个 list 内核实现。

核心不变量：物化控件 = 可见 + overscan + pinned；池里的控件没有身份；一个 QModelIndex 同时最多
对应一个控件；模型变更（insert / remove / move / dataChanged / reset）不需要业务手动 reload。

## 怎么用

### 列表：最小可用

```cpp
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>

// 1) 行内业务控件：长什么样、怎么响应用户，都是你自己的事
class OrderCardWidget : public QWidget
{
public:
    explicit OrderCardWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_title = new QLabel(this);
        m_action = new QPushButton(QStringLiteral("处理"), this);   // 按钮照常收到自己的事件
    }

    void bind(const QModelIndex &index) { m_title->setText(index.data(Qt::DisplayRole).toString()); }

    void unbind()
    {
        // 停掉与上一行绑定的定时器 / 动画 / 异步请求；控件马上会被拿去给别的行用
    }

    QSize sizeHint() const override { return QSize(600, 96); }

private:
    QLabel *m_title = nullptr;
    QPushButton *m_action = nullptr;
};

// 2) 适配器：内核只问你三件事 —— 建控件、灌数据、解绑
class OrderAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new OrderCardWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderCardWidget *>(widget)->bind(index);
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        static_cast<OrderCardWidget *>(widget)->unbind();
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(600, 96); }
};

// 3) 接上模型
OrderAdapter adapter;
auto *view = new viv::VirtualListView(&window);   // 视图交给窗口持有，不要放栈上
view->setAdapter(&adapter);
view->setUniformItemHeight(96);                   // 固定行高
view->setModel(&model);                           // 任意 QAbstractItemModel
```

### 列表：动态行高（估计值 + 测量反馈）

```cpp
// 行控件自己决定高度：视图会读它的 sizeHint()
class NoteWidget : public QWidget
{
public:
    QSize sizeHint() const override
    {
        const int body = m_expanded ? 20 + m_body->heightForWidth(qMax(200, width())) : 0;
        return QSize(600, qMax(24, 24 + body));
    }

    void bind(const QModelIndex &index)
    {
        m_expanded = index.data(kExpandedRole).toBool();
        m_body->setVisible(m_expanded);
        updateGeometry();                          // 内容变了就告诉布局
    }
};

view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
view->setEstimatedItemHeight(24);      // 还没量过的行先用这个高度，滚动范围立刻可用
view->setAutoMeasureItemHeight(true);  // 行控件绑定后自动测量 sizeHint()

// 异步改行高（图片下载完、展开/折叠之后）：在你的模型里发一次 dataChanged
void NoteModel::setExpanded(const QModelIndex &index, bool expanded)
{
    // ...改数据...
    emit dataChanged(index, index, { Qt::DisplayRole });
}
// 视图只重测这一行，并用滚动锚点补偿：视口上方的行变高时，正在看的内容不会跳。
```

### 列表：像素滚动、overscan、pin 与诊断

```cpp
view->setOverscan(2, 2);               // 视口上下各多留 2 行，减少滚动瞬间的建控件
view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
view->setWheelScrollPixels(48);        // 一个滚轮刻度 48 px，与行高无关；触控板按 pixelDelta 1:1
view->setVerticalOffset(1200);         // 也可以按像素直接定位
view->scrollByPixels(24);
view->scrollTo(model.index(500, 0), viv::VirtualItemView::ScrollHint::PositionAtCenter);

// 让某一行离开可见区后仍然存在（异步操作、行内编辑器、正在播的动画……）
// 从来没上过屏的行也会被立刻物化：pin 的语义是"这个控件归我用到 unpin 为止"
view->setItemPinned(model.index(3, 0), true);
view->pinWidget(widget);               // 等价于按控件反查 index 再 pin
view->setMaxPinnedItems(50);           // 超过阈值只 qWarning 一次，方便定位"pin 得太多"

// 诊断：实例化 / 池 / pin 的数量与累计 create / bind / recycle 次数
const viv::VirtualViewStats stats = view->stats();
view->setLifecycleLoggingEnabled(true);      // 有界事件日志（create/bind/unbind/recycle/pin）
```

### 表格：行控件模式（一行一个业务控件）

```cpp
// 行控件里用 ColumnHost 承载每一列，框架负责定位，不要自己算 x
class OrderRowWidget : public QWidget
{
public:
    explicit OrderRowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_order = new QLabel(new viv::ColumnHost(0, this));
        m_status = new QLabel(new viv::ColumnHost(1, this));
        m_amount = new QLabel(new viv::ColumnHost(2, this));
    }

    void bind(const QModelIndex &rowIndex)
    {
        m_order->setText(rowIndex.siblingAtColumn(0).data().toString());
        m_status->setText(rowIndex.siblingAtColumn(1).data().toString());
        m_amount->setText(rowIndex.siblingAtColumn(2).data().toString());
    }

private:
    QLabel *m_order = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_amount = nullptr;
};

class OrderTableAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new OrderRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderRowWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(600, 44); }
};

auto *table = new viv::VirtualTableView(&window);
table->setTableAdapter(&adapter);
table->setUniformItemHeight(44);
table->setDefaultColumnWidth(150);
table->setModel(&model);

// 列状态只有一个写入口，表头、行、合并单元格都从同一份几何读
table->setColumnWidth(1, 220);
table->setColumnHidden(3, true);
table->moveColumn(2, 0);
table->setSortingEnabled(true);
table->setSortIndicator(0, Qt::AscendingOrder);

// 表头状态持久化：列宽 / 顺序 / 隐藏 / 排序 / 横向偏移
const QByteArray state = table->saveHeaderState();
table->restoreHeaderState(state);
```

不想用 `ColumnHost`、要自己摆列的话，实现 `layoutRowWidget(widget, rowIndex, context)` 钩子：
`context.columnsToLayout()` 给出这一轮要摆的列，`context.columnX(column)` 是列在行控件内的 x，
`context.column(column)` 是完整几何（宽度、是否隐藏），`context.paneHostForColumn(column)`
是这一列该挂的裁剪容器（有冻结列或多滚动组时用）。

### 表格：单元格模式（每个可见 cell 一个控件）

```cpp
class OrderCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    // 按列分池：计数列用一套控件、状态列用另一套
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column();
    }
};

auto *table = new viv::VirtualTableView(&window);
table->setModel(&model);
table->setUniformItemHeight(28);
table->setCellAdapter(&cellAdapter);
table->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
// 只实例化 visibleRows x visibleColumns；横向与纵向滚动都复用同一批控件
```

### 表格：冻结列与冻结行

```cpp
table->setFrozenColumns({0, 1});        // 左侧冻结两列（顺序无关，按视觉顺序排列）
table->setFrozenRightColumns({N - 1});  // 右侧冻结一列，右对齐贴住视口右边
table->setFrozenRows(2);                // 顶部冻结两行
table->setFrozenBottomRows(1);          // 底部冻结一行（比如合计行）

// 冻结 pane 与滚动 pane 共用同一份列几何 / 行几何，没有第二份列宽或行高副本
table->isColumnFrozen(0);
table->panes();                         // 列方向：冻结左 / 滚动 / 冻结右（可能还有多个滚动组）
table->itemPanes();                     // 行方向：冻结上 / 滚动 / 冻结下（各自的矩形与行区间）

// 交界线：表头与 body 连成一条，行方向的横向线还会跨过行号条
viv::PaneSeparatorStyle style;
style.width = 1;
style.color = QColor();                 // 不设置就取当前样式画列分隔线的颜色
style.lineStyle = Qt::SolidLine;
table->setPaneSeparatorStyle(style);

table->clearFrozenColumns();
```

冻结不产生额外滚动空间：可滚动区少掉的像素恰好等于冻结带的宽度（列方向）/ 高度（行方向），
所以横向与纵向的滚动范围都不会因为冻结而改变。

### 表格：合并单元格（span）

```cpp
// 最省事的用法：直接给合并区域的行列跨度（从第 0 行第 0 列起横跨 3 列）
table->setSpan(0, 0, 1, 3);
table->removeSpan(0, 0);
table->clearSpans();

// 合并规则由业务决定时，实现自己的 provider
class GroupSpanProvider : public viv::TableSpanProvider
{
public:
    // 约定：只有锚点报出 span，被覆盖的格子报 1x1
    viv::TableSpan spanAt(const QModelIndex &index) const override
    {
        if (index.column() == 0 && isGroupHeader(index.row()))
            return viv::TableSpan{1, 4};
        return viv::TableSpan{};
    }

private:
    bool isGroupHeader(int row) const { /* 你的规则 */ return row % 5 == 0; }
};

GroupSpanProvider provider;             // 生命周期由业务持有
table->setSpanProvider(&provider);      // takeOwnership = true 时交给表格 delete

// 合并矩形完全由已提交的列几何与行高推出；被覆盖的格子会折回锚点
const QRect merged = table->cellRect(model.index(0, 0));
const QModelIndex anchor = table->anchorIndex(model.index(0, 2));
```

### 表格：多个滚动组

```cpp
// 一块冻结列 + 两组各自滚动的列：任意数量的 pane 与滚动组，按视觉顺序给出
viv::TablePaneSpec frozenPane;
frozenPane.logicalColumns = {0};                // 左侧冻结
frozenPane.scroll = viv::PaneScroll::Frozen;

viv::TablePaneSpec leftGroup;
leftGroup.logicalColumns = {1, 2, 3};
leftGroup.scrollGroup = 0;                      // 组 0 是主组：跟随表头与横向滚动条

viv::TablePaneSpec rightGroup;
rightGroup.logicalColumns = {4, 5, 6};
rightGroup.scrollGroup = 1;                     // 其它组自己驱动

table->setPanes({frozenPane, leftGroup, rightGroup});
table->scrollGroups();                          // {0, 1}
table->setHorizontalOffset(1, 120);             // 只动组 1；主组用 setHorizontalOffset(120)
table->maximumHorizontalOffset(1);
```

### 表格：每个可见列一个真实控件的表头

```cpp
// section 控件可以放徽标、进度、筛选按钮、搜索框……只在可见列上实例化
class SectionHeader : public QWidget
{
public:
    void setTitle(const QString &title) { m_title->setText(title); }

private:
    QLabel *m_title = nullptr;
};

class SectionHeaderAdapter : public viv::HeaderWidgetAdapter
{
public:
    explicit SectionHeaderAdapter(const QAbstractItemModel *model) : m_model(model) {}

    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        return new SectionHeader(parent);
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        static_cast<SectionHeader *>(widget)->setTitle(
            m_model->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString());
    }

private:
    const QAbstractItemModel *m_model = nullptr;
};

auto *header = new viv::VirtualHeaderView(Qt::Horizontal);
header->setAdapter(&headerAdapter);
header->setLabelModel(&model);
header->setSortInteractionEnabled(true);
header->setSectionOverscan(1);
table->setHorizontalHeader(header);      // 表格接管所有权；传 nullptr 回到默认 native 表头

// 换序动画：只有"换序"需要过渡（提交后 body 立刻到位，表头滑过去）
table->setHeaderAnimationDuration(300);  // 0 = 关闭
table->moveColumn(2, 0, viv::VirtualTableView::MoveAnimation::Animate);
// 程序化换序默认即时（MoveAnimation::Immediate）；resize 与滚动保持逐帧同步，不做动画
```

### 树

```cpp
// 适配器与列表完全相同；模型是标准 QAbstractItemModel 树
auto *tree = new viv::VirtualTreeView(&window);
tree->setAdapter(&adapter);
tree->setUniformItemHeight(26);
tree->setIndentation(20);                // 每层缩进像素
tree->setBranchIndicatorsVisible(true);  // 展开/收起图标由视图绘制并可点击（行控件不受影响）
tree->setModel(&treeModel);

tree->expand(treeModel.index(0, 0));                 // 也可以双击 / 点分支指示 / 按 Right
tree->expandRecursively(treeModel.index(0, 0));      // 递归展开整棵子树
tree->toggleExpanded(treeModel.index(0, 0));
tree->collapseAll();
tree->setRootIndex(treeModel.index(3, 0));           // 只看某一棵子树
tree->visibleRowCount();                             // 压平之后的可见行数

// 自定义分支图标：视图把每个可见行的每一格都交给你，一格一次调用
class GlyphRenderer : public viv::BranchIndicatorRenderer
{
public:
    // 简单情形：一个状态一个图标；返回空 QIcon 表示这一格不画图标
    QIcon branchIcon(const viv::BranchIndicatorState &state, const QModelIndex &) const override
    {
        if (!state.adjoinsItem || !state.hasChildren)
            return QIcon();                          // 祖先格 / 叶子交给下面画连接线
        return state.isExpanded ? m_openIcon : m_closedIcon;
    }

    // 复杂情形：直接画。state 的字段对应 QTreeView::branch 的
    // has-children / has-siblings / adjoins-item / open / closed
    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &, const QRect &cellRect) const override
    {
        if (!state.adjoinsItem) {                    // 祖先格：竖线 + 到下一层的横线
            const int cx = cellRect.center().x();
            painter->drawLine(cx, cellRect.top(), cx,
                              state.hasSiblings ? cellRect.bottom() : cellRect.center().y());
            painter->drawLine(cx, cellRect.center().y(), cellRect.right() - 2, cellRect.center().y());
            return;
        }
        viv::BranchIndicatorRenderer::paintBuiltinBranch(painter, state, cellRect);
    }

private:
    QIcon m_openIcon;
    QIcon m_closedIcon;
};

GlyphRenderer glyphRenderer;                 // 生命周期由业务持有（takeOwnership = false）
tree->setBranchIndicatorRenderer(&glyphRenderer);
tree->setBranchIndicatorRenderer(nullptr);   // 回到内置三角箭头
```

### 拖放

```cpp
// 模型侧：能不能拖、能拖什么、落在哪里算合法、落了以后怎么改数据，都由模型回答
Qt::ItemFlags OrderModel::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable
         | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

Qt::DropActions OrderModel::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

QMimeData *OrderModel::mimeData(const QModelIndexList &indexes) const
{
    auto *data = new QMimeData;
    data->setData(QStringLiteral("application/x-order-id"), packedIds(indexes));
    return data;
}

bool OrderModel::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                 int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(action);
    Q_UNUSED(column);
    Q_UNUSED(parent);
    return data->hasFormat(QStringLiteral("application/x-order-id")) && row != 0;
}

bool OrderModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                              int row, int column, const QModelIndex &parent)
{
    return action == Qt::IgnoreAction ? true : moveOrInsert(data, row, parent);
}

// 视图侧：打开开关，剩下的交给框架（拖拽期会 pin 住拖拽源控件，边缘自动滚动）
view->setDragEnabled(true);                   // 同时把视口设成可接收拖放
view->setDropIndicatorShown(true);            // 列表/表格画插入线，树画框选
view->setDefaultDropAction(Qt::MoveAction);   // 本视图发起的拖拽默认动作
view->setDragDropActions(Qt::MoveAction | Qt::CopyAction);   // 可选：覆盖模型给的动作

// 观察与诊断
connect(view, &viv::VirtualItemView::itemDropped,
        [](const QModelIndex &parent, int row, int column, Qt::DropAction action) {
            // 模型已经接受了这次拖放，这里用来刷新统计、上报日志……
        });
const viv::VirtualItemView::DropTarget target = view->dropTargetAt(viewportPos);
view->dropIndicatorRect(target);
```

落点语义：列表按行的上/下半段决定插到前面还是后面；表格跟随 `SelectionBehavior`（整行模式按行
插入，单元格模式按行列定位，冻结列做 pane 感知命中）；树的上/下 1/4 是"插到节点之间"，
中间 1/2 是"成为该节点的子节点"，最后一行下方的空白区追加到末尾。拖到视口上/下边缘会自动
滚动，并按同一个视口位置重新解析落点。

## 如何安装构建

依赖：C++17、CMake 3.16+、Qt 6.2+ 或 Qt 5.15+（Core / Gui / Widgets）。

```bash
git clone https://github.com/daonvshu/qt-virtual-item-views && cd qt-virtual-item-views

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt kit 路径>      # 默认构建静态库；加 -DVIRTUALITEMVIEWS_BUILD_SHARED=ON 构建动态库
cmake --build build
cmake --install build --prefix <安装前缀>
```

装完之后是一个标准 CMake 包（头文件 + 库 + `VirtualItemViewsConfig.cmake`），消费端只写两行，
Qt 由包的配置文件自己找回：

```cmake
find_package(VirtualItemViews REQUIRED)
target_link_libraries(app PRIVATE VirtualItemViews::VirtualItemViews)
```

把 `<安装前缀>` 与 Qt kit 一起加进消费端的 `CMAKE_PREFIX_PATH` 就行（Qt 5 与 Qt 6 各装各的
前缀）。动态库构建时共享库与可执行文件放在同一个目录，运行时不需要额外设置。
