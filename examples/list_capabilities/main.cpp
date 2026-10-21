#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QListWidget>
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <array>

#include "01_basic/example.h"
#include "02_row_widgets/example.h"
#include "03_widget_types/example.h"
#include "04_binding_context/example.h"
#include "05_model_updates/example.h"
#include "06_item_heights/example.h"
#include "07_row_spacing/example.h"
#include "08_visual_states/example.h"
#include "09_state_content/example.h"
#include "10_selection/example.h"
#include "11_item_events/example.h"
#include "12_scrolling/example.h"
#include "13_virtualization/example.h"
#include "14_frozen_rows/example.h"
#include "15_drag_drop/example.h"
#include "16_drag_animation/example.h"
#include "17_pinning/example.h"
#include "18_diagnostics/example.h"
#ifdef VIV_LIST_HAS_SMOOTH_SCROLLBAR
#include "19_smooth_scrolling/example.h"
#endif

namespace {

struct ExampleEntry
{
    const char *title;
    QWidget *(*create)(QWidget *);
};

const ExampleEntry entries[] = {
    {u8"01 基础列表", list_basic::createExample},
    {u8"02 自定义行控件", list_row_widgets::createExample},
    {u8"03 多种行类型", list_widget_types::createExample},
    {u8"04 整行数据绑定", list_binding_context::createExample},
    {u8"05 数据更新", list_model_updates::createExample},
    {u8"06 行高", list_item_heights::createExample},
    {u8"07 行间距和分割线", list_row_spacing::createExample},
    {u8"08 Hover 和选中状态", list_visual_states::createExample},
    {u8"09 状态关联内容", list_state_content::createExample},
    {u8"10 选择与当前项", list_selection::createExample},
    {u8"11 行事件与控件事件", list_item_events::createExample},
    {u8"12 滚动与定位", list_scrolling::createExample},
    {u8"13 虚拟化与复用", list_virtualization::createExample},
    {u8"14 冻结行", list_frozen_rows::createExample},
    {u8"15 指示器拖动", list_drag_drop::createExample},
    {u8"16 行移动动画拖动", list_drag_animation::createExample},
    {u8"17 屏外保留", list_pinning::createExample},
    {u8"18 运行诊断", list_diagnostics::createExample},
#ifdef VIV_LIST_HAS_SMOOTH_SCROLLBAR
    {u8"19 平滑滚动（SmoothScrollbar）", list_smooth_scrolling::createExample},
#endif
};

constexpr int kExampleCount = int(sizeof(entries) / sizeof(entries[0]));

class Browser : public QMainWindow
{
public:
    explicit Browser(int initial)
    {
        setupUi();
        connect(m_navigation, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row < 0 || row >= kExampleCount)
                return;
            if (!m_pages[row]) {
                m_pages[row] = entries[row].create(m_stack);
                m_stack->addWidget(m_pages[row]);
            }
            m_stack->setCurrentWidget(m_pages[row]);
        });
        m_navigation->setCurrentRow(initial);
        setWindowTitle(QStringLiteral("VirtualListView · 独立能力示例"));
        resize(1180, 760);
    }

private:
    void setupUi()
    {
        auto *splitter = new QSplitter(this);
        m_navigation = new QListWidget(splitter);
        m_navigation->setMinimumWidth(220);
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
    std::array<QWidget *, kExampleCount> m_pages{};
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("一个进程演示 VirtualListView 的 %1 项能力").arg(kExampleCount));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("启动时选择示例（1–%1）").arg(kExampleCount),
                      QStringLiteral("number"), QStringLiteral("1")});
    parser.process(app);
    bool ok = false;
    const int initial = parser.value(QStringLiteral("example")).toInt(&ok);
    if (!ok || initial < 1 || initial > kExampleCount) {
        qCritical() << "--example must be between 1 and" << kExampleCount;
        return 1;
    }
    Browser browser(initial - 1);
    browser.show();
    return app.exec();
}
