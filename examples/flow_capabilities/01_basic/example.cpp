#include "example.h"
#include "ui_basic.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_basic {

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

        connect(m_ui.view, &viv::VirtualFlowTableView::flowLayoutChanged, this, [this]() { updateStatus(); });
        updateStatus();
    }

    void updateStatus()
    {
        // Display rows and columns are layout coordinates, not model dimensions.
        m_ui.status->setText(QStringLiteral("模型列数=%1 / 显示列数=%2 / 显示行数=%3")
            .arg(m_model->columnCount()).arg(m_ui.view->columnCount()).arg(m_ui.view->displayRowCount()));
    }
    Ui::FlowBasic m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_basic
