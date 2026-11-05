#include "example.h"
#include "ui_scrolling.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_scrolling {

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
        for (int row = 0; row < 10000; ++row)
            m_model->appendRow(new QStandardItem(QStringLiteral("项目 %1").arg(row + 1)));
        // The view owns the adapter; the QObject parent owns the model.
        m_ui.view->setAdapter(new TextAdapter, true);
        // Flow has dedicated cell sizing; do not use inherited uniform-row height APIs.
        m_ui.view->setCellWidthRange(100, 200);
        m_ui.view->setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::Fixed);
        m_ui.view->setFixedCellHeight(100);
        // Model signals keep the materialized widgets synchronized.
        m_ui.view->setModel(m_model);

        // Overscan counts logical items; it controls the amount of offscreen work.
        m_ui.view->setOverscan(4, 4);
        connect(m_ui.overscan, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_ui.view->setOverscan(value, value);
        });
        connect(m_ui.locate, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = m_model->index(m_ui.target->value() - 1, 0);
            m_ui.view->setCurrentIndex(index);
            // Use model identity; display rows/columns can change after a resize.
            m_ui.view->scrollTo(index, viv::VirtualItemView::PositionAtCenter);
        });
        connect(m_ui.query, &QPushButton::clicked, this, [this]() { queryCurrent(); });
        connect(m_ui.view, &viv::VirtualItemView::virtualizationUpdated, this, [this]() { updateStats(); });
        connect(m_ui.view, &viv::VirtualFlowTableView::flowLayoutChanged, this, [this]() { updateStats(); });
        updateStats();
    }

    void updateStats()
    {
        const auto stats = m_ui.view->stats();
        m_ui.status->setText(QStringLiteral("总项目=%1 / 物化控件=%2 / 显示列=%3 / 显示行=%4")
            .arg(stats.logicalItems).arg(stats.materializedItems)
            .arg(m_ui.view->columnCount()).arg(m_ui.view->displayRowCount()));
    }
    void queryCurrent()
    {
        const QModelIndex index = m_ui.view->currentIndex();
        if (!index.isValid()) {
            m_ui.status->setText(QStringLiteral("先点击一个项目或使用滚动定位。"));
            return;
        }
        // Geometry queries do not force an offscreen widget to materialize.
        const QRect rect = m_ui.view->visualRect(index);
        const QModelIndex hit = m_ui.view->indexAt(rect.center());
        m_ui.status->setText(QStringLiteral("项目=%1 / 显示坐标=(%2,%3) / rect=(%4,%5 %6×%7) / 中心命中=%8")
            .arg(index.data().toString()).arg(m_ui.view->displayRowForIndex(index))
            .arg(m_ui.view->displayColumnForIndex(index)).arg(rect.x()).arg(rect.y())
            .arg(rect.width()).arg(rect.height()).arg(hit.isValid() ? hit.data().toString() : QStringLiteral("无")));
    }
    Ui::FlowScrolling m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_scrolling
