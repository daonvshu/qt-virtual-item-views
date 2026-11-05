#include "ui_lazy_loading.h"
#include <memory>
#include "example.h"
#include <QPersistentModelIndex>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>

namespace tree_lazy_loading {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeLazyLoadingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeLazyLoadingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 8; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("待加载分组 %1").arg(group + 1));
            auto *placeholder = new QStandardItem(QStringLiteral("展开后加载"));
            placeholder->setFlags(Qt::NoItemFlags);
            // A placeholder makes rowCount nonzero, so the view can offer an expansion indicator.
            branch->appendRow(placeholder);
            m_model->appendRow(branch);
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        connect(ui->view, &viv::VirtualTreeView::expanded, this, [this](const QModelIndex &index) {
            loadChildren(index);
        });
        connect(ui->load, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (index.isValid() && !index.parent().isValid()) {
                ui->view->expand(index);
                loadChildren(index);
            }
        });
        ui->view->setCurrentIndex(m_model->index(0, 0));
    }

    void loadChildren(const QModelIndex &index)
    {
        constexpr int LoadStateRole = Qt::UserRole + 1;
        if (!index.isValid() || index.parent().isValid() || index.data(LoadStateRole).toInt() != 0)
            return;
        m_model->setData(index, 1, LoadStateRole);
        m_model->setData(m_model->index(0, 0, index), QStringLiteral("加载中..."));
        const QPersistentModelIndex target(index);
        // This timer simulates a completed request on the GUI thread. Do not mutate Qt models on workers.
        QTimer::singleShot(600, this, [this, target] {
            // The node may have been deleted/reset while the request was in flight.
            if (!target.isValid() || target.model() != m_model)
                return;
            auto *branch = m_model->itemFromIndex(target);
            // Insert the loaded nodes before removing the placeholder, keeping the branch expandable.
            for (int row = 0; row < 20; ++row)
                branch->appendRow(new QStandardItem(QStringLiteral("已加载节点 %1.%2")
                    .arg(target.row() + 1).arg(row + 1)));
            branch->removeRow(0);
            m_model->setData(target, 2, Qt::UserRole + 1);
            ui->status->setText(QStringLiteral("%1：已加载 20 个子节点").arg(target.data().toString()));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_lazy_loading
