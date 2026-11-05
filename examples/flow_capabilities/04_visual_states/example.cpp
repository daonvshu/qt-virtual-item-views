#include "example.h"
#include "ui_visual_states.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_visual_states {

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

        // Select individual model items, even when display columns change on resize.
        m_ui.view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        m_ui.view->setColumnSpacing(8);
        m_ui.view->setRowSpacing(8);
        m_ui.view->setHorizontalGridLinesVisible(false);
        m_ui.view->setVerticalGridLinesVisible(false);
        connect(m_ui.visible, &QCheckBox::toggled, this, [this](bool) { applyStates(); });
        connect(m_ui.radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyStates(); });
        connect(m_ui.duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyStates(); });
        connect(m_ui.curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyStates(); });
        connect(m_ui.color, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyStates(); });
        applyStates();
        m_ui.status->setText(QStringLiteral("状态背景只覆盖真实卡片，间距与末行空白保持原背景。"));
    }

    void applyStates()
    {
        // Let the framework paint states; the adapter only supplies transparent content.
        m_ui.view->setVisualStateBackgroundVisible(m_ui.visible->isChecked());
        m_ui.view->setStateBackgroundCornerRadius(m_ui.radius->value());
        m_ui.view->setVisualStateAnimationDuration(m_ui.duration->value());
        const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::Linear, QEasingCurve::InOutCubic};
        m_ui.view->setVisualStateAnimationEasingCurve(curves[m_ui.curve->currentIndex()]);
        const bool green = m_ui.color->currentIndex() == 1;
        m_ui.view->setHoverBackgroundColor(QColor(green ? "#d5f3df" : "#dcecff"));
        m_ui.view->setSelectedBackgroundColor(QColor(green ? "#89cf9e" : "#98c3ed"));
    }
    Ui::FlowVisualStates m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_visual_states
