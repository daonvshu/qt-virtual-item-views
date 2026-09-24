// 动态高度示例：行高来自业务数据，并且会异步变化。
// 定时器每 40ms 改变若干行的高度，视图通过 dataChanged 局部更新尺寸，并用 ScrollAnchor
// 保证视口上方变化时画面不跳动。
//
// 滚动按像素进行，因此在动态行高下也能平滑移动。

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>

namespace {

constexpr int kBodyRole = Qt::UserRole + 1;
constexpr int kExpandedRole = Qt::UserRole + 2;

class NoteRowWidget : public QWidget
{
public:
    explicit NoteRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_title = new QLabel(this);
        m_body = new QLabel(this);
        m_body->setWordWrap(true);
        m_body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    }

    QSize sizeHint() const override
    {
        const int height = m_expanded ? 20 + m_body->heightForWidth(qMax(200, width())) : 24;
        return QSize(600, qMax(24, height));
    }

    void bind(const QModelIndex &index)
    {
        m_title->setText(index.data(Qt::DisplayRole).toString());
        m_body->setText(index.data(kBodyRole).toString());
        m_expanded = index.data(kExpandedRole).toBool();
        m_title->setGeometry(10, 0, 400, 22);
        m_body->setVisible(m_expanded);
        m_body->setGeometry(10, 20, qMax(100, width() - 20), qMax(0, height() - 22));
        updateGeometry();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_body->setGeometry(10, 20, qMax(100, width() - 20), qMax(0, height() - 22));
    }

private:
    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
    bool m_expanded = false;
};

class NoteAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new NoteRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<NoteRowWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(600, 40);
    }

    int created = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: dynamic height"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("记录数"),
                                  QStringLiteral("count"), QStringLiteral("50000"));
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
        auto *item = new QStandardItem(QStringLiteral("记录 %1").arg(row));
        item->setData(QStringLiteral("这是一段可变长度的正文，用于模拟异步加载或展开明细之后的"
                                     "高度变化。行号 %1。")
                          .arg(row),
                      kBodyRole);
        item->setData(row % 6 == 0, kExpandedRole);
        model.appendRow(item);
    }

    NoteAdapter adapter;
    QMainWindow window;

    // 视图由窗口持有；适配器/模型先声明，保证在视图析构时仍然存活。
    auto *view = new viv::VirtualListView(&window);
    view->setAdapter(&adapter);
    view->setItemHeightMode(viv::VirtualListView::ItemHeightMode::Variable);
    view->setEstimatedItemHeight(40);
    view->setAutoMeasureItemHeight(true);
    view->setOverscan(1, 1);
    // 像素滚动：行高异步变化时不会出现按行跳动。
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(parser.value(pixelStepOption).toInt());
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · dynamic height (异步高度变化)"));
    window.resize(760, 560);

    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);

    auto *timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, &window, [&]() {
        // 随机改变 3 行的高度（dataChanged 局部失效 + anchor 补偿）。
        for (int i = 0; i < 3; ++i) {
            const int row = int(QRandomGenerator::global()->bounded(model.rowCount()));
            const QModelIndex index = model.index(row, 0);
            const bool expanded = !model.data(index, kExpandedRole).toBool();
            model.setData(index, expanded, kExpandedRole);
        }
    });
    timer->start(40);

    const auto updateStatus = [&]() {
        status->setText(QStringLiteral("像素偏移: %1 px   实例化: %2   池: %3   内容高度: %4 px")
                            .arg(view->verticalOffset())
                            .arg(view->materializedItemCount())
                            .arg(view->pooledWidgetCount())
                            .arg(view->contentExtent()));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);

    window.show();
    updateStatus();
    return app.exec();
}

