#include "example.h"
#include "ui_spacing_grid.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>

namespace masonry_spacing_grid {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonrySpacingGridPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 26; ++row) {
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

        connect(ui->rows, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualMasonryView::setRowSpacing);
        connect(ui->columns, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualMasonryView::setColumnSpacing);
        connect(ui->margins, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int margin) {
            ui->view->setContentMargins(QMargins(margin, margin, margin, margin));
        });
        connect(ui->horizontal, &QCheckBox::toggled, ui->view, &viv::VirtualMasonryView::setHorizontalGridLinesVisible);
        connect(ui->vertical, &QCheckBox::toggled, ui->view, &viv::VirtualMasonryView::setVerticalGridLinesVisible);
        connect(ui->lineWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int width) {
            ui->view->setHorizontalGridLineWidth(width);
            ui->view->setVerticalGridLineWidth(width);
        });
        const auto applyLines = [this]() {
            const bool edges = ui->placement->currentIndex() == 1;
            // Center lines always bridge gaps; only edge mode exposes this choice.
            ui->view->setGridLinePlacement(edges ? viv::GridLinePlacement::SpacingEdges
                                                : viv::GridLinePlacement::SpacingCenter);
            ui->through->setEnabled(edges);
            ui->view->setVerticalSpacingLineThroughRowSpacing(ui->through->isChecked());
            ui->view->setHorizontalSpacingLineThroughColumnSpacing(ui->through->isChecked());
        };
        connect(ui->placement, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [applyLines](int) { applyLines(); });
        connect(ui->through, &QCheckBox::toggled, this, [applyLines](bool) { applyLines(); });
        connect(ui->colored, &QCheckBox::toggled, this, [this](bool enabled) {
            // An invalid color restores the palette/style fallback.
            ui->view->setSpacingBackgroundColor(enabled ? QColor(QStringLiteral("#ffe7bf")) : QColor());
        });
        ui->view->setHorizontalGridLineColor(QColor(QStringLiteral("#8797aa")));
        ui->view->setVerticalGridLineColor(QColor(QStringLiteral("#8797aa")));
        applyLines();
        ui->status->setText(QStringLiteral("每列项目高度独立；横线不会形成规则表格行。"));
    }

    std::unique_ptr<Ui::MasonrySpacingGridPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_spacing_grid
