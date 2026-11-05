#include "example.h"
#include <memory>
#include "ui_auto_height.h"

#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QLabel>
#include <QScrollBar>
#include <QStandardItemModel>

namespace treetable_auto_height {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeTableAutoHeightExample)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeTableAutoHeightExample> ui;

    void setupUi()
    {
        ui->setupUi(this);
        ui->pageLayout->setStretch(ui->pageLayout->count() - 1, 1);
        // This expanding sibling makes unused vertical layout space visible.
        QPalette colors = ui->remainingLabel->palette();
        colors.setColor(QPalette::Window, QColor(QStringLiteral("#edf4ff")));
        ui->remainingLabel->setPalette(colors);
        ui->remainingLabel->setAutoFillBackground(true);
        ui->previewLayout->setStretch(0, 1);
    }

    void setupExample()
    {
        m_view = new viv::VirtualTreeTableView(ui->previewHost);
        m_view->setMinimumWidth(260);
        // The view takes ownership of this chapter's independent adapter.
        // CellWidgets applies tree indentation only to logical column zero.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        m_view->setDefaultColumnWidth(150);
        // AsNeeded leaves the viewport unscrollable until its content actually overflows.
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_view->setFrameBorderWidth(1);
        m_view->setFrameBorderColor(QColor(QStringLiteral("#879bb4")));
        m_view->setVisualStateCornerRadius(6);
        m_model = new QStandardItemModel(m_view);
        m_view->setModel(m_model);
        // No stretch or vertical alignment on the view: its size policy supplies the upper bound.
        ui->previewLayout->insertWidget(0, m_view);
        ui->previewLayout->setStretch(1, 1);
        applyOptions();
        populateModel();

        for (QSpinBox *spin : {ui->maxHeightSpin, ui->parentLimitSpin, ui->heightSpin, ui->spacingSpin})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        for (QCheckBox *check : {ui->enabledCheck, ui->animationCheck, ui->autoHideTrailingLineCheck})
            connect(check, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
        connect(ui->rowCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { populateModel(); });
        connect(ui->childCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { populateModel(); });
        connect(ui->expandButton, &QPushButton::clicked, this, [this]() { expandAll(); });
        connect(ui->collapseButton, &QPushButton::clicked, m_view, &viv::VirtualTreeTableView::collapseAll);
        // A queued observer reads geometry after the parent has applied the new size hint.
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated,
                this, [this]() { updateStatus(); }, Qt::QueuedConnection);
        updateStatus();
    }

    void applyOptions()
    {
        // One inherited switch works for lists, trees, tables and tree tables.
        // It updates size hints and lets the parent layout grant or reduce space.
        m_view->setAutoHeightEnabled(ui->enabledCheck->isChecked());
        // Disabling auto hide keeps the terminal line even when the view fits its content.
        m_view->setTrailingRowGridLineAutoHideEnabled(ui->autoHideTrailingLineCheck->isChecked());
        // Reuse QWidget's normal maximum height; zero in this UI means no explicit limit.
        const int maximum = ui->maxHeightSpin->value();
        m_view->setMaximumHeight(maximum > 0 ? maximum : QWIDGETSIZE_MAX);
        // A separate parent limit demonstrates shrinking to the available layout budget.
        const int parentMaximum = ui->parentLimitSpin->value();
        ui->previewHost->setMaximumHeight(parentMaximum > 0 ? parentMaximum : QWIDGETSIZE_MAX);
        // Layout extents include row spacing and reflect uniform-height changes automatically.
        m_view->setUniformItemHeight(ui->heightSpin->value());
        m_view->setRowSpacing(ui->spacingSpin->value());
        // Expansion updates live node geometry and the adaptive outer height together.
        m_view->setExpansionAnimationEnabled(ui->animationCheck->isChecked());
    }

    void populateModel()
    {
        m_model->clear();
        m_model->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("状态")});
        // Set per-column geometry after the model has established its column schema.
        m_view->setColumnWidth(0, 280);
        for (int group = 0; group < ui->rowCountSpin->value(); ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow({branch, new QStandardItem(QStringLiteral("分组")), new QStandardItem(QStringLiteral("就绪"))});
            for (int row = 0; row < ui->childCountSpin->value(); ++row)
                branch->appendRow({new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1)),
                    new QStandardItem(QStringLiteral("记录")), new QStandardItem(QStringLiteral("正常"))});
        }
        // Prepare the dataset immediately; later user gestures keep the configured animation.
        const bool animated = m_view->expansionAnimationEnabled();
        m_view->setExpansionAnimationEnabled(false);
        expandAll();
        m_view->setExpansionAnimationEnabled(animated);
    }

    void expandAll()
    {
        // Only expanded paths contribute to the preferred height; no hidden subtree is measured.
        for (int row = 0; row < m_model->rowCount(); ++row)
            m_view->expandRecursively(m_model->index(row, 0));
    }

    void updateStatus()
    {
        ui->statusLabel->setText(QStringLiteral(
            "实际高度 %1 px | 建议高度 %2 px | 内容 %3 px | 视口 %4 px | 竖向滚动范围 %5")
            .arg(m_view->height()).arg(m_view->sizeHint().height()).arg(m_view->contentExtent())
            .arg(m_view->viewport()->height()).arg(m_view->verticalScrollBar()->maximum()));
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_auto_height
