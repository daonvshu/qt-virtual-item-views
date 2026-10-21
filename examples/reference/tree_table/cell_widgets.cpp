// Use the actual hierarchical index in every cell binding, including child rows.
#include "../table/textadapters.h"
#include "../tree/samplemodel.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTree(model);
    reference::TextCellAdapter adapter;
    viv::VirtualTreeTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(180);
    view.setModel(&model);
    view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
    view.setFrozenColumns({0});
    view.expand(model.index(0, 0));
    view.setWindowTitle(QStringLiteral("Tree table: cell widgets, frozen first column"));
    view.resize(700, 360);
    view.show();
    return app.exec();
}
