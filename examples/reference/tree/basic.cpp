// A tree consumes hierarchical model indexes, not flattened row numbers.
#include "samplemodel.h"
#include "../list/textadapter.h"
#include <virtualitemviews/virtualtreeview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTree(model);
    reference::TextAdapter adapter;
    viv::VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setModel(&model);
    view.setExpansionAnimationEnabled(true);
    view.setVisualStateBackgroundVisible(true);
    view.setVisualStateCornerRadius(6);
    view.expand(model.index(0, 0));
    view.setWindowTitle(QStringLiteral("Tree: expand and collapse"));
    view.resize(480, 360);
    view.show();
    return app.exec();
}
