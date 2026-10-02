#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolBar>

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
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("VirtualTreeTableView"));
    auto *controls = window.addToolBar(QStringLiteral("树表格"));

    QStandardItemModel model(&window);
    model.setHorizontalHeaderLabels({QStringLiteral("节点"), QStringLiteral("类型"),
                                     QStringLiteral("状态")});
    for (int device = 0; device < 20; ++device) {
        auto deviceRow = makeRow(QStringLiteral("设备 %1").arg(device), QStringLiteral("设备"),
                                 QStringLiteral("在线"));
        QStandardItem *deviceItem = deviceRow.first();
        deviceItem->setData(0, viv::VirtualTreeTableView::NodeRowSpacingBelowRole);
        deviceItem->setData(device > 0 ? 12 : 0,
                            viv::VirtualTreeTableView::NodeRowSpacingAboveRole);
        for (int channel = 0; channel < 8; ++channel) {
            auto channelRow = makeRow(QStringLiteral("通道 %1").arg(channel),
                                      QStringLiteral("通道"));
            QStandardItem *channelItem = channelRow.first();
            channelItem->setData(4, viv::VirtualTreeTableView::NodeRowSpacingRole);
            for (int point = 0; point < 4; ++point) {
                auto pointRow = makeRow(QStringLiteral("测点 %1").arg(point),
                                        QStringLiteral("测点"), QStringLiteral("正常"));
                pointRow.first()->setData(0, viv::VirtualTreeTableView::NodeRowSpacingRole);
                channelItem->appendRow(pointRow);
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
    window.setCentralWidget(&view);

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

    window.addToolBarBreak();
    auto *spacingToolbar = window.addToolBar(QStringLiteral("间距"));
    spacingToolbar->addWidget(new QLabel(QStringLiteral("默认行间距: "), &window));
    auto *rowSpacing = new QSpinBox(&window);
    rowSpacing->setRange(0, 100);
    rowSpacing->setSuffix(QStringLiteral(" px"));
    rowSpacing->setValue(view.rowSpacing());
    spacingToolbar->addWidget(rowSpacing);

    QObject::connect(rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualItemView::setRowSpacing);
    spacingToolbar->addWidget(new QLabel(QStringLiteral("列间距: "), &window));
    auto *columnSpacing = new QSpinBox(&window);
    columnSpacing->setRange(0, 100);
    columnSpacing->setSuffix(QStringLiteral(" px"));
    columnSpacing->setValue(view.columnSpacing());
    spacingToolbar->addWidget(columnSpacing);
    QObject::connect(columnSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualTableView::setColumnSpacing);
    auto *verticalThrough = new QCheckBox(QStringLiteral("竖线穿过行间距"), &window);
    verticalThrough->setChecked(view.verticalSpacingLineThroughRowSpacing());
    spacingToolbar->addWidget(verticalThrough);
    auto *horizontalThrough = new QCheckBox(QStringLiteral("横线穿过列间距"), &window);
    horizontalThrough->setChecked(view.horizontalSpacingLineThroughColumnSpacing());
    spacingToolbar->addWidget(horizontalThrough);
    QObject::connect(verticalThrough, &QCheckBox::toggled,
                     &view, &viv::VirtualTableView::setVerticalSpacingLineThroughRowSpacing);
    QObject::connect(horizontalThrough, &QCheckBox::toggled,
                     &view, &viv::VirtualTableView::setHorizontalSpacingLineThroughColumnSpacing);

    window.addToolBarBreak();
    auto *gridToolbar = window.addToolBar(QStringLiteral("分割线"));
    auto *verticalGrid = new QCheckBox(QStringLiteral("显示竖向分割线"), &window);
    verticalGrid->setChecked(view.verticalGridLinesVisible());
    gridToolbar->addWidget(verticalGrid);
    auto *horizontalGrid = new QCheckBox(QStringLiteral("显示横向分割线"), &window);
    horizontalGrid->setChecked(view.horizontalGridLinesVisible());
    gridToolbar->addWidget(horizontalGrid);
    gridToolbar->addWidget(new QLabel(QStringLiteral("竖线宽度: "), &window));
    auto *verticalLineWidth = new QSpinBox(&window);
    verticalLineWidth->setRange(1, 8);
    verticalLineWidth->setValue(view.verticalGridLineWidth());
    gridToolbar->addWidget(verticalLineWidth);
    gridToolbar->addWidget(new QLabel(QStringLiteral("横线宽度: "), &window));
    auto *horizontalLineWidth = new QSpinBox(&window);
    horizontalLineWidth->setRange(1, 8);
    horizontalLineWidth->setValue(view.horizontalGridLineWidth());
    gridToolbar->addWidget(horizontalLineWidth);
    auto *verticalLineColor = new QPushButton(QStringLiteral("竖线颜色"), &window);
    auto *horizontalLineColor = new QPushButton(QStringLiteral("横线颜色"), &window);
    const auto setColorSwatch = [](QPushButton *button, const QColor &color) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        button->setIcon(QIcon(swatch));
    };
    setColorSwatch(verticalLineColor, view.verticalGridLineColor().isValid()
        ? view.verticalGridLineColor() : viv::VirtualTableView::sectionSeparatorColor(&view));
    setColorSwatch(horizontalLineColor, view.horizontalGridLineColor().isValid()
        ? view.horizontalGridLineColor() : viv::VirtualTableView::sectionSeparatorColor(&view));
    gridToolbar->addWidget(verticalLineColor);
    gridToolbar->addWidget(horizontalLineColor);
    QObject::connect(verticalGrid, &QCheckBox::toggled,
                     &view, &viv::VirtualTableView::setVerticalGridLinesVisible);
    QObject::connect(horizontalGrid, &QCheckBox::toggled,
                     &view, &viv::VirtualTableView::setHorizontalGridLinesVisible);
    QObject::connect(verticalLineWidth, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualTableView::setVerticalGridLineWidth);
    QObject::connect(horizontalLineWidth, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualTableView::setHorizontalGridLineWidth);
    QObject::connect(verticalLineColor, &QPushButton::clicked, &view,
                     [&view, verticalLineColor, setColorSwatch]() {
                         const QColor initial = view.verticalGridLineColor().isValid()
                             ? view.verticalGridLineColor()
                             : viv::VirtualTableView::sectionSeparatorColor(&view);
                         const QColor color = QColorDialog::getColor(initial, &view,
                                                                      QStringLiteral("竖线颜色"));
                         if (color.isValid()) {
                             view.setVerticalGridLineColor(color);
                             setColorSwatch(verticalLineColor, color);
                         }
                     });
    QObject::connect(horizontalLineColor, &QPushButton::clicked, &view,
                     [&view, horizontalLineColor, setColorSwatch]() {
                         const QColor initial = view.horizontalGridLineColor().isValid()
                             ? view.horizontalGridLineColor()
                             : viv::VirtualTableView::sectionSeparatorColor(&view);
                         const QColor color = QColorDialog::getColor(initial, &view,
                                                                      QStringLiteral("横线颜色"));
                         if (color.isValid()) {
                             view.setHorizontalGridLineColor(color);
                             setColorSwatch(horizontalLineColor, color);
                         }
                     });

    gridToolbar->addWidget(new QLabel(QStringLiteral("横线范围: "), &window));
    auto *lineExtent = new QComboBox(&window);
    lineExtent->addItems({QStringLiteral("仅节点"), QStringLiteral("节点及图标"),
                          QStringLiteral("整行")});
    lineExtent->setCurrentIndex(int(view.rowGridLineExtent()));
    gridToolbar->addWidget(lineExtent);
    QObject::connect(lineExtent, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &view, [&view](int choice) {
                         view.setRowGridLineExtent(
                             static_cast<viv::VirtualTreeTableView::RowGridLineExtent>(choice));
                     });

    window.resize(1100, 560);
    window.show();
    return app.exec();
}
