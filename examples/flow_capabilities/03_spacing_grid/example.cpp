#include "example.h"
#include "ui_spacing_grid.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_spacing_grid {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setAlignment(Qt::AlignCenter);
        label->setMargin(8);
        label->setObjectName(QStringLiteral("flowCard"));
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Replace all presentation data whenever this widget is reused.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        // Flow uses its cell height configuration instead of measuring each widget.
        return QSize(160, 100);
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
        m_ui.setupUi(this);
    }

    void setupExample()
    {
        // Use a plain one-column model; display columns are calculated by the view.
        m_model = new QStandardItemModel(0, 1, m_ui.view);
        for (int row = 0; row < 17; ++row)
            m_model->appendRow(new QStandardItem(QStringLiteral("项目 %1").arg(row + 1)));
        // The view owns the adapter; the QObject parent owns the model.
        m_ui.view->setAdapter(new TextAdapter, true);
        // Flow has dedicated cell sizing; do not use inherited uniform-row height APIs.
        m_ui.view->setCellWidthRange(100, 200);
        m_ui.view->setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::Fixed);
        m_ui.view->setFixedCellHeight(100);
        // Model signals keep the materialized widgets synchronized.
        m_ui.view->setModel(m_model);

        for (QSpinBox *control : {m_ui.rowGap, m_ui.columnGap, m_ui.margin, m_ui.lineWidth})
            connect(control, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyGrid(); });
        for (QCheckBox *control : {m_ui.horizontal, m_ui.vertical, m_ui.horizontalThrough, m_ui.verticalThrough,
                                  m_ui.spacingColorEnabled, m_ui.trailingLine})
            connect(control, &QCheckBox::toggled, this, [this](bool) { applyGrid(); });
        connect(m_ui.placement, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyGrid(); });
        connect(m_ui.spacingColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyGrid(); });
        connect(m_ui.count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int count) {
            const int previous = m_model->rowCount();
            m_model->setRowCount(count);
            for (int row = previous; row < count; ++row)
                m_model->setData(m_model->index(row, 0), QStringLiteral("项目 %1").arg(row + 1));
        });
        m_ui.status->setText(QStringLiteral("中心模式在四周补半间距，两侧模式不补；半间距也使用间距背景色，设置的内容边距保留在外侧。有底部内容边距时保留末行下边线。"));
        applyGrid();
    }

    void applyGrid()
    {
        // Spacing and margins are logical pixels; line widths below are physical pixels.
        m_ui.view->setRowSpacing(m_ui.rowGap->value());
        m_ui.view->setColumnSpacing(m_ui.columnGap->value());
        const int margin = m_ui.margin->value();
        m_ui.view->setContentMargins(QMargins(margin, margin, margin, margin));
        // The inherited color API covers row/column gaps and centered outer half-gaps.
        // An invalid color restores the viewport background without painting margins.
        const QColor colors[] = {QColor("#ffe5b4"), QColor("#d6eaff"), QColor("#f6d9ec")};
        m_ui.spacingColor->setEnabled(m_ui.spacingColorEnabled->isChecked());
        m_ui.view->setSpacingBackgroundColor(m_ui.spacingColorEnabled->isChecked()
            ? colors[m_ui.spacingColor->currentIndex()] : QColor());
        // Disable auto-hide to inspect the terminal line even while scrolling.
        m_ui.view->setTrailingRowGridLineAutoHideEnabled(!m_ui.trailingLine->isChecked());
        const bool centered = m_ui.placement->currentIndex() == 0;
        // Center mode adds outer half-gaps inside contentMargins(); edge mode removes them.
        m_ui.view->setGridLinePlacement(centered ? viv::GridLinePlacement::SpacingCenter
                                                : viv::GridLinePlacement::SpacingEdges);
        m_ui.view->setHorizontalGridLinesVisible(m_ui.horizontal->isChecked());
        m_ui.view->setVerticalGridLinesVisible(m_ui.vertical->isChecked());
        m_ui.view->setHorizontalGridLineWidth(m_ui.lineWidth->value());
        m_ui.view->setVerticalGridLineWidth(m_ui.lineWidth->value());
        m_ui.view->setHorizontalGridLineColor(QColor("#4b79b8"));
        m_ui.view->setVerticalGridLineColor(QColor("#4b79b8"));
        // Center mode always bridges crossing gaps; only edge mode offers these switches.
        m_ui.horizontalThrough->setEnabled(!centered);
        m_ui.verticalThrough->setEnabled(!centered);
        m_ui.view->setHorizontalSpacingLineThroughColumnSpacing(m_ui.horizontalThrough->isChecked());
        m_ui.view->setVerticalSpacingLineThroughRowSpacing(m_ui.verticalThrough->isChecked());
    }
    Ui::FlowSpacingGrid m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_spacing_grid
