// 宽表示例：120 列 × 5 万行，仍然只实例化可见行 + overscan，
// 并且横向滚动时表头与行控件共享同一个 HeaderGeometry 偏移（不会漂移）。
// 每行只把可见列的 ColumnHost 设为可见，横向虚拟化的成本与总列数无关。

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QApplication>
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVector>

namespace {

constexpr int kRowHeight = 26;

/// 轻量宽表模型：不预分配 rowCount x columnCount 个对象。
class WideModel : public QAbstractTableModel
{
public:
    WideModel(int rowCount, int columnCount, QObject *parent = nullptr)
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
    int m_columnCount = 0;
};

/// 行控件：为每个列创建一个 ColumnHost，框架负责定位与显示/隐藏。
class WideRowWidget : public QWidget
{
public:
    WideRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        m_labels.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(2, 0, 60, 18);
            m_labels.append(label);
        }
    }

    void bind(const QModelIndex &rowIndex)
    {
        for (int column = 0; column < m_labels.size(); ++column)
            m_labels.at(column)->setText(rowIndex.siblingAtColumn(column).data().toString());
    }

private:
    QVector<QLabel *> m_labels;
};

class WideTableAdapter : public viv::TableWidgetAdapter
{
public:
    explicit WideTableAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new WideRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<WideRowWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(400, kRowHeight);
    }

    int created = 0;

private:
    int m_columnCount = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: table with many columns"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("行数"),
                                  QStringLiteral("count"), QStringLiteral("50000"));
    QCommandLineOption columnsOption(QStringLiteral("columns"), QStringLiteral("列数"),
                                     QStringLiteral("count"), QStringLiteral("120"));
    QCommandLineOption wheelOption(QStringLiteral("wheel-pixels"),
                                   QStringLiteral("横向滚轮每刻度像素"), QStringLiteral("pixels"),
                                   QStringLiteral("48"));
    QCommandLineOption scrollOption(QStringLiteral("scroll-pixels"),
                                    QStringLiteral("自动横滚每帧像素"), QStringLiteral("pixels"),
                                    QStringLiteral("16"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    QCommandLineOption frozenOption(QStringLiteral("frozen"),
                                    QStringLiteral("左侧冻结列数（§31）"),
                                    QStringLiteral("count"), QStringLiteral("0"));
    QCommandLineOption frozenRightOption(QStringLiteral("frozen-right"),
                                         QStringLiteral("右侧冻结列数（§31）"),
                                         QStringLiteral("count"), QStringLiteral("0"));
    QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                      QStringLiteral("渲染视口到 PNG 后退出"), QStringLiteral("file"));
    QCommandLineOption checkFrozenOption(QStringLiteral("check-frozen"),
                                         QStringLiteral("自检：冻结 pane 不得随滚动变化"));
    QCommandLineOption separatorWidthOption(QStringLiteral("separator-width"),
                                            QStringLiteral("冻结分界线线宽（0 = 隐藏）"),
                                            QStringLiteral("pixels"), QStringLiteral("1"));
    QCommandLineOption separatorColorOption(QStringLiteral("separator-color"),
                                            QStringLiteral("冻结分界线颜色（默认取样式分隔线颜色）"),
                                            QStringLiteral("color"), QString());
    parser.addOption(rowsOption);
    parser.addOption(columnsOption);
    parser.addOption(wheelOption);
    parser.addOption(scrollOption);
    parser.addOption(frozenOption);
    parser.addOption(frozenRightOption);
    parser.addOption(snapshotOption);
    parser.addOption(checkFrozenOption);
    parser.addOption(separatorWidthOption);
    parser.addOption(separatorColorOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    const int columnCount = qMax(1, parser.value(columnsOption).toInt());
    const int scrollPixels = qMax(1, parser.value(scrollOption).toInt());

    WideModel model(rowCount, columnCount);

    WideTableAdapter adapter(columnCount);
    QMainWindow window;

    auto *view = new viv::VirtualTableView(&window);
    view->setTableAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(90);
    view->setColumnOverscan(1);
    view->setHorizontalWheelPixels(parser.value(wheelOption).toInt());
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · %1 rows x %2 columns")
                              .arg(rowCount)
                              .arg(columnCount));
    window.resize(1000, 620);

    auto *toolbar = window.addToolBar(QStringLiteral("控制"));
    auto *autoScroll = new QCheckBox(QStringLiteral("像素横滚"), &window);
    toolbar->addWidget(autoScroll);
    // 冻结前两列（§31）：冻结列不参与横向滚动，也不额外制造滚动空间。
    auto *frozen = new QCheckBox(QStringLiteral("冻结前 2 列"), &window);
    toolbar->addWidget(frozen);
    auto *frozenRight = new QCheckBox(QStringLiteral("冻结末尾 1 列"), &window);
    toolbar->addWidget(frozenRight);
    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);

    auto *timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, view, [view, scrollPixels]() {
        if (view->horizontalOffset() + scrollPixels >= view->maximumHorizontalOffset())
            view->setHorizontalOffset(0);
        else
            view->scrollByHorizontalPixels(scrollPixels);
    });
    QObject::connect(autoScroll, &QCheckBox::toggled, timer, [timer](bool on) {
        on ? timer->start(16) : timer->stop();
    });
    QObject::connect(frozen, &QCheckBox::toggled, view, [view, columnCount](bool on) {
        view->setFrozenColumns(on ? QVector<int>({0, qMin(1, columnCount - 1)}) : QVector<int>());
    });
    QObject::connect(frozenRight, &QCheckBox::toggled, view, [view, columnCount](bool on) {
        view->setFrozenRightColumns(on ? QVector<int>({columnCount - 1}) : QVector<int>());
    });

    const auto updateStatus = [&]() {
        const viv::VirtualViewStats stats = view->stats();
        const viv::VisibleRange columns = view->visibleColumns();
        int frozenLeft = 0;
        int frozenRight = 0;
        for (const viv::TablePane &pane : view->panes()) {
            if (pane.type == viv::TablePane::Type::FrozenLeft)
                frozenLeft = pane.viewportRect.width();
            else if (pane.type == viv::TablePane::Type::FrozenRight)
                frozenRight = pane.viewportRect.width();
        }
        status->setText(QStringLiteral("逻辑行: %1   列: %2   可见视觉列: %3-%4   实例化行: %5   池: %6   "
                                       "冻结左/右: %7 / %8 px   横向偏移: %9 / %10 px")
                            .arg(stats.logicalItems)
                            .arg(view->columnCount())
                            .arg(columns.first)
                            .arg(columns.last)
                            .arg(stats.materializedItems)
                            .arg(stats.pooledWidgets)
                            .arg(frozenLeft)
                            .arg(frozenRight)
                            .arg(view->horizontalOffset())
                            .arg(view->maximumHorizontalOffset()));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    const int frozenCount = qBound(0, parser.value(frozenOption).toInt(), columnCount - 1);
    if (frozenCount > 0) {
        QVector<int> frozenColumns;
        for (int column = 0; column < frozenCount; ++column)
            frozenColumns.append(column);
        view->setFrozenColumns(frozenColumns);
        // Keep the checkbox in sync without letting its handler overwrite the
        // column set that the command line asked for.
        const QSignalBlocker blocker(frozen);
        frozen->setChecked(true);
    }
    const int frozenRightCount = qBound(0, parser.value(frozenRightOption).toInt(), columnCount - 1);
    if (frozenRightCount > 0) {
        QVector<int> frozenColumns;
        for (int column = columnCount - frozenRightCount; column < columnCount; ++column)
            frozenColumns.append(column);
        view->setFrozenRightColumns(frozenColumns);
        const QSignalBlocker blocker(frozenRight);
        frozenRight->setChecked(true);
    }

    // 冻结分界线的外观（§31）：颜色不设就用"当前样式画列分隔线用的颜色"。
    viv::PaneSeparatorStyle separatorStyle;
    separatorStyle.width = qMax(0, parser.value(separatorWidthOption).toInt());
    separatorStyle.color = QColor(parser.value(separatorColorOption));
    view->setPaneSeparatorStyle(separatorStyle);

    const QString snapshotPath = parser.value(snapshotOption);
    if (parser.isSet(checkFrozenOption)) {
        // 自检：冻结 pane 的内容与滚动偏移无关；任何差异都说明滚动列透了进来。
        const bool ok = [&]() {
            const auto frozenPanes = [&](int offset) {
                view->setHorizontalOffset(offset);
                QApplication::processEvents();
                const QImage full = view->viewport()->grab().toImage();
                QVector<QPair<QRect, QImage>> crops;
                for (const viv::TablePane &pane : view->panes()) {
                    if (pane.type == viv::TablePane::Type::Scrollable || pane.viewportRect.isEmpty())
                        continue;
                    crops.append({pane.viewportRect, full.copy(pane.viewportRect)});
                }
                return crops;
            };
            const auto first = frozenPanes(300);
            const auto second = frozenPanes(307);
            if (first.size() != second.size() || first.isEmpty()) {
                std::printf("table_many_columns: check-frozen no frozen pane to compare (panes=%d)\n",
                            int(first.size()));
                return true;
            }

            bool same = true;
            for (int i = 0; i < first.size(); ++i) {
                const QRect rect = first.at(i).first;
                const QImage &a = first.at(i).second;
                const QImage &b = second.at(i).second;
                int different = 0;
                int firstX = -1;
                int firstY = -1;
                for (int y = 0; y < a.height() && y < b.height(); ++y) {
                    for (int x = 0; x < a.width() && x < b.width(); ++x) {
                        if (a.pixel(x, y) == b.pixel(x, y))
                            continue;
                        ++different;
                        if (firstX < 0) {
                            firstX = x;
                            firstY = y;
                        }
                    }
                }
                std::printf("table_many_columns: check-frozen paneRect=(%d,%d,%dx%d) differing=%d "
                            "firstDiff=(%d,%d)\n",
                            rect.x(), rect.y(), rect.width(), rect.height(), different, firstX, firstY);
                if (different != 0)
                    same = false;
            }
            return same;
        }();
        std::fflush(stdout);
        return ok ? 0 : 1;
    }

    if (!snapshotPath.isEmpty()) {
        // 无人值守的视觉检查：先滚到横向末尾（冻结列此时最能体现差别），再导出 PNG。
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, snapshotPath]() {
            view->setHorizontalOffset(view->maximumHorizontalOffset());
            QApplication::processEvents();
            const QPixmap shot = view->grab();
            const bool saved = shot.save(snapshotPath);
            const int first = view->columnGeometry(0).viewportX;
            const int last = view->columnGeometry(view->columnCount() - 1).viewportX;
            std::printf("table_many_columns: snapshot %s (%dx%d)%s frozenLeft=%d frozenRight=%d "
                        "firstColumnX=%d lastColumnX=%d offset=%lld\n",
                        qPrintable(snapshotPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED", int(view->frozenColumns().size()),
                        int(view->frozenRightColumns().size()), first, last,
                        static_cast<long long>(view->horizontalOffset()));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    } else if (exitAfter > 0) {
        autoScroll->setChecked(true);
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);
    }

    window.show();
    return app.exec();
}
