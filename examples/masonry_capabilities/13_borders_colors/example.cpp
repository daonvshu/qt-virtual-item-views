#include "example.h"
#include "ui_borders_colors.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>

namespace masonry_borders_colors {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryBordersColorsPage)
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

        ui->view->setFrameCornerRadius(16);
        ui->view->setFrameBorderWidth(2);
        ui->view->setFrameBorderColor(QColor(QStringLiteral("#7185a0")));
        ui->view->setStateBackgroundCornerRadius(8);
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setFrameCornerRadius);
        connect(ui->borderWidth, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setFrameBorderWidth);
        connect(ui->left, &QCheckBox::toggled, this, [this](bool on) { ui->view->setFrameBorderVisible(Qt::LeftEdge, on); });
        connect(ui->top, &QCheckBox::toggled, this, [this](bool on) { ui->view->setFrameBorderVisible(Qt::TopEdge, on); });
        connect(ui->right, &QCheckBox::toggled, this, [this](bool on) { ui->view->setFrameBorderVisible(Qt::RightEdge, on); });
        connect(ui->bottom, &QCheckBox::toggled, this, [this](bool on) { ui->view->setFrameBorderVisible(Qt::BottomEdge, on); });
        connect(ui->body, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int body) {
            ui->view->setBackgroundVisible(body != 2);
            ui->view->setBackgroundColor(body == 1 ? QColor(QStringLiteral("#d9eafb")) : QColor());
        });
        connect(ui->items, &QCheckBox::toggled, this, [this](bool on) {
            // Invalid item color restores transparent items.
            ui->view->setItemBackgroundColor(on ? QColor(QStringLiteral("#eef3f8")) : QColor());
        });
        connect(ui->spacing, &QCheckBox::toggled, this, [this](bool on) {
            ui->view->setSpacingBackgroundColor(on ? QColor(QStringLiteral("#fff0cb")) : QColor());
        });
        connect(ui->qss, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int preset) {
            // Set QSS on the ancestor: selectors address framework object names.
            setStyleSheet(preset == 1 ? QStringLiteral("#vivItemView { background-color: #ffd6e8; }")
                : preset == 2 ? QStringLiteral("#vivItemView #vivViewport { background-color: #ffd6e8; }") : QString());
        });
        ui->status->setText(QStringLiteral("查看 viewport 背景时，取消项目底色和间距色；hover/selected 颜色在第 06 节。"));
    }

    std::unique_ptr<Ui::MasonryBordersColorsPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_borders_colors
