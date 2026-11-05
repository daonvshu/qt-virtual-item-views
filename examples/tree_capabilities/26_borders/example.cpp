#include "example.h"
#include <memory>
#include "ui_borders.h"

#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace tree_borders {

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
        // Transparent nodes expose the framework's state background and branch area.
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeBorderExample)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeBorderExample> ui;

    void setupUi()
    {
        ui->setupUi(this);
        ui->pageLayout->setStretch(ui->pageLayout->count() - 1, 1);
        m_stage = new BackgroundStage(ui->previewHost);
        ui->previewLayout->addWidget(m_stage);
        for (QPushButton *button : {ui->firstButton, ui->lastButton, ui->expandButton, ui->collapseButton})
            button->setFocusPolicy(Qt::NoFocus);
    }

    void setupExample()
    {
        // Use a regular tree: the frame is inherited from VirtualItemView.
        m_view = new viv::VirtualTreeView(m_stage);
        m_view->setMinimumSize(260, 220);
        m_model = new QStandardItemModel(m_view);
        populateModel(0);
        // The view owns the adapter and is also the model's QObject parent.
        m_view->setAdapter(new TextAdapter, true);
        // Stable row heights isolate frame clipping from dynamic height measurement.
        m_view->setUniformItemHeight(44);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // The view maintains its visible-node mapping from Qt model notifications.
        m_view->setModel(m_model);
        m_stage->addDemo(m_view);
        applyOptions();
        expandAll(false);
        selectNode(false);

        connect(ui->backgroundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int mode) { m_stage->setBackground(mode); });
        for (QSpinBox *spin : {ui->radiusSpin, ui->widthSpin, ui->stateRadiusSpin})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyOptions(); });
        connect(ui->colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyOptions(); });
        for (QCheckBox *check : {ui->gapsCheck, ui->linesCheck, ui->branchCheck, ui->scrollBarsCheck})
            connect(check, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
        connect(ui->dataCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            populateModel(mode);
            applyOptions();
            expandAll(false);
            selectNode(false);
        });
        connect(ui->expandButton, &QPushButton::clicked, this, [this]() { expandAll(true); });
        connect(ui->collapseButton, &QPushButton::clicked, m_view, &viv::VirtualTreeView::collapseAll);
        connect(ui->firstButton, &QPushButton::clicked, this, [this]() { selectNode(false); });
        connect(ui->lastButton, &QPushButton::clicked, this, [this]() { selectNode(true); });
    }

    void populateModel(int mode)
    {
        m_model->clear();
        m_model->setColumnCount(1);
        const int groups[] = {6, 1, 1, 0};
        const int children[] = {12, 2, 0, 0};
        for (int group = 0; group < groups[mode]; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < children[mode]; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (mode == 0 && row % 4 == 0) {
                    for (int leaf = 0; leaf < 3; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }
    }

    void applyOptions()
    {
        // Logical pixels; branch icons, node widgets and scrollbars share one frame.
        // Setting BOTH radius and width to zero removes the frame effect.
        m_view->setFrameCornerRadius(ui->radiusSpin->value());
        m_view->setFrameBorderWidth(ui->widthSpin->value());
        const QColor colors[] = {QColor(QStringLiteral("#4079ad")), QColor(QStringLiteral("#485260")),
                                 QColor(QStringLiteral("#cf668f")), QColor()};
        // An invalid color restores QPalette::Mid.
        m_view->setFrameBorderColor(colors[ui->colorCombo->currentIndex()]);
        // Hover/selection corners remain independent of the outer frame.
        m_view->setVisualStateCornerRadius(ui->stateRadiusSpin->value());
        m_view->setRowSpacing(ui->gapsCheck->isChecked() ? 10 : 0);
        m_view->setRowGridLinesVisible(ui->linesCheck->isChecked());
        // Hide branch decoration without removing hierarchical indentation.
        m_view->setBranchAreaVisible(ui->branchCheck->isChecked());
        // AsNeeded demonstrates compact and empty models without a scrollbar.
        m_view->setVerticalScrollBarPolicy(ui->scrollBarsCheck->isChecked()
            ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded);
    }

    void expandAll(bool animated)
    {
        // Populate datasets without startup animations; user actions keep normal transitions.
        const bool wasAnimated = m_view->expansionAnimationEnabled();
        if (!animated)
            m_view->setExpansionAnimationEnabled(false);
        for (int row = 0; row < m_model->rowCount(); ++row)
            m_view->expandRecursively(m_model->index(row, 0));
        if (!animated)
            m_view->setExpansionAnimationEnabled(wasAnimated);
    }

    void selectNode(bool last)
    {
        // Use the visible mapping: the last visible node may be a nested child.
        const qsizetype row = last ? m_view->visibleRowCount() - 1 : 0;
        const QModelIndex index = m_view->visibilityIndex()->indexAtVisibleRow(row);
        if (!index.isValid())
            return;
        m_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_view->setCurrentIndex(index);
        m_view->scrollTo(index, last ? viv::VirtualItemView::PositionAtBottom
                                    : viv::VirtualItemView::PositionAtTop);
    }

    BackgroundStage *m_stage = nullptr;
    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_borders
