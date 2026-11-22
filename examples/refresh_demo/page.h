#pragma once

class QWidget;
namespace viv { class VirtualItemView; }

namespace refresh_demo {
struct PageOptions {
    int pageSize = 24;
    int itemLimit = 120;
};

QWidget *page(QWidget *parent, viv::VirtualItemView *view, bool tree = false,
              const PageOptions &options = {});
QWidget *list(QWidget *parent);
QWidget *table(QWidget *parent);
QWidget *tree(QWidget *parent);
QWidget *treeTable(QWidget *parent);
QWidget *flow(QWidget *parent);
QWidget *wrap(QWidget *parent);
QWidget *masonry(QWidget *parent);
}
