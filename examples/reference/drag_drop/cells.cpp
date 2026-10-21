// View policy here; MIME and the fill-empty/insert rules are in cellmodel.h.
#include "cellmodel.h"
#include "../table/textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    reference::CellModel model(30, 3);
    for (int row = 0; row < model.rowCount(); ++row) {
        for (int column = 0; column < model.columnCount(); ++column) {
            if ((row + column) % 3 != 2)
                model.setData(model.index(row, column),
                              QStringLiteral("%1 / %2").arg(row).arg(column));
        }
    }
    reference::TextCellAdapter adapter;
    viv::VirtualTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(40);
    view.setDefaultColumnWidth(180);
    view.setModel(&model);
    view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
    view.setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
    view.setDragEnabled(true);
    view.setDragDropActions(Qt::CopyAction | Qt::MoveAction);
    view.setDefaultDropAction(Qt::MoveAction);
    view.setDropTargetMode(viv::VirtualTableView::DropTargetMode::BetweenRowsOrOnCell);
    view.setMoveRemovesSourceRows(false);
    view.setWindowTitle(QStringLiteral("Drag cell: edge inserts, middle fills empty cell"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
