// Reuse the row adapter; the tree-table owns branch geometry in column zero.
#include "../table/textadapters.h"
#include "../tree/samplemodel.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTree(model);
    reference::TextRowAdapter adapter(3);
    viv::VirtualTreeTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::RowWidgets);
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(180);
    view.setModel(&model);
    view.setExpansionAnimationEnabled(true);
    view.setVisualStateBackgroundVisible(true);
    view.expand(model.index(0, 0));
    view.setWindowTitle(QStringLiteral("Tree table: row widgets"));
    view.resize(700, 360);
    view.show();
    return app.exec();
}
