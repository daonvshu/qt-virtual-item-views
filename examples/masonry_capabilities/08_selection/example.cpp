#include "example.h"
#include "ui_selection.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <memory>

namespace masonry_selection {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonrySelectionPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 180; ++row) {
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
            using Mode = viv::VirtualItemView::SelectionMode;
            const Mode modes[]{Mode::ExtendedSelection, Mode::SingleSelection, Mode::MultiSelection, Mode::NoSelection};
            ui->view->setSelectionMode(modes[mode]);
        });
        connect(ui->select, &QPushButton::clicked, this, [this]() {
            // Programmatic selection is explicit and independent of gesture mode.
            const QItemSelection range(model->index(1, 0), model->index(4, 0));
            ui->view->selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
            ui->view->selectionModel()->setCurrentIndex(model->index(4, 0), QItemSelectionModel::NoUpdate);
        });
        connect(ui->clear, &QPushButton::clicked, ui->view->selectionModel(), &QItemSelectionModel::clearSelection);
        connect(ui->query, &QPushButton::clicked, this, [this]() { showSelection(); });
        connect(ui->view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { showSelection(); });
        connect(ui->view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() { showSelection(); });
    }

    void showSelection()
    {
        QStringList selected;
        for (const QModelIndex &index : ui->view->selectionModel()->selectedIndexes())
            selected.append(index.data().toString());
        const QModelIndex current = ui->view->currentIndex();
        const QRect rect = ui->view->visualRect(current);
        // An offscreen item's widget may be absent; selection still lives in the model.
        const bool materialized = ui->view->widgetForIndex(current) != nullptr;
        const int hit = ui->view->indexAt(rect.center()).row();
        ui->status->setText(QStringLiteral("Current=%1; selected=[%2]\nwidget=%3; rect=(%4,%5 %6x%7); hit.row=%8")
            .arg(current.data().toString(), selected.join(QStringLiteral(", ")))
            .arg(materialized).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(hit));
    }
    std::unique_ptr<Ui::MasonrySelectionPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_selection
