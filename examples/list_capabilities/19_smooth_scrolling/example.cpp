#include "example.h"
#include <smoothscroll/smoothscrollcontroller.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStringListModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral(
            "19 平滑滚动：在列表中滚动鼠标滚轮，切换开关比较平滑滚动与原生滚动。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);

        auto *controls = new QHBoxLayout;
        m_enabled = new QCheckBox(QStringLiteral("启用平滑滚动"), this);
        m_enabled->setChecked(true);
        m_step = new QSpinBox(this);
        m_step->setRange(1, 240);
        m_step->setValue(48);
        m_step->setSuffix(QStringLiteral(" px / 格"));
        m_stop = new QPushButton(QStringLiteral("停止当前动画"), this);
        controls->addWidget(m_enabled);
        controls->addWidget(new QLabel(QStringLiteral("滚轮步长"), this));
        controls->addWidget(m_step);
        controls->addWidget(m_stop);
        controls->addStretch();
        layout->addLayout(controls);

        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        QStringList rows;
        rows.reserve(2000);
        for (int row = 0; row < 2000; ++row)
            rows.append(QStringLiteral("平滑滚动条目 %1").arg(row + 1));
        auto *model = new QStringListModel(rows, m_view);

        // Transfer adapter ownership to the view so it outlives row unbinding.
        m_view->setAdapter(new TextAdapter, true);
        // All rows are 36 pixels high; no per-row measurement is needed.
        m_view->setUniformItemHeight(36);
        // Keep native wheel scrolling in pixel mode with the same step as the
        // controller, so disabling smoothing changes only the scrolling behavior.
        m_view->setWheelScrollPixels(m_step->value());
        // The model is owned by its QObject parent; setModel starts materialization.
        m_view->setModel(model);

        // Attach directly to this view. No shared example helper or global event
        // filter is involved; the view also owns the controller's lifetime.
        m_controller = new sscroll::SmoothScrollController(m_view, m_view);
        applyStep(m_step->value());
        m_controller->setEnabled(m_enabled->isChecked());
        connect(m_enabled, &QCheckBox::toggled,
                m_controller, &sscroll::SmoothScrollController::setEnabled);
        connect(m_step, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int pixels) { applyStep(pixels); });
        connect(m_stop, &QPushButton::clicked,
                this, [this] { m_controller->stop(); });
        connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this] { updateStatus(); });
        updateStatus();
    }

    void applyStep(int pixels)
    {
        // The view's wheel step is the fallback when the controller is disabled.
        m_view->setWheelScrollPixels(pixels);
        auto settings = m_controller->settings();
        settings.wheelStep = pixels;
        // Allow the entire spin-box range, including steps below the default minimum.
        settings.minimumWheelStep = 1;
        m_controller->setSettings(settings);
    }

    void updateStatus()
    {
        // Read the actual logical pixel offset as the controller advances it.
        m_status->setText(QStringLiteral("当前偏移：%1 px").arg(m_view->verticalOffset()));
    }

    viv::VirtualListView *m_view = nullptr;
    sscroll::SmoothScrollController *m_controller = nullptr;
    QCheckBox *m_enabled = nullptr;
    QSpinBox *m_step = nullptr;
    QPushButton *m_stop = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_smooth_scrolling
