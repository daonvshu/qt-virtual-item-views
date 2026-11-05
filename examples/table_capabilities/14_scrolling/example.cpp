#include "ui_scrolling.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QAbstractTableModel>
#include <QScrollBar>
#include <QTimer>

namespace table_scrolling {

class Model : public QAbstractTableModel
{
public:
    explicit Model(QObject *parent) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 100000000; }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 200; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        return QStringLiteral("行 %1 / 列 %2").arg(index.row() + 1).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1) : QString::number(section + 1);
    }
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableScrollingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableScrollingPage> ui;

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
        // Disable the row header for this large-model example.
        ui->view->setVerticalHeaderVisible(false);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new Model(ui->view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Scroll in pixels; logical offsets are not limited by the integer scrollbar range.
        ui->view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
        ui->view->setWheelScrollPixels(48);
        ui->view->setHorizontalWheelPixels(48);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            ui->status->setText(QStringLiteral("y=%1 / %2，x=%3 / %4").arg(ui->view->verticalOffset()).arg(ui->view->maximumVerticalOffset()).arg(ui->view->horizontalOffset()).arg(ui->view->maximumHorizontalOffset()));
        });
        timer->start(250);

        connect(ui->end, &QPushButton::clicked, this, [this] {
            // scrollTo positions only the row; position the last column separately.
            ui->view->scrollTo(m_model->index(m_model->rowCount() - 1, m_model->columnCount() - 1));
            ui->view->setHorizontalOffset(ui->view->maximumHorizontalOffset());
        });
        connect(ui->home, &QPushButton::clicked, this, [this] {
            ui->view->setVerticalOffset(0);
            ui->view->setHorizontalOffset(0);
        });
        connect(ui->step, &QPushButton::clicked, this, [this] {
            ui->view->scrollByPixels(100);
            ui->view->scrollByHorizontalPixels(100);
        });
    }

    Model *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_scrolling
