// 最简单的虚拟列表示例：10 万行 QLabel 行，界面里只存在可见的几十个 QWidget。
//
// 滚动全部按像素进行：滚轮/滚动条箭头走固定像素步长（WheelScrollMode::Pixels），
// 拖动 thumb 与触控板 pixelDelta 同样是像素级，行不会被"整行吸附"。
// 状态栏实时显示像素偏移、实例化数量、池大小与累计创建的控件数。

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QCommandLineParser>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QStatusBar>
#include <QStringListModel>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>

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

    void setCornerRadius(int radius)
    {
        if (m_cornerRadius == radius)
            return;
        m_cornerRadius = radius;
        update();
    }

    void setVisualState(viv::VirtualItemView::VisualState state,
                        const QColor &hoverColor, const QColor &selectedColor)
    {
        m_state = state;
        m_hoverColor = hoverColor;
        m_selectedColor = selectedColor;
        update();
    }

    void relayout(int width)
    {
        m_label->setGeometry(8, 0, qMax(0, width - 16), height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        painter.setRenderHint(QPainter::Antialiasing, m_cornerRadius > 0);
        painter.setPen(Qt::NoPen);
        const auto paintBackground = [this, &painter](const QColor &color, qreal opacity) {
            if (opacity <= 0.0)
                return;
            painter.setOpacity(opacity);
            if (m_cornerRadius > 0) {
                painter.setBrush(color);
                painter.drawRoundedRect(QRectF(rect()), m_cornerRadius, m_cornerRadius);
            } else {
                painter.fillRect(rect(), color);
            }
        };
        paintBackground(m_hoverColor, m_state.hoverProgress);
        paintBackground(m_selectedColor, m_state.selectedProgress);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout(width());
    }

private:
    QLabel *m_label = nullptr;
    viv::VirtualItemView::VisualState m_state;
    QColor m_hoverColor;
    QColor m_selectedColor;
    int m_cornerRadius = 8;
};

class SimpleAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        auto *row = new SimpleRowWidget(parent);
        row->setCornerRadius(cornerRadius);
        return row;
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<SimpleRowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void visualStateChanged(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<SimpleRowWidget *>(widget)->setVisualState(
            view->visualState(index), view->hoverBackgroundColor(),
            view->selectedBackgroundColor());
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

    void setCornerRadius(int radius)
    {
        cornerRadius = radius;
        for (auto *widget : view->viewport()->findChildren<QWidget *>()) {
            if (auto *row = dynamic_cast<SimpleRowWidget *>(widget))
                row->setCornerRadius(radius);
        }
    }

    int created = 0;
    int cornerRadius = 8;
    viv::VirtualListView *view = nullptr;
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
    adapter.view = view;
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
    auto *toolbar = window.addToolBar(QStringLiteral("列表"));
    toolbar->addWidget(new QLabel(QStringLiteral("行间距: "), &window));
    auto *rowSpacing = new QSpinBox(&window);
    rowSpacing->setRange(0, 100);
    rowSpacing->setValue(view->rowSpacing());
    rowSpacing->setSuffix(QStringLiteral(" px"));
    toolbar->addWidget(rowSpacing);
    QObject::connect(rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     view, [view](int pixels) { view->setRowSpacing(pixels); });
    auto *rowLines = new QCheckBox(QStringLiteral("分割线"), &window);
    rowLines->setChecked(view->rowGridLinesVisible());
    toolbar->addWidget(rowLines);
    QObject::connect(rowLines, &QCheckBox::toggled, view,
                     [view](bool visible) { view->setRowGridLinesVisible(visible); });
    toolbar->addWidget(new QLabel(QStringLiteral("线宽: "), &window));
    auto *lineWidth = new QSpinBox(&window);
    lineWidth->setRange(1, 12);
    lineWidth->setValue(view->rowGridLineWidth());
    lineWidth->setSuffix(QStringLiteral(" px"));
    toolbar->addWidget(lineWidth);
    QObject::connect(lineWidth, QOverload<int>::of(&QSpinBox::valueChanged), view,
                     [view](int pixels) { view->setRowGridLineWidth(pixels); });
    auto *lineColor = new QPushButton(QStringLiteral("颜色"), &window);
    const auto setColorSwatch = [lineColor](const QColor &color) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        lineColor->setIcon(QIcon(swatch));
    };
    setColorSwatch(view->palette().color(QPalette::Mid));
    toolbar->addWidget(lineColor);
    QObject::connect(lineColor, &QPushButton::clicked, view, [view, setColorSwatch]() {
        const QColor initial = view->rowGridLineColor().isValid()
            ? view->rowGridLineColor() : view->palette().color(QPalette::Mid);
        const QColor color = QColorDialog::getColor(initial, view, QStringLiteral("分割线颜色"));
        if (color.isValid()) {
            view->setRowGridLineColor(color);
            setColorSwatch(color);
        }
    });
    auto *hoverColor = new QPushButton(QStringLiteral("悬停颜色"), &window);
    auto *selectedColor = new QPushButton(QStringLiteral("选中颜色"), &window);
    auto *stateToolbar = window.addToolBar(QStringLiteral("状态"));
    window.insertToolBarBreak(stateToolbar);
    stateToolbar->addWidget(hoverColor);
    stateToolbar->addWidget(selectedColor);
    stateToolbar->addWidget(new QLabel(QStringLiteral("过渡: "), &window));
    auto *animationDuration = new QSpinBox(&window);
    animationDuration->setRange(0, 1000);
    animationDuration->setSuffix(QStringLiteral(" ms"));
    animationDuration->setValue(view->visualStateAnimationDuration());
    stateToolbar->addWidget(animationDuration);
    QObject::connect(animationDuration, QOverload<int>::of(&QSpinBox::valueChanged), view,
                     [view](int ms) { view->setVisualStateAnimationDuration(ms); });
    stateToolbar->addWidget(new QLabel(QStringLiteral("圆角: "), &window));
    auto *cornerRadius = new QSpinBox(&window);
    cornerRadius->setRange(0, 20);
    cornerRadius->setSuffix(QStringLiteral(" px"));
    cornerRadius->setValue(adapter.cornerRadius);
    stateToolbar->addWidget(cornerRadius);
    QObject::connect(cornerRadius, QOverload<int>::of(&QSpinBox::valueChanged), view,
                     [&adapter](int radius) { adapter.setCornerRadius(radius); });
    const auto setStateSwatch = [](QPushButton *button, const QColor &color) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        button->setIcon(QIcon(swatch));
    };
    setStateSwatch(hoverColor, view->hoverBackgroundColor());
    setStateSwatch(selectedColor, view->selectedBackgroundColor());
    QObject::connect(hoverColor, &QPushButton::clicked, view, [view, hoverColor, setStateSwatch]() {
        const QColor color = QColorDialog::getColor(view->hoverBackgroundColor(), view,
                                                    QStringLiteral("悬停背景颜色"));
        if (color.isValid()) {
            view->setHoverBackgroundColor(color);
            setStateSwatch(hoverColor, color);
        }
    });
    QObject::connect(selectedColor, &QPushButton::clicked, view,
                     [view, selectedColor, setStateSwatch]() {
        const QColor color = QColorDialog::getColor(view->selectedBackgroundColor(), view,
                                                    QStringLiteral("选中背景颜色"));
        if (color.isValid()) {
            view->setSelectedBackgroundColor(color);
            setStateSwatch(selectedColor, color);
        }
    });
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

