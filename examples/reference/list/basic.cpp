// Read textadapter.h for create/bind/unbind; this file is the complete list setup.
#include "textadapter.h"
#include <virtualitemviews/virtuallistview.h>
#include <QApplication>
#include <QStringListModel>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStringListModel model({QStringLiteral("Record A"), QStringLiteral("Record B"),
                           QStringLiteral("Record C")});
    reference::TextAdapter adapter;
    // Declaration order matters: the view is destroyed before its adapter/model.
    viv::VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setModel(&model);
    view.setWindowTitle(QStringLiteral("List: basic binding"));
    view.resize(480, 280);
    view.show();
    return app.exec();
}
