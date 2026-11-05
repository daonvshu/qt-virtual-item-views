#include "ui_virtualization.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/widgetrecycler.h>
#include <QAbstractListModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListVirtualizationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListVirtualizationPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // The adapter creates widgets only for rows that need materialization;
        // true transfers adapter ownership to the view.
        ui->view->setAdapter(new PoolAdapter, true);
        ui->view->setUniformItemHeight(36);
        // Materialize two extra items before and after the viewport. These are
        // item counts, not pixel distances; more overscan retains more widgets.
        ui->view->setOverscan(2, 2);
        ui->view->setModel(new LogicalModel(ui->view));
        connect(ui->before, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyOverscan(); });
        connect(ui->after, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyOverscan(); });
        connect(ui->poolLimit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int limit) {
            // Limits apply per WidgetType to idle widgets, not visible or pinned
            // rows. Zero disables pooling; a negative value removes the limit.
            ui->view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, limit);
        });
        connect(ui->trim, &QPushButton::clicked, this, [this] {
            // Retire idle widgets above each type's configured pool limit.
            ui->view->recycler()->trim();
        });
        connect(ui->clearPool, &QPushButton::clicked, this, [this] {
            // Clear only idle widgets. Materialized rows remain bound and usable.
            ui->view->recycler()->clear();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // The inclusive visible range excludes overscan. For an empty view
            // it is invalid; stats includes materialized overscan and pinned rows.
            const auto range = ui->view->visibleItemRange();
            const auto stats = ui->view->stats();
            ui->status->setText(QStringLiteral("可见范围（不含预加载）：%1–%2；逻辑行=%3；物化=%4；池内=%5；累计创建=%6；累计销毁=%7；复用命中=%8")
                .arg(range.first).arg(range.last).arg(stats.logicalItems)
                .arg(stats.materializedItems).arg(stats.pooledWidgets).arg(stats.createCount)
                .arg(ui->view->destroyedWidgetCount()).arg(ui->view->recycler()->reuseCount()));
        });
        timer->start(200);
    }

    void applyOverscan()
    {
        // Updating overscan schedules the materialization window refresh;
        // business code does not create/recycle the additional widgets itself.
        ui->view->setOverscan(ui->before->value(), ui->after->value());
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_virtualization
