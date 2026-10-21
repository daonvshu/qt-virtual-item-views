// The indentation slot width and icon size are independent settings.
#include "samplemodel.h"
#include "../list/textadapter.h"
#include <virtualitemviews/virtualtreeview.h>
#include <QApplication>
#include <QStyle>

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

    view.setIndentation(28);
    view.setBranchIconSize(14);
    view.setBranchIconColor(QColor(70, 110, 180));
    view.setExpandedBranchIcon(app.style()->standardIcon(QStyle::SP_ArrowDown));
    view.setCollapsedBranchIcon(app.style()->standardIcon(QStyle::SP_ArrowRight));
    view.setIndentLeafItems(false);
    // For indentation without icons use setBranchIndicatorsVisible(false).
    // A custom BranchIndicatorRenderer handles connector lines or other shapes.

    view.expand(model.index(0, 0));
    view.setWindowTitle(QStringLiteral("Tree: branch icon and indentation"));
    view.resize(480, 360);
    view.show();
    return app.exec();
}
