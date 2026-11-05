#include "ui_item_heights.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>

namespace list_item_heights {

constexpr int ExpandedRole = Qt::UserRole + 1;

class DetailRow : public QLabel
{
public:
    explicit DetailRow(QWidget *parent) : QLabel(parent) { setMargin(8); }
    void bind(const QModelIndex &index)
    {
        m_expanded = index.data(ExpandedRole).toBool();
        setText(index.data().toString() + (m_expanded
            ? QStringLiteral("\n详情第一行\n详情第二行\n详情第三行") : QString()));
        updateGeometry();
    }
    QSize sizeHint() const override { return QSize(400, m_expanded ? 112 : 36); }

private:
    bool m_expanded = false;
};

class HeightAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new DetailRow(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<DetailRow *>(widget)->bind(index);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<DetailRow *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return QSize(400, index.data(ExpandedRole).toBool() ? 112 : 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListItemHeightsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListItemHeightsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(ui->view);
        for (int row = 0; row < 200; ++row)
            m_model->appendRow(new QStandardItem(QStringLiteral("条目 %1 · 点击切换详情").arg(row + 1)));
        // The row's sizeHint() supplies measured height; the adapter supplies estimates.
        ui->view->setAdapter(new HeightAdapter, true);
        ui->view->setModel(m_model);
        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Example::applyHeightMode);
        connect(ui->view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // Changing model data triggers rebind and, with auto measurement,
            // a new height measurement. No manual relayout request is needed.
            m_model->setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
        });
        connect(ui->first, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_model->index(0, 0);
            m_model->setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
        });
        auto *updates = new QTimer(this);
        updates->setInterval(400);
        connect(updates, &QTimer::timeout, this, [this, row = 0]() mutable {
            if (!isVisible() || ui->mode->currentIndex() != 2)
                return;
            // Simulate data arriving later on the GUI thread. Only update the
            // model; dataChanged drives rebind, measurement and scroll anchoring.
            const QModelIndex index = m_model->index(row, 0);
            m_model->setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
            row = (row + 17) % m_model->rowCount();
        });
        connect(ui->async, &QCheckBox::toggled, updates, [updates](bool enabled) {
            if (enabled)
                updates->start();
            else
                updates->stop();
        });
        ui->mode->setCurrentIndex(2);
    }

    void applyHeightMode(int heightMode)
    {
        ui->async->setEnabled(heightMode == 2);
        if (heightMode != 2)
            ui->async->setChecked(false);
        using Mode = viv::VirtualItemView::ItemHeightMode;
        // Auto measurement reads the bound widget's sizeHint after binding.
        ui->view->setAutoMeasureItemHeight(heightMode != 0);
        // This estimate is used before a variable-height row has been materialized.
        ui->view->setEstimatedItemHeight(36);
        if (heightMode == 2) {
            // Variable stores a separate measured height for each row.
            ui->view->setItemHeightMode(Mode::Variable);
        } else {
            // This setter also switches to Uniform mode; do not call it for Variable.
            // Height 0 fits the first measured item and requires auto measurement.
            ui->view->setUniformItemHeight(heightMode == 1 ? 0 : 44);
        }
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_item_heights
