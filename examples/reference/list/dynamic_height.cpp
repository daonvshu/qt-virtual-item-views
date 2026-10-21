// Click a row to change its data and size hint; no refresh/reload is needed.
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QStandardItemModel>

constexpr int ExpandedRole = Qt::UserRole + 1;

class DetailAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        const bool expanded = index.data(ExpandedRole).toBool();
        auto *label = static_cast<QLabel *>(widget);
        label->setText(index.data().toString()
            + (expanded ? QStringLiteral("\nDetails\nMore details") : QString()));
        label->updateGeometry();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return QSize(320, index.data(ExpandedRole).toBool() ? 80 : 36);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    for (int row = 0; row < 100; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("Click record %1").arg(row)));
    DetailAdapter adapter;
    viv::VirtualListView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(36);
    view.setAutoMeasureItemHeight(true);
    view.setModel(&model);
    QObject::connect(&view, &viv::VirtualItemView::clicked, &model,
                     [&model](const QModelIndex &index) {
        model.setData(index, !index.data(ExpandedRole).toBool(), ExpandedRole);
    });
    view.setWindowTitle(QStringLiteral("List: click to change height"));
    view.resize(480, 360);
    view.show();
    return app.exec();
}
