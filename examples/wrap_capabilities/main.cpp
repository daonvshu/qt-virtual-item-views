#include "ui_browser.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <array>
#include <memory>
#include "01_basic/example.h"
#include "02_geometry/example.h"
#include "03_visual_states/example.h"
#include "04_binding_events/example.h"
#include "05_model_root/example.h"
#include "06_scrolling/example.h"
#include "07_drag_drop/example.h"
#include "08_auto_height/example.h"
#include "09_wrap_layout/example.h"
#include "10_wrap_widget/example.h"

namespace {
struct Entry { const char *title; QWidget *(*create)(QWidget *); };
const Entry entries[] = {
    {u8"01 基础接入", wrap_basic::createExample},
    {u8"02 宽度、间距与对齐", wrap_geometry::createExample},
    {u8"03 状态、颜色与圆角", wrap_visual_states::createExample},
    {u8"04 结构体、多类型与事件", wrap_binding_events::createExample},
    {u8"05 模型、代理与显示根", wrap_model_root::createExample},
    {u8"06 滚动、物化与诊断", wrap_scrolling::createExample},
    {u8"07 自动与手动拖放", wrap_drag_drop::createExample},
    {u8"08 虚拟视图自适应高度", wrap_auto_height::createExample},
    {u8"09 直接使用 WrapLayout", wrap_wrap_layout::createExample},
    {u8"10 真实控件自适应高度", wrap_wrap_widget::createExample},
};
class Browser : public QMainWindow
{
public:
    explicit Browser(int initial) : ui(new Ui::WrapBrowser)
    {
        ui->setupUi(this);
        for (const Entry &entry : entries) ui->navigation->addItem(QString::fromUtf8(entry.title));
        connect(ui->navigation, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row < 0 || row >= 10) return;
            if (!pages[row]) {
                pages[row] = entries[row].create(ui->pages);
                ui->pages->addWidget(pages[row]);
            }
            ui->pages->setCurrentWidget(pages[row]);
        });
        ui->navigation->setCurrentRow(initial);
        setWindowTitle(QStringLiteral("VirtualWrapView / WrapLayout / WrapWidget"));
        resize(1180, 720);
    }
private:
    std::unique_ptr<Ui::WrapBrowser> ui;
    std::array<QWidget *, 10> pages{};
};
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("example"), QStringLiteral("Chapter 1-10"), QStringLiteral("number"), QStringLiteral("1")});
    parser.process(app);
    bool valid = false;
    const int chapter = parser.value(QStringLiteral("example")).toInt(&valid);
    if (!valid || chapter < 1 || chapter > 10) {
        qCritical() << "--example must be between 1 and 10";
        return 1;
    }
    Browser browser(chapter - 1);
    browser.show();
    return app.exec();
}
