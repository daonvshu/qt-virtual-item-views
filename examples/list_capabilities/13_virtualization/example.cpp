#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/widgetrecycler.h>
#include <QAbstractListModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace list_virtualization {

class LogicalModel : public QAbstractListModel
{
public:
    explicit LogicalModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 10000; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        return index.isValid() && role == Qt::DisplayRole
            ? QVariant(QStringLiteral("条目 %1").arg(index.row() + 1)) : QVariant();
    }
};

class ReusedRow : public QLabel
{
public:
    ReusedRow(int id, QWidget *parent) : QLabel(parent), instanceId(id) { setMargin(8); }
    const int instanceId;
};

class PoolAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new ReusedRow(++m_nextId, parent);
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<ReusedRow *>(widget);
        row->setText(QStringLiteral("%1    ← 控件实例 #%2").arg(index.data().toString()).arg(row->instanceId));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<ReusedRow *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(400, 36); }

private:
    int m_nextId = 0;
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
        auto *intro = new QLabel(QStringLiteral("13 虚拟化：滚动后，同一控件实例编号会绑定到不同条目；上下预加载分别设置。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *controls = new QHBoxLayout;
        m_before = new QSpinBox(this);
        m_after = new QSpinBox(this);
        for (auto *spin : {m_before, m_after}) {
            spin->setRange(0, 100);
            spin->setValue(2);
        }
        m_before->setPrefix(QStringLiteral("上方预加载 "));
        m_after->setPrefix(QStringLiteral("下方预加载 "));
        controls->addWidget(m_before);
        controls->addWidget(m_after);
        layout->addLayout(controls);
        auto *poolControls = new QHBoxLayout;
        m_poolLimit = new QSpinBox(this);
        m_poolLimit->setRange(-1, 256);
        m_poolLimit->setValue(256);
        m_poolLimit->setPrefix(QStringLiteral("空闲池上限 "));
        m_poolLimit->setSpecialValueText(QStringLiteral("空闲池不限量"));
        m_trim = new QPushButton(QStringLiteral("按上限裁剪池"), this);
        m_clearPool = new QPushButton(QStringLiteral("清空空闲池"), this);
        poolControls->addWidget(m_poolLimit);
        poolControls->addWidget(m_trim);
        poolControls->addWidget(m_clearPool);
        layout->addLayout(poolControls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // The adapter creates widgets only for rows that need materialization;
        // true transfers adapter ownership to the view.
        m_view->setAdapter(new PoolAdapter, true);
        m_view->setUniformItemHeight(36);
        // Materialize two extra items before and after the viewport. These are
        // item counts, not pixel distances; more overscan retains more widgets.
        m_view->setOverscan(2, 2);
        m_view->setModel(new LogicalModel(m_view));
        connect(m_before, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyOverscan(); });
        connect(m_after, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyOverscan(); });
        connect(m_poolLimit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int limit) {
            // Limits apply per WidgetType to idle widgets, not visible or pinned
            // rows. Zero disables pooling; a negative value removes the limit.
            m_view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, limit);
        });
        connect(m_trim, &QPushButton::clicked, this, [this] {
            // Retire idle widgets above each type's configured pool limit.
            m_view->recycler()->trim();
        });
        connect(m_clearPool, &QPushButton::clicked, this, [this] {
            // Clear only idle widgets. Materialized rows remain bound and usable.
            m_view->recycler()->clear();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // The inclusive visible range excludes overscan. For an empty view
            // it is invalid; stats includes materialized overscan and pinned rows.
            const auto range = m_view->visibleItemRange();
            const auto stats = m_view->stats();
            m_status->setText(QStringLiteral("可见范围（不含预加载）：%1–%2；逻辑行=%3；物化=%4；池内=%5；累计创建=%6；累计销毁=%7；复用命中=%8")
                .arg(range.first).arg(range.last).arg(stats.logicalItems)
                .arg(stats.materializedItems).arg(stats.pooledWidgets).arg(stats.createCount)
                .arg(m_view->destroyedWidgetCount()).arg(m_view->recycler()->reuseCount()));
        });
        timer->start(200);
    }

    void applyOverscan()
    {
        // Updating overscan schedules the materialization window refresh;
        // business code does not create/recycle the additional widgets itself.
        m_view->setOverscan(m_before->value(), m_after->value());
    }

    viv::VirtualListView *m_view = nullptr;
    QSpinBox *m_before = nullptr;
    QSpinBox *m_after = nullptr;
    QSpinBox *m_poolLimit = nullptr;
    QPushButton *m_trim = nullptr;
    QPushButton *m_clearPool = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_virtualization
