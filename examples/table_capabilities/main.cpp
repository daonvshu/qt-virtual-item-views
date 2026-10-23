#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QListWidget>
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <array>

#include "01_row_widgets/example.h"
#include "02_cell_widgets/example.h"
#include "03_widget_types/example.h"
#include "04_binding_context/example.h"
#include "05_model_updates/example.h"
#include "06_row_heights/example.h"
#include "07_columns/example.h"
#include "08_stretch_columns/example.h"
#include "09_spacing/example.h"
#include "10_visual_states/example.h"
#include "11_state_content/example.h"
#include "12_selection/example.h"
#include "13_item_events/example.h"
#include "14_scrolling/example.h"
#include "15_virtualization/example.h"
#include "16_frozen_areas/example.h"
#include "17_panes/example.h"
#include "18_headers/example.h"
#include "19_custom_header/example.h"
#include "20_header_persistence/example.h"
#include "21_sorting/example.h"
#include "22_row_reordering/example.h"
#include "23_spans/example.h"
#include "24_drag_indicator/example.h"
#include "25_drag_animation/example.h"
#include "26_cell_drop/example.h"
#include "27_pinning/example.h"
#include "28_diagnostics/example.h"
#ifdef VIV_TABLE_HAS_SMOOTH_SCROLLBAR
#include "29_smooth_scrolling/example.h"
#endif
#include "30_borders/example.h"

namespace {

struct ExampleEntry
{
    int number;
    const char *title;
    QWidget *(*create)(QWidget *);
};

const ExampleEntry entries[] = {
    {1, u8"01 行控件与 ColumnHost", table_row_widgets::createExample},
    {2, u8"02 单元格物化", table_cell_widgets::createExample},
    {3, u8"03 多种控件类型", table_widget_types::createExample},
    {4, u8"04 整行结构体绑定", table_binding_context::createExample},
    {5, u8"05 模型更新", table_model_updates::createExample},
    {6, u8"06 固定与动态行高", table_row_heights::createExample},
    {7, u8"07 列宽、隐藏与换序", table_columns::createExample},
    {8, u8"08 比例列宽与填满视图", table_stretch_columns::createExample},
    {9, u8"09 间距与分割线", table_spacing::createExample},
    {10, u8"10 Hover 与选中背景", table_visual_states::createExample},
    {11, u8"11 状态关联控件", table_state_content::createExample},
    {12, u8"12 选择与当前项", table_selection::createExample},
    {13, u8"13 行内事件与几何查询", table_item_events::createExample},
    {14, u8"14 双向滚动与定位", table_scrolling::createExample},
    {15, u8"15 二维虚拟化与复用", table_virtualization::createExample},
    {16, u8"16 冻结行列与分隔线", table_frozen_areas::createExample},
    {17, u8"17 多区域与独立滚动组", table_panes::createExample},
    {18, u8"18 表头与换序动画", table_headers::createExample},
    {19, u8"19 自定义表头控件", table_custom_header::createExample},
    {20, u8"20 列布局保存与恢复", table_header_persistence::createExample},
    {21, u8"21 排序与代理模型", table_sorting::createExample},
    {22, u8"22 行号拖动排序", table_row_reordering::createExample},
    {23, u8"23 合并单元格", table_spans::createExample},
    {24, u8"24 整行指示器拖动", table_drag_indicator::createExample},
    {25, u8"25 整行移动动画拖动", table_drag_animation::createExample},
    {26, u8"26 单格插入与空白格投放", table_cell_drop::createExample},
    {27, u8"27 屏外保留", table_pinning::createExample},
    {28, u8"28 运行统计与生命周期", table_diagnostics::createExample},
#ifdef VIV_TABLE_HAS_SMOOTH_SCROLLBAR
    {29, u8"29 平滑滚动（可选）", table_smooth_scrolling::createExample},
#endif
    {30, u8"30 外边框与圆角", table_borders::createExample},
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
        setWindowTitle(QStringLiteral("VirtualTableView · 独立能力示例"));
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
    parser.setApplicationDescription(QStringLiteral("一个进程演示 VirtualTableView 的 %1 项能力").arg(kExampleCount));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("启动时选择章节编号（1–30；29 需要 SmoothScrollbar）"),
                      QStringLiteral("number"), QStringLiteral("1")});
    parser.process(app);
    bool ok = false;
    const int number = parser.value(QStringLiteral("example")).toInt(&ok);
    int initial = -1;
    for (int i = 0; ok && i < kExampleCount; ++i) {
        if (entries[i].number == number) {
            initial = i;
            break;
        }
    }
    if (initial < 0) {
        qCritical() << "--example must name an available chapter (1-28, optional 29, or 30)";
        return 1;
    }
    Browser browser(initial);
    browser.show();
    return app.exec();
}
