#include "example.h"
#include "ui_scrolling.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>
#include <QAbstractListModel>
#include <QTimer>
#include <QPersistentModelIndex>

namespace masonry_scrolling {

class CatalogModel : public QAbstractListModel
{
public:
    explicit CatalogModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 10000; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        if (role == Qt::DisplayRole) return QStringLiteral("Record %1").arg(index.row() + 1);
        if (role == Qt::UserRole + 1) return QSize(160, 80 + index.row() % 7 * 25);
        return {};
    }
};
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryScrollingPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new CatalogModel(ui->view);
        ui->view->setAdapter(new CardAdapter, true);
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(100, 200);
        ui->view->setRowSpacing(10);
        ui->view->setColumnSpacing(10);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        ui->view->setOverscan(2, 2);
        ui->view->setModel(model);

        connect(ui->jump, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = model->index(ui->row->value() - 1, 0);
            ui->view->setCurrentIndex(index);
            using Hint = viv::VirtualItemView::ScrollHint;
            const Hint hints[]{Hint::PositionAtCenter, Hint::PositionAtTop, Hint::PositionAtBottom, Hint::EnsureVisible};
            ui->view->scrollTo(index, hints[ui->position->currentIndex()]);
        });
        const auto overscan = [this]() { ui->view->setOverscan(ui->before->value(), ui->after->value()); };
        connect(ui->before, QOverload<int>::of(&QSpinBox::valueChanged), this, [overscan](int) { overscan(); });
        connect(ui->after, QOverload<int>::of(&QSpinBox::valueChanged), this, [overscan](int) { overscan(); });
        connect(ui->pin, &QCheckBox::toggled, this, [this](bool on) {
            if (pinned.isValid()) ui->view->setItemPinned(pinned, false);
            pinned = on ? ui->view->currentIndex() : QModelIndex();
            if (pinned.isValid()) ui->view->setItemPinned(pinned);
        });
        connect(ui->logging, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setPerformanceLoggingEnabled);
        connect(ui->wheel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setWheelScrollMode(mode == 0 ? viv::VirtualItemView::WheelScrollMode::Pixels
                                                 : viv::VirtualItemView::WheelScrollMode::Items);
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            const auto stats = ui->view->stats();
            ui->status->setText(QStringLiteral("logical=%1; materialized=%2; pooled=%3; pinned=%4; created=%5; binds=%6")
                .arg(stats.logicalItems).arg(stats.materializedItems).arg(stats.pooledWidgets)
                .arg(stats.pinnedWidgets).arg(stats.createCount).arg(stats.bindCount));
        });
        timer->start(250);
    }

    std::unique_ptr<Ui::MasonryScrollingPage> ui;
    QAbstractItemModel *model = nullptr;
    QPersistentModelIndex pinned;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_scrolling
