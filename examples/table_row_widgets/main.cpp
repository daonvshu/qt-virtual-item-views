// 企业表格示例（v0.4 Table MVP）：一行一个真实 QWidget（Row Widget Mode），
// 每列由 ColumnHost 承载业务控件，列宽/顺序/隐藏全部来自 HeaderGeometry。
//
// 演示：原生 QHeaderView（水平 + 行号）、拖动列宽、点击表头排序、隐藏/恢复列、
// 移动列、保存/恢复列状态、横向像素滚动、百万级行只实例化可见行。

#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QApplication>
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {

enum Column {
    ColumnOrder = 0,
    ColumnCustomer,
    ColumnStatus,
    ColumnProgress,
    ColumnAction,
    ColumnCount,
};

constexpr int kRowHeight = 44;

/// 轻量模型：数据按需生成，不预先分配每一格的对象。
class OrderModel : public QAbstractTableModel
{
public:
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
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        switch (index.column()) {
        case ColumnOrder:
            return QStringLiteral("#%1").arg(200000 + index.row());
        case ColumnCustomer:
            return QStringLiteral("客户-%1").arg(index.row() % 5000);
        case ColumnStatus:
            return index.row() % 3 == 0 ? QStringLiteral("已完成") : QStringLiteral("处理中");
        case ColumnProgress:
            return (index.row() * 7) % 101;
        default:
            return QStringLiteral("-");
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Vertical)
            return section + 1;
        switch (section) {
        case ColumnOrder:
            return QStringLiteral("订单号");
        case ColumnCustomer:
            return QStringLiteral("客户");
        case ColumnStatus:
            return QStringLiteral("状态");
        case ColumnProgress:
            return QStringLiteral("进度");
        default:
            return QStringLiteral("操作");
        }
    }

private:
    int m_rowCount = 0;
};

class OrderRowWidget : public QWidget
{
public:
    explicit OrderRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        for (int column = 0; column < ColumnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            switch (column) {
            case ColumnStatus:
                m_status = new QLabel(host);
                break;
            case ColumnProgress:
                m_progress = new QProgressBar(host);
                m_progress->setRange(0, 100);
                break;
            case ColumnAction:
                m_action = new QPushButton(QStringLiteral("详情"), host);
                break;
            default:
                m_labels.append({column, new QLabel(host)});
                break;
            }
        }
    }

    void bind(const QModelIndex &rowIndex)
    {
        for (const auto &entry : m_labels)
            entry.second->setText(rowIndex.siblingAtColumn(entry.first).data().toString());
        m_status->setText(rowIndex.siblingAtColumn(ColumnStatus).data().toString());
        m_progress->setValue(rowIndex.siblingAtColumn(ColumnProgress).data().toInt());
    }

private:
    QVector<QPair<int, QLabel *>> m_labels;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_action = nullptr;
};

class OrderAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new OrderRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderRowWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(700, kRowHeight);
    }

    int created = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: table row widgets"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("订单数量"),
                                  QStringLiteral("count"), QStringLiteral("200000"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(rowsOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    OrderModel model(rowCount);

    OrderAdapter adapter;
    QMainWindow window;

    // 视图由窗口持有；模型/适配器先声明，生命周期覆盖视图。
    auto *view = new viv::VirtualTableView(&window);
    view->setTableAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(150);
    view->setColumnWidth(ColumnOrder, 110);
    view->setColumnWidth(ColumnCustomer, 160);
    view->setColumnWidth(ColumnStatus, 100);
    view->setColumnWidth(ColumnProgress, 140);
    view->setColumnWidth(ColumnAction, 120);
    view->setStretchLastColumn(true);
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(48);
    view->setSortingEnabled(true);
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · table row widgets (%1 rows)").arg(rowCount));
    window.resize(980, 600);

    // 列状态操作：全部通过 HeaderGeometry 生效，body 只查询它。
    auto *toolbar = window.addToolBar(QStringLiteral("列"));
    auto *hideCustomer = new QCheckBox(QStringLiteral("隐藏“客户”"), &window);
    toolbar->addWidget(hideCustomer);
    auto *moveStatusFirst = new QPushButton(QStringLiteral("状态列移到最前"), &window);
    toolbar->addWidget(moveStatusFirst);
    auto *saveState = new QPushButton(QStringLiteral("保存列状态"), &window);
    auto *restoreState = new QPushButton(QStringLiteral("恢复列状态"), &window);
    toolbar->addWidget(saveState);
    toolbar->addWidget(restoreState);
    toolbar->addWidget(new QLabel(QStringLiteral("  像素横滚: "), &window));
    auto *horizontalOffset = new QSpinBox(&window);
    horizontalOffset->setRange(0, 5000);
    toolbar->addWidget(horizontalOffset);

    QByteArray savedState;
    QObject::connect(hideCustomer, &QCheckBox::toggled, view, [view](bool hidden) {
        view->setColumnHidden(ColumnCustomer, hidden);
    });
    QObject::connect(moveStatusFirst, &QPushButton::clicked, view, [view]() {
        view->moveColumn(ColumnStatus, 0);
    });
    QObject::connect(saveState, &QPushButton::clicked, view, [view, &savedState]() {
        savedState = view->saveHeaderState();
    });
    QObject::connect(restoreState, &QPushButton::clicked, view, [view, &savedState]() {
        if (!savedState.isEmpty())
            view->restoreHeaderState(savedState);
    });
    // QOverload keeps this working on Qt 5 (deprecated QString overload) and Qt 6.
    QObject::connect(horizontalOffset, QOverload<int>::of(&QSpinBox::valueChanged), view, [view](int offset) {
        view->setHorizontalOffset(offset);
    });

    auto *status = new QLabel(&window);
    const auto updateStatus = [&]() {
        const viv::VirtualViewStats stats = view->stats();
        const viv::VisibleRange columns = view->visibleColumns();
        status->setText(QStringLiteral("逻辑行: %1   列: %2（可见视觉列 %3-%4）   实例化行: %5   池: %6   "
                                       "横向偏移: %7 px   create/bind/recycle: %8/%9/%10")
                            .arg(stats.logicalItems)
                            .arg(view->columnCount())
                            .arg(columns.first)
                            .arg(columns.last)
                            .arg(stats.materializedItems)
                            .arg(stats.pooledWidgets)
                            .arg(view->horizontalOffset())
                            .arg(stats.createCount)
                            .arg(stats.bindCount)
                            .arg(stats.recycleCount));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);

    window.show();
    return app.exec();
}
