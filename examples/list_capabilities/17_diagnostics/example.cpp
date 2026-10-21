#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>

namespace list_diagnostics {

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
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
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
        auto *intro = new QLabel(QStringLiteral("17 诊断：滚动列表后查看统计和生命周期日志；日志有界，默认最多 400 条。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *controls = new QHBoxLayout;
        m_logging = new QCheckBox(QStringLiteral("记录生命周期"), this);
        m_logging->setChecked(true);
        m_refresh = new QPushButton(QStringLiteral("刷新日志快照"), this);
        m_clear = new QPushButton(QStringLiteral("清空日志"), this);
        m_modify = new QPushButton(QStringLiteral("修改当前行"), this);
        controls->addWidget(m_logging);
        controls->addWidget(m_refresh);
        controls->addWidget(m_clear);
        controls->addWidget(m_modify);
        layout->addLayout(controls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        m_log = new QPlainTextEdit(this);
        m_log->setReadOnly(true);
        m_log->setMaximumBlockCount(int(viv::VirtualItemView::kLifecycleLogCapacity));
        m_log->setMaximumHeight(200);
        layout->addWidget(m_log);
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 5000; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        m_model = new QStringListModel(records, m_view);
        // Install a view-owned adapter and fixed row height; the diagnostic
        // code observes virtualization rather than managing widgets itself.
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        // Enable before model assignment to include initial lifecycle activity.
        // Logging is opt-in and bounded by kLifecycleLogCapacity.
        m_view->setLifecycleLoggingEnabled(true);
        m_view->setModel(m_model);
        connect(m_logging, &QCheckBox::toggled, m_view, &viv::VirtualItemView::setLifecycleLoggingEnabled);
        connect(m_refresh, &QPushButton::clicked, this, [this] {
            // Take a snapshot only on request so newly appended events do not
            // continuously move the log reader's scroll position.
            m_log->setPlainText(m_view->lifecycleLog().join(QStringLiteral("\n")));
        });
        connect(m_clear, &QPushButton::clicked, this, [this] {
            // Clear recorded messages, not the view's widgets or cumulative counters.
            m_view->clearLifecycleLog();
            m_log->clear();
        });
        connect(m_modify, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid())
                m_model->setData(index, index.data().toString() + QStringLiteral(" *"));
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // logical/materialized/pooled/pinned are current counts;
            // create/bind/recycle are cumulative and can exceed the model size.
            const auto stats = m_view->stats();
            // The inclusive range excludes overscan; an empty range has isValid()==false.
            const auto visible = m_view->visibleItemRange();
            m_status->setText(QStringLiteral(
                "逻辑=%1 / 物化=%2 / 池内=%3 / 保留=%4\n"
                "累计 create=%5 / bind=%6 / recycle=%7 / destroyed=%8\n"
                "当前可见索引=%9–%10 / 日志条数=%11")
                .arg(stats.logicalItems).arg(stats.materializedItems).arg(stats.pooledWidgets)
                .arg(stats.pinnedWidgets).arg(stats.createCount).arg(stats.bindCount)
                .arg(stats.recycleCount).arg(m_view->destroyedWidgetCount())
                .arg(visible.first).arg(visible.last).arg(m_view->lifecycleLog().size()));
        });
        timer->start(200);
    }

    viv::VirtualListView *m_view = nullptr;
    QStringListModel *m_model = nullptr;
    QCheckBox *m_logging = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_modify = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_diagnostics
