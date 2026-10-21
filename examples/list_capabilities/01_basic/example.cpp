#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace list_basic {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        // Creation is needed only when the recycler has no compatible widget.
        // Use the supplied parent so the view controls the widget lifetime.
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // The same widget can serve many indexes; replace all item-dependent content.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        // Detach the old item before reuse. Real adapters also stop timers,
        // async requests and subscriptions associated with that item here.
        static_cast<QLabel *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
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
        layout->addWidget(new QLabel(QStringLiteral("01 基础列表：切换显示模型根节点或某个父节点的直接子项。"), this));
        m_root = new QComboBox(this);
        m_root->addItems({QStringLiteral("模型根节点"), QStringLiteral("分组 A 的子项"), QStringLiteral("分组 B 的子项")});
        layout->addWidget(m_root);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(m_view);
        for (const auto &name : {QStringLiteral("分组 A"), QStringLiteral("分组 B")}) {
            auto *group = new QStandardItem(name);
            for (int row = 0; row < 80; ++row)
                group->appendRow(new QStandardItem(QStringLiteral("%1 / 条目 %2").arg(name).arg(row + 1)));
            m_model->appendRow(group);
        }
        // setAdapter installs the row-widget lifecycle. The second argument
        // transfers adapter ownership to the view; it will be destroyed after
        // the view has finished unbinding its rows.
        m_view->setAdapter(new TextAdapter, true);
        // Uniform mode avoids measuring every row when all rows have the same height.
        m_view->setUniformItemHeight(36);
        // setModel starts materialization. Model notifications update the view
        // automatically; there is no reload() call in the business code.
        // setModel does not take ownership: this model is owned through its
        // QObject parent, assigned when QStandardItemModel(m_view) was constructed.
        m_view->setModel(m_model);
        connect(m_root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int root) {
            // An invalid root means the model's invisible root. A valid root
            // makes the list show that item's direct children.
            m_view->setRootIndex(root == 0 ? QModelIndex() : m_model->index(root - 1, 0));
        });
    }

    viv::VirtualListView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *m_root = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_basic
