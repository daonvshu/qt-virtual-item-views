#include "ui_spans.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>

namespace table_spans {

class GroupSpans : public viv::TableSpanProvider
{
public:
    viv::TableSpan spanAt(const QModelIndex &index) const override
    {
        return index.column() == 0 && index.row() % 5 == 0 ? viv::TableSpan{1, 3} : viv::TableSpan{};
    }
    viv::TableSpan maximumSpan() const override { return {1, 3}; }
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableSpansPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableSpansPage> ui;

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
        // setSpan creates a view-owned map provider; custom rules can use setSpanProvider instead.
        ui->view->setSpan(1, 1, 1, 3);
        ui->view->setSpan(4, 2, 3, 2);
        m_model->setData(m_model->index(1, 1), QStringLiteral("横跨三列"));
        m_model->setData(m_model->index(4, 2), QStringLiteral("三行 × 两列"));

        connect(ui->provider, &QPushButton::clicked, this, [this] {
            // The view owns the provider, which reports spans only at anchors and declares its maximum span.
            ui->view->setSpanProvider(new GroupSpans, true);
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            ui->view->setSpanProvider(nullptr);
        });
        connect(ui->apply, &QPushButton::clicked, this, [this] {
            ui->view->setSpanProvider(nullptr);
            ui->view->setSpan(1, 1, 1, 3);
            ui->view->setSpan(4, 2, 3, 2);
        });
        connect(ui->query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            const QModelIndex anchor = ui->view->anchorIndex(index);
            const QRect rect = ui->view->spanRect(anchor);
            ui->status->setText(QStringLiteral("锚点=(%1,%2)，区域=%3×%4").arg(anchor.row()).arg(anchor.column()).arg(rect.width()).arg(rect.height()));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_spans
