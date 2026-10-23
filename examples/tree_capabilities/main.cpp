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
#include "02_row_widgets/example.h"
#include "03_widget_types/example.h"
#include "04_binding_context/example.h"
#include "05_model_updates/example.h"
#include "06_item_heights/example.h"
#include "07_expansion/example.h"
#include "08_root_index/example.h"
#include "09_indentation/example.h"
#include "10_branch_icons/example.h"
#include "11_branch_renderer/example.h"
#include "12_row_spacing/example.h"
#include "13_visual_states/example.h"
#include "14_state_content/example.h"
#include "15_selection/example.h"
#include "16_item_events/example.h"
#include "17_scrolling/example.h"
#include "18_virtualization/example.h"
#include "19_frozen_rows/example.h"
#include "20_drag_drop/example.h"
#include "21_sorting_filtering/example.h"
#include "22_lazy_loading/example.h"
#include "23_pinning/example.h"
#include "24_diagnostics/example.h"
#ifdef VIV_TREE_HAS_SMOOTH_SCROLLBAR
#include "25_smooth_scrolling/example.h"
#endif

namespace {
struct Entry { const char *title; QWidget *(*create)(QWidget *); };
const Entry entries[] = {
    {u8"01 基础接入", tree_basic::createExample},
    {u8"02 组合行控件与数据回写", tree_row_widgets::createExample},
    {u8"03 不同节点类型与复用池", tree_widget_types::createExample},
    {u8"04 节点结构体与绑定上下文", tree_binding_context::createExample},
    {u8"05 层级模型更新", tree_model_updates::createExample},
    {u8"06 固定、首项测量与动态行高", tree_item_heights::createExample},
    {u8"07 展开收起与过渡动画", tree_expansion::createExample},
    {u8"08 切换显示根与相对深度", tree_root_index::createExample},
    {u8"09 分支格宽与叶子对齐", tree_indentation::createExample},
    {u8"10 内置分支图标配置", tree_branch_icons::createExample},
    {u8"11 自定义分支渲染器", tree_branch_renderer::createExample},
    {u8"12 节点间距与分割线", tree_row_spacing::createExample},
    {u8"13 状态背景、范围与圆角", tree_visual_states::createExample},
    {u8"14 状态关联内容", tree_state_content::createExample},
    {u8"15 选择、当前项与键盘导航", tree_selection::createExample},
    {u8"16 行事件、正确索引与几何查询", tree_item_events::createExample},
    {u8"17 大树滚动、定位与可见映射", tree_scrolling::createExample},
    {u8"18 可见行映射与控件复用", tree_virtualization::createExample},
    {u8"19 顶部底部冻结与区域分隔线", tree_frozen_rows::createExample},
    {u8"20 节点拖放与手动起拖", tree_drag_drop::createExample},
    {u8"21 层级排序与递归过滤", tree_sorting_filtering::createExample},
    {u8"22 业务驱动的延迟加载", tree_lazy_loading::createExample},
    {u8"23 屏外保留与编辑生命周期", tree_pinning::createExample},
    {u8"24 统计与生命周期日志", tree_diagnostics::createExample},
#ifdef VIV_TREE_HAS_SMOOTH_SCROLLBAR
    {u8"25 平滑滚动（可选）", tree_smooth_scrolling::createExample},
#endif
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
            // Each capability owns its model and adapter; create only when first visited.
            if (!m_pages[row]) {
                m_pages[row] = entries[row].create(m_stack);
                m_stack->addWidget(m_pages[row]);
            }
            m_stack->setCurrentWidget(m_pages[row]);
        });
        m_navigation->setCurrentRow(initial);
        setWindowTitle(QStringLiteral("VirtualTreeView · 独立能力示例"));
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
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualTreeView 功能示例"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("章节编号（1-24；25 需要 SmoothScrollbar）"),
                      QStringLiteral("number"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("exit-after"), QStringLiteral("指定毫秒后退出"), QStringLiteral("ms")});
    parser.process(app);
    bool valid = false;
    const int number = parser.value(QStringLiteral("example")).toInt(&valid);
    if (!valid || number < 1 || number > kCount) {
        qCritical() << "--example must name an available chapter (1-24, optional 25)";
        return 1;
    }
    Browser browser(number - 1);
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
