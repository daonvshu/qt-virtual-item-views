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
    parser.addOption(rowsOption);
    parser.addOption(columnsOption);
    parser.addOption(wheelOption);
    parser.addOption(scrollOption);
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

    const auto updateStatus = [&]() {
        const viv::VirtualViewStats stats = view->stats();
        const viv::VisibleRange columns = view->visibleColumns();
        status->setText(QStringLiteral("逻辑行: %1   列: %2   可见视觉列: %3-%4   实例化行: %5   池: %6   "
                                       "横向偏移: %7 / %8 px")
                            .arg(stats.logicalItems)
                            .arg(view->columnCount())
                            .arg(columns.first)
                            .arg(columns.last)
                            .arg(stats.materializedItems)
                            .arg(stats.pooledWidgets)
                            .arg(view->horizontalOffset())
                            .arg(view->maximumHorizontalOffset()));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0) {
        autoScroll->setChecked(true);
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);
    }

    window.show();
    return app.exec();
}
