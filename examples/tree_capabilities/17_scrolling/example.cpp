#include "ui_scrolling.h"
#include <memory>
#include "example.h"
#include <QAbstractItemModel>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

namespace tree_scrolling {

// Generate data on demand instead of allocating a million QStandardItems.
// internalId 0 denotes a root; a child's internalId stores its parent root + 1.
class LargeTreeModel : public QAbstractItemModel
{
public:
    explicit LargeTreeModel(QObject *parent) : QAbstractItemModel(parent) {}
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        return createIndex(row, column, parent.isValid() ? quintptr(parent.row() + 1) : quintptr(0));
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        return index.isValid() && index.internalId() != 0
            ? createIndex(int(index.internalId()) - 1, 0, quintptr(0)) : QModelIndex();
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (!parent.isValid())
            return 5;
        return parent.column() == 0 && parent.internalId() == 0 ? 200000 : 0;
    }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return index.internalId() == 0 ? QStringLiteral("分组 %1").arg(index.row() + 1)
            : QStringLiteral("节点 %1.%2").arg(index.internalId()).arg(index.row() + 1);
    }
};

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Every bind replaces the previous node's content, including after scrolling.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeScrollingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeScrollingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        auto *model = new LargeTreeModel(ui->view);
        ui->view->setAdapter(new TextAdapter, true);
        ui->view->setUniformItemHeight(36);
        // Wheel units remain logical pixels even with very large scroll ranges.
        ui->view->setWheelScrollPixels(48);
        ui->view->setModel(model);
        ui->view->expand(model->index(0, 0));
        connect(ui->go, &QPushButton::clicked, this, [this, model] {
            const QModelIndex parent = model->index(ui->group->value() - 1, 0);
            // scrollTo does not open arbitrary ancestors; expand the required path first.
            ui->view->expand(parent);
            const QModelIndex index = model->index(ui->node->value() - 1, 0, parent);
            ui->view->scrollTo(index, static_cast<viv::VirtualItemView::ScrollHint>(ui->hint->currentIndex()));
            ui->view->setCurrentIndex(index);
        });
        connect(ui->step, &QPushButton::clicked, this, [this] { ui->view->scrollByPixels(120); });
        connect(ui->end, &QPushButton::clicked, this, [this] {
            ui->view->setVerticalOffset(ui->view->maximumVerticalOffset());
        });
        connect(ui->wheel, &QCheckBox::toggled, this, [this](bool items) {
            if (items)
                ui->view->setWheelScrollItems(1);
            else
                ui->view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
        });
        connect(ui->view, &viv::VirtualItemView::virtualizationUpdated, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            ui->status->setText(QStringLiteral("offset=%1 / extent=%2 / maximum=%3 / 可见映射节点=%4 / 当前映射行=%5")
                .arg(ui->view->verticalOffset()).arg(ui->view->contentExtent()).arg(ui->view->maximumVerticalOffset())
                .arg(ui->view->visibleRowCount()).arg(ui->view->visibilityIndex()->visibleRowForIndex(index)));
        });
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_scrolling
