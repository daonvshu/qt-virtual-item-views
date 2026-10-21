// Header row reordering uses moveRows(), not MIME/native item dragging.
#include "textadapters.h"
#include <virtualitemviews/reorderabletablemodel.h>
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

class RecordModel : public viv::ReorderableTableModel
{
public:
    RecordModel() : viv::ReorderableTableModel(100, 3) {}

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        // Read data by its stable source identity, not its current view position.
        return QStringLiteral("Record %1 / %2").arg(sourceRow(index.row())).arg(index.column());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    RecordModel model;
    reference::TextRowAdapter adapter(3);
    viv::VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(160);
    view.setModel(&model);
    view.setVerticalHeaderVisible(true);
    view.setVerticalHeaderDragEnabled(true);
    view.setWindowTitle(QStringLiteral("Table: drag row headers to reorder"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
