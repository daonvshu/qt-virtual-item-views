# 插入、删除与重载后保持滚动位置

## 1. 按变化范围更新模型

| 页面操作 | 模型操作 | 滚动处理 |
| --- | --- | --- |
| 改名称、状态、图标 | 更新数据并通知 `dataChanged`，携带实际变化的 roles | 通常交给视图处理 |
| 插入、删除或追加一页 | 在当前模型的正确 parent 下插入或删除对应行 | 普通纵向列表会尝试保持顶部可见项的位置 |
| 同一条记录换位置 | 模型支持时使用 `moveRows()` 或成对的移动通知 | 保留记录的索引身份；排序由源模型或代理负责 |
| 返回同一数据集的完整快照 | 能按稳定 ID 算出差异时更新受影响的行；必须 reset 或换模型时先保存位置 | 使用第 4 节恢复同一条记录的位置 |
| 切换到新的搜索结果或另一份数据集 | 应用新的查询结果 | 通常从顶部展示；仅在需要恢复该数据集的浏览现场时使用书签 |

## 2. 在原模型上插入、删除

`QStandardItemModel` 的公开增删函数已经发出结构通知。下面的 `source` 是显示数据的源模型，`recordId` 是来自数据源的稳定标识，`insertAt` 是源模型中的插入位置。

```cpp
constexpr int IdRole = Qt::UserRole + 1;
auto *item = new QStandardItem(title);
item->setData(recordId, IdRole);
source->insertRow(insertAt, item);          // 追加到末尾也可以用 appendRow(item)

// 稍后删除：按 ID 重新定位，避免使用插入时的旧行号。
const auto hits = source->match(source->index(0, 0), IdRole, recordId,
                                1, Qt::MatchExactly);
if (!hits.isEmpty())
    source->removeRow(hits.first().row(), hits.first().parent());
```

自定义 `QAbstractItemModel` 在修改底层容器**之前**调用 `beginInsertRows(parent, first, last)`，修改后调用 `endInsertRows()`；删除同理。两个端点都包含在内：插入 3 项到位置 5，范围是 `[5, 7]`。先校验边界及数量，数量为零直接返回。同一父节点的连续操作尽量合为一个范围；删除多个不连续范围时，从后向前处理，避免前面的删除改变后续行号。

不要用 `dataChanged` 表示行数变化，也不要屏蔽模型信号后调用 `viewport()->update()` 补救。重绘无法修正视图中的行布局、选择及索引映射。

## 3. 为什么普通增删通常能留在原处

以纵向列表为例，视图在行数即将变化时记录滚动区顶部项的 `QPersistentModelIndex` 和已经滚入该项的像素数。结构更新后，只要该项仍可定位，布局就能据此补偿偏移。比如第 50 条记录顶部已被裁掉 8 px，在它前面一次插入 3 行，预期仍看到原来那条记录及其裁剪位置。此时再写回插入前的绝对偏移，反而会抵消补偿。

如果删除的就是顶部项，旧索引随之失效。希望下一条记录仍留在它原来的屏幕位置时，删除前先保存下一条的 ID 和它自己的纵坐标；删除后按第 4 节恢复。接近底部且剩余内容不足以填满视口时，允许的位置受新滚动范围限制。

## 4. 必须 reset 或换模型时，按稳定 ID 恢复

先决定要保留什么。只需回到距顶部相同的像素数时，保存 `qint64 y = view->verticalOffset()`，更新模型并调用 `flushPendingRelayout()` 后，再调用 `setVerticalOffset(y)`。如果视口上方的记录会增删、换序或改变高度，则应保存**可见记录的 ID 和它在视口中的实际 y 坐标**。相同行号或相同滚动条刻度都可能指向另一条记录。

下面是默认根节点、纵向排列的平面 `VirtualListView` 示例。模型第 0 列通过 `IdRole` 返回唯一且跨重载不变的业务 ID。代码同时记录下一条可见记录，作为首选记录消失时的备用。

```cpp
#include <virtualitemviews/virtuallistview.h>
#include <QAbstractItemModel>
#include <QVector>

struct RowMark {
    QVariant id;
    int viewportY;
};

struct ListBookmark {
    qint64 offset;
    QVector<RowMark> rows;
};

ListBookmark savePosition(viv::VirtualListView &view, int idRole)
{
    view.flushPendingRelayout();
    ListBookmark mark{view.verticalOffset(), {}};
    auto *model = view.model();
    const auto visible = view.visibleItemRange();
    if (!model || !visible.isValid())
        return mark;
    for (auto row = visible.first; row <= visible.last && mark.rows.size() < 2; ++row) {
        const auto index = model->index(int(row), 0);
        const QVariant id = index.data(idRole);
        const QRect rect = view.visualRect(index);
        if (id.isValid() && !rect.isEmpty())
            mark.rows.append(RowMark{id, rect.top()});
    }
    return mark;
}

void restorePosition(viv::VirtualListView &view, const ListBookmark &mark, int idRole)
{
    view.flushPendingRelayout();
    auto *model = view.model();
    if (!model || model->rowCount() == 0)
        return;
    for (const auto &row : mark.rows) {
        const auto hits = model->match(model->index(0, 0), idRole, row.id,
                                       1, Qt::MatchExactly);
        if (hits.isEmpty())
            continue;
        const QPersistentModelIndex index(hits.first());
        view.scrollTo(index, viv::VirtualItemView::PositionAtTop);
        if (!index.isValid() || index.model() != view.model())
            continue;
        const int currentY = view.visualRect(index).top();
        view.scrollByPixels(qint64(currentY) - row.viewportY);
        return;
    }
    view.setVerticalOffset(mark.offset);
}
```

调用顺序是 `savePosition(view, IdRole)` → 替换数据并完成 reset（或 `setModel(newModel)`）→ `restorePosition(view, mark, IdRole)`。例如原来首项的 y 为 −8，第一次定位后 y 为 0，再向下滚动 8 px 就能恢复局部裁剪。两条候选都消失时，上面的实现退回旧像素偏移，由视图限制在新的有效范围内。大数据集可用业务模型的 ID 索引代替 `match()` 的线性查找。

在 GUI 线程连续完成同步替换和恢复。若数据异步返回，在应用最新结果前记录当前浏览位置，**数据和布局就绪后**恢复；这样能保留等待期间用户的滚动。`flushPendingRelayout()` 处理已经排队的布局，不能等待异步数据或未来才确定的行高。启用了分页或刷新动画时，在视图位置稳定时取样，图片异步改变高度时尽量预留最终尺寸。

## 5. 代理、树、冻结区域与选择

- **排序或筛选代理**：视图给出的索引属于 `view.model()`。修改源模型前先 `mapToSource()`，重载后若从源模型按 ID 查找，则用 `mapFromSource()` 转回视图索引再滚动。过滤掉的记录按备用锚点处理。
- **树和树表**：`index.row()` 是同一父节点下的行号，不能当作展开后的屏幕行号。保存稳定节点 ID（必要时保存祖先 ID 路径），先恢复根节点、展开状态及排序，再对可见节点定位。
- **冻结行和多列布局**：从滚动区域选锚点，跳过固定在屏幕边缘的冻结行。Flow、Wrap、Masonry 一行可能有多项，可用可见记录的实际矩形选择候选。表格需要保留横向位置时另存 `horizontalOffset()`，待列布局恢复后调用 `setHorizontalOffset()`；独立滚动组分别保存。
- **选中项和编辑器**：单独按 ID 保存当前项与选中集合，重新绑定后使用新的 `selectionModel()`。需要恢复选择时，先恢复选择，最后恢复滚动。reset 前按业务需要提交或取消编辑；提交被拒绝时先处理草稿。不要长期保存行控件指针，控件滚出屏幕后可能已被复用。
