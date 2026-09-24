// 百万行示例（验收 A1/A2）：
//  - 打开时 QWidget 数量与逻辑行数无关；
//  - 长时间滚动过程中创建/销毁数量不增长，池大小趋于稳定；
//  - 滚动全部按像素进行：自动滚动用 scrollByPixels() 平滑推进，跳转用
//    setVerticalOffset() 精确定位到像素位置。

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

#include <QAbstractListModel>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {

constexpr int kRowHeight = 24;

/// 超轻量模型：按需给出文本，不预先分配一百万个字符串对象。
class MillionRowModel : public QAbstractListModel
{
public:
    explicit MillionRowModel(int rowCount, QObject *parent = nullptr)
        : QAbstractListModel(parent)
        , m_rowCount(qMax(1, rowCount))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rowCount;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("第 %1 行 · 逻辑行数 %2").arg(index.row() + 1).arg(m_rowCount);
    }

    int logicalRowCount() const { return m_rowCount; }

private:
    int m_rowCount = 0;
};

class MillionAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        auto *widget = new QWidget(parent);
        auto *label = new QLabel(widget);
        label->setObjectName(QStringLiteral("rowLabel"));
        label->setGeometry(8, 0, 400, kRowHeight);
        return widget;
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (auto *label = widget->findChild<QLabel *>(QStringLiteral("rowLabel")))
            label->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        if (auto *label = widget->findChild<QLabel *>(QStringLiteral("rowLabel")))
            label->clear();
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(500, kRowHeight);
    }

    int created = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: million rows"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("逻辑行数"),
                                  QStringLiteral("count"), QStringLiteral("1000000"));
    QCommandLineOption pixelStepOption(QStringLiteral("wheel-pixels"),
                                       QStringLiteral("每个滚轮刻度滚动的像素数"),
                                       QStringLiteral("pixels"), QStringLiteral("48"));
    QCommandLineOption scrollPixelsOption(QStringLiteral("scroll-pixels"),
                                          QStringLiteral("自动滚动每帧推进的像素数"),
                                          QStringLiteral("pixels"), QStringLiteral("24"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(rowsOption);
    parser.addOption(pixelStepOption);
    parser.addOption(scrollPixelsOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    const int scrollPixelsPerTick = qMax(1, parser.value(scrollPixelsOption).toInt());

    MillionRowModel model(rowCount);
    MillionAdapter adapter;
    QMainWindow window;

    // 视图由窗口持有；模型/适配器先声明，生命周期覆盖视图。
    auto *view = new viv::VirtualListView(&window);
    view->setAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setOverscan(3, 3);
    // 像素滚动：滚轮/箭头固定像素步长，程序内也用像素位移。
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(parser.value(pixelStepOption).toInt());
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · %1 rows").arg(rowCount));
    window.resize(900, 600);

    auto *toolbar = window.addToolBar(QStringLiteral("控制"));
    toolbar->addWidget(new QLabel(QStringLiteral("跳转行号: "), &window));
    auto *jump = new QSpinBox(&window);
    jump->setRange(1, rowCount);
    jump->setSingleStep(1000);
    toolbar->addWidget(jump);
    auto *smooth = new QCheckBox(QStringLiteral("像素自动滚动"), &window);
    toolbar->addWidget(smooth);
    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);

    // 按行号跳转仍然是"定位到像素偏移"，不改变滚动的像素本质。
    // QOverload keeps this working on Qt 5 (which still has the QString
    // overload of QSpinBox::valueChanged) and on Qt 6.
    QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), view, [view, &model](int row) {
        Q_UNUSED(model);
        view->setVerticalOffset(qint64(row - 1) * kRowHeight);
    });

    auto *timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, view, [view, scrollPixelsPerTick]() {
        const qint64 next = view->verticalOffset() + scrollPixelsPerTick;
        if (next >= view->maximumVerticalOffset())
            view->setVerticalOffset(0);
        else
            view->scrollByPixels(scrollPixelsPerTick);
    });
    QObject::connect(smooth, &QCheckBox::toggled, timer, [timer](bool on) {
        on ? timer->start(16) : timer->stop();
    });

    const auto updateStatus = [&]() {
        // VirtualViewStats 是给 debug overlay / 长期运行诊断用的快照。
        const viv::VirtualViewStats stats = view->stats();
        status->setText(QStringLiteral("逻辑行: %1   像素偏移: %2 px   实例化: %3   池: %4   pinned: %5   "
                                       "create/bind/recycle: %6/%7/%8")
                            .arg(stats.logicalItems)
                            .arg(view->verticalOffset())
                            .arg(stats.materializedItems)
                            .arg(stats.pooledWidgets)
                            .arg(stats.pinnedWidgets)
                            .arg(stats.createCount)
                            .arg(stats.bindCount)
                            .arg(stats.recycleCount));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0) {
        // 顺手验证自动滚动路径，便于无人值守运行示例。
        smooth->setChecked(true);
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);
    }

    window.show();
    return app.exec();
}
