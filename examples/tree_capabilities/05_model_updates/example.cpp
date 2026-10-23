#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("05 模型更新：选中节点后修改、增加子节点或删除；重置后展开状态重新建立。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        rename = new QPushButton(QStringLiteral("修改名称"), this);
        controls->addWidget(rename);
        insert = new QPushButton(QStringLiteral("增加子节点"), this);
        controls->addWidget(insert);
        remove = new QPushButton(QStringLiteral("删除当前节点"), this);
        controls->addWidget(remove);
        reset = new QPushButton(QStringLiteral("重置模型"), this);
        controls->addWidget(reset);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
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
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        connect(rename, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid())
                m_model->setData(index, index.data().toString() + QStringLiteral(" *"));
        });
        connect(insert, &QPushButton::clicked, this, [this] {
            const QModelIndex parent = m_view->currentIndex();
            auto *item = parent.isValid() ? m_model->itemFromIndex(parent) : m_model->invisibleRootItem();
            // QStandardItem emits the required structural model notifications.
            item->appendRow(new QStandardItem(QStringLiteral("新增节点 %1").arg(++m_serial)));
            if (parent.isValid())
                m_view->expand(parent);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid())
                m_model->removeRow(index.row(), index.parent());
        });
        connect(reset, &QPushButton::clicked, this, [this] {
            // clear emits modelReset; old persistent indexes and expansion identities expire.
            m_model->clear();
            auto *branch = new QStandardItem(QStringLiteral("重新创建的分组"));
            branch->appendRow(new QStandardItem(QStringLiteral("重置后的节点")));
            m_model->appendRow(branch);
            m_view->expand(branch->index());
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *rename = nullptr;
    QPushButton *insert = nullptr;
    QPushButton *remove = nullptr;
    QPushButton *reset = nullptr;
    int m_serial = 0;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_model_updates
