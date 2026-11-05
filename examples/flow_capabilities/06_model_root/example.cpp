#include "example.h"
#include "ui_model_root.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_model_root {

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
        // The view owns the adapter; the QObject parent owns the model.
        m_ui.view->setAdapter(new TextAdapter, true);
        // Flow has dedicated cell sizing; do not use inherited uniform-row height APIs.
        m_ui.view->setCellWidthRange(100, 200);
        m_ui.view->setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::Fixed);
        m_ui.view->setFixedCellHeight(100);
        // Model signals keep the materialized widgets synchronized.
        m_ui.view->setModel(m_model);

        for (int group = 0; group < 2; ++group) {
            auto *parent = new QStandardItem(group == 0 ? QStringLiteral("分组 A") : QStringLiteral("分组 B"));
            m_model->appendRow(parent);
            for (int row = 0; row < 17; ++row)
                parent->appendRow(new QStandardItem(QStringLiteral("项目 %1.%2").arg(group + 1).arg(row + 1)));
        }
        connect(m_ui.root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int root) {
            // Flow displays direct children of this index; it does not flatten descendants.
            m_ui.view->setRootIndex(root == 0 ? QModelIndex() : m_model->index(root - 1, 0));
        });
        connect(m_ui.append, &QPushButton::clicked, this, [this]() {
            QStandardItem *parent = m_ui.view->rootIndex().isValid()
                ? m_model->itemFromIndex(m_ui.view->rootIndex()) : m_model->invisibleRootItem();
            parent->appendRow(new QStandardItem(QStringLiteral("新增 %1").arg(++m_next)));
        });
        connect(m_ui.remove, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = m_ui.view->currentIndex();
            // Keep the two navigation roots stable; only child records are editable here.
            if (index.isValid() && index.parent().isValid())
                m_model->removeRow(index.row(), index.parent());
        });
        connect(m_ui.rename, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = m_ui.view->currentIndex();
            if (index.isValid() && index.parent().isValid())
                m_model->setData(index, QStringLiteral("已修改 %1").arg(++m_next));
        });
        // Standard model signals trigger rebind/reflow; no manual reload is needed.
        m_ui.root->setCurrentIndex(1);
        m_ui.status->setText(QStringLiteral("分组根保留用于导航；进入分组后可选择子项并修改或删除。"));
    }

    Ui::FlowModelRoot m_ui;
    QStandardItemModel *m_model = nullptr;
    int m_next = 0;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_model_root
