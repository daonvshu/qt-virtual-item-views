// Advanced panes 示例（§43）：5 个 pane —— 左侧冻结 2 列 | 滚动组 0（4 列）|
// 中间冻结 1 列 | 滚动组 1（4 列）| 右侧冻结 1 列。
//
// 两个滚动组各自独立滚动：底部滚动条（视图自己的）驱动组 0，工具栏里的滚动条
// 通过 setHorizontalOffset(1, offset) 驱动组 1。每个滚动 pane 有框架提供的裁剪
// 容器，所以滚出去的列不会画到邻居 pane 上；命中测试也在 pane 边界折回。
//
// 无人值守：
//   --check                自检（组 1 滚动不影响其它 pane、每个 pane 的命中归属）并退出
//   --snapshot <path>      导出 PNG
//   --exit-after <ms>      自动退出

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAbstractTableModel>
#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QScrollBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <cstdio>
#include <limits>

namespace {

constexpr int kRowHeight = 26;
constexpr int kColumnWidth = 110;
constexpr int kColumns = 12;

/// 轻量宽表模型：不预分配 rowCount x columnCount 个对象。
class PaneModel : public QAbstractTableModel
{
public:
    explicit PaneModel(int rowCount, QObject *parent = nullptr)
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
        return parent.isValid() ? 0 : kColumns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("%1:%2").arg(index.row()).arg(index.column() + 1);
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
};

/// 一行：每列一个 ColumnHost，框架负责摆放与裁剪。
class PaneRowWidget : public QWidget
{
public:
    explicit PaneRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        for (int column = 0; column < kColumns; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            m_hosts.append(host);
            m_labels.append(new QLabel(host));
        }
    }

    void bind(const QModelIndex &rowIndex)
    {
        for (int column = 0; column < m_labels.size(); ++column) {
            m_labels.at(column)->setText(rowIndex.siblingAtColumn(column).data().toString());
            // 让标签填满自己的 host（host 的几何由框架给出）。
            m_labels.at(column)->setGeometry(4, 0, qMax(0, m_hosts.at(column)->width() - 8),
                                             kRowHeight);
        }
    }

    QVector<viv::ColumnHost *> hosts() const { return m_hosts; }

private:
    QVector<viv::ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

class PaneTableAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new PaneRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<PaneRowWidget *>(widget)->bind(index);
    }

    void layoutRowWidget(QWidget *widget, const QModelIndex &,
                         const viv::TableRowLayoutContext &) override
    {
        auto *row = static_cast<PaneRowWidget *>(widget);
        for (viv::ColumnHost *host : row->hosts()) {
            for (QLabel *label : host->findChildren<QLabel *>())
                label->setGeometry(4, 0, qMax(0, host->width() - 8), kRowHeight);
        }
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * kColumns, kRowHeight);
    }
};

viv::TablePaneSpec frozenPane(const QVector<int> &columns)
{
    viv::TablePaneSpec spec;
    spec.logicalColumns = columns;
    spec.scroll = viv::PaneScroll::Frozen;
    return spec;
}

viv::TablePaneSpec scrollingPane(const QVector<int> &columns, int group)
{
    viv::TablePaneSpec spec;
    spec.logicalColumns = columns;
    spec.scroll = viv::PaneScroll::Scrollable;
    spec.scrollGroup = group;
    return spec;
}

QVector<viv::TablePaneSpec> defaultPanes()
{
    return {frozenPane({0, 1}), scrollingPane({2, 3, 4, 5}, 0), frozenPane({6}),
            scrollingPane({7, 8, 9, 10}, 1), frozenPane({11})};
}

QString paneName(const viv::TablePane &pane)
{
    switch (pane.type) {
    case viv::TablePane::Type::FrozenLeft:
        return QStringLiteral("冻结(左)");
    case viv::TablePane::Type::FrozenRight:
        return QStringLiteral("冻结(右)");
    case viv::TablePane::Type::Scrollable:
        return QStringLiteral("滚动组 %1").arg(pane.scrollGroup);
    }
    return QString();
}

/// 自检：组 1 单独滚动时，其它 pane 的列一个像素都不许动；每个 pane 的命中测试
/// 都属于它自己的列。
bool runCheck(viv::VirtualTableView &view)
{
    bool ok = true;
    const int columnCount = view.columnCount();
    const QVector<viv::TablePane> panes = view.panes();
    if (panes.size() != 5) {
        std::printf("table_panes: check expected 5 panes, got %d\n", int(panes.size()));
        return false;
    }
    if (view.scrollGroups() != QVector<int>({0, 1})) {
        std::printf("table_panes: check expected scroll groups {0,1}\n");
        return false;
    }

    QVector<int> before(columnCount);
    for (int column = 0; column < columnCount; ++column)
        before[column] = view.columnGeometry(column).viewportX;

    const qint64 step = qMin<qint64>(kColumnWidth, view.maximumHorizontalOffset(1));
    view.setHorizontalOffset(1, step);
    view.flushPendingRelayout();

    for (int column = 0; column < columnCount; ++column) {
        const int paneIndex = view.paneIndexOfColumn(column);
        const viv::TablePane pane = panes.at(paneIndex);
        const int now = view.columnGeometry(column).viewportX;
        if (pane.type != viv::TablePane::Type::Scrollable || pane.scrollGroup != 1) {
            // 冻结列与组 0 的列都不受组 1 影响。
            if (now != before.at(column)) {
                std::printf("table_panes: check FAILED column %d moved with group 1 "
                            "(pane=%s, %d -> %d)\n",
                            column, qPrintable(paneName(pane)), before.at(column), now);
                ok = false;
            }
            continue;
        }
        if (now != before.at(column) - int(step) && step > 0) {
            std::printf("table_panes: check FAILED column %d did not follow group 1 "
                        "(%d -> %d, step=%lld)\n",
                        column, before.at(column), now, static_cast<long long>(step));
            ok = false;
        }
    }

    // 每个 pane 的第一像素都属于这个 pane 的列。
    for (const viv::TablePane &pane : panes) {
        if (pane.viewportRect.width() <= 0)
            continue;
        const int viewportX = pane.viewportRect.x() + 2;
        const int column = view.columnAtViewportX(viewportX);
        if (column < 0 || view.paneIndexOfColumn(column) != panes.indexOf(pane)) {
            std::printf("table_panes: check FAILED hit at x=%d (pane %s) landed on column %d\n",
                        viewportX, qPrintable(paneName(pane)), column);
            ok = false;
        }
    }

    std::printf("table_panes: check %s (group0=%lld/%lld group1=%lld/%lld panes=%d)\n",
                ok ? "PASSED" : "FAILED",
                static_cast<long long>(view.horizontalOffset(0)),
                static_cast<long long>(view.maximumHorizontalOffset(0)),
                static_cast<long long>(view.horizontalOffset(1)),
                static_cast<long long>(view.maximumHorizontalOffset(1)), int(panes.size()));
    std::fflush(stdout);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: advanced panes (§43)"));
    parser.addHelpOption();
    const QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("逻辑行数"),
                                        QStringLiteral("count"), QStringLiteral("50000"));
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

    PaneModel model(parser.value(rowsOption).toInt());
    PaneTableAdapter adapter;

    viv::VirtualTableView *view = new viv::VirtualTableView;
    view->setTableAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(kColumnWidth);
    view->setModel(&model);
    view->setPanes(defaultPanes());

    QMainWindow window;
    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · advanced panes"));

    // 组 1 的滚动条：组 0 由视图自己的（底部）滚动条驱动。
    auto *groupBar = new QScrollBar(Qt::Horizontal);
    groupBar->setRange(0, 0);
    QObject::connect(groupBar, &QScrollBar::valueChanged, view, [view](int value) {
        view->setHorizontalOffset(1, value);
    });
    auto *bar = window.addToolBar(QStringLiteral("滚动组 1"));
    bar->addWidget(new QLabel(QStringLiteral("滚动组 1（独立） ")));
    bar->addWidget(groupBar);

    const auto syncBars = [view, groupBar]() {
        const int maximum = int(qMin<qint64>(view->maximumHorizontalOffset(1),
                                             qint64(std::numeric_limits<int>::max())));
        if (groupBar->maximum() != maximum)
            groupBar->setRange(0, maximum);
        if (groupBar->value() != int(view->horizontalOffset(1)))
            groupBar->setValue(int(view->horizontalOffset(1)));
    };
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, view, syncBars);
    auto *timer = new QTimer(view);
    QObject::connect(timer, &QTimer::timeout, view, [view, &window, syncBars]() {
        syncBars();
        QStringList parts;
        const QVector<viv::TablePane> panes = view->panes();
        for (const viv::TablePane &pane : panes) {
            parts << QStringLiteral("%1: x=%2 w=%3 offset=%4")
                         .arg(paneName(pane))
                         .arg(pane.viewportRect.x())
                         .arg(pane.viewportRect.width())
                         .arg(view->horizontalOffset(pane.scrollGroup));
        }
        window.statusBar()->showMessage(QStringLiteral("%1 行 · %2 列 · %3")
                                            .arg(view->model() ? view->model()->rowCount() : 0)
                                            .arg(view->columnCount())
                                            .arg(parts.join(QStringLiteral("  |  "))));
    });
    timer->start(200);

    window.resize(1000, 620);
    window.show();
    QTimer::singleShot(0, view, syncBars);

    const QString snapshotPath = parser.value(snapshotOption);
    const int exitAfter = parser.value(exitAfterOption).toInt();
    if (parser.isSet(checkOption)) {
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, &app]() {
            const bool ok = runCheck(*view);
            app.exit(ok ? 0 : 1);
        });
    } else if (!snapshotPath.isEmpty()) {
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, &window, snapshotPath]() {
            // 组 1 滚到末尾而组 0 不动：截图里能一眼看出两个组各自独立。
            view->setHorizontalOffset(1, view->maximumHorizontalOffset(1));
            QApplication::processEvents();
            const QPixmap shot = window.grab();
            const bool saved = shot.save(snapshotPath);
            std::printf("table_panes: snapshot %s (%dx%d)%s group0=%lld group1=%lld\n",
                        qPrintable(snapshotPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED",
                        static_cast<long long>(view->horizontalOffset(0)),
                        static_cast<long long>(view->horizontalOffset(1)));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    } else if (exitAfter > 0) {
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
