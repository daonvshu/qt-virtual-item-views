#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QFont>

namespace tree_widget_types {

constexpr int KindRole = Qt::UserRole + 1;
constexpr viv::WidgetType GroupType = 1;
constexpr viv::WidgetType ProgressType = 2;
class TypedAdapter : public viv::WidgetAdapter
{
public:
    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.data(KindRole).toInt();
    }
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == ProgressType) {
            auto *bar = new QProgressBar(parent);
            bar->setRange(0, 100);
            return bar;
        }
        auto *label = new QLabel(parent);
        label->setMargin(8);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        return label;
    }
    void bindWidget(viv::WidgetType type, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        // Use the stored recycler type, not a fresh classification of the index.
        if (type == ProgressType) {
            auto *bar = static_cast<QProgressBar *>(widget);
            bar->setValue(context.index.data(Qt::UserRole + 2).toInt());
            bar->setFormat(context.index.data().toString() + QStringLiteral(" %p%"));
        } else {
            static_cast<QLabel *>(widget)->setText(context.index.data().toString());
        }
        widget->setToolTip(QStringLiteral("type=%1 / depth=%2").arg(type).arg(context.depth));
    }
    void unbindWidget(viv::WidgetType type, QWidget *widget, const QModelIndex &) override
    {
        if (type == ProgressType) {
            auto *bar = static_cast<QProgressBar *>(widget);
            bar->setValue(0);
            bar->resetFormat();
        } else {
            static_cast<QLabel *>(widget)->clear();
        }
        widget->setToolTip(QString());
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
        auto *description = new QLabel(QStringLiteral("03 节点类型：分组用文本，叶子用进度条，复用池按 WidgetType 隔离。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 8; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 25; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        for (int group = 0; group < m_model->rowCount(); ++group) {
            auto *branch = m_model->item(group);
            branch->setData(GroupType, KindRole);
            for (int row = 0; row < branch->rowCount(); ++row) {
                branch->child(row)->setData(ProgressType, KindRole);
                branch->child(row)->setData((group * 11 + row * 7) % 101, Qt::UserRole + 2);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new TypedAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_widget_types
