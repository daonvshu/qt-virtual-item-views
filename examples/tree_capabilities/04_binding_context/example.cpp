#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace tree_binding_context {
struct GroupData { QString name; int count = 0; };
struct RecordData { int id = 0; QString name; };
}
Q_DECLARE_METATYPE(tree_binding_context::GroupData)
Q_DECLARE_METATYPE(tree_binding_context::RecordData)

namespace tree_binding_context {

class PayloadAdapter : public viv::WidgetAdapter
{
public:
    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.data(viv::kDefaultItemDataRole).canConvert<GroupData>() ? 1 : 2;
    }
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType type, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        auto *label = static_cast<QLabel *>(widget);
        // rowData belongs to this node; it is not inherited from its parent.
        if (type == 1) {
            const auto group = context.rowData.value<GroupData>();
            label->setText(QStringLiteral("%1 / %2 个节点 / depth=%3")
                .arg(group.name).arg(group.count).arg(context.depth));
        } else {
            const auto node = context.rowData.value<RecordData>();
            label->setText(QStringLiteral("ID=%1 / %2 / depth=%3")
                .arg(node.id).arg(node.name).arg(context.depth));
        }
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(480, 36); }
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
        auto *description = new QLabel(QStringLiteral("04 整节点数据：不同层级分别存结构体，bind 直接读取 rowData 和 depth。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        change = new QPushButton(QStringLiteral("更新第一个记录"), this);
        controls->addWidget(change);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem;
            branch->setData(QVariant::fromValue(GroupData{QStringLiteral("分组 %1").arg(group + 1), 20}),
                            viv::kDefaultItemDataRole);
            m_model->appendRow(branch);
            for (int row = 0; row < 20; ++row) {
                auto *node = new QStandardItem;
                node->setData(QVariant::fromValue(RecordData{group * 100 + row,
                    QStringLiteral("记录 %1").arg(row + 1)}), viv::kDefaultItemDataRole);
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new PayloadAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Set a different role here when the application's payload uses another role.
        m_view->setItemDataRole(viv::kDefaultItemDataRole);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        connect(change, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_model->index(0, 0, m_model->index(0, 0));
            auto record = index.data(viv::kDefaultItemDataRole).value<RecordData>();
            record.name += QStringLiteral(" *");
            // setData emits dataChanged, which rebuilds the binding context automatically.
            m_model->setData(index, QVariant::fromValue(record), viv::kDefaultItemDataRole);
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *change = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_binding_context
