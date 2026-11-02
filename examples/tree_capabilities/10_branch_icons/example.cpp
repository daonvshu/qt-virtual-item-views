#include "example.h"
#include <QIcon>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
        auto *description = new QLabel(QStringLiteral("10 分支图标：单图标随展开动画旋转，双图标立即切换；调整大小、颜色、分支格宽、对齐与四向留白。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        custom = new QComboBox(this);
        custom->addItems({QStringLiteral("内置图标（旋转）"), QStringLiteral("仅展开图标（旋转）"),
                          QStringLiteral("仅收起图标（旋转）"), QStringLiteral("双图标（立即切换）")});
        controls->addWidget(custom);
        size = new QSpinBox(this);
        size->setRange(1, 36);
        size->setValue(viv::kBranchIndicatorSize);
        size->setPrefix(QStringLiteral("图标大小 "));
        controls->addWidget(size);
        branchWidth = new QSpinBox(this);
        branchWidth->setRange(0, 60);
        branchWidth->setValue(20);
        branchWidth->setPrefix(QStringLiteral("分支格宽 "));
        controls->addWidget(branchWidth);
        tint = new QCheckBox(QStringLiteral("蓝色着色"), this);
        tint->setChecked(false);
        controls->addWidget(tint);
        visible = new QCheckBox(QStringLiteral("显示图标"), this);
        visible->setChecked(true);
        controls->addWidget(visible);
        controls->addStretch();
        layout->addLayout(controls);

        auto *indicatorLayout = new QHBoxLayout;
        indicatorLayout->addWidget(new QLabel(QStringLiteral("分支格对齐"), this));
        alignment = new QComboBox(this);
        alignment->addItems({QStringLiteral("左上"), QStringLiteral("上"), QStringLiteral("右上"),
                             QStringLiteral("左"), QStringLiteral("居中"), QStringLiteral("右"),
                             QStringLiteral("左下"), QStringLiteral("下"), QStringLiteral("右下")});
        alignment->setCurrentIndex(4);
        indicatorLayout->addWidget(alignment);
        indicatorLayout->addSpacing(12);
        indicatorLayout->addWidget(new QLabel(QStringLiteral("padding"), this));
        paddingLeft = new QSpinBox(this);
        paddingLeft->setRange(0, 40);
        paddingLeft->setValue(0);
        paddingLeft->setPrefix(QStringLiteral("左 "));
        indicatorLayout->addWidget(paddingLeft);
        paddingTop = new QSpinBox(this);
        paddingTop->setRange(0, 40);
        paddingTop->setPrefix(QStringLiteral("上 "));
        indicatorLayout->addWidget(paddingTop);
        paddingRight = new QSpinBox(this);
        paddingRight->setRange(0, 40);
        paddingRight->setPrefix(QStringLiteral("右 "));
        indicatorLayout->addWidget(paddingRight);
        paddingBottom = new QSpinBox(this);
        paddingBottom->setRange(0, 40);
        paddingBottom->setPrefix(QStringLiteral("下 "));
        indicatorLayout->addWidget(paddingBottom);
        resetLayout = new QPushButton(QStringLiteral("重置对齐/留白"), this);
        indicatorLayout->addWidget(resetLayout);
        hideBranch = new QCheckBox(QStringLiteral("隐藏 branch 区域"), this);
        hideBranch->setToolTip(QStringLiteral("隐藏分支图标及其占位，保留父子层级缩进。双击或方向键仍可展开节点。"));
        indicatorLayout->addWidget(hideBranch);
        indicatorLayout->addStretch();
        layout->addLayout(indicatorLayout);
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

        connect(custom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            // One icon is reused through a 90-degree rotation on the expansion timeline.
            // Two icons switch immediately; clearing both restores the built-in rotation.
            m_view->setExpandedBranchIcon(mode == 1 || mode == 3
                ? style()->standardIcon(QStyle::SP_ArrowDown) : QIcon());
            m_view->setCollapsedBranchIcon(mode == 2 || mode == 3
                ? style()->standardIcon(QStyle::SP_ArrowRight) : QIcon());
        });
        connect(size, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeView::setBranchIconSize);
        connect(branchWidth, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeView::setIndentation);
        // Hide the icon cell independently of hierarchy indentation and icon settings.
        connect(hideBranch, &QCheckBox::toggled, this, [this](bool hidden) {
            m_view->setBranchAreaVisible(!hidden);
        });
        m_view->setIndentation(branchWidth->value());
        connect(tint, &QCheckBox::toggled, this, [this](bool enabled) {
            // An invalid color restores the icon's own colors / palette text color.
            m_view->setBranchIconColor(enabled ? QColor(QStringLiteral("#2676c8")) : QColor());
        });
        connect(visible, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setBranchIndicatorsVisible);

        // The default renderer places the icon in its branch cell. These controls
        // expose the nine alignments and the four-sided padding.
        const auto applyAlignment = [this](int index) {
            static const Qt::Alignment alignments[] = {
                Qt::AlignLeft | Qt::AlignTop,
                Qt::AlignHCenter | Qt::AlignTop,
                Qt::AlignRight | Qt::AlignTop,
                Qt::AlignLeft | Qt::AlignVCenter,
                Qt::AlignCenter,
                Qt::AlignRight | Qt::AlignVCenter,
                Qt::AlignLeft | Qt::AlignBottom,
                Qt::AlignHCenter | Qt::AlignBottom,
                Qt::AlignRight | Qt::AlignBottom,
            };
            if (index >= 0 && index < int(sizeof(alignments) / sizeof(alignments[0])))
                m_view->setBranchIndicatorAlignment(alignments[index]);
        };
        const auto applyPadding = [this] {
            m_view->setBranchIndicatorPadding(
                QMargins(paddingLeft->value(), paddingTop->value(),
                         paddingRight->value(), paddingBottom->value()));
        };
        connect(alignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyAlignment);
        for (QSpinBox *spin : {paddingLeft, paddingTop, paddingRight, paddingBottom})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, applyPadding);
        connect(resetLayout, &QPushButton::clicked, this,
                [this, applyAlignment, applyPadding] {
                    alignment->setCurrentIndex(4);
                    paddingLeft->setValue(0);
                    paddingTop->setValue(0);
                    paddingRight->setValue(0);
                    paddingBottom->setValue(0);
                    // setValue()/setCurrentIndex() do not emit when the value is
                    // already current, so apply the defaults explicitly.
                    applyAlignment(alignment->currentIndex());
                    applyPadding();
                });
        applyAlignment(alignment->currentIndex());
        applyPadding();
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *custom = nullptr;
    QSpinBox *size = nullptr;
    QSpinBox *branchWidth = nullptr;
    QCheckBox *tint = nullptr;
    QCheckBox *visible = nullptr;
    QCheckBox *hideBranch = nullptr;
    QComboBox *alignment = nullptr;
    QSpinBox *paddingLeft = nullptr;
    QSpinBox *paddingTop = nullptr;
    QSpinBox *paddingRight = nullptr;
    QSpinBox *paddingBottom = nullptr;
    QPushButton *resetLayout = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_branch_icons
