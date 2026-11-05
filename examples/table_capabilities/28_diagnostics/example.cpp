#include "ui_diagnostics.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

namespace table_diagnostics {

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

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableDiagnosticsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableDiagnosticsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(120, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Lifecycle logging is disabled by default; enable it to inspect binding, recycling and pinning.
        ui->view->setLifecycleLoggingEnabled(true);

        connect(ui->stats, &QPushButton::clicked, this, [this] {
            const auto s = ui->view->stats();
            ui->status->setText(QStringLiteral("create=%1 bind=%2 recycle=%3 当前格=%4").arg(s.createCount).arg(s.bindCount).arg(s.recycleCount).arg(ui->view->materializedCellIndexes().size()));
        });
        connect(ui->log, &QPushButton::clicked, this, [this] {
            const QStringList log = ui->view->lifecycleLog();
            // The log holds at most 400 entries; show only the last eight here.
            ui->status->setText(log.mid(qMax(0, int(log.size()) - 8)).join(QStringLiteral("\n")));
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            ui->view->clearLifecycleLog();
            ui->status->clear();
        });
        connect(ui->enabled, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setLifecycleLoggingEnabled(enabled);
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_diagnostics
