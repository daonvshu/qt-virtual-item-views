#include "ui_item_heights.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>

namespace tree_item_heights {

constexpr int DetailRole = Qt::UserRole + 1;
class DetailRow : public QLabel
{
public:
    explicit DetailRow(QWidget *parent) : QLabel(parent) { setMargin(8); }
    void bind(const QModelIndex &index)
    {
        m_detail = index.data(DetailRole).toBool();
        setText(index.data().toString() + (m_detail
            ? QStringLiteral("\n详情第一行\n详情第二行\n详情第三行") : QString()));
        updateGeometry();
    }
    QSize sizeHint() const override { return QSize(480, m_detail ? 112 : 36); }
private:
    bool m_detail = false;
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
        return QSize(480, index.data(DetailRole).toBool() ? 112 : 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeItemHeightsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeItemHeightsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 40; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        for (int row = 0; row < m_model->item(0)->rowCount(); ++row)
            m_model->item(0)->child(row)->setData(row % 3 == 0, DetailRole);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new HeightAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            applyHeightMode(value);
        });
        connect(ui->view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // A data change triggers binding and size measurement of this node.
            m_model->setData(index, !index.data(DetailRole).toBool(), DetailRole);
        });
        connect(ui->first, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_model->index(0, 0);
            m_model->setData(index, !index.data(DetailRole).toBool(), DetailRole);
        });
        ui->mode->setCurrentIndex(2);
    }

    void applyHeightMode(int heightMode)
    {
        // Measurement reads each bound row's sizeHint; estimates cover unmeasured nodes.
        ui->view->setAutoMeasureItemHeight(heightMode != 0);
        ui->view->setEstimatedItemHeight(36);
        if (heightMode == 2)
            ui->view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
        else
            // setUniformItemHeight also selects Uniform mode; zero fits the first item.
            ui->view->setUniformItemHeight(heightMode == 1 ? 0 : 44);
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_item_heights
