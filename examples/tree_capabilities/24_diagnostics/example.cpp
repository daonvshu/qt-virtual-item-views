#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QTimer>
#include <QPlainTextEdit>

namespace tree_diagnostics {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Every bind replaces the previous node's content, including after scrolling.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 36);
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
        auto *description = new QLabel(QStringLiteral("24 运行诊断：滚动、展开或收起后查看物化统计与有界的 create/bind/unbind/recycle 日志。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        logging = new QCheckBox(QStringLiteral("记录生命周期"), this);
        logging->setChecked(true);
        controls->addWidget(logging);
        snapshot = new QPushButton(QStringLiteral("刷新日志快照"), this);
        controls->addWidget(snapshot);
        clear = new QPushButton(QStringLiteral("清空日志"), this);
        controls->addWidget(clear);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        m_log = new QPlainTextEdit(this);
        m_log->setReadOnly(true);
        m_log->setMaximumBlockCount(int(viv::VirtualItemView::kLifecycleLogCapacity));
        m_log->setMaximumHeight(180);
        layout->addWidget(m_log);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 20; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 80; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // Enable logging before the model is assigned to include the first binding pass.
        m_view->setLifecycleLoggingEnabled(true);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        connect(logging, &QCheckBox::toggled, m_view, &viv::VirtualItemView::setLifecycleLoggingEnabled);
        connect(snapshot, &QPushButton::clicked, this, [this] {
            // Snapshot on demand so ongoing activity does not scroll the log reader.
            m_log->setPlainText(m_view->lifecycleLog().join(QStringLiteral("\n")));
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            m_view->clearLifecycleLog();
            m_log->clear();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            if (!isVisible())
                return;
            const auto stats = m_view->stats();
            m_status->setText(QStringLiteral("映射节点=%1 / 物化=%2 / 池=%3 / 保留=%4 / 累计创建=%5 / 绑定=%6 / 回收=%7 / 日志=%8")
                .arg(m_view->visibleRowCount()).arg(stats.materializedItems).arg(stats.pooledWidgets)
                .arg(stats.pinnedWidgets).arg(stats.createCount).arg(stats.bindCount).arg(stats.recycleCount)
                .arg(m_view->lifecycleLog().size()));
        });
        timer->start(200);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *logging = nullptr;
    QPushButton *snapshot = nullptr;
    QPushButton *clear = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_diagnostics
