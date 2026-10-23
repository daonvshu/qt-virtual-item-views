#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace tree_state_content {

class ActionRow : public QWidget
{
public:
    explicit ActionRow(QWidget *parent) : QWidget(parent) { setupUi(); }
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
class ActionAdapter : public viv::WidgetAdapter
{
public:
    explicit ActionAdapter(viv::VirtualTreeView &view, QLabel &status) : m_view(view), m_status(status) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new ActionRow(parent);
        QObject::connect(row->action, &QPushButton::clicked, row, [this, row] {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_status.setText(QStringLiteral("事件当前绑定：%1 / parent=%2")
                    .arg(index.data().toString()).arg(index.parent().data().toString()));
        });
        return row;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ActionRow *>(widget)->title->setText(index.data().toString());
    }
    void visualStateChanged(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        const auto state = m_view.visualState(index);
        // Visual state controls auxiliary content; it does not change business check data.
        static_cast<ActionRow *>(widget)->action->setVisible(state.hovered || state.selected);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<ActionRow *>(widget);
        row->title->clear();
        row->action->hide();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(480, 40); }
private:
    viv::VirtualTreeView &m_view;
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
        auto *description = new QLabel(QStringLiteral("14 状态关联内容：仅 hover 或 selected 的节点显示操作按钮，背景仍由框架绘制。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
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
            for (int row = 0; row < 80; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new ActionAdapter(*m_view, *m_status), true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        m_view->setVisualStateCornerRadius(6);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_state_content
