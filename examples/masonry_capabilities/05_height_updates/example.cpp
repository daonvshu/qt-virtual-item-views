#include "example.h"
#include "ui_height_updates.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>

namespace masonry_height_updates {

class CardAdapter : public viv::WidgetAdapter
{
public:
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
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryHeightUpdatesPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 55; ++row) {
            auto *item = new QStandardItem(QStringLiteral("Card %1").arg(row + 1));
            item->setData(QSize(160, 80 + (row * 47) % 180), Qt::UserRole + 1);
            model->appendRow(item);
        }
        // The view owns the adapter; the model lives through card teardown.
        ui->view->setAdapter(new CardAdapter, true);
        // Estimates describe aspect ratios; columns receive independent heights.
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(100, 200);
        ui->view->setRowSpacing(10);
        ui->view->setColumnSpacing(10);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        ui->view->setModel(model);

        // Height animation is opt-in and applies to authoritative height changes.
        ui->view->setItemHeightAnimationEnabled(true);
        ui->view->setItemHeightAnimationDuration(300);
        connect(ui->animate, &QCheckBox::toggled, ui->view, &viv::VirtualMasonryView::setItemHeightAnimationEnabled);
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualMasonryView::setItemHeightAnimationDuration);
        connect(ui->estimate, &QPushButton::clicked, this, [this]() {
            ui->view->setItemEstimatedSize(model->index(ui->row->value() - 1, 0), QSize(160, ui->height->value()));
        });
        connect(ui->bulk, &QPushButton::clicked, this, [this]() {
            QVector<QSize> estimates;
            for (int row = 0; row < model->rowCount(); ++row)
                estimates.append(QSize(160, ui->height->value() + row % 3 * 25));
            // Submit one manifest to avoid a full rebuild for every estimate.
            ui->view->setItemEstimatedSizes(estimates);
        });
        connect(ui->metadata, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = model->index(ui->row->value() - 1, 0);
            model->setData(index, QSize(160, ui->height->value()), Qt::UserRole + 1);
            ui->view->invalidateItemSize(index);
        });
        connect(ui->authoritative, &QPushButton::clicked, this, [this]() {
            // The value is a logical pixel height, not an aspect-ratio estimate.
            ui->view->setItemHeight(model->index(ui->row->value() - 1, 0), ui->height->value());
        });
        ui->status->setText(QStringLiteral("选择一个项目，修改高度后点击按钮；拖动滚动条观察后续卡片重排。"));
    }

    std::unique_ptr<Ui::MasonryHeightUpdatesPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_height_updates
