#include "ui_binding_context.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>

struct TableRecord
{
    QString title;
    QString owner;
    int progress = 0;
};
Q_DECLARE_METATYPE(TableRecord)
namespace table_binding_context {

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent) : QWidget(parent) { setupUi(); }
    QVector<QLabel *> labels;
private:
    void setupUi()
    {
        for (int column = 0; column < 3; ++column) {
            // The framework manages ColumnHost position, width, visibility and clipping in frozen panes.
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            layout->addWidget(label);
            labels.append(label);
        }
    }
};
class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new RowWidget(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        const TableRecord record = context.rowData.value<TableRecord>();
        const QStringList fields{record.title, record.owner, QString::number(record.progress)};
        auto *row = static_cast<RowWidget *>(widget);
        for (int column = 0; column < row->labels.size(); ++column)
            row->labels[column]->setText(fields.value(column));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        for (QLabel *label : static_cast<RowWidget *>(widget)->labels)
            label->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(0, 40); }
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const viv::ItemBindingContext &context) override
    {
        // Each cell receives the complete row payload; logicalColumn is always a model column.
        const TableRecord record = context.rowData.value<TableRecord>();
        const QStringList fields{record.title, record.owner, QString::number(record.progress)};
        static_cast<QLabel *>(widget)->setText(fields.value(context.logicalColumn));
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableBindingContextPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableBindingContextPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Both adapters receive complete rowData; switching modes changes widget granularity.
        ui->view->setTableAdapter(new RowAdapter, true);
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(60, 3, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Choose the row payload role; the view reads it from logical column zero.
        ui->view->setItemDataRole(viv::kDefaultItemDataRole);
        for (int r = 0; r < m_model->rowCount(); ++r) {
            const TableRecord record{QStringLiteral("记录 %1").arg(r + 1), QStringLiteral("负责人"), r % 101};
            m_model->setData(m_model->index(r, 0), QVariant::fromValue(record), viv::kDefaultItemDataRole);
        }

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setMaterializationMode(mode == 0 ? viv::VirtualTableView::MaterializationMode::CellWidgets
                                                    : viv::VirtualTableView::MaterializationMode::RowWidgets);
        });
        connect(ui->update, &QPushButton::clicked, this, [this] {
            const QModelIndex current = ui->view->currentIndex();
            if (!current.isValid()) return;
            const QModelIndex payloadIndex = current.siblingAtColumn(0);
            auto record = payloadIndex.data(viv::kDefaultItemDataRole).value<TableRecord>();
            record.progress = (record.progress + 10) % 101;
            // The setData notification rebinds every materialized cell in this row.
            m_model->setData(payloadIndex, QVariant::fromValue(record), viv::kDefaultItemDataRole);
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_binding_context
