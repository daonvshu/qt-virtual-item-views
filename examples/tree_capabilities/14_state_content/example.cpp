#include "ui_state_content.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeStateContentPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeStateContentPage> ui;

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
            for (int row = 0; row < 80; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new ActionAdapter(*ui->view, *ui->status), true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        ui->view->setVisualStateCornerRadius(6);
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_state_content
