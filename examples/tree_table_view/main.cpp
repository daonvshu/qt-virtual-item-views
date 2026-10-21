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

struct NodeData
{
    QString name;
    QString type;
    QString state;
};
Q_DECLARE_METATYPE(NodeData)

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

    void bind(const NodeData &data, int depth)
    {
        m_labels[0]->setText(data.name);
        m_labels[1]->setText(data.type);
        m_labels[2]->setText(data.state);
        setToolTip(QStringLiteral("节点深度：%1").arg(depth));
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

    void bindWidget(viv::WidgetType, QWidget *widget,
                    const viv::ItemBindingContext &context) override
    {
        static_cast<TreeTableRow *>(widget)->bind(context.rowData.value<NodeData>(), context.depth);
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(0, 32); }
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

    void bindCellWidget(QWidget *widget, const viv::ItemBindingContext &context) override
    {
        const NodeData data = context.rowData.value<NodeData>();
        QString text;
        switch (context.logicalColumn) {
        case 0: text = data.name; break;
        case 1: text = data.type; break;
        case 2: text = data.state; break;
        }
        if (auto *progress = qobject_cast<QProgressBar *>(widget)) {
            progress->setFormat(text);
            progress->setValue(text == QStringLiteral("正常") ? 100 : 0);
        } else {
            static_cast<QLabel *>(widget)->setText(text);
        }
        widget->setToolTip(QStringLiteral("节点深度：%1，逻辑列：%2")
                               .arg(context.depth).arg(context.logicalColumn));
    }
};

QList<QStandardItem *> makeRow(const QString &name, const QString &type,
                               const QString &state = QString())
{
    QList<QStandardItem *> row;
    row << new QStandardItem(name) << new QStandardItem(type);
    if (!state.isNull())
        row << new QStandardItem(state);
    row.first()->setData(QVariant::fromValue(NodeData{name, type, state}),
                         viv::kDefaultItemDataRole);
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
        deviceItem->setData(device > 0 ? 12 : 0,
                            viv::VirtualTreeTableView::NodeRowSpacingAboveRole);
        for (int channel = 0; channel < 8; ++channel) {
            auto channelRow = makeRow(QStringLiteral("通道 %1").arg(channel),
                                      QStringLiteral("通道"));
            QStandardItem *channelItem = channelRow.first();
            for (int point = 0; point < 4; ++point) {
                auto pointRow = makeRow(QStringLiteral("测点 %1").arg(point),
                                        QStringLiteral("测点"), QStringLiteral("正常"));
                channelItem->appendRow(pointRow);
            }
            deviceItem->appendRow(channelRow);
        }
        model.appendRow(deviceRow);
    }

    viv::VirtualTreeTableView view;
    view.setExpansionAnimationEnabled(true);
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
    auto *expansionAnimation = new QCheckBox(QStringLiteral("展开动画"), &window);
    expansionAnimation->setChecked(view.expansionAnimationEnabled());
    controls->addWidget(expansionAnimation);
    auto *expansionDuration = new QSpinBox(&window);
    expansionDuration->setRange(0, 1000);
    expansionDuration->setSuffix(QStringLiteral(" ms"));
    expansionDuration->setValue(view.expansionAnimationDuration());
    controls->addWidget(expansionDuration);
    QObject::connect(expansionAnimation, &QCheckBox::toggled,
                     &view, &viv::VirtualTreeTableView::setExpansionAnimationEnabled);
    QObject::connect(expansionDuration, QOverload<int>::of(&QSpinBox::valueChanged),
                     &view, &viv::VirtualTreeTableView::setExpansionAnimationDuration);
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

    window.addToolBarBreak();
    auto *stateToolbar = window.addToolBar(QStringLiteral("状态背景"));
    window.resize(1100, 560);
    window.show();
    return app.exec();
}
