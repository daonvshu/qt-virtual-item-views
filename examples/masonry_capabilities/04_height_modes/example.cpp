#include "example.h"
#include "ui_height_modes.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>

namespace masonry_height_modes {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryHeightModesPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 40; ++row) {
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

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            using Mode = viv::VirtualMasonryView::ItemHeightMode;
            ui->view->setItemHeightMode(mode == 0 ? Mode::AdapterEstimate : mode == 1 ? Mode::Fixed : Mode::AspectRatio);
        });
        // This height is also the fallback for absent/invalid per-item estimates.
        ui->view->setFixedItemHeight(100);
        ui->view->setItemAspectRatio(1.5);
        connect(ui->fixed, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualMasonryView::setFixedItemHeight);
        connect(ui->ratio, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double ratio) {
            ui->view->setItemAspectRatio(ratio);
        });
        connect(ui->invalid, &QCheckBox::toggled, this, [this](bool invalid) {
            model->setData(model->index(0, 0), invalid ? QSize() : QSize(160, 80), Qt::UserRole + 1);
            // Read a new adapter estimate after metadata changes; calls are coalesced.
            ui->view->invalidateItemSize(model->index(0, 0));
        });
        connect(ui->view, &viv::VirtualMasonryView::masonryLayoutChanged, this, [this]() {
            ui->status->setText(QStringLiteral("First card actual size=%1 x %2")
                .arg(ui->view->visualRect(model->index(0, 0)).width())
                .arg(ui->view->visualRect(model->index(0, 0)).height()));
        });
    }

    std::unique_ptr<Ui::MasonryHeightModesPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_height_modes
