// 最简单的虚拟列表示例：10 万行 QLabel 行，界面里只存在可见的几十个 QWidget。
//
// 滚动全部按像素进行：滚轮/滚动条箭头走固定像素步长（WheelScrollMode::Pixels），
// 拖动 thumb 与触控板 pixelDelta 同样是像素级，行不会被"整行吸附"。
// 状态栏实时显示像素偏移、实例化数量、池大小与累计创建的控件数。

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QResizeEvent>
#include <QStatusBar>
#include <QStringListModel>
#include <QTimer>

namespace {

class SimpleRowWidget : public QWidget
{
public:
    explicit SimpleRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("rowLabel"));
    }

    QLabel *label() const { return m_label; }

    void setText(const QString &text) { m_label->setText(text); }

    void relayout(int width)
    {
        m_label->setGeometry(8, 0, qMax(0, width - 16), height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout(width());
    }

private:
    QLabel *m_label = nullptr;
};

class SimpleAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new SimpleRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<SimpleRowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        // 停止与旧 index 绑定的业务活动；这里只有一个静态文本。
        static_cast<SimpleRowWidget *>(widget)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(400, 28);
    }

    int created = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: simple list"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("逻辑行数"),
                                  QStringLiteral("count"), QStringLiteral("100000"));
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
    QStringList rows;
    rows.reserve(rowCount);
    for (int row = 0; row < rowCount; ++row)
        rows.append(QStringLiteral("列表行 %1 —— 只有可见范围内的行拥有真实 QWidget").arg(row));
    QStringListModel model(rows);

    // 适配器/模型在窗口之前声明，销毁顺序上晚于窗口：视图析构时仍可安全调用
    // unbindWidget()。
    SimpleAdapter adapter;
    QMainWindow window;

    // 视图由窗口持有。注意不要用栈上的 QWidget：窗口析构时会 delete 自己的子控件，
    // 而 delete 一个栈对象正是"退出时报异常"的典型原因。
    auto *view = new viv::VirtualListView(&window);
    view->setAdapter(&adapter);
    view->setUniformItemHeight(28);
    view->setOverscan(2, 2);
    // 像素滚动：一个滚轮刻度固定滚动 N 像素，与行高无关。
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(parser.value(pixelStepOption).toInt());
    view->setModel(&model);

    auto *status = new QLabel(&window);
    status->setObjectName(QStringLiteral("statusLabel"));
    window.setCentralWidget(view);
    window.statusBar()->addPermanentWidget(status);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · simple list (%1 rows)").arg(rowCount));
    window.resize(720, 480);

    const auto updateStatus = [&]() {
        status->setText(QStringLiteral("逻辑行: %1   像素偏移: %2 px   实例化: %3   池: %4   累计创建: %5")
                            .arg(model.rowCount())
                            .arg(view->verticalOffset())
                            .arg(view->materializedItemCount())
                            .arg(view->pooledWidgetCount())
                            .arg(adapter.created));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);

    window.show();
    return app.exec();
}

