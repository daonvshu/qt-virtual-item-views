#include "example.h"
#include "ui_visual_states.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <QEasingCurve>
#include <memory>
#include <QPointer>

namespace masonry_visual_states {

class CardAdapter : public viv::WidgetAdapter
{
    QPointer<viv::VirtualMasonryView> m_view;
public:
    explicit CardAdapter(viv::VirtualMasonryView *view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Replace all data when a cached widget acquires a new identity.
        widget->setProperty("title", index.data().toString());
        updateContent(widget, index);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        widget->setProperty("title", QVariant());
        static_cast<QLabel *>(widget)->clear();
    }
    void visualStateChanged(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        updateContent(widget, index);
    }
    void updateContent(QWidget *widget, const QModelIndex &index)
    {
        const auto state = m_view ? m_view->visualState(index) : viv::VirtualItemView::VisualState{};
        static_cast<QLabel *>(widget)->setText(widget->property("title").toString()
            + (state.hovered || state.selected ? QStringLiteral("\nMore details") : QString()));
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        // Layout uses model metadata without constructing offscreen cards.
        return index.data(Qt::UserRole + 1).toSize();
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryVisualStatesPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 65; ++row) {
            auto *item = new QStandardItem(QStringLiteral("Card %1").arg(row + 1));
            item->setData(QSize(160, 80 + (row * 47) % 180), Qt::UserRole + 1);
            model->appendRow(item);
        }
        // The view owns the adapter; the model lives through card teardown.
        ui->view->setAdapter(new CardAdapter(ui->view), true);
        // Estimates describe aspect ratios; columns receive independent heights.
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(100, 200);
        ui->view->setRowSpacing(10);
        ui->view->setColumnSpacing(10);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        ui->view->setModel(model);

        // Masonry has its own item-rectangle state painter and radius setter.
        ui->view->setStateBackgroundCornerRadius(10);
        ui->view->setHoverBackgroundColor(QColor(QStringLiteral("#dceaff")));
        ui->view->setSelectedBackgroundColor(QColor(QStringLiteral("#acd0ff")));
        ui->view->setVisualStateAnimationDuration(180);
        ui->view->setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic);
        connect(ui->background, &QCheckBox::toggled, ui->view, &viv::VirtualMasonryView::setVisualStateBackgroundVisible);
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualMasonryView::setStateBackgroundCornerRadius);
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setVisualStateAnimationDuration);
        connect(ui->easing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int curve) {
            ui->view->setVisualStateAnimationEasingCurve(curve == 0 ? QEasingCurve::OutCubic
                : curve == 1 ? QEasingCurve::Linear : QEasingCurve::InOutCubic);
        });
        ui->status->setText(QStringLiteral("hover 时附加文字可见；选中时仍可见。背景与动画全部由框架负责。"));
    }

    std::unique_ptr<Ui::MasonryVisualStatesPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_visual_states
