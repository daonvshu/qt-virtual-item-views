#include "ui_smooth_scrolling.h"
#include <memory>
#include "example.h"
#include <smoothscroll/smoothscrollcontroller.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStringListModel>

namespace list_smooth_scrolling {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Recycled labels must always receive the current row's content.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListSmoothScrollingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListSmoothScrollingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        QStringList rows;
        rows.reserve(2000);
        for (int row = 0; row < 2000; ++row)
            rows.append(QStringLiteral("平滑滚动条目 %1").arg(row + 1));
        auto *model = new QStringListModel(rows, ui->view);

        // Transfer adapter ownership to the view so it outlives row unbinding.
        ui->view->setAdapter(new TextAdapter, true);
        // All rows are 36 pixels high; no per-row measurement is needed.
        ui->view->setUniformItemHeight(36);
        // Keep native wheel scrolling in pixel mode with the same step as the
        // controller, so disabling smoothing changes only the scrolling behavior.
        ui->view->setWheelScrollPixels(ui->step->value());
        // The model is owned by its QObject parent; setModel starts materialization.
        ui->view->setModel(model);

        // Attach directly to this view. No shared example helper or global event
        // filter is involved; the view also owns the controller's lifetime.
        m_controller = new sscroll::SmoothScrollController(ui->view, ui->view);
        applyStep(ui->step->value());
        m_controller->setEnabled(ui->enabled->isChecked());
        connect(ui->enabled, &QCheckBox::toggled,
                m_controller, &sscroll::SmoothScrollController::setEnabled);
        connect(ui->step, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int pixels) { applyStep(pixels); });
        connect(ui->stop, &QPushButton::clicked,
                this, [this] { m_controller->stop(); });
        connect(ui->view->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this] { updateStatus(); });
        updateStatus();
    }

    void applyStep(int pixels)
    {
        // The view's wheel step is the fallback when the controller is disabled.
        ui->view->setWheelScrollPixels(pixels);
        auto settings = m_controller->settings();
        settings.wheelStep = pixels;
        // Allow the entire spin-box range, including steps below the default minimum.
        settings.minimumWheelStep = 1;
        m_controller->setSettings(settings);
    }

    void updateStatus()
    {
        // Read the actual logical pixel offset as the controller advances it.
        ui->status->setText(QStringLiteral("当前偏移：%1 px").arg(ui->view->verticalOffset()));
    }

    sscroll::SmoothScrollController *m_controller = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_smooth_scrolling
