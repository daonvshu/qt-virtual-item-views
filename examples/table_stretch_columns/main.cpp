// 列宽比例分配示例（§9 of docs/table-layout.md）：固定宽度的列保持自己的宽度，
// 其余列按比例分掉剩余宽度。列数少、总宽正好等于视口宽度，所以不会出现横向滚动。
//
// 想直接看到的几件事：
//   1. 拖窗口宽度：比例不变，参与分配的列一起变宽/变窄，表头与 body 逐像素同步；
//   2. 拖动某一列的边界：这一列变成固定宽度（QHeaderView 的 Stretch -> Interactive），
//      剩下的列立刻重新分；
//   3. "固定后两列"：固定宽度放在末尾也一样，比例只描述还在流动的列；
//   4. 拖动左侧行号条换行序——行序属于模型，库只发 rowMoveRequested()，真正移动由
//      模型的 moveRows() 完成（本示例的 OrderModel 实现了它）。
//
// 无人值守：
//   --check              自检（总宽 = 视口宽、没有横向滚动、比例正确、拖动即固定、
//                        固定后两列、换行序）后退出
//   --snapshot <path>    导出 PNG 后退出
//   --exit-after <ms>    毫秒后退出

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <cstdio>

namespace {

constexpr int kRowHeight = 28;
constexpr int kIndexWidth = 56;      // 第 0 列的固定宽度
constexpr int kPlainWidth = 120;     // 不参与分配的列的固定宽度

/// 5 列的小表：第 0 列（行号）固定，其余 4 列按比例分剩余宽度。
///
/// 行序由模型持有（`m_order`：视图行 → 数据行），因为行号条拖动是"请求模型移动"：
/// 渲染器只发 `sectionMoveRequested`，表格转成 `rowMoveRequested()` 再调 `moveRows()`。
/// `data()` / `headerData()` 都按数据行取值，所以拖动行号条以后整行内容（连同行号标识）
/// 一起换位，一眼就能看出移动真的生效了——`QAbstractTableModel` 的默认 `moveRows()`
/// 只返回 false，什么都不会发生（那样拖动就只是预览一下然后回弹）。
class OrderModel : public QAbstractTableModel
{
public:
    enum Column {
        ColumnIndex = 0,
        ColumnOrder,
        ColumnProduct,
        ColumnStatus,
        ColumnAmount,
        ColumnCount,
    };

    explicit OrderModel(int rowCount, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
        for (int row = 0; row < qMax(1, rowCount); ++row)
            m_order.append(row);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_order.size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(ColumnCount);
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return QVariant();
        if (role == Qt::TextAlignmentRole) {
            switch (index.column()) {
            case ColumnIndex:
            case ColumnStatus:
                return int(Qt::AlignCenter);
            case ColumnAmount:
                return int(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return int(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
        if (role != Qt::DisplayRole)
            return QVariant();
        // 数据行（稳定身份）：换行序以后 view row 与它不再相等。
        const int row = sourceRow(index.row());
        switch (index.column()) {
        case ColumnIndex:
            return row + 1;
        case ColumnOrder:
            return QStringLiteral("SO-%1").arg(240000 + row);
        case ColumnProduct:
        {
            static const QStringList products{
                QStringLiteral("人体工学椅"), QStringLiteral("4K 显示器"),
                QStringLiteral("机械键盘"), QStringLiteral("降噪耳机"),
                QStringLiteral("USB-C 扩展坞")};
            return products.at(row % products.size());
        }
        case ColumnStatus:
        {
            static const QStringList states{QStringLiteral("已付款"), QStringLiteral("待发货"),
                                            QStringLiteral("运输中"), QStringLiteral("已完成")};
            return states.at(row % states.size());
        }
        case ColumnAmount:
            return QStringLiteral("¥ %1").arg((row % 97 + 1) * 37);
        default:
            return QVariant();
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || section < 0 || section >= m_order.size())
            return QVariant();
        if (orientation == Qt::Vertical)
            return QStringLiteral("R%1").arg(sourceRow(section) + 1);
        switch (section) {
        case ColumnIndex:
            return QStringLiteral("行号");
        case ColumnOrder:
            return QStringLiteral("订单号");
        case ColumnProduct:
            return QStringLiteral("商品");
        case ColumnStatus:
            return QStringLiteral("状态");
        case ColumnAmount:
            return QStringLiteral("金额");
        default:
            return QVariant();
        }
    }

    /// 行号条拖动的落点：库先发 `rowMoveRequested()`，再调用这里。
    ///
    /// `destinationChild` 是 Qt 的"插到这一行之前"语义；往后面移时目的行要 +1，
    /// 视图侧（`VirtualTableView::moveRowsForStripDrag()`）已经换算好了。
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if (sourceParent.isValid() || destinationParent.isValid() || count <= 0 || sourceRow < 0
            || sourceRow + count > m_order.size())
            return false;
        if (destinationChild < 0 || destinationChild > m_order.size())
            return false;
        if (destinationChild >= sourceRow && destinationChild <= sourceRow + count)
            return false;   // 移到自己身上 = 没移动（Qt 的约定）
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent,
                           destinationChild))
            return false;
        QVector<int> moved;
        for (int index = 0; index < count; ++index)
            moved.append(m_order.at(sourceRow + index));
        m_order.remove(sourceRow, count);
        const int target =
            destinationChild > sourceRow ? destinationChild - count : destinationChild;
        for (int index = 0; index < count; ++index)
            m_order.insert(target + index, moved.at(index));
        endMoveRows();
        return true;
    }

    /// 数据行（稳定身份）of a view row.
    int sourceRow(int viewRow) const { return m_order.value(viewRow, viewRow); }

private:
    QVector<int> m_order;
};

/// 一行：每列一个 ColumnHost，框架负责定位（列宽来自 HeaderGeometry，业务不自己算 x）。
class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < int(OrderModel::ColumnCount); ++column) {
            auto *host = new viv::ColumnHost(column, this);
            m_hosts.append(host);
            m_labels.append(new QLabel(host));
        }
    }

    void bind(const QModelIndex &rowIndex)
    {
        for (int column = 0; column < m_labels.size(); ++column) {
            const QModelIndex cell = rowIndex.siblingAtColumn(column);
            QLabel *label = m_labels.at(column);
            label->setText(cell.data().toString());
            label->setAlignment(Qt::Alignment(cell.data(Qt::TextAlignmentRole).toInt()));
            label->setGeometry(6, 0, qMax(0, m_hosts.at(column)->width() - 12), kRowHeight);
        }
    }

    QVector<viv::ColumnHost *> hosts() const { return m_hosts; }

private:
    QVector<viv::ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new RowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowWidget *>(widget)->bind(index);
    }

    void layoutRowWidget(QWidget *widget, const QModelIndex &,
                         const viv::TableRowLayoutContext &) override
    {
        for (viv::ColumnHost *host : static_cast<RowWidget *>(widget)->hosts()) {
            for (QLabel *label : host->findChildren<QLabel *>())
                label->setGeometry(6, 0, qMax(0, host->width() - 12), kRowHeight);
        }
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kPlainWidth * int(OrderModel::ColumnCount), kRowHeight);
    }

    int created = 0;
};

/// 一种分配方式：4 个可分配列的权重，0 = 这一列保持自己的固定宽度。
struct RatioPreset
{
    /// 中文名直接存 QString：源码是 UTF-8，用 fromLatin1() 解码会得到乱码。
    QString name;
    double factors[int(OrderModel::ColumnCount) - 1];
};

const RatioPreset kPresets[] = {
    {QStringLiteral("等分 1 : 1 : 1 : 1"), {1.0, 1.0, 1.0, 1.0}},
    {QStringLiteral("商品优先 2 : 4 : 1 : 1"), {2.0, 4.0, 1.0, 1.0}},
    {QStringLiteral("只拉伸商品列 0 : 1 : 0 : 0"), {0.0, 1.0, 0.0, 0.0}},
};
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));
constexpr int kDefaultPreset = 1;

/// 应用一种分配方式：参与分配的列写 factor，其余列回到固定宽度。
///
/// \a fixLastTwoColumns 是"固定最后两列"（状态/金额）：它们退出分配、保持自己的宽度，
/// 于是剩余宽度落到中间的列上——固定列在两端都可以，比例只描述还在流动的那些列。
void applyPreset(viv::VirtualTableView &view, const RatioPreset &preset,
                 bool fixLastTwoColumns)
{
    view.setColumnWidth(OrderModel::ColumnIndex, kIndexWidth);
    const int tailStart = int(OrderModel::ColumnCount) - 2;
    for (int offset = 0; offset < int(OrderModel::ColumnCount) - 1; ++offset) {
        const int column = OrderModel::ColumnOrder + offset;
        const bool fixed = fixLastTwoColumns && column >= tailStart;
        const double factor = fixed ? 0.0 : preset.factors[offset];
        view.setColumnStretchFactor(column, factor);
        if (factor <= 0.0)
            view.setColumnWidth(column, kPlainWidth);
    }
}

QString widthsText(const viv::VirtualTableView &view)
{
    QStringList parts;
    for (int column = 0; column < view.columnCount(); ++column) {
        const QString width = QString::number(view.columnWidth(column));
        parts << (view.columnStretchFactor(column) > 0.0 ? width + QStringLiteral("*") : width);
    }
    return parts.join(QStringLiteral(" | "));
}

/// 自检：总宽 = 视口宽、没有横向滚动、比例正确、拖动边界即固定。
bool runCheck(viv::VirtualTableView &view)
{
    bool ok = true;
    const auto fail = [&ok](const QString &what) {
        std::printf("table_stretch_columns: check FAILED %s\n", qPrintable(what));
        ok = false;
    };

    view.setFrozenColumns(QVector<int>());
    applyPreset(view, kPresets[kDefaultPreset], false);
    QApplication::processEvents();
    view.flushPendingRelayout();

    const auto checkFillsTheViewport = [&](const char *what) {
        const int viewportWidth = view.viewport()->width();
        if (view.horizontalContentExtent() != viewportWidth) {
            fail(QStringLiteral("%1: content extent %2 != viewport width %3")
                     .arg(QString::fromLatin1(what))
                     .arg(view.horizontalContentExtent())
                     .arg(viewportWidth));
        }
        if (view.maximumHorizontalOffset() != 0) {
            fail(QStringLiteral("%1: horizontal scrolling is possible (%2 px)")
                     .arg(QString::fromLatin1(what))
                     .arg(view.maximumHorizontalOffset()));
        }
        const viv::ColumnGeometry last
            = view.columnGeometry(int(OrderModel::ColumnCount) - 1);
        if (last.viewportX + last.width != viewportWidth) {
            fail(QStringLiteral("%1: the last column ends at %2, viewport is %3 px")
                     .arg(QString::fromLatin1(what))
                     .arg(last.viewportX + last.width)
                     .arg(viewportWidth));
        }
    };

    // 1) 所有列正好填满视口，横向滚动条没有活干。
    checkFillsTheViewport("default preset");

    // 2) 比例：固定列先拿自己的宽度，其余按 factor 分，最后一个参与者吃掉取整余数。
    const auto checkRatios = [&](const char *what) {
        const int viewportWidth = view.viewport()->width();
        qint64 fixedTotal = 0;
        qreal totalFactor = 0.0;
        QVector<int> participants;
        for (int column = 0; column < view.columnCount(); ++column) {
            const qreal factor = view.columnStretchFactor(column);
            if (factor > 0.0) {
                participants.append(column);
                totalFactor += factor;
            } else {
                fixedTotal += view.columnWidth(column);
            }
        }
        if (participants.isEmpty() || totalFactor <= 0.0) {
            fail(QStringLiteral("%1: no stretching column").arg(QString::fromLatin1(what)));
            return;
        }
        const qint64 leftover = viewportWidth - fixedTotal;
        qint64 assigned = 0;
        for (int index = 0; index < participants.size(); ++index) {
            const int column = participants.at(index);
            const qint64 expected = index + 1 < participants.size()
                ? qint64(qreal(leftover) * view.columnStretchFactor(column) / totalFactor)
                : leftover - assigned;
            assigned += expected;
            if (view.columnWidth(column) != int(expected)) {
                fail(QStringLiteral("%1: column %2 is %3 px, the ratio asks for %4")
                         .arg(QString::fromLatin1(what))
                         .arg(column)
                         .arg(view.columnWidth(column))
                         .arg(expected));
            }
        }
    };
    checkRatios("default preset");

    // 3) 窗口变宽：同一个比例重新量一遍，依然正好填满。
    const int startWidth = view.width();
    view.resize(startWidth + 180, view.height());
    QApplication::processEvents();
    view.flushPendingRelayout();
    checkFillsTheViewport("after resize");
    checkRatios("after resize");
    view.resize(startWidth, view.height());
    QApplication::processEvents();
    view.flushPendingRelayout();

    // 4) 拖动某一列的边界 = 这一列以后固定（factor 被清掉），其余的重新分。
    const int dragged = OrderModel::ColumnOrder;
    const qreal factorBefore = view.columnStretchFactor(dragged);
    view.setColumnWidth(dragged, 150);
    if (factorBefore <= 0.0 || view.columnStretchFactor(dragged) != 0.0) {
        fail(QStringLiteral("resizing a stretched column did not make it fixed"));
    }
    if (view.columnWidth(dragged) != 150) {
        fail(QStringLiteral("the dragged column did not keep its width"));
    }
    checkFillsTheViewport("after a resize gesture");
    checkRatios("after a resize gesture");

    // 5) "固定最后两列"：固定列也可以在末尾，比例只描述还在流动的列——后两列退出分配、
    //    保持自己的宽度，剩下的正好被中间的列填满。
    applyPreset(view, kPresets[kDefaultPreset], true);
    view.flushPendingRelayout();
    checkFillsTheViewport("fixed tail columns");
    checkRatios("fixed tail columns");
    for (int column = int(OrderModel::ColumnCount) - 2; column < int(OrderModel::ColumnCount);
         ++column) {
        if (view.columnStretchFactor(column) != 0.0 || view.columnWidth(column) != kPlainWidth) {
            fail(QStringLiteral("column %1 is not fixed at the tail (factor %2, width %3)")
                     .arg(column)
                     .arg(view.columnStretchFactor(column))
                     .arg(view.columnWidth(column)));
        }
    }

    // 6) 行号条拖动 = 请求模型换行序：库调用的正是 moveRows()，模型不给它就没法生效。
    const auto rowId = [&view](int viewRow) {
        return view.model()->index(viewRow, OrderModel::ColumnIndex).data().toInt();
    };
    const int firstId = rowId(0);
    const int thirdId = rowId(2);
    if (!view.model()->moveRows(QModelIndex(), 2, 1, QModelIndex(), 0)) {
        fail(QStringLiteral("the model refused to move a row: is moveRows() implemented?"));
    } else {
        view.flushPendingRelayout();
        if (rowId(0) != thirdId || rowId(1) != firstId) {
            fail(QStringLiteral("the rows did not change order after the model moved one"));
        }
        if (view.model()->headerData(0, Qt::Vertical, Qt::DisplayRole).toString()
            != QStringLiteral("R%1").arg(thirdId)) {
            fail(QStringLiteral("the row strip does not label the moved row"));
        }
        // 移回原处，后面的断言仍然从原始行序开始。
        view.model()->moveRows(QModelIndex(), 0, 1, QModelIndex(), 3);
        view.flushPendingRelayout();
        if (rowId(0) != firstId) {
            fail(QStringLiteral("the row order was not restored"));
        }
    }

    std::printf("table_stretch_columns: check %s (viewport=%d widths=[%s] extent=%lld maxOffset=%lld)\n",
                ok ? "PASSED" : "FAILED", view.viewport()->width(),
                qPrintable(widthsText(view)), static_cast<long long>(view.horizontalContentExtent()),
                static_cast<long long>(view.maximumHorizontalOffset()));
    std::fflush(stdout);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("VirtualItemViews: column stretch ratios (§9 of docs/table-layout.md)"));
    parser.addHelpOption();
    const QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("逻辑行数"),
                                        QStringLiteral("count"), QStringLiteral("200"));
    const QCommandLineOption checkOption(QStringLiteral("check"),
                                        QStringLiteral("自检后退出（无人值守）"));
    const QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                           QStringLiteral("导出 PNG 后退出"),
                                           QStringLiteral("path"));
    const QCommandLineOption exitAfterOption(QStringLiteral("exit-after"),
                                            QStringLiteral("毫秒后退出"),
                                            QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(rowsOption);
    parser.addOption(checkOption);
    parser.addOption(snapshotOption);
    parser.addOption(exitAfterOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    OrderModel model(rowCount);
    RowAdapter adapter;

    QMainWindow window;
    auto *view = new viv::VirtualTableView(&window);
    view->setTableAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(kPlainWidth);
    view->setModel(&model);
    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · stretch columns"));
    window.resize(760, 560);

    // 比例写在几何里，表头与 body 读的是同一份，所以永远不会不一致。
    applyPreset(*view, kPresets[kDefaultPreset], false);

    auto *toolbar = window.addToolBar(QStringLiteral("列宽"));

    // "固定后两列"先建好：分配比例的 lambda 要看它当前的状态（两个控件是同一份状态的
    // 两个入口，改哪个都重新按比例量一遍）。
    auto *fixTail = new QCheckBox(QStringLiteral("固定后两列"), &window);
    fixTail->setToolTip(QStringLiteral(
        "固定宽度可以在任何位置，包括末尾：勾上以后“状态/金额”退出分配、保持自己的宽度，\n"
        "剩余宽度只落在还在流动的列上。比例描述的是“谁在分剩余宽度”，与列的先后顺序无关。"));

    toolbar->addWidget(new QLabel(QStringLiteral("分配比例: "), &window));
    auto *presetBox = new QComboBox(&window);
    for (const RatioPreset &preset : kPresets)
        presetBox->addItem(preset.name);
    presetBox->setCurrentIndex(kDefaultPreset);
    presetBox->setToolTip(QStringLiteral(
        "setColumnStretchFactor()：0 的列保持自己的固定宽度，> 0 的列按权重分剩余宽度。\n"
        "带 * 的列在状态栏里就是参与分配的列。"));
    toolbar->addWidget(presetBox);
    QObject::connect(presetBox, qOverload<int>(&QComboBox::currentIndexChanged), view,
                     [view, fixTail](int index) {
                         if (index >= 0 && index < kPresetCount)
                             applyPreset(*view, kPresets[index], fixTail->isChecked());
                     });

    toolbar->addWidget(new QLabel(QStringLiteral("  首列固定 "), &window));
    auto *indexWidth = new QSpinBox(&window);
    indexWidth->setRange(24, 200);
    indexWidth->setSingleStep(4);
    indexWidth->setValue(kIndexWidth);
    indexWidth->setSuffix(QStringLiteral(" px"));
    toolbar->addWidget(indexWidth);
    QObject::connect(indexWidth, qOverload<int>(&QSpinBox::valueChanged), view, [view](int width) {
        view->setColumnWidth(OrderModel::ColumnIndex, width);
    });

    toolbar->addWidget(fixTail);
    QObject::connect(fixTail, &QCheckBox::toggled, view, [view, presetBox](bool on) {
        const int index = presetBox->currentIndex();
        if (index >= 0 && index < kPresetCount)
            applyPreset(*view, kPresets[index], on);
    });

    auto *freezeFirst = new QCheckBox(QStringLiteral("冻结第 1 列"), &window);
    freezeFirst->setToolTip(QStringLiteral(
        "冻结列与其它列共用同一份几何：冻结不改变列宽，也不产生额外滚动空间。"));
    toolbar->addWidget(freezeFirst);
    QObject::connect(freezeFirst, &QCheckBox::toggled, view, [view](bool on) {
        view->setFrozenColumns(on ? QVector<int>({OrderModel::ColumnIndex}) : QVector<int>());
    });

    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);
    const auto updateStatus = [&]() {
        status->setText(QStringLiteral("列宽(* = 参与分配): %1    合计 %2 = 视口 %3    横向偏移 %4/%5"
                                       "    行 %6    实例化 %7")
                            .arg(widthsText(*view))
                            .arg(view->horizontalContentExtent())
                            .arg(view->viewport()->width())
                            .arg(view->horizontalOffset())
                            .arg(view->maximumHorizontalOffset())
                            .arg(view->model()->rowCount())
                            .arg(view->materializedItemCount()));
    };
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    auto *statusTimer = new QTimer(&window);
    QObject::connect(statusTimer, &QTimer::timeout, &window, updateStatus);
    statusTimer->start(200);
    QTimer::singleShot(0, &window, updateStatus);

    window.show();

    // 行号条的拖动是"请求模型移动"：示例的 OrderModel 实现了 moveRows()，所以能真的换行序
    // （模型不给的话，拖动只有预览，松手回弹）。
    if (view->verticalHeader()) {
        view->verticalHeader()->headerWidget()->setToolTip(QStringLiteral(
            "拖动行号可以换行序：行号条只发 rowMoveRequested()，真正的移动由模型的 "
            "moveRows() 完成。"));
    }

    const QString snapshotPath = parser.value(snapshotOption);
    const int exitAfter = parser.value(exitAfterOption).toInt();
    if (parser.isSet(checkOption)) {
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, &app]() {
            app.exit(runCheck(*view) ? 0 : 1);
        });
    } else if (!snapshotPath.isEmpty()) {
        QTimer::singleShot(qMax(1, exitAfter), &app, [&window, snapshotPath, view]() {
            const QPixmap shot = window.grab();
            const bool saved = shot.save(snapshotPath);
            std::printf("table_stretch_columns: snapshot %s (%dx%d)%s widths=[%s] extent=%lld\n",
                        qPrintable(snapshotPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED", qPrintable(widthsText(*view)),
                        static_cast<long long>(view->horizontalContentExtent()));
            std::fflush(stdout);
            QApplication::quit();
        });
    } else if (exitAfter > 0) {
        QTimer::singleShot(exitAfter, &app, [view, presetBox, fixTail]() {
            // 日志行只用 ASCII：控制台/重定向的编码各不相同，中文会显示成乱码。
            QString factors;
            for (int column = 0; column < view->columnCount(); ++column) {
                factors += QStringLiteral("%1%2")
                               .arg(column == 0 ? QString() : QStringLiteral(","))
                               .arg(view->columnStretchFactor(column), 0, 'g', 3);
            }
            std::printf("table_stretch_columns: %d rows, preset=%d, factors=[%s], fixTail=%d, "
                        "widths=[%s], extent=%lld, maxOffset=%lld\n",
                        view->model()->rowCount(), presetBox->currentIndex(),
                        qPrintable(factors),
                        fixTail->isChecked() ? 1 : 0, qPrintable(widthsText(*view)),
                        static_cast<long long>(view->horizontalContentExtent()),
                        static_cast<long long>(view->maximumHorizontalOffset()));
            std::fflush(stdout);
            QApplication::quit();
        });
    }

    return app.exec();
}
