#include "ui_item_events.h"
#include <memory>
#include "example.h"
#include <QStringList>
#include <QPersistentModelIndex>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>

namespace tree_item_events {

class ActionRow : public QWidget
{
public:
    explicit ActionRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QPersistentModelIndex index;
    QLabel *title = nullptr;
    QPushButton *action = nullptr;
private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 8, 2);
        title = new QLabel(this);
        action = new QPushButton(QStringLiteral("行内事件"), this);
        action->setFocusPolicy(Qt::NoFocus);
        // Reserve the action's space while hidden to avoid moving the title on hover.
        auto policy = action->sizePolicy();
        policy.setRetainSizeWhenHidden(true);
        action->setSizePolicy(policy);
        action->hide();
        layout->addWidget(title, 1);
        layout->addWidget(action);
    }
};
class EventAdapter : public viv::WidgetAdapter
{
public:
    explicit EventAdapter(QLabel &status) : m_status(status) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new ActionRow(parent);
        QObject::connect(row->action, &QPushButton::clicked, row, [this, row] {
            // The persistent index must be replaced on every bind, not only at creation.
            const QModelIndex index = row->index;
            if (index.isValid())
                m_status.setText(QStringLiteral("事件当前绑定：%1 / parent=%2")
                    .arg(index.data().toString()).arg(index.parent().data().toString()));
        });
        return row;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<ActionRow *>(widget);
        row->index = QPersistentModelIndex(index);
        row->title->setText(index.data().toString());
        row->action->show();
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<ActionRow *>(widget);
        row->index = QPersistentModelIndex();
        row->title->clear();
        row->action->hide();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(480, 40); }
private:
    QLabel &m_status;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeItemEventsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeItemEventsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 100; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new EventAdapter(*ui->status), true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        ui->view->setCurrentIndex(m_model->index(0, 0, m_model->index(0, 0)));
        connect(ui->query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            const QRect rect = ui->view->visualRect(index);
            QWidget *widget = ui->view->widgetForIndex(index);
            const QModelIndex hit = ui->view->indexAt(rect.center());
            QStringList selected;
            for (const QModelIndex &node : ui->view->selectionModel()->selectedIndexes())
                selected.append(node.data().toString());
            ui->status->setText(QStringLiteral("current=%1 / selected=[%2] / widget=%3 / rect=(%4,%5 %6x%7) / hit=%8")
                .arg(index.data().toString()).arg(selected.join(QStringLiteral(", ")))
                .arg(widget ? QStringLiteral("已物化") : QStringLiteral("未物化"))
                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(hit.data().toString()));
        });
        connect(ui->view, &viv::VirtualItemView::doubleClicked, this, [this](const QModelIndex &index) {
            ui->status->setText(QStringLiteral("doubleClicked: %1").arg(index.data().toString()));
        });
        connect(ui->view, &viv::VirtualItemView::activated, this, [this](const QModelIndex &index) {
            ui->status->setText(QStringLiteral("activated: %1").arg(index.data().toString()));
        });
        connect(ui->activate, &QPushButton::clicked, this, [this] {
            ui->view->activateIndex(ui->view->currentIndex());
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_item_events
