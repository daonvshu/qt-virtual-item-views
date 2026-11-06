#include "example.h"
#include "ui_model_root.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>
#include <QSortFilterProxyModel>

namespace masonry_model_root {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryModelRootPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int group = 0; group < 2; ++group) {
            groups[group] = makeItem(QStringLiteral("Group %1").arg(group + 1), group);
            groups[group]->setData(QStringLiteral("match"), Qt::UserRole + 2);
            model->appendRow(groups[group]);
            for (int row = 0; row < 35; ++row)
                groups[group]->appendRow(makeItem(QStringLiteral("Group %1 / Item %2").arg(group + 1).arg(row + 1), row));
        }
        proxy = new QSortFilterProxyModel(ui->view);
        proxy->setSourceModel(model);
        proxy->setFilterRole(Qt::UserRole + 2);
        ui->view->setAdapter(new CardAdapter, true);
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(120, 220);
        ui->view->setRowSpacing(10);
        ui->view->setColumnSpacing(10);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        // Install the proxy, then map source roots into that proxy.
        ui->view->setModel(proxy);

        ui->root->setCurrentIndex(1);
        applyRoot();
        connect(ui->root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyRoot(); });
        connect(ui->filter, &QCheckBox::toggled, this, [this](bool enabled) {
            proxy->setFilterFixedString(enabled ? QStringLiteral("match") : QString());
            applyRoot();
        });
        connect(ui->sort, &QCheckBox::toggled, this, [this](bool reverse) {
            proxy->sort(0, reverse ? Qt::DescendingOrder : Qt::AscendingOrder);
        });
        connect(ui->append, &QPushButton::clicked, this, [this]() {
            QStandardItem *parent = ui->root->currentIndex() == 0 ? model->invisibleRootItem() : groups[ui->root->currentIndex() - 1];
            ++serial;
            parent->appendRow(makeItem(QStringLiteral("Added %1").arg(serial), serial));
        });
        connect(ui->remove, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = proxy->mapToSource(ui->view->currentIndex());
            // Keep the two demonstration roots alive.
            if (index.isValid() && index.parent().isValid()) model->removeRow(index.row(), index.parent());
        });
        connect(ui->edit, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = proxy->mapToSource(ui->view->currentIndex());
            if (index.isValid()) model->setData(index, QStringLiteral("Updated %1").arg(++serial));
        });
        ui->status->setText(QStringLiteral("删除只作用于分组子项；顶层分组保持存在，便于切换显示根。"));
    }

    static QStandardItem *makeItem(const QString &title, int row)
    {
        auto *item = new QStandardItem(title);
        item->setData(QSize(160, 90 + row % 5 * 30), Qt::UserRole + 1);
        item->setData(row % 2 == 0 ? QStringLiteral("match") : QStringLiteral("other"), Qt::UserRole + 2);
        return item;
    }
    void applyRoot()
    {
        const int root = ui->root->currentIndex();
        ui->view->setRootIndex(root == 0 ? QModelIndex() : proxy->mapFromSource(groups[root - 1]->index()));
    }
    int serial = 100;
    std::unique_ptr<Ui::MasonryModelRootPage> ui;
    QStandardItemModel *model = nullptr;
    QSortFilterProxyModel *proxy = nullptr;
    QStandardItem *groups[2]{};
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_model_root
