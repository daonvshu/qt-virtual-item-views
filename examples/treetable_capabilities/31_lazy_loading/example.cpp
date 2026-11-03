#include "example.h"
#include <QPersistentModelIndex>
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QTimer>

namespace treetable_lazy_loading {

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        // Every bind replaces content from the previous node and logical column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
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
        auto *description = new QLabel(QStringLiteral("31 异步加载子节点：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        load = new QPushButton(QStringLiteral("加载当前分组"), this);
        controls->addWidget(load);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(m_view);
        m_model->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("状态")});
        for (int group = 0; group < 8; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("待加载分组 %1").arg(group + 1));
            auto *placeholder = new QStandardItem(QStringLiteral("展开后加载"));
            placeholder->setFlags(Qt::NoItemFlags);
            // A placeholder makes rowCount nonzero, so the view can offer an expansion indicator.
            branch->appendRow({placeholder, new QStandardItem, new QStandardItem});
            m_model->appendRow({branch, new QStandardItem(QStringLiteral("分组")), new QStandardItem(QStringLiteral("待加载"))});
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        // CellWidgets applies tree indentation only to logical column zero.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        m_view->setDefaultColumnWidth(150);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);
        m_view->setColumnWidth(0, 280);

        connect(m_view, &viv::VirtualTreeTableView::expanded, this, [this](const QModelIndex &index) {
            loadChildren(index);
        });
        connect(load, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex().siblingAtColumn(0);
            if (index.isValid() && !index.parent().isValid()) {
                m_view->expand(index);
                loadChildren(index);
            }
        });
        m_view->setCurrentIndex(m_model->index(0, 0));
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
                branch->appendRow({new QStandardItem(QStringLiteral("已加载节点 %1.%2")
                    .arg(target.row() + 1).arg(row + 1)), new QStandardItem(QStringLiteral("记录")),
                    new QStandardItem(QStringLiteral("已加载"))});
            branch->removeRow(0);
            m_model->setData(target, 2, Qt::UserRole + 1);
            m_status->setText(QStringLiteral("%1：已加载 20 个子节点").arg(target.data().toString()));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *load = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_lazy_loading
