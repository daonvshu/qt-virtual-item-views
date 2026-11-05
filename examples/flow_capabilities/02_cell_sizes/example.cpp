#include "example.h"
#include "ui_cell_sizes.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_cell_sizes {

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

        m_ui.maximum->setMinimum(m_ui.minimum->value());
        connect(m_ui.minimum, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_ui.maximum->setMinimum(value);
            applySizes();
        });
        connect(m_ui.maximum, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applySizes(); });
        connect(m_ui.mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applySizes(); });
        connect(m_ui.height, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applySizes(); });
        connect(m_ui.ratio, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applySizes(); });
        connect(m_ui.view, &viv::VirtualFlowTableView::flowLayoutChanged, this, [this]() { updateStatus(); });
        applySizes();
    }

    void applySizes()
    {
        // Available viewport width determines the display column count.
        m_ui.view->setCellWidthRange(m_ui.minimum->value(), m_ui.maximum->value());
        // Configure both values; only the selected height mode is used by the layout.
        m_ui.view->setFixedCellHeight(m_ui.height->value());
        m_ui.view->setCellAspectRatio(m_ui.ratio->value());
        m_ui.view->setCellHeightMode(m_ui.mode->currentIndex() == 0
            ? viv::VirtualFlowTableView::CellHeightMode::Fixed
            : viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
        m_ui.height->setEnabled(m_ui.mode->currentIndex() == 0);
        m_ui.ratio->setEnabled(m_ui.mode->currentIndex() == 1);
        updateStatus();
    }
    void updateStatus()
    {
        m_ui.status->setText(QStringLiteral("显示列=%1 / 首列宽=%2 / 首项高度=%3")
            .arg(m_ui.view->columnCount()).arg(m_ui.view->columnWidth(0))
            .arg(m_ui.view->visualRect(m_model->index(0, 0)).height()));
    }
    Ui::FlowCellSizes m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_cell_sizes
