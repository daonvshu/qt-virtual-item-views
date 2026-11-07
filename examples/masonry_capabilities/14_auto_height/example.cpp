#include "example.h"
#include "ui_auto_height.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <QScrollBar>
#include <memory>

namespace masonry_auto_height {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryAutoHeightPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 4; ++row) {
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

        // Auto height measures the tallest packed column, not the sum of all cards.
        ui->view->setAutoHeightEnabled(true);
        ui->view->setAutoHeightMaximumHeight(280);
        connect(ui->adaptive, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setAutoHeightEnabled);
        connect(ui->maximum, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setAutoHeightMaximumHeight);
        connect(ui->count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int count) {
            while (model->rowCount() > count) model->removeRow(model->rowCount() - 1);
            while (model->rowCount() < count) {
                const int row = model->rowCount();
                auto *item = new QStandardItem(QStringLiteral("Card %1").arg(row + 1));
                item->setData(QSize(160, 80 + row % 4 * 40), Qt::UserRole + 1);
                model->appendRow(item);
            }
        });
        connect(ui->forceBar, &QCheckBox::toggled, this, [this](bool on) {
            // Visibility and scrollability differ: a forced bar may have no range.
            ui->view->setVerticalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded);
        });
        connect(ui->trailing, &QCheckBox::toggled, this, [this](bool on) {
            ui->view->setTrailingRowGridLineAutoHideEnabled(!on);
        });
        connect(ui->view, &viv::VirtualMasonryView::masonryLayoutChanged, this, [this]() {
            ui->status->setText(QStringLiteral("sizeHint=%1; height=%2; internal scroll maximum=%3")
                .arg(ui->view->sizeHint().height()).arg(ui->view->height()).arg(ui->view->verticalScrollBar()->maximum()));
        });
    }

    std::unique_ptr<Ui::MasonryAutoHeightPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_auto_height
