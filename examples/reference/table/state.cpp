// Configure framework-painted state backgrounds; keep row/cell widgets transparent.
#include "samplemodel.h"
#include "textadapters.h"
#include <virtualitemviews/virtualtableview.h>
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    reference::populateTable(model);
    reference::TextCellAdapter adapter;
    viv::VirtualTableView view;
    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    view.setCellAdapter(&adapter);
    view.setUniformItemHeight(36);
    view.setDefaultColumnWidth(160);
    view.setModel(&model);
    view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
    // State scope is independent of cell materialization and selection behavior.
    // Change Row to Cell for per-cell hover/selection backgrounds.
    view.setVisualStateScope(viv::VirtualTableView::VisualStateScope::Row);
    // The view paints the complete background, including under transparent cells.
    // No subclass or paint override is needed for the built-in rounded style.
    view.setVisualStateBackgroundVisible(true);
    view.setVisualStateCornerRadius(6);
    view.setHoverBackgroundColor(QColor(220, 232, 248));
    view.setSelectedBackgroundColor(QColor(170, 205, 245));
    view.setWindowTitle(QStringLiteral("Table: rounded row hover with cell widgets"));
    view.resize(620, 360);
    view.show();
    return app.exec();
}
