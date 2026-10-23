#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("06 行高：点击节点切换详情高度；切换固定、首项测量、独立动态高度。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        mode = new QComboBox(this);
        mode->addItems({QStringLiteral("固定 44 px"), QStringLiteral("按首项测量统一高度"), QStringLiteral("每节点动态高度")});
        controls->addWidget(mode);
        first = new QPushButton(QStringLiteral("切换首节点详情"), this);
        controls->addWidget(first);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
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
        m_view->setAdapter(new HeightAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            applyHeightMode(value);
        });
        connect(m_view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // A data change triggers binding and size measurement of this node.
            m_model->setData(index, !index.data(DetailRole).toBool(), DetailRole);
        });
        connect(first, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_model->index(0, 0);
            m_model->setData(index, !index.data(DetailRole).toBool(), DetailRole);
        });
        mode->setCurrentIndex(2);
    }

    void applyHeightMode(int mode)
    {
        // Measurement reads each bound row's sizeHint; estimates cover unmeasured nodes.
        m_view->setAutoMeasureItemHeight(mode != 0);
        m_view->setEstimatedItemHeight(36);
        if (mode == 2)
            m_view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
        else
            // setUniformItemHeight also selects Uniform mode; zero fits the first item.
            m_view->setUniformItemHeight(mode == 1 ? 0 : 44);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *mode = nullptr;
    QPushButton *first = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_item_heights
