#include "ui_diagnostics.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeDiagnosticsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeDiagnosticsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->log->setMaximumBlockCount(int(viv::VirtualItemView::kLifecycleLogCapacity));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 20; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 80; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // Enable logging before the model is assigned to include the first binding pass.
        ui->view->setLifecycleLoggingEnabled(true);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        connect(ui->logging, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setLifecycleLoggingEnabled);
        connect(ui->snapshot, &QPushButton::clicked, this, [this] {
            // Snapshot on demand so ongoing activity does not scroll the log reader.
            ui->log->setPlainText(ui->view->lifecycleLog().join(QStringLiteral("\n")));
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            ui->view->clearLifecycleLog();
            ui->log->clear();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            if (!isVisible())
                return;
            const auto stats = ui->view->stats();
            ui->status->setText(QStringLiteral("映射节点=%1 / 物化=%2 / 池=%3 / 保留=%4 / 累计创建=%5 / 绑定=%6 / 回收=%7 / 日志=%8")
                .arg(ui->view->visibleRowCount()).arg(stats.materializedItems).arg(stats.pooledWidgets)
                .arg(stats.pinnedWidgets).arg(stats.createCount).arg(stats.bindCount).arg(stats.recycleCount)
                .arg(ui->view->lifecycleLog().size()));
        });
        timer->start(200);
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_diagnostics
