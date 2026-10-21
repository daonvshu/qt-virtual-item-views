#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral("06 行高：点击行展开详情。动态模式下滚动到中间，再改变首行高度，观察滚动锚点。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *controls = new QHBoxLayout;
        m_mode = new QComboBox(this);
        m_mode->addItems({QStringLiteral("固定 44 px"), QStringLiteral("按首个物化项测量统一高度"), QStringLiteral("每行动态高度")});
        m_first = new QPushButton(QStringLiteral("切换首行详情"), this);
        m_async = new QCheckBox(QStringLiteral("模拟后台更新高度"), this);
        controls->addWidget(m_mode);
        controls->addWidget(m_first);
        controls->addWidget(m_async);
        layout->addLayout(controls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        m_model = new QStandardItemModel(m_view);
        for (int row = 0; row < 200; ++row)
            m_model->appendRow(new QStandardItem(QStringLiteral("条目 %1 · 点击切换详情").arg(row + 1)));
        // The row's sizeHint() supplies measured height; the adapter supplies estimates.
        m_view->setAdapter(new HeightAdapter, true);
        m_view->setModel(m_model);
        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Example::applyHeightMode);
        connect(m_view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // Changing model data triggers rebind and, with auto measurement,
            // a new height measurement. No manual relayout request is needed.
            m_model->setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
        });
        connect(m_first, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_model->index(0, 0);
            m_model->setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
        });
        auto *updates = new QTimer(this);
        updates->setInterval(400);
        connect(updates, &QTimer::timeout, this, [this, row = 0]() mutable {
            if (!isVisible() || m_mode->currentIndex() != 2)
                return;
            // Simulate data arriving later on the GUI thread. Only update the
            // model; dataChanged drives rebind, measurement and scroll anchoring.
            const QModelIndex index = m_model->index(row, 0);
            m_model->setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
            row = (row + 17) % m_model->rowCount();
        });
        connect(m_async, &QCheckBox::toggled, updates, [updates](bool enabled) {
            if (enabled)
                updates->start();
            else
                updates->stop();
        });
        m_mode->setCurrentIndex(2);
    }

    void applyHeightMode(int mode)
    {
        m_async->setEnabled(mode == 2);
        if (mode != 2)
            m_async->setChecked(false);
        using Mode = viv::VirtualItemView::ItemHeightMode;
        // Auto measurement reads the bound widget's sizeHint after binding.
        m_view->setAutoMeasureItemHeight(mode != 0);
        // This estimate is used before a variable-height row has been materialized.
        m_view->setEstimatedItemHeight(36);
        if (mode == 2) {
            // Variable stores a separate measured height for each row.
            m_view->setItemHeightMode(Mode::Variable);
        } else {
            // This setter also switches to Uniform mode; do not call it for Variable.
            // Height 0 fits the first measured item and requires auto measurement.
            m_view->setUniformItemHeight(mode == 1 ? 0 : 44);
        }
    }

    viv::VirtualListView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *m_mode = nullptr;
    QPushButton *m_first = nullptr;
    QCheckBox *m_async = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_item_heights
