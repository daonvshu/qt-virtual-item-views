// 企业表格示例（v0.4 Table MVP）：一行一个真实 QWidget（Row Widget Mode），
// 每列由 ColumnHost 承载业务控件，列宽/顺序/隐藏全部来自 HeaderGeometry。
//
// 演示：原生 QHeaderView（水平 + 行号）、拖动列宽、点击表头排序、隐藏/恢复列、
// 移动列、保存/恢复列状态、横向像素滚动、百万级行只实例化可见行。

#include <virtualitemviews/headerview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QApplication>
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCommandLineParser>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {

enum Column {
    ColumnOrder = 0,
    ColumnCustomer,
    ColumnStatus,
    ColumnProgress,
    ColumnAction,
    ColumnCount,
};

constexpr int kRowHeight = 44;

/// 轻量模型：数据按需生成，不预先分配每一格的对象。
class OrderModel : public QAbstractTableModel
{
public:
    explicit OrderModel(int rowCount, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rowCount(qMax(1, rowCount))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rowCount;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        switch (index.column()) {
        case ColumnOrder:
            return QStringLiteral("#%1").arg(200000 + index.row());
        case ColumnCustomer:
            return QStringLiteral("客户-%1").arg(index.row() % 5000);
        case ColumnStatus:
            return index.row() % 3 == 0 ? QStringLiteral("已完成") : QStringLiteral("处理中");
        case ColumnProgress:
            return (index.row() * 7) % 101;
        default:
            return QStringLiteral("-");
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Vertical)
            return section + 1;
        switch (section) {
        case ColumnOrder:
            return QStringLiteral("订单号");
        case ColumnCustomer:
            return QStringLiteral("客户");
        case ColumnStatus:
            return QStringLiteral("状态");
        case ColumnProgress:
            return QStringLiteral("进度");
        default:
            return QStringLiteral("操作");
        }
    }

private:
    int m_rowCount = 0;
};

class OrderTableView : public viv::VirtualTableView
{
public:
    using viv::VirtualTableView::VirtualTableView;

    void setCornerRadius(int radius)
    {
        m_cornerRadius = radius;
        viewport()->update();
    }

protected:
    void paintStateBackgroundLayer(QPainter *painter, const QRect &extended,
                                  const QRegion &clip, const QRegion &,
                                  const QColor &color) const override
    {
        paintRoundedStateBackgroundLayer(painter, extended, clip, color, m_cornerRadius);
    }

private:
    int m_cornerRadius = 8;
};

class OrderRowWidget : public QWidget
{
public:
    explicit OrderRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        for (int column = 0; column < ColumnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            switch (column) {
            case ColumnStatus:
                m_status = new QLabel(host);
                break;
            case ColumnProgress:
                m_progress = new QProgressBar(host);
                m_progress->setRange(0, 100);
                break;
            case ColumnAction:
                m_action = new QPushButton(QStringLiteral("详情"), host);
                break;
            default:
                m_labels.append({column, new QLabel(host)});
                break;
            }
        }
    }

    void bind(const QModelIndex &rowIndex)
    {
        m_rowIndex = QPersistentModelIndex(rowIndex);
        for (const auto &entry : m_labels)
            entry.second->setText(rowIndex.siblingAtColumn(entry.first).data().toString());
        m_status->setText(rowIndex.siblingAtColumn(ColumnStatus).data().toString());
        m_progress->setValue(rowIndex.siblingAtColumn(ColumnProgress).data().toInt());
    }

private:
    QPersistentModelIndex m_rowIndex;
    QVector<QPair<int, QLabel *>> m_labels;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_action = nullptr;
};

class OrderAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new OrderRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<OrderRowWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(700, kRowHeight);
    }

    void setCornerRadius(int radius)
    {
        cornerRadius = radius;
        view->setCornerRadius(radius);
    }

    int created = 0;
    int cornerRadius = 8;
    OrderTableView *view = nullptr;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: table row widgets"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("订单数量"),
                                  QStringLiteral("count"), QStringLiteral("200000"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                      QStringLiteral("渲染整个窗口到 PNG 后退出"), QStringLiteral("file"));
    QCommandLineOption spacingPreviewOption(QStringLiteral("spacing-preview"),
                                            QStringLiteral("间隙截图状态：through、none、custom、zero、grid-off 或 zero-grid-off"),
                                            QStringLiteral("state"));
    QCommandLineOption moveOption(QStringLiteral("move-status-first"),
                                  QStringLiteral("启动时把状态列移到最前（列移动回归检查）"));
    parser.addOption(rowsOption);
    parser.addOption(snapshotOption);
    parser.addOption(spacingPreviewOption);
    parser.addOption(moveOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    OrderModel model(rowCount);

    OrderAdapter adapter;
    QMainWindow window;

    // 视图由窗口持有；模型/适配器先声明，生命周期覆盖视图。
    auto *view = new OrderTableView(&window);
    adapter.view = view;
    view->setTableAdapter(&adapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(150);
    view->setColumnWidth(ColumnOrder, 110);
    view->setColumnWidth(ColumnCustomer, 160);
    view->setColumnWidth(ColumnStatus, 100);
    view->setColumnWidth(ColumnProgress, 140);
    view->setColumnWidth(ColumnAction, 120);
    view->setStretchLastColumn(true);
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(48);
    view->setSortingEnabled(true);
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · table row widgets (%1 rows)").arg(rowCount));
    window.resize(980, 600);

    // 列状态操作：全部通过 HeaderGeometry 生效，body 只查询它。
    auto *toolbar = window.addToolBar(QStringLiteral("列"));
    auto *spacingToolbar = window.addToolBar(QStringLiteral("间距"));
    window.insertToolBarBreak(spacingToolbar);
    spacingToolbar->addWidget(new QLabel(QStringLiteral("行间距: "), &window));
    auto *rowSpacing = new QSpinBox(&window);
    rowSpacing->setRange(0, 100);
    rowSpacing->setSuffix(QStringLiteral(" px"));
    rowSpacing->setValue(view->rowSpacing());
    spacingToolbar->addWidget(rowSpacing);
    spacingToolbar->addWidget(new QLabel(QStringLiteral("列间距: "), &window));
    auto *columnSpacing = new QSpinBox(&window);
    columnSpacing->setRange(0, 100);
    columnSpacing->setSuffix(QStringLiteral(" px"));
    columnSpacing->setValue(view->columnSpacing());
    spacingToolbar->addWidget(columnSpacing);
    auto *verticalThrough = new QCheckBox(QStringLiteral("竖线穿过行间距"), &window);
    verticalThrough->setChecked(view->verticalSpacingLineThroughRowSpacing());
    spacingToolbar->addWidget(verticalThrough);
    auto *horizontalThrough = new QCheckBox(QStringLiteral("横线穿过列间距"), &window);
    horizontalThrough->setChecked(view->horizontalSpacingLineThroughColumnSpacing());
    spacingToolbar->addWidget(horizontalThrough);
    auto *customGaps = new QCheckBox(QStringLiteral("自定义间隙控件"), &window);
    spacingToolbar->addWidget(customGaps);
    auto *gridToolbar = window.addToolBar(QStringLiteral("分割线"));
    window.insertToolBarBreak(gridToolbar);
    auto *stateToolbar = window.addToolBar(QStringLiteral("状态"));
    window.insertToolBarBreak(stateToolbar);
    auto *stateScope = new QComboBox(&window);
    stateScope->addItem(QStringLiteral("状态: 整行"));
    stateScope->addItem(QStringLiteral("状态: 单元格"));
    stateToolbar->addWidget(stateScope);
    QObject::connect(stateScope, QOverload<int>::of(&QComboBox::currentIndexChanged), view,
                     [view](int index) {
        view->selectionModel()->clearSelection();
        view->setSelectionBehavior(index == 0
            ? viv::VirtualItemView::SelectionBehavior::SelectRows
            : viv::VirtualItemView::SelectionBehavior::SelectItems);
        view->setVisualStateScope(index == 0
            ? viv::VirtualTableView::VisualStateScope::Row
            : viv::VirtualTableView::VisualStateScope::Cell);
    });
    auto *hoverColor = new QPushButton(QStringLiteral("悬停颜色"), &window);
    auto *selectedColor = new QPushButton(QStringLiteral("选中颜色"), &window);
    stateToolbar->addWidget(hoverColor);
    stateToolbar->addWidget(selectedColor);
    auto *backgroundToolbar = window.addToolBar(QStringLiteral("背景间距"));
    window.insertToolBarBreak(backgroundToolbar);
    auto *hoverRowSpacing = new QCheckBox(QStringLiteral("悬停覆盖行间距"), &window);
    hoverRowSpacing->setChecked(view->hoverBackgroundThroughRowSpacing());
    backgroundToolbar->addWidget(hoverRowSpacing);
    QObject::connect(hoverRowSpacing, &QCheckBox::toggled, view,
                     &viv::VirtualTableView::setHoverBackgroundThroughRowSpacing);
    auto *selectedRowSpacing = new QCheckBox(QStringLiteral("选中覆盖行间距"), &window);
    selectedRowSpacing->setChecked(view->selectedBackgroundThroughRowSpacing());
    backgroundToolbar->addWidget(selectedRowSpacing);
    QObject::connect(selectedRowSpacing, &QCheckBox::toggled, view,
                     &viv::VirtualTableView::setSelectedBackgroundThroughRowSpacing);
    auto *hoverColumnSpacing = new QCheckBox(QStringLiteral("悬停覆盖列间距"), &window);
    hoverColumnSpacing->setChecked(view->hoverBackgroundThroughColumnSpacing());
    backgroundToolbar->addWidget(hoverColumnSpacing);
    QObject::connect(hoverColumnSpacing, &QCheckBox::toggled, view,
                     &viv::VirtualTableView::setHoverBackgroundThroughColumnSpacing);
    auto *selectedColumnSpacing = new QCheckBox(QStringLiteral("选中覆盖列间距"), &window);
    selectedColumnSpacing->setChecked(view->selectedBackgroundThroughColumnSpacing());
    backgroundToolbar->addWidget(selectedColumnSpacing);
    QObject::connect(selectedColumnSpacing, &QCheckBox::toggled, view,
                     &viv::VirtualTableView::setSelectedBackgroundThroughColumnSpacing);
    stateToolbar->addWidget(new QLabel(QStringLiteral("过渡: "), &window));
    auto *animationDuration = new QSpinBox(&window);
    animationDuration->setRange(0, 1000);
    animationDuration->setSuffix(QStringLiteral(" ms"));
    animationDuration->setValue(view->visualStateAnimationDuration());
    stateToolbar->addWidget(animationDuration);
    QObject::connect(animationDuration, QOverload<int>::of(&QSpinBox::valueChanged), view,
                     [view](int ms) { view->setVisualStateAnimationDuration(ms); });
    stateToolbar->addWidget(new QLabel(QStringLiteral("圆角: "), &window));
    auto *cornerRadius = new QSpinBox(&window);
    cornerRadius->setRange(0, 20);
    cornerRadius->setSuffix(QStringLiteral(" px"));
    cornerRadius->setValue(adapter.cornerRadius);
    stateToolbar->addWidget(cornerRadius);
    QObject::connect(cornerRadius, QOverload<int>::of(&QSpinBox::valueChanged), view,
                     [&adapter](int radius) { adapter.setCornerRadius(radius); });
    const auto setStateSwatch = [](QPushButton *button, const QColor &color) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        button->setIcon(QIcon(swatch));
    };
    setStateSwatch(hoverColor, view->hoverBackgroundColor());
    setStateSwatch(selectedColor, view->selectedBackgroundColor());
    QObject::connect(hoverColor, &QPushButton::clicked, view, [view, hoverColor, setStateSwatch]() {
        const QColor color = QColorDialog::getColor(view->hoverBackgroundColor(), view,
                                                    QStringLiteral("悬停背景颜色"));
        if (color.isValid()) {
            view->setHoverBackgroundColor(color);
            setStateSwatch(hoverColor, color);
        }
    });
    QObject::connect(selectedColor, &QPushButton::clicked, view,
                     [view, selectedColor, setStateSwatch]() {
        const QColor color = QColorDialog::getColor(view->selectedBackgroundColor(), view,
                                                    QStringLiteral("选中背景颜色"));
        if (color.isValid()) {
            view->setSelectedBackgroundColor(color);
            setStateSwatch(selectedColor, color);
        }
    });
    auto *verticalGrid = new QCheckBox(QStringLiteral("显示竖向分割线"), &window);
    verticalGrid->setChecked(view->verticalGridLinesVisible());
    gridToolbar->addWidget(verticalGrid);
    auto *horizontalGrid = new QCheckBox(QStringLiteral("显示横向分割线"), &window);
    horizontalGrid->setChecked(view->horizontalGridLinesVisible());
    gridToolbar->addWidget(horizontalGrid);
    gridToolbar->addWidget(new QLabel(QStringLiteral("竖线宽度: "), &window));
    auto *verticalLineWidth = new QSpinBox(&window);
    verticalLineWidth->setRange(1, 8);
    verticalLineWidth->setValue(view->verticalGridLineWidth());
    gridToolbar->addWidget(verticalLineWidth);
    gridToolbar->addWidget(new QLabel(QStringLiteral("横线宽度: "), &window));
    auto *horizontalLineWidth = new QSpinBox(&window);
    horizontalLineWidth->setRange(1, 8);
    horizontalLineWidth->setValue(view->horizontalGridLineWidth());
    gridToolbar->addWidget(horizontalLineWidth);
    auto *verticalLineColor = new QPushButton(QStringLiteral("竖线颜色"), &window);
    auto *horizontalLineColor = new QPushButton(QStringLiteral("横线颜色"), &window);
    QPixmap defaultSwatch(16, 16);
    defaultSwatch.fill(viv::VirtualTableView::sectionSeparatorColor(view));
    verticalLineColor->setIcon(QIcon(defaultSwatch));
    horizontalLineColor->setIcon(QIcon(defaultSwatch));
    gridToolbar->addWidget(verticalLineColor);
    gridToolbar->addWidget(horizontalLineColor);
    auto *hideCustomer = new QCheckBox(QStringLiteral("隐藏“客户”"), &window);
    toolbar->addWidget(hideCustomer);
    auto *moveStatusFirst = new QPushButton(QStringLiteral("状态列移到最前"), &window);
    toolbar->addWidget(moveStatusFirst);
    auto *saveState = new QPushButton(QStringLiteral("保存列状态"), &window);
    auto *restoreState = new QPushButton(QStringLiteral("恢复列状态"), &window);
    toolbar->addWidget(saveState);
    toolbar->addWidget(restoreState);
    toolbar->addWidget(new QLabel(QStringLiteral("  像素横滚: "), &window));
    auto *horizontalOffset = new QSpinBox(&window);
    horizontalOffset->setRange(0, 5000);
    toolbar->addWidget(horizontalOffset);

    QObject::connect(rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     view, [view](int pixels) { view->setRowSpacing(pixels); });
    QObject::connect(columnSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                     view, [view](int pixels) { view->setColumnSpacing(pixels); });
    QObject::connect(verticalThrough, &QCheckBox::toggled, view,
                     [view](bool enabled) { view->setVerticalSpacingLineThroughRowSpacing(enabled); });
    QObject::connect(horizontalThrough, &QCheckBox::toggled, view,
                     [view](bool enabled) { view->setHorizontalSpacingLineThroughColumnSpacing(enabled); });
    QObject::connect(verticalGrid, &QCheckBox::toggled, view,
                     [view](bool visible) { view->setVerticalGridLinesVisible(visible); });
    QObject::connect(horizontalGrid, &QCheckBox::toggled, view,
                     [view](bool visible) { view->setHorizontalGridLinesVisible(visible); });
    QObject::connect(verticalLineWidth, QOverload<int>::of(&QSpinBox::valueChanged),
                     view, [view](int pixels) { view->setVerticalGridLineWidth(pixels); });
    QObject::connect(horizontalLineWidth, QOverload<int>::of(&QSpinBox::valueChanged),
                     view, [view](int pixels) { view->setHorizontalGridLineWidth(pixels); });
    QObject::connect(verticalLineColor, &QPushButton::clicked, view, [view, verticalLineColor]() {
        const QColor color = QColorDialog::getColor(
            view->verticalGridLineColor().isValid() ? view->verticalGridLineColor()
                                                    : viv::VirtualTableView::sectionSeparatorColor(view),
            view, QStringLiteral("竖线颜色"));
        if (color.isValid()) {
            view->setVerticalGridLineColor(color);
            QPixmap swatch(16, 16);
            swatch.fill(color);
            verticalLineColor->setIcon(QIcon(swatch));
        }
    });
    QObject::connect(horizontalLineColor, &QPushButton::clicked, view, [view, horizontalLineColor]() {
        const QColor color = QColorDialog::getColor(
            view->horizontalGridLineColor().isValid() ? view->horizontalGridLineColor()
                                                      : viv::VirtualTableView::sectionSeparatorColor(view),
            view, QStringLiteral("横线颜色"));
        if (color.isValid()) {
            view->setHorizontalGridLineColor(color);
            QPixmap swatch(16, 16);
            swatch.fill(color);
            horizontalLineColor->setIcon(QIcon(swatch));
        }
    });
    QObject::connect(customGaps, &QCheckBox::toggled, view, [view](bool enabled) {
        if (!enabled) {
            view->setHeaderColumnSpacingFactory({});
            view->setColumnSpacingFactory({});
            return;
        }
        view->setHeaderColumnSpacingFactory([](int, QWidget *parent) -> QWidget * {
            auto *stripe = new QWidget(parent);
            stripe->setStyleSheet(QStringLiteral("background: #f4d35e;"));
            return stripe;
        });
        view->setColumnSpacingFactory([](int, QWidget *parent) -> QWidget * {
            auto *stripe = new QWidget(parent);
            stripe->setStyleSheet(QStringLiteral("background: #83c5be;"));
            return stripe;
        });
    });
    const QString spacingPreview = parser.value(spacingPreviewOption);
    if (spacingPreview == QLatin1String("through") || spacingPreview == QLatin1String("none") ||
        spacingPreview == QLatin1String("custom") || spacingPreview == QLatin1String("grid-off")) {
        rowSpacing->setValue(8);
        columnSpacing->setValue(12);
    }
    if (spacingPreview == QLatin1String("through")) {
        horizontalThrough->setChecked(true);
    } else if (spacingPreview == QLatin1String("none")) {
        verticalThrough->setChecked(false);
        horizontalThrough->setChecked(false);
    } else if (spacingPreview == QLatin1String("custom")) {
        customGaps->setChecked(true);
    } else if (spacingPreview == QLatin1String("zero")) {
        rowSpacing->setValue(0);
        columnSpacing->setValue(0);
    } else if (spacingPreview == QLatin1String("grid-off")) {
        verticalGrid->setChecked(false);
        horizontalGrid->setChecked(false);
    } else if (spacingPreview == QLatin1String("zero-grid-off")) {
        rowSpacing->setValue(0);
        columnSpacing->setValue(0);
        verticalGrid->setChecked(false);
        horizontalGrid->setChecked(false);
    }

    QByteArray savedState;
    QObject::connect(hideCustomer, &QCheckBox::toggled, view, [view](bool hidden) {
        view->setColumnHidden(ColumnCustomer, hidden);
    });
    QObject::connect(moveStatusFirst, &QPushButton::clicked, view, [view]() {
        view->moveColumn(ColumnStatus, 0);
    });
    QObject::connect(saveState, &QPushButton::clicked, view, [view, &savedState]() {
        savedState = view->saveHeaderState();
    });
    QObject::connect(restoreState, &QPushButton::clicked, view, [view, &savedState]() {
        if (!savedState.isEmpty())
            view->restoreHeaderState(savedState);
    });
    // QOverload keeps this working on Qt 5 (deprecated QString overload) and Qt 6.
    QObject::connect(horizontalOffset, QOverload<int>::of(&QSpinBox::valueChanged), view, [view](int offset) {
        view->setHorizontalOffset(offset);
    });

    auto *status = new QLabel(&window);
    // 必须挂到状态栏：只给 window 当 parent 的话它会停在 (0, 0)，压住工具条。
    window.statusBar()->addPermanentWidget(status);
    const auto updateStatus = [&]() {
        const viv::VirtualViewStats stats = view->stats();
        const viv::VisibleRange columns = view->visibleColumns();
        status->setText(QStringLiteral("逻辑行: %1   列: %2（可见视觉列 %3-%4）   实例化行: %5   池: %6   "
                                       "横向偏移: %7 px   create/bind/recycle: %8/%9/%10")
                            .arg(stats.logicalItems)
                            .arg(view->columnCount())
                            .arg(columns.first)
                            .arg(columns.last)
                            .arg(stats.materializedItems)
                            .arg(stats.pooledWidgets)
                            .arg(view->horizontalOffset())
                            .arg(stats.createCount)
                            .arg(stats.bindCount)
                            .arg(stats.recycleCount));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    const QString snapshotPath = parser.value(snapshotOption);
    const bool moveStatusToFront = parser.isSet(moveOption);
    if (!snapshotPath.isEmpty() || moveStatusToFront) {
        QTimer::singleShot(qMax(1, exitAfter), &app,
                           [&window, view, snapshotPath, moveStatusToFront]() {
            if (moveStatusToFront)
                view->moveColumn(ColumnStatus, 0);
            QApplication::processEvents();
            if (snapshotPath.isEmpty())
                return;
            const QPixmap shot = window.grab();
            const bool saved = shot.save(snapshotPath);
            std::printf("table_row_widgets: snapshot %s (%dx%d)%s firstVisualColumn=%d\n",
                        qPrintable(snapshotPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED", view->horizontalHeaderGeometry()->logicalIndex(0));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    } else if (exitAfter > 0) {
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);
    }

    window.show();
    return app.exec();
}
