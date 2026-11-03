#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace treetable_diagnostics {

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
        // Replace old content on every bind; a widget can be reused for a different row or column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

// Each chapter owns this helper; no implementation is shared with other pages.
// Only column zero owns children, and every child row supplies the full column schema.
static void appendChildren(QStandardItemModel *model)
{
    const int columns = model->columnCount();
    for (int group = 0; group < model->rowCount(); ++group) {
        auto *branch = model->item(group, 0);
        branch->removeRows(0, branch->rowCount());
        branch->setColumnCount(columns);
        for (int row = 0; row < 8; ++row) {
            QList<QStandardItem *> cells;
            for (int column = 0; column < columns; ++column)
                cells.append(new QStandardItem(QStringLiteral("节点 %1.%2 / 列 %3")
                    .arg(group + 1).arg(row + 1).arg(column + 1)));
            branch->appendRow(cells);
            if (row == 0) {
                for (int leaf = 0; leaf < 3; ++leaf) {
                    QList<QStandardItem *> leaves;
                    for (int column = 0; column < columns; ++column)
                        leaves.append(new QStandardItem(QStringLiteral("叶子 %1.%2.%3 / 列 %4")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1).arg(column + 1)));
                    cells.first()->appendRow(leaves);
                }
            }
        }
    }
}

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
        auto *description = new QLabel(QStringLiteral("33 统计与性能诊断：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_stats = new QPushButton(QStringLiteral("统计快照"), this);
        controls->addWidget(m_stats);
        m_log = new QPushButton(QStringLiteral("最近生命周期记录"), this);
        controls->addWidget(m_log);
        m_clear = new QPushButton(QStringLiteral("清空日志"), this);
        controls->addWidget(m_clear);
        m_enabled = new QCheckBox(QStringLiteral("记录日志"), this);
        m_enabled->setChecked(true);
        controls->addWidget(m_enabled);
        m_performance = new QCheckBox(QStringLiteral("每秒性能摘要（调试日志）"), this);
        controls->addWidget(m_performance);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        m_view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        m_view->setExpansionAnimationEnabled(false);
        m_view->expandRecursively(m_model->index(0, 0));
        m_view->setExpansionAnimationEnabled(true);
        m_view->setColumnWidth(0, 280);
        // Lifecycle logging is disabled by default; enable it to inspect binding, recycling and pinning.
        m_view->setLifecycleLoggingEnabled(true);

        connect(m_stats, &QPushButton::clicked, this, [this] {
            const auto s = m_view->stats();
            m_status->setText(QStringLiteral("create=%1 bind=%2 recycle=%3 物化格=%4 可见树节点=%5").arg(s.createCount).arg(s.bindCount).arg(s.recycleCount).arg(m_view->materializedCellIndexes().size()).arg(m_view->visibleRowCount()));
        });
        connect(m_log, &QPushButton::clicked, this, [this] {
            const QStringList log = m_view->lifecycleLog();
            // The log holds at most 400 entries; show only the last eight here.
            m_status->setText(log.mid(qMax(0, int(log.size()) - 8)).join(QStringLiteral("\n")));
        });
        connect(m_clear, &QPushButton::clicked, this, [this] {
            m_view->clearLifecycleLog();
            m_status->clear();
        });
        // Timing is opt-in and aggregated; disabled probes skip statistics collection.
        // Filter summaries with viv.performance.refresh / viv.performance.paint;
        // enable viv.performance.details for the complete stage timing table.
        connect(m_performance, &QCheckBox::toggled, m_view, &viv::VirtualItemView::setPerformanceLoggingEnabled);
        connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setLifecycleLoggingEnabled(enabled);
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_stats = nullptr;
    QPushButton *m_log = nullptr;
    QPushButton *m_clear = nullptr;
    QCheckBox *m_enabled = nullptr;
    QCheckBox *m_performance = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_diagnostics
