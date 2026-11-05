#include "ui_expansion.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>

namespace tree_expansion {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeExpansionPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeExpansionPage> ui;

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
        for (int group = 0; group < 8; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 8; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 4; ++leaf)
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

        // Expansion animation is enabled by default; it changes visuals, not model identities.
        ui->view->setExpansionAnimationDuration(300);
        // Fading is enabled by default. The checkbox calls setExpansionFadeEnabled(false)
        // to disable it without disabling the live height transition.
        ui->fade->setChecked(ui->view->expansionFadeEnabled());
        ui->view->setCurrentIndex(m_model->index(0, 0));
        connect(ui->open, &QPushButton::clicked, this, [this] { ui->view->expand(ui->view->currentIndex()); });
        connect(ui->close, &QPushButton::clicked, this, [this] { ui->view->collapse(ui->view->currentIndex()); });
        connect(ui->toggle, &QPushButton::clicked, this, [this] { ui->view->toggleExpanded(ui->view->currentIndex()); });
        connect(ui->recursive, &QPushButton::clicked, this, [this] { ui->view->expandRecursively(ui->view->currentIndex()); });
        connect(ui->animated, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setExpansionAnimationEnabled);
        connect(ui->fade, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setExpansionFadeEnabled);
        connect(ui->animated, &QCheckBox::toggled, ui->fade, &QWidget::setEnabled);
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), ui->view,
                &viv::VirtualTreeView::setExpansionAnimationDuration);
        const auto report = [this](const QModelIndex &index) {
            ui->status->setText(QStringLiteral("%1 / expanded=%2 / hasChildren=%3 / 可见树节点=%4")
                .arg(index.data().toString()).arg(ui->view->isExpanded(index))
                .arg(ui->view->hasChildren(index)).arg(ui->view->visibleRowCount()));
        };
        connect(ui->view, &viv::VirtualTreeView::expanded, this, report);
        connect(ui->view, &viv::VirtualTreeView::collapsed, this, report);
        connect(ui->all, &QPushButton::clicked, this, [this, report] {
            ui->view->collapseAll();
            report(ui->view->currentIndex());
        });
        // Populate the status before showing the page. An initially empty label
        // grows on the first expanded signal, resizing the tree and ending its animation.
        report(ui->view->currentIndex());
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_expansion
