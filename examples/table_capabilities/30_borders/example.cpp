#include "example.h"
#include "ui_example.h"

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace table_borders {

class BackgroundStage : public QWidget
{
public:
    explicit BackgroundStage(QWidget *parent) : QWidget(parent)
    {
        // This small dynamic container has one child; the page controls live in example.ui.
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(24, 24, 24, 24);
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
    QVBoxLayout *m_layout = nullptr;
    int m_background = 0;
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
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
        m_ui.firstButton->setFocusPolicy(Qt::NoFocus);
        m_ui.lastButton->setFocusPolicy(Qt::NoFocus);
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(this);
        populateModel(0);
        setupView();
        connect(m_ui.backgroundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int mode) { m_stage->setBackground(mode); });
        for (QSpinBox *spin : {m_ui.radiusSpin, m_ui.widthSpin, m_ui.stateRadiusSpin})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        connect(m_ui.colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyOptions(); });
        for (QCheckBox *check : {m_ui.headersCheck, m_ui.gapsCheck, m_ui.frozenCheck, m_ui.scrollBarsCheck})
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
        const int columns[] = {12, 3, 1, 3};
        m_model->setRowCount(rows[mode]);
        m_model->setColumnCount(columns[mode]);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            m_model->setHeaderData(row, Qt::Vertical, row + 1);
            for (int column = 0; column < m_model->columnCount(); ++column)
                m_model->setData(m_model->index(row, column),
                                 QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
    }

    void setupView()
    {
        m_view = new viv::VirtualTableView(m_stage);
        auto *view = m_view;
        view->setMinimumSize(260, 220);
        // Materialize cells independently so the frame composes headers, cells and scrollbars together.
        view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        view->setCellAdapter(new CellAdapter, true);
        // Keep row geometry stable while the example focuses on the outer frame and its clipping.
        view->setUniformItemHeight(44);
        view->setDefaultColumnWidth(150);
        view->setHeaderHeight(38);
        // Enable header selection so the rounded frame also covers header state painting.
        view->setHorizontalHeaderSelectionEnabled(true);
        view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // The view listens to model signals and rebuilds its virtualized contents automatically.
        view->setModel(m_model);
        m_stage->addDemo(view);
        applyOptions();
        selectRow(0);
        m_ui.hintLabel->setText(QStringLiteral(
            "框架内置 Alpha 合成：表头、单元格和滚动条统一裁成圆角，支持真实透明。"
            "可切换少量或空数据，观察空白区、行状态与冻结区域。"));
    }

    void applyOptions()
    {
        // Frame and visual-state settings are shared VirtualItemView features inherited by the table.
        m_view->setFrameCornerRadius(m_ui.radiusSpin->value());
        m_view->setFrameBorderWidth(m_ui.widthSpin->value());
        m_view->setVisualStateCornerRadius(m_ui.stateRadiusSpin->value());
        const QColor colors[] = {QColor(QStringLiteral("#4079ad")), QColor(QStringLiteral("#485260")),
                                 QColor(QStringLiteral("#cf668f"))};
        m_view->setFrameBorderColor(colors[m_ui.colorCombo->currentIndex()]);
        const bool headers = m_ui.headersCheck->isChecked();
        // Header visibility changes the content geometry used by the rounded frame.
        m_view->setHorizontalHeaderVisible(headers);
        m_view->setVerticalHeaderVisible(headers);
        const int spacing = m_ui.gapsCheck->isChecked() ? 10 : 0;
        // Row and column gaps are part of the view layout, so state backgrounds cross them consistently.
        m_view->setRowSpacing(spacing);
        m_view->setColumnSpacing(spacing);
        const bool frozen = m_ui.frozenCheck->isChecked();
        // Freeze the first/last columns and rows to exercise all pane edges under the same frame.
        m_view->setFrozenColumns(frozen && m_view->columnCount() > 0 ? QVector<int>{0} : QVector<int>{});
        const int last = m_view->columnCount() - 1;
        m_view->setFrozenRightColumns(frozen && last > 0 ? QVector<int>{last} : QVector<int>{});
        m_view->setFrozenRows(frozen ? 1 : 0);
        m_view->setFrozenBottomRows(frozen ? 1 : 0);
        const auto policy = m_ui.scrollBarsCheck->isChecked() ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded;
        // AlwaysOn makes the scrollbar corners easy to inspect; AsNeeded covers the compact datasets too.
        m_view->setHorizontalScrollBarPolicy(policy);
        m_view->setVerticalScrollBarPolicy(policy);
    }

    void selectRow(int row)
    {
        auto *view = m_view;
        const QModelIndex index = m_model->index(row, 0);
        if (!index.isValid())
            return;
        // Write the selection model explicitly so the complete row is selected in every mode.
        view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(index);
        view->scrollTo(index, row == 0 ? viv::VirtualItemView::PositionAtTop
                                      : viv::VirtualItemView::PositionAtBottom);
    }

    Ui::BorderExample m_ui;
    BackgroundStage *m_stage = nullptr;
    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_borders
