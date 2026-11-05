#include "ui_diagnostics.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringListModel>
#include <QTimer>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListDiagnosticsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListDiagnosticsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->log->setMaximumBlockCount(int(viv::VirtualItemView::kLifecycleLogCapacity));
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 5000; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        m_model = new QStringListModel(records, ui->view);
        // Install a view-owned adapter and fixed row height; the diagnostic
        // code observes virtualization rather than managing widgets itself.
        ui->view->setAdapter(new TextAdapter, true);
        ui->view->setUniformItemHeight(36);
        // Enable before model assignment to include initial lifecycle activity.
        // Logging is opt-in and bounded by kLifecycleLogCapacity.
        ui->view->setLifecycleLoggingEnabled(true);
        ui->view->setModel(m_model);
        connect(ui->logging, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setLifecycleLoggingEnabled);
        connect(ui->refresh, &QPushButton::clicked, this, [this] {
            // Take a snapshot only on request so newly appended events do not
            // continuously move the log reader's scroll position.
            ui->log->setPlainText(ui->view->lifecycleLog().join(QStringLiteral("\n")));
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            // Clear recorded messages, not the view's widgets or cumulative counters.
            ui->view->clearLifecycleLog();
            ui->log->clear();
        });
        connect(ui->modify, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (index.isValid())
                m_model->setData(index, index.data().toString() + QStringLiteral(" *"));
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // logical/materialized/pooled/pinned are current counts;
            // create/bind/recycle are cumulative and can exceed the model size.
            const auto stats = ui->view->stats();
            // The inclusive range excludes overscan; an empty range has isValid()==false.
            const auto visible = ui->view->visibleItemRange();
            ui->status->setText(QStringLiteral(
                "逻辑=%1 / 物化=%2 / 池内=%3 / 保留=%4\n"
                "累计 create=%5 / bind=%6 / recycle=%7 / destroyed=%8\n"
                "当前可见索引=%9–%10 / 日志条数=%11")
                .arg(stats.logicalItems).arg(stats.materializedItems).arg(stats.pooledWidgets)
                .arg(stats.pinnedWidgets).arg(stats.createCount).arg(stats.bindCount)
                .arg(stats.recycleCount).arg(ui->view->destroyedWidgetCount())
                .arg(visible.first).arg(visible.last).arg(ui->view->lifecycleLog().size()));
        });
        timer->start(200);
    }

    QStringListModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_diagnostics
