#include "example.h"
#include "ui_auto_height.h"
#include <virtualitemviews/virtualflowtableview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QScrollBar>
#include <QStandardItemModel>

namespace flow_auto_height {

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
        // The Designer page embeds this view in a widget-resizable QScrollArea.
        // When auto height leaves no internal scroll range, wheel events reach the outer area.
        m_ui.pageLayout->setStretch(m_ui.pageLayout->count() - 1, 1);
        QPalette colors = m_ui.remainingLabel->palette();
        colors.setColor(QPalette::Window, QColor("#edf4ff"));
        m_ui.remainingLabel->setPalette(colors);
        m_ui.remainingLabel->setAutoFillBackground(true);
    }

    void setupExample()
    {
        m_view = new viv::VirtualFlowTableView(m_ui.previewHost);
        // Own the adapter through unbinding; the model is parented to the view.
        m_view->setAdapter(new TextAdapter, true);
        m_model = new QStandardItemModel(0, 1, m_view);
        m_view->setModel(m_model);
        m_view->setFrameBorderWidth(1);
        m_view->setFrameBorderColor(QColor("#879bb4"));
        m_view->setContentMargins(QMargins(8, 8, 8, 8));
        // The framework fixes the content height; spare space belongs to the sibling.
        m_ui.previewLayout->insertWidget(0, m_view);
        m_ui.previewLayout->setStretch(1, 1);
        applyOptions();
        populateModel();

        for (QSpinBox *spin : {m_ui.maximum, m_ui.parentLimit, m_ui.cellHeight,
                              m_ui.cellWidth, m_ui.rowGap, m_ui.columnGap})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        connect(m_ui.enabled, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
        connect(m_ui.mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyOptions(); });
        connect(m_ui.ratio, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyOptions(); });
        connect(m_ui.count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { populateModel(); });
        // Observe after layout requests; the preferred height may change when columns reflow.
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated, this,
                [this]() { updateStatus(); }, Qt::QueuedConnection);
        connect(m_view, &viv::VirtualFlowTableView::flowLayoutChanged, this,
                [this]() { updateStatus(); }, Qt::QueuedConnection);
        updateStatus();
    }

    void applyOptions()
    {
        // Reuse the inherited whole-view auto-height API; cell height remains a Flow setting.
        m_view->setAutoHeightEnabled(m_ui.enabled->isChecked());
        // Zero means full content height; parent space does not reduce the computed height.
        m_view->setAutoHeightMaximumHeight(m_ui.maximum->value());
        m_ui.previewHost->setMaximumHeight(m_ui.parentLimit->value() > 0
            ? m_ui.parentLimit->value() : QWIDGETSIZE_MAX);
        m_view->setCellWidthRange(100, m_ui.cellWidth->value());
        m_view->setFixedCellHeight(m_ui.cellHeight->value());
        m_view->setCellAspectRatio(m_ui.ratio->value());
        m_view->setCellHeightMode(m_ui.mode->currentIndex() == 0
            ? viv::VirtualFlowTableView::CellHeightMode::Fixed
            : viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
        m_view->setRowSpacing(m_ui.rowGap->value());
        m_view->setColumnSpacing(m_ui.columnGap->value());
        m_ui.cellHeight->setEnabled(m_ui.mode->currentIndex() == 0);
        m_ui.ratio->setEnabled(m_ui.mode->currentIndex() == 1);
        // Flow stabilizes scrollbar visibility itself; do not force AlwaysOn/AlwaysOff here.
    }

    void populateModel()
    {
        const int previous = m_model->rowCount();
        m_model->setRowCount(m_ui.count->value());
        for (int row = previous; row < m_model->rowCount(); ++row)
            m_model->setData(m_model->index(row, 0), QStringLiteral("项目 %1").arg(row + 1));
    }

    void updateStatus()
    {
        m_ui.status->setText(QStringLiteral(
            "显示 %1 列 / %2 行 | 实际高度 %3 | 建议高度 %4 | 内容高度 %5 | 视口高度 %6 | 滚动范围 %7")
            .arg(m_view->columnCount()).arg(m_view->displayRowCount())
            .arg(m_view->height()).arg(m_view->sizeHint().height())
            .arg(m_view->contentExtent()).arg(m_view->viewport()->height())
            .arg(m_view->verticalScrollBar()->maximum()));
    }

    Ui::FlowAutoHeight m_ui;
    viv::VirtualFlowTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_auto_height
