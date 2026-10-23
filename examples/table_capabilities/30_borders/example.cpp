#include "example.h"
#include "borderdemo.h"
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
    void addDemo(BorderDemo *demo) { m_layout->addWidget(demo, 1); }

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
            painter.fillRect(rect(), stageColor());
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
        rebuildDemo();
        connect(m_ui.schemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { rebuildDemo(); });
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
        m_model->setRowCount(mode == 0 ? 200 : 3);
        m_model->setColumnCount(mode == 0 ? 12 : 3);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            m_model->setHeaderData(row, Qt::Vertical, row + 1);
            for (int column = 0; column < m_model->columnCount(); ++column)
                m_model->setData(m_model->index(row, column),
                                 QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
    }

    void rebuildDemo()
    {
        delete m_demo;
        const int mode = m_ui.schemeCombo->currentIndex();
        m_demo = mode == 0 ? createCornerCover(m_stage) : createAlphaFrame(m_stage);
        auto *view = m_demo->view();
        // Both techniques use the same model and table configuration for comparison.
        view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        view->setCellAdapter(new CellAdapter, true);
        view->setUniformItemHeight(44);
        view->setDefaultColumnWidth(150);
        view->setHeaderHeight(38);
        view->setHorizontalHeaderSelectionEnabled(true);
        view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        view->setModel(m_model);
        m_stage->addDemo(m_demo);
        applyOptions();
        selectRow(0);
        const QString hints[] = {
            QStringLiteral("方案 2：上层以纯色遮盖四角；切换渐变或棋盘格背景，可看到色块接缝。覆盖层不拦截鼠标。"),
            QStringLiteral("方案 3：整张表及子控件通过抗锯齿 Alpha 遮罩合成；支持真实透明，滚动与动画时需承担离屏合成开销。")
        };
        m_ui.hintLabel->setText(hints[mode]);
    }

    void applyOptions()
    {
        BorderOptions options;
        options.radius = m_ui.radiusSpin->value();
        options.width = m_ui.widthSpin->value();
        options.stateRadius = m_ui.stateRadiusSpin->value();
        const QColor colors[] = {QColor(QStringLiteral("#4079ad")), QColor(QStringLiteral("#485260")),
                                 QColor(QStringLiteral("#cf668f"))};
        options.color = colors[m_ui.colorCombo->currentIndex()];
        options.headers = m_ui.headersCheck->isChecked();
        options.gaps = m_ui.gapsCheck->isChecked();
        options.frozen = m_ui.frozenCheck->isChecked();
        options.scrollBars = m_ui.scrollBarsCheck->isChecked();
        m_demo->setOptions(options);
    }

    void selectRow(int row)
    {
        auto *view = m_demo->view();
        const QModelIndex index = m_model->index(row, 0);
        // Write the selection model explicitly so the complete row is selected in every mode.
        view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(index);
        view->scrollTo(index, row == 0 ? viv::VirtualItemView::PositionAtTop
                                      : viv::VirtualItemView::PositionAtBottom);
    }

    Ui::BorderExample m_ui;
    BackgroundStage *m_stage = nullptr;
    BorderDemo *m_demo = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_borders
