// Independent configuration functions: copy only the feature you need.
#include "samplemodel.h"
#include "textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

static void configureFrozenAreas(viv::VirtualTableView &view)
{
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({5});
    view.setFrozenRows(1);
}

static void configureSpacing(viv::VirtualTableView &view)
{
    view.setRowSpacing(6);
    view.setColumnSpacing(8);
    view.setHorizontalGridLinesVisible(true);
    view.setVerticalGridLinesVisible(true);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTable(model, 100, 6);
    reference::TextCellAdapter adapter;
    viv::VirtualTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(160);
    view.setModel(&model);
    configureFrozenAreas(view);
    configureSpacing(view);
    view.setWindowTitle(QStringLiteral("Table: frozen areas and spacing"));
    view.resize(700, 360);
    view.show();
    return app.exec();
}
