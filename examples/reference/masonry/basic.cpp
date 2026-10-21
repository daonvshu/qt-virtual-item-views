// Size estimates come from data without creating widgets for offscreen cards.
#include "../list/textadapter.h"
#include <virtualitemviews/virtualmasonryview.h>
#include <QApplication>
#include <QStringListModel>

class CardAdapter : public reference::TextAdapter
{
public:
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return QSize(180, 60 + (index.row() % 4) * 30);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStringList records;
    for (int row = 0; row < 100; ++row)
        records.append(QStringLiteral("Card %1").arg(row));
    QStringListModel model(records);
    CardAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(140, 220);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setColumnSpacing(8);
    view.setRowSpacing(8);
    view.setStateBackgroundCornerRadius(6);
    view.setModel(&model);
    view.setWindowTitle(QStringLiteral("Masonry: estimated card sizes"));
    view.resize(640, 360);
    view.show();
    return app.exec();
}
