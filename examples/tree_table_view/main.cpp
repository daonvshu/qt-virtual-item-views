#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QProgressBar>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace {

class TreeTableRow : public QWidget
{
public:
    explicit TreeTableRow(QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < 3; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            m_labels[column] = new QLabel(host);
            layout->addWidget(m_labels[column]);
        }
    }

    void bind(const QModelIndex &index)
    {
        for (int column = 0; column < 3; ++column) {
            const QModelIndex cell = index.siblingAtColumn(column);
            m_labels[column]->setText(cell.data().toString());
        }
    }

private:
    QLabel *m_labels[3] = {nullptr, nullptr, nullptr};
};

class TreeTableAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new TreeTableRow(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<TreeTableRow *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 32); }
};

class TreeTableCellAdapter : public viv::CellWidgetAdapter
{
public:
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        const bool pointStatus = index.column() == 2 && index.parent().isValid()
            && index.parent().parent().isValid();
        return pointStatus ? 1 : 0;
    }

    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *progress = new QProgressBar(parent);
            progress->setRange(0, 100);
            return progress;
        }
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        const QString text = index.data().toString();
        if (cellWidgetType(index) == 1) {
            auto *progress = static_cast<QProgressBar *>(widget);
            progress->setFormat(text);
            progress->setValue(text == QStringLiteral("正常") ? 100 : 0);
        } else {
            static_cast<QLabel *>(widget)->setText(text);
        }
    }
};

QList<QStandardItem *> makeRow(const QString &name, const QString &type,
                               const QString &state = QString())
{
    QList<QStandardItem *> row;
    row << new QStandardItem(name) << new QStandardItem(type);
    if (!state.isNull())
        row << new QStandardItem(state);
    return row;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle(QStringLiteral("VirtualTreeTableView"));
    auto *layout = new QVBoxLayout(&window);
    auto *controls = new QHBoxLayout;
    layout->addLayout(controls);

    QStandardItemModel model(&window);
    model.setHorizontalHeaderLabels({QStringLiteral("节点"), QStringLiteral("类型"),
                                     QStringLiteral("状态")});
    for (int device = 0; device < 20; ++device) {
        auto deviceRow = makeRow(QStringLiteral("设备 %1").arg(device), QStringLiteral("设备"),
                                 QStringLiteral("在线"));
        QStandardItem *deviceItem = deviceRow.first();
        for (int channel = 0; channel < 8; ++channel) {
            auto channelRow = makeRow(QStringLiteral("通道 %1").arg(channel),
                                      QStringLiteral("通道"));
            QStandardItem *channelItem = channelRow.first();
            for (int point = 0; point < 4; ++point) {
                channelItem->appendRow(makeRow(QStringLiteral("测点 %1").arg(point),
                                               QStringLiteral("测点"),
                                               QStringLiteral("正常")));
            }
            deviceItem->appendRow(channelRow);
        }
        model.appendRow(deviceRow);
    }

    viv::VirtualTreeTableView view;
    view.setModel(&model);
    view.setTableAdapter(new TreeTableAdapter, true);
    view.setCellAdapter(new TreeTableCellAdapter, true);
    view.setUniformItemHeight(32);
    view.setColumnWidth(0, 240);
    view.setColumnWidth(1, 160);
    view.setColumnWidth(2, 160);
    view.setColumnDragEnabled(true);
    view.setFrozenColumns({0});
    view.setVisualStateBackgroundVisible(true);
    view.setHoverBackgroundColor(QColor(231, 244, 237));
    view.setSelectedBackgroundColor(QColor(185, 217, 241));
    view.expand(model.index(0, 0));
    layout->addWidget(&view);

    auto *mode = new QComboBox(&window);
    mode->addItems({QStringLiteral("行控件"), QStringLiteral("单元格控件")});
    controls->addWidget(mode);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &view, [&view](int choice) {
                         using Mode = viv::VirtualTableView::MaterializationMode;
                         using Selection = viv::VirtualTableView::SelectionBehavior;
                         view.setMaterializationMode(choice == 0 ? Mode::RowWidgets
                                                                 : Mode::CellWidgets);
                         view.setSelectionBehavior(choice == 0 ? Selection::SelectRows
                                                               : Selection::SelectItems);
                         const QModelIndex current = view.currentIndex();
                         if (current.isValid())
                             view.setCurrentIndex(current);
                         else if (view.selectionModel())
                             view.selectionModel()->clearSelection();
                     });

    auto *rowSpacing = new QSpinBox(&window);
    rowSpacing->setRange(0, 24);
    rowSpacing->setPrefix(QStringLiteral("行距 "));
    controls->addWidget(rowSpacing);
    QObject::connect(rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualItemView::setRowSpacing);

    auto *depthSpacing = new QSpinBox(&window);
    depthSpacing->setRange(0, 24);
    depthSpacing->setPrefix(QStringLiteral("深度 1 行距 "));
    controls->addWidget(depthSpacing);
    QObject::connect(depthSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, [&view](int pixels) { view.setDepthRowSpacing(1, pixels); });

    auto *columnSpacing = new QSpinBox(&window);
    columnSpacing->setRange(0, 24);
    columnSpacing->setPrefix(QStringLiteral("列距 "));
    controls->addWidget(columnSpacing);
    QObject::connect(columnSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualTableView::setColumnSpacing);

    auto *lineExtent = new QComboBox(&window);
    lineExtent->addItems({QStringLiteral("横线整行"), QStringLiteral("横线避开缩进"),
                          QStringLiteral("横线避开图标")});
    controls->addWidget(lineExtent);
    QObject::connect(lineExtent, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &view, [&view](int choice) {
                         using Extent = viv::VirtualTreeTableView::RowGridLineExtent;
                         view.setRowGridLineExtent(choice == 1 ? Extent::NodeAndIcon
                                                   : choice == 2 ? Extent::NodeOnly
                                                                 : Extent::FullWidth);
                     });
    controls->addStretch();

    window.resize(860, 560);
    window.show();
    return app.exec();
}
