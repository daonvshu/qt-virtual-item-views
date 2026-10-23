#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <smoothscroll/smoothscrollcontroller.h>

namespace tree_smooth_scrolling {

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
        // Every bind replaces the previous node's content, including after scrolling.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 36);
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
        auto *description = new QLabel(QStringLiteral("25 平滑滚动：切换控制器与原生滚轮，调整步长；仅检测到匹配的 SmoothScrollbar 时编译本章。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        enabled = new QCheckBox(QStringLiteral("平滑滚动"), this);
        enabled->setChecked(true);
        controls->addWidget(enabled);
        step = new QSpinBox(this);
        step->setRange(1, 240);
        step->setValue(48);
        step->setPrefix(QStringLiteral("每格像素 "));
        controls->addWidget(step);
        stop = new QPushButton(QStringLiteral("停止当前动画"), this);
        controls->addWidget(stop);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 400; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        // Parent the controller to this view; no shared example helper or global filter is required.
        m_controller = new sscroll::SmoothScrollController(m_view, m_view);
        applyStep(48);
        m_controller->setEnabled(true);
        connect(enabled, &QCheckBox::toggled, m_controller, &sscroll::SmoothScrollController::setEnabled);
        connect(step, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) { applyStep(value); });
        connect(stop, &QPushButton::clicked, this, [this] { m_controller->stop(); });
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated, this, [this] {
            m_status->setText(QStringLiteral("offset=%1 px / 可见映射节点=%2")
                .arg(m_view->verticalOffset()).arg(m_view->visibleRowCount()));
        });
    }

    void applyStep(int pixels)
    {
        // Keep native fallback and controller wheel steps consistent.
        m_view->setWheelScrollPixels(pixels);
        auto settings = m_controller->settings();
        settings.wheelStep = pixels;
        settings.minimumWheelStep = 1;
        m_controller->setSettings(settings);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *enabled = nullptr;
    QSpinBox *step = nullptr;
    QPushButton *stop = nullptr;
    sscroll::SmoothScrollController *m_controller = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_smooth_scrolling
