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
#include "03_cell_widgets/example.h"
#include "04_widget_types/example.h"
#include "05_binding_context/example.h"
#include "06_model_updates/example.h"
#include "07_row_heights/example.h"
#include "08_expansion/example.h"
#include "09_root_index/example.h"
#include "10_indentation/example.h"
#include "11_branch_renderer/example.h"
#include "12_columns/example.h"
#include "13_stretch_columns/example.h"
#include "14_spacing/example.h"
#include "15_visual_states/example.h"
#include "16_state_content/example.h"
#include "17_selection/example.h"
#include "18_item_events/example.h"
#include "19_scrolling/example.h"
#include "20_virtualization/example.h"
#include "21_frozen_columns/example.h"
#include "22_panes/example.h"
#include "23_headers/example.h"
#include "24_custom_header/example.h"
#include "25_header_persistence/example.h"
#include "26_sorting_filtering/example.h"
#include "27_row_reordering/example.h"
#include "28_spans/example.h"
#include "29_drag_indicator/example.h"
#include "30_drag_animation/example.h"
#include "31_lazy_loading/example.h"
#include "32_pinning/example.h"
#include "33_diagnostics/example.h"
#ifdef VIV_TREETABLE_HAS_SMOOTH_SCROLLBAR
#include "34_smooth_scrolling/example.h"
#endif
#include "35_borders/example.h"
#include "36_auto_height/example.h"
#include "37_colors/example.h"
#include "38_editors/example.h"

namespace {
struct Entry { int number; const char *title; QWidget *(*create)(QWidget *); };
const Entry entries[] = {
    {1, u8"01 基础多列树", treetable_basic::createExample},
    {2, u8"02 整行物化与 ColumnHost", treetable_row_widgets::createExample},
    {3, u8"03 单元格物化", treetable_cell_widgets::createExample},
    {4, u8"04 层级与列控件类型", treetable_widget_types::createExample},
    {5, u8"05 节点结构体与绑定上下文", treetable_binding_context::createExample},
    {6, u8"06 层级模型更新", treetable_model_updates::createExample},
    {7, u8"07 固定与动态行高", treetable_row_heights::createExample},
    {8, u8"08 展开收起与淡入淡出", treetable_expansion::createExample},
    {9, u8"09 显示根与相对深度", treetable_root_index::createExample},
    {10, u8"10 缩进与分支配置", treetable_indentation::createExample},
    {11, u8"11 自定义分支绘制", treetable_branch_renderer::createExample},
    {12, u8"12 列宽、隐藏与换序", treetable_columns::createExample},
    {13, u8"13 比例列宽与适应视口", treetable_stretch_columns::createExample},
    {14, u8"14 节点间距与分割线", treetable_spacing::createExample},
    {15, u8"15 状态背景与圆角", treetable_visual_states::createExample},
    {16, u8"16 状态关联内容", treetable_state_content::createExample},
    {17, u8"17 共享选择与键盘导航", treetable_selection::createExample},
    {18, u8"18 行内事件与几何查询", treetable_item_events::createExample},
    {19, u8"19 大树定位与滚动", treetable_scrolling::createExample},
    {20, u8"20 二维虚拟化与复用", treetable_virtualization::createExample},
    {21, u8"21 左右冻结列", treetable_frozen_columns::createExample},
    {22, u8"22 独立横向滚动分组", treetable_panes::createExample},
    {23, u8"23 表头交互与调整", treetable_headers::createExample},
    {24, u8"24 表头样式、Adapter 与 Corner", treetable_custom_header::createExample},
    {25, u8"25 保存与恢复布局", treetable_header_persistence::createExample},
    {26, u8"26 同级排序与递归过滤", treetable_sorting_filtering::createExample},
    {27, u8"27 同级行号拖动排序", treetable_row_reordering::createExample},
    {28, u8"28 同级单元格合并", treetable_spans::createExample},
    {29, u8"29 节点拖放与指示器", treetable_drag_indicator::createExample},
    {30, u8"30 节点拖放与位移动画", treetable_drag_animation::createExample},
    {31, u8"31 异步加载子节点", treetable_lazy_loading::createExample},
    {32, u8"32 屏外保留与释放", treetable_pinning::createExample},
    {33, u8"33 统计与性能诊断", treetable_diagnostics::createExample},
#ifdef VIV_TREETABLE_HAS_SMOOTH_SCROLLBAR
    {34, u8"34 可选平滑滚动", treetable_smooth_scrolling::createExample},
#endif
    {35, u8"35 圆角边框与透明视图", treetable_borders::createExample},
    {36, u8"36 自适应高度与滚动上限", treetable_auto_height::createExample},
    {37, u8"37 颜色与样式表", treetable_colors::createExample},
    {38, u8"38 覆盖编辑器与 Popup", treetable_editors::createExample},
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
        setWindowTitle(QStringLiteral("VirtualTreeTableView · 独立能力示例"));
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
    parser.setApplicationDescription(QStringLiteral("VirtualTreeTableView 功能示例"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("章节编号（1–37；34 需要 SmoothScrollbar）"),
                      QStringLiteral("number"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("exit-after"), QStringLiteral("指定毫秒后退出"), QStringLiteral("ms")});
    parser.process(app);
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
        qCritical() << "--example must name an available chapter (1-37, optional 34)";
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
