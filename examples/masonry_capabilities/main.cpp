#include "ui_browser.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <array>
#include <memory>
#include "01_basic/example.h"
#include "02_columns/example.h"
#include "03_spacing_grid/example.h"
#include "04_height_modes/example.h"
#include "05_height_updates/example.h"
#include "06_visual_states/example.h"
#include "07_binding_events/example.h"
#include "08_selection/example.h"
#include "09_model_root/example.h"
#include "10_scrolling/example.h"
#include "11_drag_drop/example.h"
#include "12_images/example.h"
#include "13_borders_colors/example.h"
#include "14_auto_height/example.h"

namespace {
struct Entry { const char *title; QWidget *(*create)(QWidget *); };
const Entry entries[] = {
    {u8"01 基础接入", masonry_basic::createExample},
    {u8"02 列数与列宽", masonry_columns::createExample},
    {u8"03 间距与分割线", masonry_spacing_grid::createExample},
    {u8"04 三种高度模式", masonry_height_modes::createExample},
    {u8"05 高度更新与动画", masonry_height_updates::createExample},
    {u8"06 状态背景与内容", masonry_visual_states::createExample},
    {u8"07 结构体、多类型与事件", masonry_binding_events::createExample},
    {u8"08 选择与几何查询", masonry_selection::createExample},
    {u8"09 模型、代理与显示根", masonry_model_root::createExample},
    {u8"10 滚动、物化与诊断", masonry_scrolling::createExample},
    {u8"11 自动与手动拖放", masonry_drag_drop::createExample},
    {u8"12 异步图片卡片", masonry_images::createExample},
    {u8"13 圆角、边框与颜色", masonry_borders_colors::createExample},
    {u8"14 自适应高度", masonry_auto_height::createExample},
};
constexpr int chapterCount = int(sizeof(entries) / sizeof(entries[0]));

class Browser : public QMainWindow
{
public:
    explicit Browser(int initial) : ui(new Ui::MasonryBrowser)
    {
        ui->setupUi(this);
        for (const Entry &entry : entries) ui->navigation->addItem(QString::fromUtf8(entry.title));
        connect(ui->navigation, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row < 0 || row >= chapterCount) return;
            if (!pages[row]) {
                // Each chapter is independent and created only when opened.
                pages[row] = entries[row].create(ui->pages);
                ui->pages->addWidget(pages[row]);
            }
            ui->pages->setCurrentWidget(pages[row]);
        });
        ui->navigation->setCurrentRow(initial);
        setWindowTitle(QStringLiteral("VirtualMasonryView capabilities"));
        resize(1180, 720);
    }
private:
    std::unique_ptr<Ui::MasonryBrowser> ui;
    std::array<QWidget *, chapterCount> pages{};
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("Chapter 1-14"), QStringLiteral("number"), QStringLiteral("1")});
    parser.process(app);
    bool valid = false;
    const int chapter = parser.value(QStringLiteral("example")).toInt(&valid);
    if (!valid || chapter < 1 || chapter > chapterCount) {
        qCritical() << "--example must be between 1 and" << chapterCount;
        return 1;
    }
    Browser browser(chapter - 1);
    browser.show();
    return app.exec();
}
