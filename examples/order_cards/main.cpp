// 复杂业务行示例：每行是一张订单卡片（标题、状态标签、进度条、两个按钮），
// 高度由内容决定，因此使用 Variable 高度模式 + 自动测量。
//
// 滚动按像素进行：滚轮/箭头固定像素步长，拖动 thumb 与触控板也是像素级。因为行高不等，
// "按像素滚动"不会出现 3 行 = 288px 那种跳跃感。

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kTitleRole = Qt::UserRole + 1;
constexpr int kCustomerRole = Qt::UserRole + 2;
constexpr int kProgressRole = Qt::UserRole + 3;
constexpr int kStatusRole = Qt::UserRole + 4;

/// 订单卡片：真实业务控件，不是 delegate 绘制。
class OrderCardWidget : public QWidget
{
public:
    explicit OrderCardWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        auto *header = new QHBoxLayout();
        m_title = new QLabel(this);
        m_status = new QLabel(this);
        header->addWidget(m_title);
        header->addStretch(1);
        header->addWidget(m_status);
        layout->addLayout(header);

        m_customer = new QLabel(this);
        layout->addWidget(m_customer);

        auto *footer = new QHBoxLayout();
        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 100);
        m_progress->setTextVisible(true);
        auto *detail = new QPushButton(QStringLiteral("详情"), this);
        auto *cancel = new QPushButton(QStringLiteral("取消"), this);
        footer->addWidget(m_progress, 1);
        footer->addWidget(detail);
        footer->addWidget(cancel);
        layout->addLayout(footer);
    }

    void bind(const QModelIndex &index)
    {
        m_title->setText(index.data(kTitleRole).toString());
        m_customer->setText(index.data(kCustomerRole).toString());
        m_progress->setValue(index.data(kProgressRole).toInt());
        const QString status = index.data(kStatusRole).toString();
        m_status->setText(status);
        m_status->setStyleSheet(status == QStringLiteral("已完成")
                                    ? QStringLiteral("color: #2e7d32; font-weight: bold;")
                                    : QStringLiteral("color: #ef6c00; font-weight: bold;"));
    }

private:
    QLabel *m_title = nullptr;
    QLabel *m_customer = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
};

class OrderAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new OrderCardWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderCardWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(600, 96);
    }

    int created = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: order cards"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("订单数量"),
                                  QStringLiteral("count"), QStringLiteral("20000"));
    QCommandLineOption pixelStepOption(QStringLiteral("wheel-pixels"),
                                       QStringLiteral("每个滚轮刻度滚动的像素数"),
                                       QStringLiteral("pixels"), QStringLiteral("48"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(rowsOption);
    parser.addOption(pixelStepOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    QStandardItemModel model;
    for (int row = 0; row < rowCount; ++row) {
        auto *item = new QStandardItem(QStringLiteral("订单卡片 %1").arg(row));
        item->setData(QStringLiteral("订单 #%1").arg(200000 + row), kTitleRole);
        item->setData(QStringLiteral("客户：客户-%1").arg(row % 5000), kCustomerRole);
        item->setData((row * 7) % 101, kProgressRole);
        item->setData(row % 3 == 0 ? QStringLiteral("已完成") : QStringLiteral("处理中"), kStatusRole);
        model.appendRow(item);
    }

    OrderAdapter adapter;
    QMainWindow window;

    // 视图由窗口持有；适配器/模型在窗口之前声明，保证生命周期覆盖视图。
    auto *view = new viv::VirtualListView(&window);
    view->setAdapter(&adapter);
    // 让卡片按自身 sizeHint 决定行高（Variable 模式 + 自动测量）。
    view->setItemHeightMode(viv::VirtualListView::ItemHeightMode::Variable);
    view->setEstimatedItemHeight(96);
    view->setAutoMeasureItemHeight(true);
    view->setOverscan(1, 1);
    // 像素滚动：行高不固定时尤其重要。
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(parser.value(pixelStepOption).toInt());
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · order cards (复杂业务行)"));
    window.resize(760, 560);

    auto *status = new QLabel(&window);
    const auto updateStatus = [&]() {
        status->setText(QStringLiteral("像素偏移: %1 px   实例化卡片: %2   池: %3   累计创建: %4   内容高度: %5 px")
                            .arg(view->verticalOffset())
                            .arg(view->materializedItemCount())
                            .arg(view->pooledWidgetCount())
                            .arg(adapter.created)
                            .arg(view->contentExtent()));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    window.statusBar()->addPermanentWidget(status);

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);

    window.show();
    updateStatus();
    return app.exec();
}

