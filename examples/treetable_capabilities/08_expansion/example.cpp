#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace treetable_expansion {

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

// Each chapter owns this helper; no implementation is shared with other pages.
// Only column zero owns children, and every child row supplies the full column schema.
static void appendChildren(QStandardItemModel *model)
{
    const int columns = model->columnCount();
    for (int group = 0; group < model->rowCount(); ++group) {
        auto *branch = model->item(group, 0);
        branch->removeRows(0, branch->rowCount());
        branch->setColumnCount(columns);
        for (int row = 0; row < 8; ++row) {
            QList<QStandardItem *> cells;
            for (int column = 0; column < columns; ++column)
                cells.append(new QStandardItem(QStringLiteral("节点 %1.%2 / 列 %3")
                    .arg(group + 1).arg(row + 1).arg(column + 1)));
            branch->appendRow(cells);
            if (row == 0) {
                for (int leaf = 0; leaf < 3; ++leaf) {
                    QList<QStandardItem *> leaves;
                    for (int column = 0; column < columns; ++column)
                        leaves.append(new QStandardItem(QStringLiteral("叶子 %1.%2.%3 / 列 %4")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1).arg(column + 1)));
                    cells.first()->appendRow(leaves);
                }
            }
        }
    }
}

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
        auto *description = new QLabel(QStringLiteral("08 展开收起与淡入淡出：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        open = new QPushButton(QStringLiteral("展开"), this);
        controls->addWidget(open);
        close = new QPushButton(QStringLiteral("收起"), this);
        controls->addWidget(close);
        toggle = new QPushButton(QStringLiteral("切换"), this);
        controls->addWidget(toggle);
        recursive = new QPushButton(QStringLiteral("递归展开"), this);
        controls->addWidget(recursive);
        all = new QPushButton(QStringLiteral("全部收起"), this);
        controls->addWidget(all);
        controls->addStretch();
        layout->addLayout(controls);
        auto *options = new QHBoxLayout;
        animated = new QCheckBox(QStringLiteral("动画"), this);
        animated->setChecked(true);
        options->addWidget(animated);
        fade = new QCheckBox(QStringLiteral("子节点淡入淡出"), this);
        fade->setChecked(true);
        options->addWidget(fade);
        duration = new QSpinBox(this);
        duration->setRange(0, 1000);
        duration->setValue(300);
        duration->setPrefix(QStringLiteral("时长 ms "));
        options->addWidget(duration);
        options->addStretch();
        layout->addLayout(options);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A node is identified by its column-zero index, never by its visible row number.
        m_model = new QStandardItemModel(6, 6, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column) {
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
            for (int row = 0; row < m_model->rowCount(); ++row)
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("分组 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
        appendChildren(m_model);

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

        // Expansion animation is enabled by default; it changes visuals, not model identities.
        m_view->setExpansionAnimationDuration(300);
        // Fading is enabled by default. The checkbox calls setExpansionFadeEnabled(false)
        // to disable it without disabling the live height transition.
        fade->setChecked(m_view->expansionFadeEnabled());
        m_view->setCurrentIndex(m_model->index(0, 0));
        connect(open, &QPushButton::clicked, this, [this] { m_view->expand(m_view->currentIndex()); });
        connect(close, &QPushButton::clicked, this, [this] { m_view->collapse(m_view->currentIndex()); });
        connect(toggle, &QPushButton::clicked, this, [this] { m_view->toggleExpanded(m_view->currentIndex()); });
        connect(recursive, &QPushButton::clicked, this, [this] { m_view->expandRecursively(m_view->currentIndex()); });
        connect(animated, &QCheckBox::toggled, m_view, &viv::VirtualTreeTableView::setExpansionAnimationEnabled);
        connect(fade, &QCheckBox::toggled, m_view, &viv::VirtualTreeTableView::setExpansionFadeEnabled);
        connect(animated, &QCheckBox::toggled, fade, &QWidget::setEnabled);
        connect(duration, QOverload<int>::of(&QSpinBox::valueChanged), m_view,
                &viv::VirtualTreeTableView::setExpansionAnimationDuration);
        const auto report = [this](const QModelIndex &index) {
            m_status->setText(QStringLiteral("%1 / expanded=%2 / hasChildren=%3 / 可见树节点=%4")
                .arg(index.data().toString()).arg(m_view->isExpanded(index))
                .arg(m_view->hasChildren(index)).arg(m_view->visibleRowCount()));
        };
        connect(m_view, &viv::VirtualTreeTableView::expanded, this, report);
        connect(m_view, &viv::VirtualTreeTableView::collapsed, this, report);
        connect(all, &QPushButton::clicked, this, [this, report] {
            m_view->collapseAll();
            report(m_view->currentIndex());
        });
        // Populate the status before showing the page. An initially empty label
        // grows on the first expanded signal, resizing the tree and ending its animation.
        report(m_view->currentIndex());
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *open = nullptr;
    QPushButton *close = nullptr;
    QPushButton *toggle = nullptr;
    QPushButton *recursive = nullptr;
    QPushButton *all = nullptr;
    QCheckBox *animated = nullptr;
    QCheckBox *fade = nullptr;
    QSpinBox *duration = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_expansion
