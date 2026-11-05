#include "example.h"
#include "ui_selection_events.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_selection_events {

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

        m_ui.mode->setCurrentIndex(3);
        const auto applyMode = [this]() {
            const viv::VirtualItemView::SelectionMode modes[] = {
                viv::VirtualItemView::SelectionMode::NoSelection,
                viv::VirtualItemView::SelectionMode::SingleSelection,
                viv::VirtualItemView::SelectionMode::MultiSelection,
                viv::VirtualItemView::SelectionMode::ExtendedSelection};
            m_ui.view->setSelectionMode(modes[m_ui.mode->currentIndex()]);
        };
        connect(m_ui.mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [applyMode](int) { applyMode(); });
        connect(m_ui.selectRange, &QPushButton::clicked, this, [this]() {
            // A direct selection-model operation selects the complete range in one call.
            // SelectionMode controls user gestures, not this programmatic selection.
            const QItemSelection range(m_model->index(1, 0), m_model->index(4, 0));
            m_ui.view->selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
            // Move the current item without replacing the range selection.
            m_ui.view->selectionModel()->setCurrentIndex(m_model->index(4, 0), QItemSelectionModel::NoUpdate);
            m_ui.view->setFocus();
        });
        connect(m_ui.clear, &QPushButton::clicked, m_ui.view->selectionModel(), &QItemSelectionModel::clearSelection);
        connect(m_ui.view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { updateStatus(); });
        connect(m_ui.view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() { updateStatus(); });
        connect(m_ui.view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // The event index is current even after widgets have been recycled.
            m_lastClicked = index.data().toString();
            updateStatus();
        });
        applyMode();
        updateStatus();
    }

    void updateStatus()
    {
        QStringList selected;
        for (const QModelIndex &index : m_ui.view->selectionModel()->selectedIndexes())
            selected.append(index.data().toString());
        m_ui.status->setText(QStringLiteral("最近点击=%1 / 当前项=%2 / 已选=%3")
            .arg(m_lastClicked, m_ui.view->currentIndex().data().toString(), selected.join(QStringLiteral(", "))));
    }
    Ui::FlowSelectionEvents m_ui;
    QStandardItemModel *m_model = nullptr;
    QString m_lastClicked;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_selection_events
