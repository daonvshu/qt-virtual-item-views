#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("14 双向滚动与定位：一亿行按需生成数据，纵向 64 位像素偏移，横向像素偏移定位"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_end = new QPushButton(QStringLiteral("定位最后一格"), this);
        controls->addWidget(m_end);
        m_home = new QPushButton(QStringLiteral("回到左上"), this);
        controls->addWidget(m_home);
        m_step = new QPushButton(QStringLiteral("双向滚动 100px"), this);
        controls->addWidget(m_step);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Disable the row header for this large-model example.
        m_view->setVerticalHeaderVisible(false);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new Model(m_view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // Scroll in pixels; logical offsets are not limited by the integer scrollbar range.
        m_view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
        m_view->setWheelScrollPixels(48);
        m_view->setHorizontalWheelPixels(48);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            m_status->setText(QStringLiteral("y=%1 / %2，x=%3 / %4").arg(m_view->verticalOffset()).arg(m_view->maximumVerticalOffset()).arg(m_view->horizontalOffset()).arg(m_view->maximumHorizontalOffset()));
        });
        timer->start(250);

        connect(m_end, &QPushButton::clicked, this, [this] {
            // scrollTo positions only the row; position the last column separately.
            m_view->scrollTo(m_model->index(m_model->rowCount() - 1, m_model->columnCount() - 1));
            m_view->setHorizontalOffset(m_view->maximumHorizontalOffset());
        });
        connect(m_home, &QPushButton::clicked, this, [this] {
            m_view->setVerticalOffset(0);
            m_view->setHorizontalOffset(0);
        });
        connect(m_step, &QPushButton::clicked, this, [this] {
            m_view->scrollByPixels(100);
            m_view->scrollByHorizontalPixels(100);
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    Model *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_end = nullptr;
    QPushButton *m_home = nullptr;
    QPushButton *m_step = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_scrolling
