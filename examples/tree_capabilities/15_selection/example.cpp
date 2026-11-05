#include "ui_selection.h"
#include <memory>
#include "example.h"
#include <QItemSelectionModel>
#include <QStringList>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>

namespace tree_selection {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeSelectionPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeSelectionPage> ui;

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
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 12; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        // Tree rows have one logical column. The selection model retains offscreen identities.
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setSelectionMode(static_cast<viv::VirtualItemView::SelectionMode>(value));
        });
        ui->mode->setCurrentIndex(3);
        connect(ui->select, &QPushButton::clicked, this, [this] {
            const QModelIndex parent = m_model->index(0, 0);
            // Select a range in one parent and another node in a different parent explicitly.
            QItemSelection selection(m_model->index(1, 0, parent), m_model->index(4, 0, parent));
            selection.select(m_model->index(1, 0), m_model->index(1, 0));
            ui->view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
            ui->view->selectionModel()->setCurrentIndex(m_model->index(4, 0, parent), QItemSelectionModel::NoUpdate);
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] { ui->view->selectionModel()->clearSelection(); });
        connect(ui->query, &QPushButton::clicked, this, [this] { reportSelection(); });
        connect(ui->view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { reportSelection(); });
        connect(ui->view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { reportSelection(); });
    }

    void reportSelection()
    {
        QStringList names;
        for (const QModelIndex &index : ui->view->selectionModel()->selectedIndexes())
            names.append(index.data().toString());
        ui->status->setText(QStringLiteral("current=%1 / selected=[%2]")
            .arg(ui->view->currentIndex().data().toString()).arg(names.join(QStringLiteral(", "))));
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_selection
