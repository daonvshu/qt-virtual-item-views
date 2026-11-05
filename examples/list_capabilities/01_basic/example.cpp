#include "ui_basic.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QStandardItemModel>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListBasicPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListBasicPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(ui->view);
        for (const auto &name : {QStringLiteral("分组 A"), QStringLiteral("分组 B")}) {
            auto *group = new QStandardItem(name);
            for (int row = 0; row < 80; ++row)
                group->appendRow(new QStandardItem(QStringLiteral("%1 / 条目 %2").arg(name).arg(row + 1)));
            m_model->appendRow(group);
        }
        // setAdapter installs the row-widget lifecycle. The second argument
        // transfers adapter ownership to the view; it will be destroyed after
        // the view has finished unbinding its rows.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform mode avoids measuring every row when all rows have the same height.
        ui->view->setUniformItemHeight(36);
        // setModel starts materialization. Model notifications update the view
        // automatically; there is no reload() call in the business code.
        // setModel does not take ownership: this model is owned through its
        // QObject parent, assigned when QStandardItemModel(ui->view) was constructed.
        ui->view->setModel(m_model);
        connect(ui->root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int root) {
            // An invalid root means the model's invisible root. A valid root
            // makes the list show that item's direct children.
            ui->view->setRootIndex(root == 0 ? QModelIndex() : m_model->index(root - 1, 0));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_basic
