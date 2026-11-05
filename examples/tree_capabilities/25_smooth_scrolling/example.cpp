#include "ui_smooth_scrolling.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeSmoothScrollingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeSmoothScrollingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 400; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        // Parent the controller to this view; no shared example helper or global filter is required.
        m_controller = new sscroll::SmoothScrollController(ui->view, ui->view);
        applyStep(48);
        m_controller->setEnabled(true);
        connect(ui->enabled, &QCheckBox::toggled, m_controller, &sscroll::SmoothScrollController::setEnabled);
        connect(ui->step, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) { applyStep(value); });
        connect(ui->stop, &QPushButton::clicked, this, [this] { m_controller->stop(); });
        connect(ui->view, &viv::VirtualItemView::virtualizationUpdated, this, [this] {
            ui->status->setText(QStringLiteral("offset=%1 px / 可见映射节点=%2")
                .arg(ui->view->verticalOffset()).arg(ui->view->visibleRowCount()));
        });
    }

    void applyStep(int pixels)
    {
        // Keep native fallback and controller wheel steps consistent.
        ui->view->setWheelScrollPixels(pixels);
        auto settings = m_controller->settings();
        settings.wheelStep = pixels;
        settings.minimumWheelStep = 1;
        m_controller->setSettings(settings);
    }

    QStandardItemModel *m_model = nullptr;
    sscroll::SmoothScrollController *m_controller = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_smooth_scrolling
