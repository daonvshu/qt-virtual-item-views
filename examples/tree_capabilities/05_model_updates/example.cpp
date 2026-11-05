#include "ui_model_updates.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>

namespace tree_model_updates {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeModelUpdatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeModelUpdatesPage> ui;

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
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 8; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        connect(ui->rename, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (index.isValid())
                m_model->setData(index, index.data().toString() + QStringLiteral(" *"));
        });
        connect(ui->insert, &QPushButton::clicked, this, [this] {
            const QModelIndex parent = ui->view->currentIndex();
            auto *item = parent.isValid() ? m_model->itemFromIndex(parent) : m_model->invisibleRootItem();
            // QStandardItem emits the required structural model notifications.
            item->appendRow(new QStandardItem(QStringLiteral("新增节点 %1").arg(++m_serial)));
            if (parent.isValid())
                ui->view->expand(parent);
        });
        connect(ui->remove, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (index.isValid())
                m_model->removeRow(index.row(), index.parent());
        });
        connect(ui->reset, &QPushButton::clicked, this, [this] {
            // clear emits modelReset; old persistent indexes and expansion identities expire.
            m_model->clear();
            auto *branch = new QStandardItem(QStringLiteral("重新创建的分组"));
            branch->appendRow(new QStandardItem(QStringLiteral("重置后的节点")));
            m_model->appendRow(branch);
            ui->view->expand(branch->index());
        });
    }

    QStandardItemModel *m_model = nullptr;
    int m_serial = 0;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_model_updates
