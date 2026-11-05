#include "ui_indentation.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>

namespace tree_indentation {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeIndentationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeIndentationPage> ui;

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
        for (int group = 0; group < 4; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 6; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 3; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // One indentation cell is reserved per retained level, independently of icon size.
        ui->view->setIndentation(24);
        // True keeps each leaf's own branch cell. False only omits all-leaf sibling groups.
        ui->view->setIndentLeafItems(true);
        ui->view->expandRecursively(m_model->index(0, 0));
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualTreeView::setIndentation);
        // Remove each node's icon cell while retaining depth * indentation().
        // The width control still changes the distance between hierarchy levels.
        connect(ui->hideBranch, &QCheckBox::toggled, this, [this](bool hidden) {
            ui->view->setBranchAreaVisible(!hidden);
        });
        connect(ui->leaves, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setIndentLeafItems);
        // This only hides the icon; it preserves the current indentation width.
        connect(ui->icons, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setBranchIndicatorsVisible);
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_indentation
