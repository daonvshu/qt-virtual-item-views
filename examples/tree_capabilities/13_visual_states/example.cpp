#include "ui_visual_states.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>

namespace tree_visual_states {

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
        auto *label = static_cast<QLabel *>(widget);
        label->setText(index.data().toString());
        // Keep the widget narrower than the row so the background modes differ.
        // Recompute on every bind because recycled nodes can have different text.
        label->setMaximumWidth(qMax(1, label->sizeHint().width()));
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeVisualStatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeVisualStatesPage> ui;

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
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 12; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // The built-in background sits below transparent node widgets and branch decorations.
        ui->view->setVisualStateBackgroundVisible(true);
        ui->view->setVisualStateCornerRadius(6);
        ui->view->setVisualStateAnimationDuration(180);
        ui->view->setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic);
        ui->view->expandRecursively(m_model->index(0, 0));
        connect(ui->enabled, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setVisualStateBackgroundVisible);
        // WidgetOnly follows the widget; WidgetAndIcon also covers a visible branch icon's cell.
        // Leaves and hidden indicators use WidgetOnly geometry, including for mouse hits.
        // NodeAndIcon similarly falls back to NodeOnly when no indicator is shown.
        connect(ui->extent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setVisualStateBackgroundExtent(static_cast<viv::VirtualTreeView::VisualStateBackgroundExtent>(value));
        });
        ui->extent->setCurrentIndex(2);
        // Insets affect state backgrounds and mouse hits, never the node widget or row height.
        // With a 36px row, top/bottom margins of 4 leave a 28px state background.
        const auto applyMargins = [this] {
            ui->view->setVisualStateBackgroundMargins(
                ui->marginLeft->value(), ui->marginTop->value(), ui->marginRight->value(), ui->marginBottom->value());
        };
        for (QSpinBox *spin : {ui->marginLeft, ui->marginTop, ui->marginRight, ui->marginBottom})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, applyMargins);
        // Zero restores automatic height. A positive height is centered within the inset area.
        connect(ui->backgroundHeight, QOverload<int>::of(&QSpinBox::valueChanged), ui->view,
                &viv::VirtualTreeView::setVisualStateBackgroundHeight);
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualTreeView::setVisualStateCornerRadius);
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setVisualStateAnimationDuration);
        connect(ui->curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setVisualStateAnimationEasingCurve(value == 0 ? QEasingCurve::OutCubic : QEasingCurve::Linear);
        });
        connect(ui->colors, &QCheckBox::toggled, this, [this](bool customColors) {
            ui->view->setHoverBackgroundColor(QColor(customColors ? QStringLiteral("#fff0cf") : QStringLiteral("#e7f4ed")));
            ui->view->setSelectedBackgroundColor(QColor(customColors ? QStringLiteral("#f2c884") : QStringLiteral("#b9d9f1")));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_visual_states
