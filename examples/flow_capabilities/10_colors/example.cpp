#include "example.h"
#include "ui_colors.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QItemSelectionModel>
#include <QEasingCurve>

namespace flow_colors {

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
        // Restore the framework selector after Designer assigns the UI member name.
        m_ui.view->setObjectName(QStringLiteral("vivItemView"));
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

        m_ui.view->setRowSpacing(12);
        m_ui.view->setColumnSpacing(12);
        m_ui.view->setContentMargins(QMargins(12, 12, 12, 12));
        // These colors are drawn below the transparent business labels.
        m_ui.view->setHoverBackgroundColor(QColor("#d5e8ff"));
        m_ui.view->setSelectedBackgroundColor(QColor("#89b5ef"));
        m_ui.view->selectionModel()->select(m_model->index(0, 0), QItemSelectionModel::ClearAndSelect);
        connect(m_ui.background, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyColors(); });
        connect(m_ui.state, &QCheckBox::toggled, this, [this](bool) { applyColors(); });
        connect(m_ui.qss, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyColors(); });
        applyColors();
    }

    void applyColors()
    {
        // Invalid color restores palette Base; visibility controls view and viewport layers.
        m_ui.view->setBackgroundColor(m_ui.background->currentIndex() == 1 ? QColor("#fff0cc") : QColor());
        m_ui.view->setBackgroundVisible(m_ui.background->currentIndex() != 2);
        m_ui.view->setVisualStateBackgroundVisible(m_ui.state->isChecked());
        QString qss;
        if (m_ui.qss->currentIndex() == 1)
            qss = QStringLiteral("#vivItemView { background-color: #c4edda; }");
        else if (m_ui.qss->currentIndex() == 2)
            qss = QStringLiteral("#vivItemView #vivViewport { background-color: #ffd6e8; }");
        else if (m_ui.qss->currentIndex() == 3)
            qss = QStringLiteral("QLabel#flowCard { background-color: #ffca96; color: #402820; }");
        // Scope QSS to this preview so it does not style the chapter browser.
        m_ui.view->setStyleSheet(qss);
        m_ui.status->setText(QStringLiteral("显式主体颜色优先于 View 的 Base；viewport 在主体上方绘制。QLabel 底色会遮住卡片状态。"));
    }
    Ui::FlowColors m_ui;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_colors
