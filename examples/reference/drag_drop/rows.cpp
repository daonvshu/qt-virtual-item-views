// QStandardItemModel supplies MIME/copy insertion; the view removes moved source rows.
#include "../table/samplemodel.h"
#include "../table/textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTable(model);
    reference::TextRowAdapter adapter(3);
    viv::VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(160);
    view.setModel(&model);
    view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
    view.setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
    view.setDragEnabled(true);
    view.setDragDropActions(Qt::CopyAction | Qt::MoveAction);
    view.setDefaultDropAction(Qt::MoveAction);
    // Keep false if your model itself moves/removes source rows.
    view.setMoveRemovesSourceRows(true);
    view.setWindowTitle(QStringLiteral("Drag rows: move, Ctrl to copy"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
