// One payload on column zero of every node; all cells receive it with depth/column.
#include <virtualitemviews/virtualtreetableview.h>
#include <QApplication>
#include <QLabel>
#include <QStandardItemModel>

struct NodeData { QString name; QString state; };
Q_DECLARE_METATYPE(NodeData)

class NodeCellAdapter : public viv::CellWidgetAdapter
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
        const NodeData node = context.rowData.value<NodeData>();
        auto *label = static_cast<QLabel *>(widget);
        label->setText(context.logicalColumn == 0 ? node.name : node.state);
        label->setToolTip(QStringLiteral("Depth: %1, column: %2")
                          .arg(context.depth).arg(context.logicalColumn));
    }
};

static QList<QStandardItem *> makeNode(const NodeData &data)
{
    auto *first = new QStandardItem;
    first->setData(QVariant::fromValue(data), viv::kDefaultItemDataRole);
    // Column count describes the node schema even though data lives only on column zero.
    return {first, new QStandardItem};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("State")});
    const auto group = makeNode({QStringLiteral("Group"), QStringLiteral("Ready")});
    model.appendRow(group);
    for (int row = 0; row < 30; ++row)
        group.first()->appendRow(makeNode({QStringLiteral("Record %1").arg(row),
                                          QStringLiteral("Ready")}));
    NodeCellAdapter adapter;
    viv::VirtualTreeTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(220);
    view.setModel(&model);
    view.expand(model.index(0, 0));
    view.setWindowTitle(QStringLiteral("Tree table: node payload in cell bindings"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
