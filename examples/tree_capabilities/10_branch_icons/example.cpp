#include "ui_branch_icons.h"
#include <memory>
#include "example.h"
#include <QIcon>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeBranchIconsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeBranchIconsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->size->setValue(viv::kBranchIndicatorSize);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
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
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        connect(ui->custom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            // One icon is reused through a 90-degree rotation on the expansion timeline.
            // Two icons switch immediately; clearing both restores the built-in rotation.
            ui->view->setExpandedBranchIcon(mode == 1 || mode == 3
                ? style()->standardIcon(QStyle::SP_ArrowDown) : QIcon());
            ui->view->setCollapsedBranchIcon(mode == 2 || mode == 3
                ? style()->standardIcon(QStyle::SP_ArrowRight) : QIcon());
        });
        connect(ui->size, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualTreeView::setBranchIconSize);
        connect(ui->branchWidth, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualTreeView::setIndentation);
        // Hide the icon cell independently of hierarchy indentation and icon settings.
        connect(ui->hideBranch, &QCheckBox::toggled, this, [this](bool hidden) {
            ui->view->setBranchAreaVisible(!hidden);
        });
        ui->view->setIndentation(ui->branchWidth->value());
        connect(ui->tint, &QCheckBox::toggled, this, [this](bool enabled) {
            // An invalid color restores the icon's own colors / palette text color.
            ui->view->setBranchIconColor(enabled ? QColor(QStringLiteral("#2676c8")) : QColor());
        });
        connect(ui->visible, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setBranchIndicatorsVisible);

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
                ui->view->setBranchIndicatorAlignment(alignments[index]);
        };
        const auto applyPadding = [this] {
            ui->view->setBranchIndicatorPadding(
                QMargins(ui->paddingLeft->value(), ui->paddingTop->value(),
                         ui->paddingRight->value(), ui->paddingBottom->value()));
        };
        connect(ui->alignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyAlignment);
        for (QSpinBox *spin : {ui->paddingLeft, ui->paddingTop, ui->paddingRight, ui->paddingBottom})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, applyPadding);
        connect(ui->resetLayout, &QPushButton::clicked, this,
                [this, applyAlignment, applyPadding] {
                    ui->alignment->setCurrentIndex(4);
                    ui->paddingLeft->setValue(0);
                    ui->paddingTop->setValue(0);
                    ui->paddingRight->setValue(0);
                    ui->paddingBottom->setValue(0);
                    // setValue()/setCurrentIndex() do not emit when the value is
                    // already current, so apply the defaults explicitly.
                    applyAlignment(ui->alignment->currentIndex());
                    applyPadding();
                });
        applyAlignment(ui->alignment->currentIndex());
        applyPadding();
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_branch_icons
