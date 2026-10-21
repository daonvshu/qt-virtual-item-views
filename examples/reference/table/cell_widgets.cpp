// Only visible/overscan cells own widgets, even when the model has many columns.
#include "samplemodel.h"
#include "textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTable(model, 100, 50);
    reference::TextCellAdapter adapter;
    viv::VirtualTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(160);
    view.setModel(&model);
    view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
    view.setWindowTitle(QStringLiteral("Table: one widget per visible cell"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
