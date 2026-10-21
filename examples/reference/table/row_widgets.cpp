// One QWidget per row. Read TextRow/TextRowAdapter in textadapters.h.
#include "samplemodel.h"
#include "textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTable(model);
    reference::TextRowAdapter adapter(3);
    viv::VirtualTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::RowWidgets);
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(160);
    view.setModel(&model);
    view.setWindowTitle(QStringLiteral("Table: one widget per row"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
