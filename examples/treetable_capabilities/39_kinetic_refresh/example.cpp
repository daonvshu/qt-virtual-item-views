#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtualtreetableview.h>

QWidget *refresh_demo::treeTable(QWidget *parent)
{
    auto *view = new viv::VirtualTreeTableView;
    view->setUniformItemHeight(40);
    return page(parent, view, true);
}
