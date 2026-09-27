// 列宽比例分配示例（§9 of docs/table-layout.md）：固定宽度的列保持自己的宽度，
// 其余列按比例分掉剩余宽度。列数少、总宽正好等于视口宽度，所以不会出现横向滚动。
//
// 想直接看到的三件事：
//   1. 拖窗口宽度：比例不变，参与分配的列一起变宽/变窄，表头与 body 逐像素同步；
//   2. 拖动某一列的边界：这一列变成固定宽度（QHeaderView 的 Stretch -> Interactive），
//      剩下的列立刻重新分；
//   3. "最后一列吃掉剩余宽度"（setStretchLastColumn(true)）是同一套机制的退化形式。
//
// 无人值守：
//   --check              自检（总宽 = 视口宽、没有横向滚动、比例正确、拖动即固定）后退出
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

/// 5 列的小表：第 0 列（序号）固定，其余 4 列按比例分剩余宽度。
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
        , m_rowCount(qMax(1, rowCount))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rowCount;
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
        const int row = index.row();
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
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Vertical)
            return section + 1;
        switch (section) {
        case ColumnIndex:
            return QStringLiteral("#");
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

private:
    int m_rowCount = 0;
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
    const char *name;
    double factors[int(OrderModel::ColumnCount) - 1];
};

constexpr RatioPreset kPresets[] = {
    {"等分 1 : 1 : 1 : 1", {1.0, 1.0, 1.0, 1.0}},
    {"商品优先 2 : 4 : 1 : 1", {2.0, 4.0, 1.0, 1.0}},
    {"只拉伸商品列 0 : 1 : 0 : 0", {0.0, 1.0, 0.0, 0.0}},
};
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));
constexpr int kDefaultPreset = 1;

/// 应用一种分配方式：参与分配的列写 factor，其余列回到固定宽度。
void applyPreset(viv::VirtualTableView &view, const RatioPreset &preset)
{
    view.setStretchLastColumn(false);
    view.setColumnWidth(OrderModel::ColumnIndex, kIndexWidth);
    for (int offset = 0; offset < int(OrderModel::ColumnCount) - 1; ++offset) {
        const int column = OrderModel::ColumnOrder + offset;
        const double factor = preset.factors[offset];
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
    applyPreset(view, kPresets[kDefaultPreset]);
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

    // 5) "最后一列吃掉剩余宽度"：同一个机制的退化形式。
    applyPreset(view, kPresets[kDefaultPreset]);
    view.setStretchLastColumn(true);
    view.flushPendingRelayout();
    checkFillsTheViewport("stretch last column");

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
    applyPreset(*view, kPresets[kDefaultPreset]);

    auto *toolbar = window.addToolBar(QStringLiteral("列宽"));
    toolbar->addWidget(new QLabel(QStringLiteral("分配比例: "), &window));
    auto *presetBox = new QComboBox(&window);
    for (const RatioPreset &preset : kPresets)
        presetBox->addItem(QString::fromLatin1(preset.name));
    presetBox->setCurrentIndex(kDefaultPreset);
    presetBox->setToolTip(QStringLiteral(
        "setColumnStretchFactor()：0 的列保持自己的固定宽度，> 0 的列按权重分剩余宽度。\n"
        "带 * 的列在状态栏里就是参与分配的列。"));
    toolbar->addWidget(presetBox);
    QObject::connect(presetBox, qOverload<int>(&QComboBox::currentIndexChanged), view,
                     [view](int index) {
                         if (index >= 0 && index < kPresetCount)
                             applyPreset(*view, kPresets[index]);
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

    auto *lastStretch = new QCheckBox(QStringLiteral("最后一列吃掉剩余宽度"), &window);
    lastStretch->setToolTip(QStringLiteral(
        "setStretchLastColumn(true)：线上一列（可见顺序的最后一列）作为唯一参与者吃掉全部剩余宽度。\n"
        "它是 setColumnStretchFactor() 的退化形式——把因子设成 1 的只有它一个。"));
    toolbar->addWidget(lastStretch);
    QObject::connect(lastStretch, &QCheckBox::toggled, view,
                     &viv::VirtualTableView::setStretchLastColumn);

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
        QTimer::singleShot(exitAfter, &app, [view]() {
            std::printf("table_stretch_columns: %d rows, widths=[%s], extent=%lld, maxOffset=%lld\n",
                        view->model()->rowCount(), qPrintable(widthsText(*view)),
                        static_cast<long long>(view->horizontalContentExtent()),
                        static_cast<long long>(view->maximumHorizontalOffset()));
            std::fflush(stdout);
            QApplication::quit();
        });
    }

    return app.exec();
}
