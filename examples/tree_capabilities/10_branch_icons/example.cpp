#include "example.h"
#include <QIcon>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QStyle>

namespace tree_branch_icons {

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
        auto *description = new QLabel(QStringLiteral("10 分支图标：替换展开/收起图标，调整大小与颜色；隐藏图标时保留缩进。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        custom = new QCheckBox(QStringLiteral("替换图标"), this);
        custom->setChecked(false);
        controls->addWidget(custom);
        size = new QSpinBox(this);
        size->setRange(1, 36);
        size->setValue(8);
        size->setPrefix(QStringLiteral("图标大小 "));
        controls->addWidget(size);
        tint = new QCheckBox(QStringLiteral("蓝色着色"), this);
        tint->setChecked(false);
        controls->addWidget(tint);
        visible = new QCheckBox(QStringLiteral("显示图标"), this);
        visible->setChecked(true);
        controls->addWidget(visible);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 8; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf)
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

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        connect(custom, &QCheckBox::toggled, this, [this](bool enabled) {
            // Null icons restore the built-in triangle for each state independently.
            m_view->setExpandedBranchIcon(enabled ? style()->standardIcon(QStyle::SP_TitleBarShadeButton) : QIcon());
            m_view->setCollapsedBranchIcon(enabled ? style()->standardIcon(QStyle::SP_TitleBarUnshadeButton) : QIcon());
        });
        connect(size, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeView::setBranchIconSize);
        connect(tint, &QCheckBox::toggled, this, [this](bool enabled) {
            // An invalid color restores the icon's own colors / palette text color.
            m_view->setBranchIconColor(enabled ? QColor(QStringLiteral("#2676c8")) : QColor());
        });
        connect(visible, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setBranchIndicatorsVisible);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *custom = nullptr;
    QSpinBox *size = nullptr;
    QCheckBox *tint = nullptr;
    QCheckBox *visible = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_branch_icons
