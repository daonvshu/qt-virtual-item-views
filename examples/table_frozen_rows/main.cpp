// 行冻结示例（§31 行方向，docs/row-freezing.md）：顶部/底部冻结行 + 左侧冻结列，
// 行号条按行 pane 切分。
//
// 想直接看到的三件事：
//   1. 冻结行不随纵向滚动移动（滚动行在它们下面走）；
//   2. 行号条跟着 pane 切分，冻结行旁边就是它们自己的行号（不再错位）；
//   3. 冻结不产生额外滚动空间：最大纵向偏移与不冻结时一模一样。
//
// 无人值守：
//   --check                自检上面三件事 + 命中归属，然后退出
//   --snapshot <path>      导出 PNG
//   --exit-after <ms>      自动退出

#include <virtualitemviews/itempane.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <cstdio>

namespace {

constexpr int kRowHeight = 26;
constexpr int kColumnWidth = 130;

/// 轻量模型：不预分配 rowCount x columnCount 个对象。
class FrozenRowsModel : public QAbstractTableModel
{
public:
    FrozenRowsModel(int rowCount, int columnCount, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rowCount(qMax(1, rowCount))
        , m_columnCount(qMax(1, columnCount))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rowCount;
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columnCount;
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("行 %1 · 列 %2").arg(index.row() + 1).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1)
                                             : QString::number(section + 1);
    }

private:
    int m_rowCount = 0;
    int m_columnCount = 0;
};

/// 一行：每列一个 ColumnHost，框架负责摆放与裁剪（行方向由内核的行 pane 容器负责）。
class RowsWidget : public QWidget
{
public:
    RowsWidget(int columnCount, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            m_hosts.append(host);
            m_labels.append(new QLabel(host));
        }
    }

    void bind(const QModelIndex &rowIndex)
    {
        for (int column = 0; column < m_labels.size(); ++column) {
            m_labels.at(column)->setText(rowIndex.siblingAtColumn(column).data().toString());
            m_labels.at(column)->setGeometry(6, 0, qMax(0, m_hosts.at(column)->width() - 12),
                                             kRowHeight);
        }
    }

    QVector<viv::ColumnHost *> hosts() const { return m_hosts; }

private:
    QVector<viv::ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

class RowsAdapter : public viv::TableWidgetAdapter
{
public:
    explicit RowsAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new RowsWidget(m_columnCount, parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowsWidget *>(widget)->bind(index);
    }

    void layoutRowWidget(QWidget *widget, const QModelIndex &,
                         const viv::TableRowLayoutContext &) override
    {
        auto *row = static_cast<RowsWidget *>(widget);
        for (viv::ColumnHost *host : row->hosts()) {
            for (QLabel *label : host->findChildren<QLabel *>())
                label->setGeometry(6, 0, qMax(0, host->width() - 12), kRowHeight);
        }
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * m_columnCount, kRowHeight);
    }

    int created = 0;

private:
    int m_columnCount = 0;
};

/// 可见的垂直表头 = 每个行 pane 一条。
QList<QHeaderView *> rowStrips(viv::VirtualTableView &view)
{
    QList<QHeaderView *> strips;
    for (QHeaderView *header :
         view.findChildren<QHeaderView *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (header->orientation() == Qt::Vertical && header->isVisible())
            strips.append(header);
    }
    return strips;
}

QWidget *widgetForRow(viv::VirtualTableView &view, int row)
{
    for (const viv::MaterializedItem &item : view.materializedItems()) {
        if (int(item.index.row()) == row)
            return item.widget;
    }
    return nullptr;
}

/// 自检：冻结行不动、滚动范围不变、行号条按 pane 贴合、命中归属正确。
bool runCheck(viv::VirtualTableView &view, QAbstractItemModel *model)
{
    bool ok = true;
    const int frozenTop = view.frozenRows();
    const int frozenBottom = view.frozenBottomRows();
    if (frozenTop + frozenBottom == 0) {
        std::printf("table_frozen_rows: check needs frozen rows (--frozen-rows N)\n");
        return false;
    }

    // 1) 冻结不产生额外滚动空间。
    const qint64 frozenMaximum = view.maximumVerticalOffset();
    view.setFrozenRows(0);
    view.setFrozenBottomRows(0);
    view.flushPendingRelayout();
    const qint64 plainMaximum = view.maximumVerticalOffset();
    view.setFrozenRows(frozenTop);
    view.setFrozenBottomRows(frozenBottom);
    view.flushPendingRelayout();
    if (frozenMaximum != plainMaximum) {
        std::printf("table_frozen_rows: check FAILED scroll range changed with freezing "
                    "(%lld vs %lld)\n",
                    static_cast<long long>(frozenMaximum), static_cast<long long>(plainMaximum));
        ok = false;
    }

    // 2) 滚动：冻结行不动，滚动 pane 顶边的行恰好贴在 pane 顶边（= 滚动行跟随偏移）。
    const QVector<viv::ItemPane> panes = view.itemPanes();
    const viv::VirtualItemView &asView = view;
    QWidget *frozenRow = widgetForRow(view, 0);
    const QRect scrollingPane = view.itemPaneRect(viv::ItemPane::Type::Scrollable);
    const qint64 step = qMin<qint64>(5 * kRowHeight, view.maximumVerticalOffset());
    view.setVerticalOffset(step);
    view.flushPendingRelayout();
    if (!frozenRow || frozenRow->mapTo(view.viewport(), QPoint(0, 0)).y() != 0) {
        std::printf("table_frozen_rows: check FAILED the frozen row moved (y=%d)\n",
                    frozenRow ? frozenRow->mapTo(view.viewport(), QPoint(0, 0)).y() : -1);
        ok = false;
    }
    const QModelIndex topRow = asView.indexAt(QPoint(5, scrollingPane.y() + 2));
    const QRect topRect = view.visualRect(topRow);
    if (!topRow.isValid() || topRect.y() != scrollingPane.y()) {
        std::printf("table_frozen_rows: check FAILED the scrolling row did not follow the offset "
                    "(row=%d y=%d expected=%d)\n",
                    topRow.isValid() ? topRow.row() : -1, topRect.y(), scrollingPane.y());
        ok = false;
    }
    view.setVerticalOffset(0);
    view.flushPendingRelayout();

    // 3) 行号条：每个行 pane 一条，且贴着自己的行。
    const QList<QHeaderView *> strips = rowStrips(view);
    if (strips.size() != panes.size()) {
        std::printf("table_frozen_rows: check FAILED %d row strips for %d panes\n",
                    int(strips.size()), int(panes.size()));
        ok = false;
    }
    const QRect viewportRect = view.viewport()->geometry();
    for (const viv::ItemPane &pane : panes) {
        const int paneTop = viewportRect.y() + pane.viewportRect.y();
        QHeaderView *strip = nullptr;
        for (QHeaderView *candidate : strips) {
            if (candidate->geometry().y() == paneTop
                && candidate->geometry().height() == pane.viewportRect.height()) {
                strip = candidate;
                break;
            }
        }
        if (!strip) {
            std::printf("table_frozen_rows: check FAILED no row strip on pane %d\n",
                        int(pane.type));
            ok = false;
            continue;
        }
        for (int row = int(pane.firstRow); row <= int(pane.lastRow); ++row) {
            const QRect rowRect = view.visualRect(model->index(row, 0));
            if (rowRect.y() < pane.viewportRect.y() || rowRect.bottom() > pane.viewportRect.bottom())
                continue; // the row is scrolled out of this band
            if (strip->geometry().y() + strip->sectionViewportPosition(row)
                != viewportRect.y() + rowRect.y()) {
                std::printf("table_frozen_rows: check FAILED row %d number not glued to its row "
                            "(strip y=%d pos=%d row y=%d)\n",
                            row, strip->geometry().y(), row, rowRect.y());
                ok = false;
            }
            break;
        }
    }

    // 4) 命中：每个 pane 的第一像素都属于它自己的行。
    for (const viv::ItemPane &pane : panes) {
        if (pane.viewportRect.height() <= 0)
            continue;
        const QModelIndex hit = asView.indexAt(QPoint(5, pane.viewportRect.y() + 2));
        if (!hit.isValid() || hit.row() < pane.firstRow || hit.row() > pane.lastRow) {
            std::printf("table_frozen_rows: check FAILED hit in pane %d landed on row %d\n",
                        int(pane.type), hit.isValid() ? hit.row() : -1);
            ok = false;
        }
    }

    std::printf("table_frozen_rows: check %s (frozenRows=%d+%d panes=%d offset=%lld/%lld)\n",
                ok ? "PASSED" : "FAILED", view.frozenRows(), view.frozenBottomRows(),
                int(panes.size()), static_cast<long long>(view.verticalOffset()),
                static_cast<long long>(view.maximumVerticalOffset()));
    std::fflush(stdout);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: frozen rows (§31)"));
    parser.addHelpOption();
    const QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("逻辑行数"),
                                        QStringLiteral("count"), QStringLiteral("100000"));
    const QCommandLineOption columnsOption(QStringLiteral("columns"), QStringLiteral("列数"),
                                          QStringLiteral("count"), QStringLiteral("8"));
    const QCommandLineOption frozenRowsOption(QStringLiteral("frozen-rows"),
                                             QStringLiteral("顶部冻结行数"),
                                             QStringLiteral("count"), QStringLiteral("3"));
    const QCommandLineOption frozenBottomRowsOption(QStringLiteral("frozen-bottom-rows"),
                                                   QStringLiteral("底部冻结行数"),
                                                   QStringLiteral("count"), QStringLiteral("2"));
    const QCommandLineOption frozenColumnsOption(QStringLiteral("frozen-columns"),
                                                QStringLiteral("左侧冻结列数"),
                                                QStringLiteral("count"), QStringLiteral("1"));
    const QCommandLineOption checkOption(QStringLiteral("check"),
                                        QStringLiteral("自检后退出（无人值守）"));
    const QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                           QStringLiteral("导出 PNG 后退出"),
                                           QStringLiteral("path"));
    const QCommandLineOption exitAfterOption(QStringLiteral("exit-after"),
                                            QStringLiteral("毫秒后退出"),
                                            QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(rowsOption);
    parser.addOption(columnsOption);
    parser.addOption(frozenRowsOption);
    parser.addOption(frozenBottomRowsOption);
    parser.addOption(frozenColumnsOption);
    parser.addOption(checkOption);
    parser.addOption(snapshotOption);
    parser.addOption(exitAfterOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    const int columnCount = qMax(1, parser.value(columnsOption).toInt());
    FrozenRowsModel model(rowCount, columnCount);
    RowsAdapter adapter(columnCount);

    QMainWindow window;
    auto *view = new viv::VirtualTableView(&window);
    view->setTableAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(kColumnWidth);
    view->setModel(&model);
    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · frozen rows"));
    window.resize(1000, 620);

    auto *toolbar = window.addToolBar(QStringLiteral("冻结"));
    const auto addSpin = [&](const QString &label, int value, int maximum) {
        toolbar->addWidget(new QLabel(label + QStringLiteral(" "), &window));
        auto *spin = new QSpinBox(&window);
        spin->setRange(0, maximum);
        spin->setValue(value);
        toolbar->addWidget(spin);
        return spin;
    };
    auto *frozenTop = addSpin(QStringLiteral("顶部行"), qMax(0, parser.value(frozenRowsOption).toInt()),
                              qMin(20, rowCount));
    auto *frozenBottom = addSpin(QStringLiteral("底部行"),
                                 qMax(0, parser.value(frozenBottomRowsOption).toInt()),
                                 qMin(20, rowCount));
    auto *frozenLeft = addSpin(QStringLiteral("左侧列"),
                               qMax(0, parser.value(frozenColumnsOption).toInt()),
                               qMax(0, columnCount - 1));

    const auto applyFreezing = [&]() {
        view->setFrozenRows(frozenTop->value());
        view->setFrozenBottomRows(frozenBottom->value());
        QVector<int> columns;
        for (int column = 0; column < frozenLeft->value(); ++column)
            columns.append(column);
        view->setFrozenColumns(columns);
    };
    QObject::connect(frozenTop, qOverload<int>(&QSpinBox::valueChanged), view, [&](int) {
        applyFreezing();
    });
    QObject::connect(frozenBottom, qOverload<int>(&QSpinBox::valueChanged), view, [&](int) {
        applyFreezing();
    });
    QObject::connect(frozenLeft, qOverload<int>(&QSpinBox::valueChanged), view, [&](int) {
        applyFreezing();
    });
    applyFreezing();

    // 自动纵向滚动：冻结行停在原地，滚动行在它们下面走。
    auto *autoScroll = new QCheckBox(QStringLiteral("自动滚动"), &window);
    toolbar->addWidget(autoScroll);
    auto *timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, view, [view]() {
        const qint64 step = 6;
        if (view->verticalOffset() + step >= view->maximumVerticalOffset())
            view->setVerticalOffset(0);
        else
            view->setVerticalOffset(view->verticalOffset() + step);
    });
    QObject::connect(autoScroll, &QCheckBox::toggled, timer, [timer](bool on) {
        on ? timer->start(16) : timer->stop();
    });

    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);
    const auto updateStatus = [&]() {
        QStringList parts;
        const QVector<viv::ItemPane> panes = view->itemPanes();
        for (const viv::ItemPane &pane : panes) {
            parts << QStringLiteral("%1 行[%2..%3] y=%4 h=%5")
                         .arg(pane.type == viv::ItemPane::Type::FrozenTop ? QStringLiteral("冻结顶")
                              : pane.type == viv::ItemPane::Type::FrozenBottom
                                  ? QStringLiteral("冻结底")
                                  : QStringLiteral("滚动"))
                         .arg(pane.firstRow)
                         .arg(pane.lastRow)
                         .arg(pane.viewportRect.y())
                         .arg(pane.viewportRect.height());
        }
        status->setText(QStringLiteral("%1 行 · %2 列 · 行号条 %3 条 · 偏移 %4/%5 · 实例化行 %6 · %7")
                            .arg(view->model()->rowCount())
                            .arg(view->columnCount())
                            .arg(rowStrips(*view).size())
                            .arg(view->verticalOffset())
                            .arg(view->maximumVerticalOffset())
                            .arg(view->materializedItemCount())
                            .arg(parts.join(QStringLiteral(" | "))));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    auto *statusTimer = new QTimer(&window);
    QObject::connect(statusTimer, &QTimer::timeout, &window, updateStatus);
    statusTimer->start(200);
    QTimer::singleShot(0, &window, updateStatus);

    window.show();

    const QString snapshotPath = parser.value(snapshotOption);
    const int exitAfter = parser.value(exitAfterOption).toInt();
    if (parser.isSet(checkOption)) {
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, &model, &app]() {
            const bool ok = runCheck(*view, &model);
            app.exit(ok ? 0 : 1);
        });
    } else if (!snapshotPath.isEmpty()) {
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, &window, snapshotPath]() {
            // 先滚一段：截图里能看出冻结行没动、滚动行换了内容。
            view->setVerticalOffset(qMin<qint64>(6 * kRowHeight, view->maximumVerticalOffset()));
            QApplication::processEvents();
            const QPixmap shot = window.grab();
            const bool saved = shot.save(snapshotPath);
            std::printf("table_frozen_rows: snapshot %s (%dx%d)%s frozenRows=%d+%d strips=%d "
                        "offset=%lld\n",
                        qPrintable(snapshotPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED", view->frozenRows(), view->frozenBottomRows(),
                        int(rowStrips(*view).size()),
                        static_cast<long long>(view->verticalOffset()));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    } else if (exitAfter > 0) {
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
