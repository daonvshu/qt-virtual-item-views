#include "example.h"
#include <memory>
#include "ui_auto_height.h"

#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QLabel>
#include <QScrollBar>
#include <QStandardItemModel>

namespace table_auto_height {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableAutoHeightExample)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableAutoHeightExample> ui;

    void setupUi()
    {
        ui->setupUi(this);
        // The Designer page embeds this view in a widget-resizable QScrollArea.
        // When auto height leaves no internal scroll range, wheel events reach the outer area.
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
        m_view = new viv::VirtualTableView(ui->previewHost);
        m_view->setMinimumWidth(260);
        // Cell mode uses the same adaptive-height path as row widgets.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        m_view->setHeaderHeight(36);
        // AsNeeded leaves the viewport unscrollable until its content actually overflows.
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_view->setFrameBorderWidth(1);
        m_view->setFrameBorderColor(QColor(QStringLiteral("#879bb4")));
        m_view->setVisualStateCornerRadius(6);
        m_model = new QStandardItemModel(m_view);
        m_view->setModel(m_model);
        // Auto height fixes the view's height; spare space belongs to the sibling.
        ui->previewLayout->insertWidget(0, m_view);
        ui->previewLayout->setStretch(1, 1);
        applyOptions();
        populateModel();

        for (QSpinBox *spin : {ui->maxHeightSpin, ui->parentLimitSpin, ui->heightSpin, ui->spacingSpin})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        for (QCheckBox *check : {ui->enabledCheck, ui->headersCheck, ui->wideColumnsCheck, ui->autoHideTrailingLineCheck})
            connect(check, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
        connect(ui->rowCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { populateModel(); });
        // A queued observer reads geometry after content and scrollbar updates settle.
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated,
                this, [this]() { updateStatus(); }, Qt::QueuedConnection);
        updateStatus();
    }

    void applyOptions()
    {
        // One inherited switch works for lists, trees, tables and tree tables.
        // It fixes the content height independently of available parent layout space.
        m_view->setAutoHeightEnabled(ui->enabledCheck->isChecked());
        // Disabling auto hide keeps the terminal line even when the view fits its content.
        m_view->setTrailingRowGridLineAutoHideEnabled(ui->autoHideTrailingLineCheck->isChecked());
        // The independent auto-height limit includes content and decorations; zero means unlimited.
        const int maximum = ui->maxHeightSpin->value();
        m_view->setAutoHeightMaximumHeight(maximum);
        // This only limits the parent. Handling insufficient parent space belongs to the application.
        const int parentMaximum = ui->parentLimitSpin->value();
        ui->previewHost->setMaximumHeight(parentMaximum > 0 ? parentMaximum : QWIDGETSIZE_MAX);
        // Layout extents include row spacing and reflect uniform-height changes automatically.
        m_view->setUniformItemHeight(ui->heightSpin->value());
        m_view->setRowSpacing(ui->spacingSpin->value());
        // Header visibility and horizontal scrollbar height are included in the preferred height.
        m_view->setHorizontalHeaderVisible(ui->headersCheck->isChecked());
        m_view->setDefaultColumnWidth(ui->wideColumnsCheck->isChecked() ? 420 : 150);
    }

    void populateModel()
    {
        m_model->setColumnCount(3);
        m_model->setRowCount(ui->rowCountSpin->value());
        for (int column = 0; column < 3; ++column) {
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
            for (int row = 0; row < m_model->rowCount(); ++row)
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
    }

    void updateStatus()
    {
        ui->statusLabel->setText(QStringLiteral(
            "实际高度 %1 px | 建议高度 %2 px | 内容 %3 px | 视口 %4 px | 竖向滚动范围 %5")
            .arg(m_view->height()).arg(m_view->sizeHint().height()).arg(m_view->contentExtent())
            .arg(m_view->viewport()->height()).arg(m_view->verticalScrollBar()->maximum()));
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_auto_height
