// One complete payload per row; bind directly from ItemBindingContext::rowData.
#include "textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>
#include <QStandardItemModel>

struct RecordData
{
    int id = 0;
    QString name;
    QString state;
};
Q_DECLARE_METATYPE(RecordData)

class RecordAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new reference::TextRow(3, parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget,
                    const viv::ItemBindingContext &context) override
    {
        const RecordData record = context.rowData.value<RecordData>();
        static_cast<reference::TextRow *>(widget)->setTexts(
            {QString::number(record.id), record.name, record.state});
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<reference::TextRow *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(0, 36);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model(100, 3);
    model.setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Name"),
                                    QStringLiteral("State")});
    for (int row = 0; row < model.rowCount(); ++row) {
        const RecordData record{row, QStringLiteral("Record %1").arg(row), QStringLiteral("Ready")};
        model.setData(model.index(row, 0), QVariant::fromValue(record), viv::kDefaultItemDataRole);
    }
    RecordAdapter adapter;
    viv::VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(180);
    view.setItemDataRole(viv::kDefaultItemDataRole);
    view.setModel(&model);
    view.setWindowTitle(QStringLiteral("Table: one structure per row"));
    view.resize(700, 360);
    view.show();
    return app.exec();
}
