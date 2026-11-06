#include "example.h"
#include "ui_columns.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>

namespace masonry_columns {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryColumnsPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 90; ++row) {
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

        connect(ui->columns, QOverload<int>::of(&QSpinBox::valueChanged), ui->view,
                &viv::VirtualMasonryView::setColumnCount);
        const auto applyRange = [this]() {
            // The minimum is a hard bound; keep the editor values consistent.
            ui->maximum->setMinimum(ui->minimum->value());
            ui->view->setCellWidthRange(ui->minimum->value(), ui->maximum->value());
        };
        connect(ui->minimum, QOverload<int>::of(&QSpinBox::valueChanged), this, [applyRange](int) { applyRange(); });
        connect(ui->maximum, QOverload<int>::of(&QSpinBox::valueChanged), this, [applyRange](int) { applyRange(); });
        connect(ui->view, &viv::VirtualMasonryView::masonryLayoutChanged, this, [this]() {
            ui->status->setText(QStringLiteral("Requested=%1; actual=%2; first card width=%3")
                .arg(ui->columns->value()).arg(ui->view->columnCount())
                .arg(ui->view->visualRect(model->index(0, 0)).width()));
        });
    }

    std::unique_ptr<Ui::MasonryColumnsPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_columns
