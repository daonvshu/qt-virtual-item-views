#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QListWidget>
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <array>

#include "01_basic/example.h"
#include "02_cell_sizes/example.h"
#include "03_spacing_grid/example.h"
#include "04_visual_states/example.h"
#include "05_selection_events/example.h"
#include "06_model_root/example.h"
#include "07_scrolling/example.h"
#include "08_drag_drop/example.h"
#include "09_images/example.h"
#include "10_colors/example.h"
#include "11_auto_height/example.h"
#include <virtualitemviews/accessibility.h>

namespace {
struct Entry { int number; const char *title; QWidget *(*create)(QWidget *); };
const Entry entries[] = {
    {1, u8"01 基础接入", flow_basic::createExample},
    {2, u8"02 尺寸与自动换列", flow_cell_sizes::createExample},
    {3, u8"03 间距、边距与分割线", flow_spacing_grid::createExample},
    {4, u8"04 状态背景与动画", flow_visual_states::createExample},
    {5, u8"05 选择与项目事件", flow_selection_events::createExample},
    {6, u8"06 模型更新与显示根", flow_model_root::createExample},
    {7, u8"07 滚动、定位与物化", flow_scrolling::createExample},
    {8, u8"08 项目拖放", flow_drag_drop::createExample},
    {9, u8"09 异步图片卡片", flow_images::createExample},
    {10, u8"10 颜色与样式表", flow_colors::createExample},
    {11, u8"11 自适应高度", flow_auto_height::createExample},
};
constexpr int kCount = int(sizeof(entries) / sizeof(entries[0]));

class Browser : public QMainWindow
{
public:
    explicit Browser(int initial)
    {
        setupUi();
        connect(m_navigation, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row < 0 || row >= kCount)
                return;
            // Each chapter owns its model and adapter; create only when first visited.
            if (!m_pages[row]) {
                m_pages[row] = entries[row].create(m_stack);
                m_stack->addWidget(m_pages[row]);
            }
            m_stack->setCurrentWidget(m_pages[row]);
        });
        m_navigation->setCurrentRow(initial);
        setWindowTitle(QStringLiteral("VirtualFlowTableView · 独立能力示例"));
        resize(1180, 760);
    }
private:
    void setupUi()
    {
        auto *splitter = new QSplitter(this);
        m_navigation = new QListWidget(splitter);
        m_navigation->setMinimumWidth(250);
        m_navigation->setStyleSheet(QStringLiteral("QListWidget::item { min-height: 36px; }"));
        for (const auto &entry : entries)
            m_navigation->addItem(QString::fromUtf8(entry.title));
        m_stack = new QStackedWidget(splitter);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        setCentralWidget(splitter);
    }
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_stack = nullptr;
    std::array<QWidget *, kCount> m_pages{};
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    viv::installAccessibilityFactory();
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualFlowTableView 功能示例"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("章节编号（1–11）"),
                      QStringLiteral("number"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("exit-after"), QStringLiteral("指定毫秒后退出"), QStringLiteral("ms")});
    parser.addOption({QStringLiteral("accessibility-trace"), QStringLiteral("输出 Flow 辅助几何诊断日志")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("accessibility-trace")))
        qputenv("VIV_FLOW_ACCESSIBILITY_TRACE", "1");
    bool valid = false;
    const int number = parser.value(QStringLiteral("example")).toInt(&valid);
    int initial = -1;
    for (int row = 0; valid && row < kCount; ++row) {
        if (entries[row].number == number) {
            initial = row;
            break;
        }
    }
    if (initial < 0) {
        qCritical() << "--example must name an available chapter (1-11)";
        return 1;
    }
    Browser browser(initial);
    browser.show();
    if (parser.isSet(QStringLiteral("exit-after"))) {
        bool validDuration = false;
        const int duration = parser.value(QStringLiteral("exit-after")).toInt(&validDuration);
        if (!validDuration || duration <= 0) {
            qCritical() << "--exit-after must be a positive number of milliseconds";
            return 1;
        }
        QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
