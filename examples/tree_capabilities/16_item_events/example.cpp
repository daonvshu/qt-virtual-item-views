#include "example.h"
#include <QStringList>
#include <QPersistentModelIndex>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("16 行事件：按钮使用每次绑定更新的持久索引；查询屏外项的索引、控件和几何各不相同。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        query = new QPushButton(QStringLiteral("查询当前和选中项"), this);
        controls->addWidget(query);
        activate = new QPushButton(QStringLiteral("程序触发 activated"), this);
        controls->addWidget(activate);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 100; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new EventAdapter(*m_status), true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        m_view->setCurrentIndex(m_model->index(0, 0, m_model->index(0, 0)));
        connect(query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            const QRect rect = m_view->visualRect(index);
            QWidget *widget = m_view->widgetForIndex(index);
            const QModelIndex hit = m_view->indexAt(rect.center());
            QStringList selected;
            for (const QModelIndex &node : m_view->selectionModel()->selectedIndexes())
                selected.append(node.data().toString());
            m_status->setText(QStringLiteral("current=%1 / selected=[%2] / widget=%3 / rect=(%4,%5 %6x%7) / hit=%8")
                .arg(index.data().toString()).arg(selected.join(QStringLiteral(", ")))
                .arg(widget ? QStringLiteral("已物化") : QStringLiteral("未物化"))
                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(hit.data().toString()));
        });
        connect(m_view, &viv::VirtualItemView::doubleClicked, this, [this](const QModelIndex &index) {
            m_status->setText(QStringLiteral("doubleClicked: %1").arg(index.data().toString()));
        });
        connect(m_view, &viv::VirtualItemView::activated, this, [this](const QModelIndex &index) {
            m_status->setText(QStringLiteral("activated: %1").arg(index.data().toString()));
        });
        connect(activate, &QPushButton::clicked, this, [this] {
            m_view->activateIndex(m_view->currentIndex());
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *query = nullptr;
    QPushButton *activate = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_item_events
