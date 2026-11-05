#include "example.h"
#include "ui_drag_drop.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_drag_drop {

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

        // QStandardItemModel supplies MIME and inserts copied records on drop.
        // Source deletion belongs to the view for this model's Move semantics.
        m_ui.view->setMoveRemovesSourceRows(true);
        m_ui.view->setDragDropActions(Qt::MoveAction | Qt::CopyAction);
        m_ui.view->setDefaultDropAction(Qt::MoveAction);
        m_ui.view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        m_ui.view->setDragEnabled(true);
        m_ui.view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        m_ui.view->setDropIndicatorColor(QColor("#d04d55"));
        m_ui.view->setColumnSpacing(8);
        m_ui.view->setRowSpacing(8);
        connect(m_ui.enabled, &QCheckBox::toggled, m_ui.view, &viv::VirtualItemView::setDragEnabled);
        connect(m_ui.action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int action) {
            m_ui.view->setDefaultDropAction(action == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(m_ui.dashed, &QCheckBox::toggled, this, [this](bool checked) {
            m_ui.view->setDropIndicatorPenStyle(checked ? Qt::DashLine : Qt::SolidLine);
        });
        connect(m_ui.width, QOverload<int>::of(&QSpinBox::valueChanged), m_ui.view, &viv::VirtualItemView::setDropIndicatorWidth);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, [this]() { updateStatus(); });
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this]() { updateStatus(); });
        updateStatus();
    }

    void updateStatus()
    {
        m_ui.status->setText(QStringLiteral("项目数量=%1。Flow 使用插入指示器，落点最终由 model 判断。").arg(m_model->rowCount()));
    }
    Ui::FlowDragDrop m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_drag_drop
