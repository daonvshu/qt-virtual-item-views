// Different recycler types and payload types for group/record nodes.
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QStandardItemModel>

struct GroupData { QString name; int count = 0; };
struct RecordData { int id = 0; QString name; };
Q_DECLARE_METATYPE(GroupData)
Q_DECLARE_METATYPE(RecordData)
constexpr int TypeRole = Qt::UserRole + 1;
constexpr viv::WidgetType GroupType = 0;
constexpr viv::WidgetType RecordType = 1;

class NodeAdapter : public viv::WidgetAdapter
{
public:
    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.data(TypeRole).toInt();
    }

    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        QFont font = label->font();
        font.setBold(type == GroupType);
        label->setFont(font);
        return label;
    }

    void bindWidget(viv::WidgetType type, QWidget *widget,
                    const viv::ItemBindingContext &context) override
    {
        auto *label = static_cast<QLabel *>(widget);
        // type is the stored recycler identity, not a new widgetType() lookup.
        if (type == GroupType) {
            const GroupData group = context.rowData.value<GroupData>();
            label->setText(QStringLiteral("%1 (%2)").arg(group.name).arg(group.count));
        } else {
            const RecordData record = context.rowData.value<RecordData>();
            label->setText(QStringLiteral("%1: %2").arg(record.id).arg(record.name));
        }
        label->setToolTip(QStringLiteral("Depth: %1").arg(context.depth));
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(320, 36);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    auto *group = new QStandardItem;
    group->setData(GroupType, TypeRole);
    group->setData(QVariant::fromValue(GroupData{QStringLiteral("Group"), 30}),
                   viv::kDefaultItemDataRole);
    model.appendRow(group);
    for (int row = 0; row < 30; ++row) {
        auto *record = new QStandardItem;
        record->setData(RecordType, TypeRole);
        record->setData(QVariant::fromValue(RecordData{row, QStringLiteral("Record")}),
                        viv::kDefaultItemDataRole);
        group->appendRow(record);
    }
    NodeAdapter adapter;
    viv::VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setModel(&model);
    view.expand(model.index(0, 0));
    view.setWindowTitle(QStringLiteral("Tree: node types, structures and depth"));
    view.resize(520, 360);
    view.show();
    return app.exec();
}
