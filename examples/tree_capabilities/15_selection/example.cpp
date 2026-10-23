#include "example.h"
#include <QItemSelectionModel>
#include <QStringList>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("15 选择：节点身份和当前项独立；程序选择可跨父节点，扩展选择支持 Ctrl/Shift。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        mode = new QComboBox(this);
        mode->addItems({QStringLiteral("NoSelection"), QStringLiteral("SingleSelection"), QStringLiteral("MultiSelection"), QStringLiteral("ExtendedSelection")});
        controls->addWidget(mode);
        select = new QPushButton(QStringLiteral("程序选择多个节点"), this);
        controls->addWidget(select);
        clear = new QPushButton(QStringLiteral("清空选择"), this);
        controls->addWidget(clear);
        query = new QPushButton(QStringLiteral("查询当前和选中"), this);
        controls->addWidget(query);
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
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 12; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        // Tree rows have one logical column. The selection model retains offscreen identities.
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setSelectionMode(static_cast<viv::VirtualItemView::SelectionMode>(value));
        });
        mode->setCurrentIndex(3);
        connect(select, &QPushButton::clicked, this, [this] {
            const QModelIndex parent = m_model->index(0, 0);
            // Select a range in one parent and another node in a different parent explicitly.
            QItemSelection selection(m_model->index(1, 0, parent), m_model->index(4, 0, parent));
            selection.select(m_model->index(1, 0), m_model->index(1, 0));
            m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
            m_view->selectionModel()->setCurrentIndex(m_model->index(4, 0, parent), QItemSelectionModel::NoUpdate);
        });
        connect(clear, &QPushButton::clicked, this, [this] { m_view->selectionModel()->clearSelection(); });
        connect(query, &QPushButton::clicked, this, [this] { reportSelection(); });
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { reportSelection(); });
        connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { reportSelection(); });
    }

    void reportSelection()
    {
        QStringList names;
        for (const QModelIndex &index : m_view->selectionModel()->selectedIndexes())
            names.append(index.data().toString());
        m_status->setText(QStringLiteral("current=%1 / selected=[%2]")
            .arg(m_view->currentIndex().data().toString()).arg(names.join(QStringLiteral(", "))));
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *mode = nullptr;
    QPushButton *select = nullptr;
    QPushButton *clear = nullptr;
    QPushButton *query = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_selection
