#include "example.h"
#include "ui_example.h"

#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace list_borders {

class BackgroundStage : public QWidget
{
public:
    explicit BackgroundStage(QWidget *parent) : QWidget(parent)
    {
        setupUi();
    }
    void setBackground(int mode) { m_background = mode; update(); }
    void addDemo(QWidget *demo) { m_layout->addWidget(demo, 1); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (m_background == 1) {
            QLinearGradient gradient(0, 0, width(), height());
            gradient.setColorAt(0, QColor(QStringLiteral("#d3efff")));
            gradient.setColorAt(0.5, QColor(QStringLiteral("#e8d9f5")));
            gradient.setColorAt(1, QColor(QStringLiteral("#ffe0c6")));
            painter.fillRect(rect(), gradient);
        } else if (m_background == 2) {
            painter.fillRect(rect(), QColor(QStringLiteral("#f5f7fa")));
            const int side = 16;
            for (int y = 0; y < height(); y += side) {
                for (int x = 0; x < width(); x += side) {
                    if (((x / side) + (y / side)) % 2 == 0)
                        painter.fillRect(QRect(x, y, side, side), QColor(QStringLiteral("#c4cfdd")));
                }
            }
        } else {
            painter.fillRect(rect(), QColor(QStringLiteral("#e8edf4")));
        }
    }

private:
    void setupUi()
    {
        // Only this single-child preview container needs a code-based layout.
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(24, 24, 24, 24);
    }

    QVBoxLayout *m_layout = nullptr;
    int m_background = 0;
};

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        // Transparent rows leave hover/selection backgrounds to VirtualListView.
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
        return QSize(480, 44);
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
        m_stage = new BackgroundStage(m_ui.previewHost);
        m_ui.previewLayout->addWidget(m_stage);
        for (QPushButton *button : {m_ui.firstButton, m_ui.lastButton})
            button->setFocusPolicy(Qt::NoFocus);
    }

    void setupExample()
    {
        // Use a regular view: the frame is inherited from VirtualItemView.
        m_view = new viv::VirtualListView(m_stage);
        m_view->setMinimumSize(260, 220);
        m_model = new QStandardItemModel(m_view);
        populateModel(0);
        // The view owns the adapter and is also the model's QObject parent.
        m_view->setAdapter(new TextAdapter, true);
        // Fixed height keeps this chapter focused on frame and state geometry.
        m_view->setUniformItemHeight(44);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Model notifications refresh virtualized rows without replacing the model.
        m_view->setModel(m_model);
        m_stage->addDemo(m_view);
        applyOptions();
        selectRow(0);

        connect(m_ui.backgroundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int mode) { m_stage->setBackground(mode); });
        for (QSpinBox *spin : {m_ui.radiusSpin, m_ui.widthSpin, m_ui.stateRadiusSpin})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        connect(m_ui.colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyOptions(); });
        for (QCheckBox *check : {m_ui.gapsCheck, m_ui.linesCheck, m_ui.frozenCheck, m_ui.scrollBarsCheck})
            connect(check, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
        connect(m_ui.dataCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            populateModel(mode);
            applyOptions();
            selectRow(0);
        });
        connect(m_ui.firstButton, &QPushButton::clicked, this, [this]() { selectRow(0); });
        connect(m_ui.lastButton, &QPushButton::clicked, this, [this]() { selectRow(m_model->rowCount() - 1); });
    }

    void populateModel(int mode)
    {
        m_model->clear();
        const int rows[] = {200, 3, 1, 0};
        m_model->setColumnCount(1);
        for (int row = 0; row < rows[mode]; ++row)
            m_model->appendRow(new QStandardItem(QStringLiteral("条目 %1").arg(row + 1)));
    }

    void applyOptions()
    {
        // Logical pixels; the framework composes the view, its rows and scrollbars.
        // Setting BOTH radius and width to zero removes the frame effect.
        m_view->setFrameCornerRadius(m_ui.radiusSpin->value());
        m_view->setFrameBorderWidth(m_ui.widthSpin->value());
        const QColor colors[] = {QColor(QStringLiteral("#4079ad")), QColor(QStringLiteral("#485260")),
                                 QColor(QStringLiteral("#cf668f")), QColor()};
        // An invalid color restores QPalette::Mid instead of a custom border color.
        m_view->setFrameBorderColor(colors[m_ui.colorCombo->currentIndex()]);
        // Row hover/selection corners are independent of the outer frame.
        m_view->setVisualStateCornerRadius(m_ui.stateRadiusSpin->value());
        // Gaps and separators are still drawn by the framework, inside the same frame.
        m_view->setRowSpacing(m_ui.gapsCheck->isChecked() ? 10 : 0);
        m_view->setRowGridLinesVisible(m_ui.linesCheck->isChecked());
        const bool frozen = m_ui.frozenCheck->isChecked();
        // Do not overlap the frozen bands for the single-row or empty datasets.
        m_view->setFrozenRows(frozen && m_model->rowCount() > 0 ? 1 : 0);
        m_view->setFrozenBottomRows(frozen && m_model->rowCount() > 1 ? 1 : 0);
        // AsNeeded also demonstrates the frame without a scrollbar for compact data.
        m_view->setVerticalScrollBarPolicy(m_ui.scrollBarsCheck->isChecked()
            ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded);
    }

    void selectRow(int row)
    {
        const QModelIndex index = m_model->index(row, 0);
        if (!index.isValid())
            return;
        // Selection is model-based; the target need not be materialized yet.
        m_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_view->setCurrentIndex(index);
        m_view->scrollTo(index, row == 0 ? viv::VirtualItemView::PositionAtTop
                                        : viv::VirtualItemView::PositionAtBottom);
    }

    Ui::ListBorderExample m_ui;
    BackgroundStage *m_stage = nullptr;
    viv::VirtualListView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_borders
