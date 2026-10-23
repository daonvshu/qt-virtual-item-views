#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("09 缩进：格宽控制每级缩进；关闭叶子缩进后，全叶子兄弟组直接对齐父项内容。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        width = new QSpinBox(this);
        width->setRange(0, 60);
        width->setValue(24);
        width->setPrefix(QStringLiteral("分支格宽 "));
        controls->addWidget(width);
        leaves = new QCheckBox(QStringLiteral("缩进叶子项"), this);
        leaves->setChecked(true);
        controls->addWidget(leaves);
        icons = new QCheckBox(QStringLiteral("显示分支图标"), this);
        icons->setChecked(true);
        controls->addWidget(icons);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
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
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // One indentation cell is reserved per retained level, independently of icon size.
        m_view->setIndentation(24);
        // True keeps each leaf's own branch cell. False only omits all-leaf sibling groups.
        m_view->setIndentLeafItems(true);
        m_view->expandRecursively(m_model->index(0, 0));
        connect(width, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeView::setIndentation);
        connect(leaves, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setIndentLeafItems);
        connect(icons, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setBranchIndicatorsVisible);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSpinBox *width = nullptr;
    QCheckBox *leaves = nullptr;
    QCheckBox *icons = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_indentation
