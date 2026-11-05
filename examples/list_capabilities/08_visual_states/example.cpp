#include "ui_visual_states.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QStringListModel>

namespace list_visual_states {

class StateAdapter : public viv::WidgetAdapter
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
        auto *label = static_cast<QLabel *>(widget);
        label->setText(index.data().toString());
        // Recompute for each binding so reused widgets follow the current text width.
        // A narrow widget makes FullWidth and WidgetOnly easy to compare.
        label->setMaximumWidth(qMax(1, label->sizeHint().width()));
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<QLabel *>(widget);
        row->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListVisualStatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListVisualStatesPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->easing->setItemData(0, int(QEasingCurve::OutCubic));
        ui->easing->setItemData(1, int(QEasingCurve::InOutCubic));
        ui->easing->setItemData(2, int(QEasingCurve::Linear));
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1 · 支持 hover 与 selected 同时存在").arg(row + 1));
        auto *model = new QStringListModel(records, ui->view);
        // The framework paints states by default; the adapter only provides text.
        ui->view->setAdapter(new StateAdapter, true);
        ui->view->setUniformItemHeight(36);
        // Radius only affects the item; row spacing always keeps its own background.
        ui->view->setVisualStateCornerRadius(8);
        ui->view->setRowGridLinesVisible(false);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        // The view paints these colors below the transparent row labels.
        ui->view->setHoverBackgroundColor(QColor(QStringLiteral("#e6effb")));
        ui->view->setSelectedBackgroundColor(QColor(QStringLiteral("#afcbee")));
        // OutCubic responds quickly when the pointer only visits a row briefly.
        // OutCubic is also the framework default; the explicit call demonstrates the API.
        ui->view->setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic);
        ui->view->setModel(model);
        // Both painting and mouse hits follow the chosen rectangle; selection APIs stay unchanged.
        connect(ui->extent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setVisualStateBackgroundExtent(
                static_cast<viv::VirtualListView::VisualStateBackgroundExtent>(value));
        });
        // Insets shrink the state area without changing widget geometry or row height.
        const auto applyMargins = [this] {
            ui->view->setVisualStateBackgroundMargins(ui->marginLeft->value(), ui->marginTop->value(),
                                                   ui->marginRight->value(), ui->marginBottom->value());
        };
        for (QSpinBox *spin : {ui->marginLeft, ui->marginTop, ui->marginRight, ui->marginBottom})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, applyMargins);
        // Zero restores automatic height; a positive height is centered after margins.
        connect(ui->backgroundHeight, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualListView::setVisualStateBackgroundHeight);
        // Duration is shared by hover and selection transitions; 0 switches immediately.
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualItemView::setVisualStateAnimationDuration);
        // Zero gives square corners; the radius applies to both visual states.
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualListView::setVisualStateCornerRadius);
        connect(ui->easing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            // The same curve controls hover and selection, including fade-out.
            const auto type = static_cast<QEasingCurve::Type>(ui->easing->currentData().toInt());
            ui->view->setVisualStateAnimationEasingCurve(QEasingCurve(type));
        });
        connect(ui->colors, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int color) {
            // Changing a visual-state color notifies materialized rows to repaint.
            ui->view->setHoverBackgroundColor(QColor(color == 0 ? QStringLiteral("#e6effb") : QStringLiteral("#e1f1e5")));
            ui->view->setSelectedBackgroundColor(QColor(color == 0 ? QStringLiteral("#afcbee") : QStringLiteral("#a2d4b0")));
        });
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_visual_states
