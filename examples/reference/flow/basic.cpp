// Display columns come from viewport width; the model remains a one-column list.
#include "../list/textadapter.h"
#include <virtualitemviews/virtualflowtableview.h>
#include <QApplication>
#include <QStringListModel>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStringList records;
    for (int row = 0; row < 100; ++row)
        records.append(QStringLiteral("Card %1").arg(row));
    QStringListModel model(records);
    reference::TextAdapter adapter;
    viv::VirtualFlowTableView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(140, 220);
    view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::Fixed);
    view.setFixedCellHeight(80);
    view.setColumnSpacing(8);
    view.setRowSpacing(8);
    view.setStateBackgroundCornerRadius(6);
    view.setModel(&model);
    view.setWindowTitle(QStringLiteral("Flow table: resize to change columns"));
    view.resize(640, 360);
    view.show();
    return app.exec();
}
