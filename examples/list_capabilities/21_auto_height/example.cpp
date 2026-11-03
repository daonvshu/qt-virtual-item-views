#include "example.h"
#include "ui_auto_height.h"

#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QScrollBar>
#include <QStandardItemModel>

namespace list_auto_height {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        // Keep the row transparent; the framework paints hover and selection.
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 40);
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
        m_ui.pageLayout->setStretch(m_ui.pageLayout->count() - 1, 1);
        // This expanding sibling makes unused vertical layout space visible.
        QPalette colors = m_ui.remainingLabel->palette();
        colors.setColor(QPalette::Window, QColor(QStringLiteral("#edf4ff")));
        m_ui.remainingLabel->setPalette(colors);
        m_ui.remainingLabel->setAutoFillBackground(true);
        m_ui.previewLayout->setStretch(0, 1);
    }

    void setupExample()
    {
        m_view = new viv::VirtualListView(m_ui.previewHost);
        m_view->setMinimumWidth(260);
        // The view takes ownership of this chapter's independent adapter.
        m_view->setAdapter(new TextAdapter, true);
        // AsNeeded leaves the viewport unscrollable until its content actually overflows.
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_view->setFrameBorderWidth(1);
        m_view->setFrameBorderColor(QColor(QStringLiteral("#879bb4")));
        m_view->setVisualStateCornerRadius(6);
        m_model = new QStandardItemModel(m_view);
        m_view->setModel(m_model);
        // No stretch or vertical alignment on the view: its size policy supplies the upper bound.
        m_ui.previewLayout->insertWidget(0, m_view);
        m_ui.previewLayout->setStretch(1, 1);
        applyOptions();
        populateModel();

        for (QSpinBox *spin : {m_ui.maxHeightSpin, m_ui.parentLimitSpin, m_ui.heightSpin, m_ui.spacingSpin})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        for (QCheckBox *check : {m_ui.enabledCheck, m_ui.autoHideTrailingLineCheck})
            connect(check, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
        connect(m_ui.rowCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { populateModel(); });
        // A queued observer reads geometry after the parent has applied the new size hint.
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated,
                this, [this]() { updateStatus(); }, Qt::QueuedConnection);
        updateStatus();
    }

    void applyOptions()
    {
        // One inherited switch works for lists, trees, tables and tree tables.
        // It updates size hints and lets the parent layout grant or reduce space.
        m_view->setAutoHeightEnabled(m_ui.enabledCheck->isChecked());
        // Disabling auto hide keeps the terminal line even when the view fits its content.
        m_view->setTrailingRowGridLineAutoHideEnabled(m_ui.autoHideTrailingLineCheck->isChecked());
        // Reuse QWidget's normal maximum height; zero in this UI means no explicit limit.
        const int maximum = m_ui.maxHeightSpin->value();
        m_view->setMaximumHeight(maximum > 0 ? maximum : QWIDGETSIZE_MAX);
        // A separate parent limit demonstrates shrinking to the available layout budget.
        const int parentMaximum = m_ui.parentLimitSpin->value();
        m_ui.previewHost->setMaximumHeight(parentMaximum > 0 ? parentMaximum : QWIDGETSIZE_MAX);
        // Layout extents include row spacing and reflect uniform-height changes automatically.
        m_view->setUniformItemHeight(m_ui.heightSpin->value());
        m_view->setRowSpacing(m_ui.spacingSpin->value());
    }

    void populateModel()
    {
        m_model->setColumnCount(1);
        m_model->setRowCount(m_ui.rowCountSpin->value());
        for (int row = 0; row < m_model->rowCount(); ++row)
            m_model->setData(m_model->index(row, 0), QStringLiteral("条目 %1").arg(row + 1));
    }

    void updateStatus()
    {
        m_ui.statusLabel->setText(QStringLiteral(
            "实际高度 %1 px | 建议高度 %2 px | 内容 %3 px | 视口 %4 px | 竖向滚动范围 %5")
            .arg(m_view->height()).arg(m_view->sizeHint().height()).arg(m_view->contentExtent())
            .arg(m_view->viewport()->height()).arg(m_view->verticalScrollBar()->maximum()));
    }

    Ui::ListAutoHeightExample m_ui;
    viv::VirtualListView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_auto_height
