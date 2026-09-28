#include <smoothscroll/smoothscrollcontroller.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolBar>

namespace {

constexpr int kRows = 50000;
constexpr int kColumns = 24;
constexpr int kRowHeight = 36;

class TableModel : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : kRows;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : kColumns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return QStringLiteral("Row %1 / Column %2")
            .arg(index.row() + 1).arg(index.column() + 1);
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        return orientation == Qt::Horizontal
            ? QStringLiteral("Column %1").arg(section + 1)
            : QString::number(section + 1);
    }
};

class TableRow : public QWidget
{
public:
    explicit TableRow(QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < kColumns; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            layout->addWidget(label);
            m_labels.append(label);
        }
    }

    void bind(const QModelIndex &index)
    {
        for (int column = 0; column < m_labels.size(); ++column)
            m_labels.at(column)->setText(index.sibling(index.row(), column).data().toString());
    }

private:
    QVector<QLabel *> m_labels;
};

class TableAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new TableRow(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<TableRow *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(1400, kRowHeight);
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    TableModel model;
    TableAdapter adapter;
    QMainWindow window;

    auto *table = new viv::VirtualTableView(&window);
    table->setTableAdapter(&adapter);
    table->setUniformItemHeight(kRowHeight);
    table->setDefaultColumnWidth(170);
    table->setWheelScrollPixels(48);
    table->setHorizontalWheelPixels(48);
    table->setModel(&model);
    table->setFrozenColumns({0});
    window.setCentralWidget(table);

    auto *controller = new sscroll::SmoothScrollController(table, table);
    sscroll::SmoothScrollSettings settings;
    settings.wheelStep = 48;
    controller->setSettings(settings);

    auto *toolbar = window.addToolBar(QStringLiteral("Scrolling"));
    auto *smooth = new QCheckBox(QStringLiteral("Smooth wheel"), &window);
    smooth->setChecked(true);
    toolbar->addWidget(smooth);
    QObject::connect(smooth, &QCheckBox::toggled,
                     controller, &sscroll::SmoothScrollController::setEnabled);

    auto *offsets = new QLabel(&window);
    window.statusBar()->addPermanentWidget(offsets);
    const auto updateOffsets = [table, offsets]() {
        offsets->setText(QStringLiteral("Vertical: %1 px    Horizontal: %2 px")
                             .arg(table->verticalOffset()).arg(table->horizontalOffset()));
    };
    QObject::connect(table->verticalScrollBar(), &QScrollBar::valueChanged,
                     &window, updateOffsets);
    QObject::connect(table, &viv::VirtualTableView::horizontalOffsetChanged,
                     &window, updateOffsets);
    updateOffsets();

    window.setWindowTitle(QStringLiteral("VirtualTableView + SmoothScrollbar (Qt 5)"));
    window.resize(1000, 620);
    window.show();
    return app.exec();
}
