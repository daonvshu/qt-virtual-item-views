#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QMainWindow>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>

namespace {

class LabelAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VirtualItemViews"));
    QCoreApplication::setApplicationName(QStringLiteral("TableHeaderPersistence"));

    QStandardItemModel model(100, 6);
    model.setHorizontalHeaderLabels({QStringLiteral("订单号"), QStringLiteral("客户"),
                                     QStringLiteral("商品"), QStringLiteral("数量"),
                                     QStringLiteral("状态"), QStringLiteral("日期")});
    for (int row = 0; row < model.rowCount(); ++row) {
        for (int column = 0; column < model.columnCount(); ++column) {
            model.setData(model.index(row, column),
                          QStringLiteral("%1-%2").arg(row + 1).arg(column + 1));
        }
    }

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty() || !QDir().mkpath(configDir))
        return 1;
    QSettings settings(QDir(configDir).filePath(QStringLiteral("table.ini")),
                       QSettings::IniFormat);

    LabelAdapter adapter;
    QMainWindow window;
    auto *table = new viv::VirtualTableView(&window);
    table->setCellAdapter(&adapter);
    table->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    table->setUniformItemHeight(32);
    table->setDefaultColumnWidth(150);
    table->setModel(&model);
    table->setColumnDragEnabled(true);

    const QByteArray defaultState = table->saveHeaderState();
    const QByteArray savedState = settings.value(QStringLiteral("table/headerState")).toByteArray();
    if (!savedState.isEmpty() && !table->restoreHeaderState(savedState))
        settings.remove(QStringLiteral("table/headerState"));

    window.setCentralWidget(table);
    auto *toolbar = window.addToolBar(QStringLiteral("列布局"));
    auto *reset = toolbar->addAction(QStringLiteral("恢复默认布局"));
    QObject::connect(reset, &QAction::triggered, &window, [table, defaultState]() {
        table->restoreHeaderState(defaultState);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&settings, table]() {
        settings.setValue(QStringLiteral("table/headerState"), table->saveHeaderState());
        settings.sync();
    });

    window.statusBar()->showMessage(settings.fileName());
    window.setWindowTitle(QStringLiteral("表格列布局持久化"));
    window.resize(900, 500);
    window.show();
    return app.exec();
}
