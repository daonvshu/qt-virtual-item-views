// §43 "spans" 示例（规格见 docs/spans.md）：合并单元格怎么用、怎么和别的能力组合。
//
// 演示四件事：
//  1. 跨列合并（分组标题、小计）：框架把被覆盖列的 ColumnHost 隐藏，锚点 host 占合并宽度 ——
//     业务不需要自己算列宽，列宽/列序/隐藏列一变，合并宽度跟着变；
//  2. 跨行合并（Cell Widget Mode）：框架只给锚点物化 cell 控件并放大到合并矩形；
//  3. 命中与拖放：合并区域是一个目标，点击/拖放/键盘都折回锚点，插入指示器画合并矩形；
//  4. 与冻结列共存：span 不跨 pane，跨边界的部分被裁剪到锚点所在 pane。
//
// 自检：--no-spans 对比（同一张表不带合并）、--hover <row>:<column> 合成一次悬停把插入指示器
// 画出来、--snapshot <png> 导出截图、--exit-after <ms> 无人值守运行。

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMimeData>
#include <QScopedPointer>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kRowHeight = 28;
constexpr int kColumnWidth = 110;
constexpr int kColumns = 8;
/// Rows of one "group": a group title row merged across every column, then
/// three data rows.
constexpr int kGroupSize = 4;
constexpr int kGroupTitleSpan = 4;
constexpr int kRowSpanAnchor = 1;

/// Row widget: one ColumnHost per column, each with a label. The framework owns
/// the host geometry (§26/§27), so a merged cell is just "the anchor host is
/// wider and the covered hosts are hidden".
class SpanRowWidget : public QWidget
{
public:
    SpanRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        m_hosts.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            // The host is resized by the framework (a merged anchor is wider than
            // one column), so the content follows through a layout.
            auto *hostLayout = new QHBoxLayout(host);
            hostLayout->setContentsMargins(6, 0, 6, 0);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            hostLayout->addWidget(label);
            m_hosts.append(host);
            m_labels.append(label);
        }
    }

    viv::ColumnHost *host(int column) const { return m_hosts.value(column); }
    QLabel *label(int column) const { return m_labels.value(column); }

    QSize sizeHint() const override { return QSize(kColumnWidth * kColumns, kRowHeight); }

private:
    QVector<viv::ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

class SpanTableAdapter : public viv::TableWidgetAdapter
{
public:
    explicit SpanTableAdapter(bool cellMode)
        : m_cellMode(cellMode)
    {
    }

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new SpanRowWidget(parent, kColumns);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<SpanRowWidget *>(widget);
        for (int column = 0; column < kColumns; ++column)
            row->label(column)->setText(index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<SpanRowWidget *>(widget);
        for (int column = 0; column < kColumns; ++column)
            row->label(column)->clear();
    }

    /// §43: the framework reports what it decided for this row, so a business
    /// row widget can style a merged cell (here: centre and emphasise the group
    /// title) without recomputing any column geometry.
    void layoutRowWidget(QWidget *widget, const QModelIndex &rowIndex,
                         const viv::TableRowLayoutContext &context) override
    {
        auto *row = static_cast<SpanRowWidget *>(widget);
        for (int column : context.columnsToLayout()) {
            const viv::TableSpan span = context.spans().spanOf(column);
            if (!span.isMerged())
                continue;
            QLabel *label = row->label(column);
            if (!label)
                continue;
            label->setAlignment(Qt::AlignCenter);
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
            label->setText(rowIndex.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * kColumns, kRowHeight);
    }

    int created = 0;
    bool m_cellMode = false;
};

/// Cell Widget Mode adapter: one widget per visible cell (§28), which is what
/// makes a cross-row merge renderable by the framework itself.
class SpanCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("cellLabel"));
        label->setAlignment(Qt::AlignCenter);
        return label;
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    int created = 0;
};

QStandardItemModel *buildModel(QObject *parent)
{
    const int groups = 30;
    auto *model = new QStandardItemModel(groups * kGroupSize, kColumns, parent);
    QStringList headers;
    for (int column = 0; column < kColumns; ++column)
        headers << QStringLiteral("列 %1").arg(column + 1);
    model->setHorizontalHeaderLabels(headers);

    for (int group = 0; group < groups; ++group) {
        const int titleRow = group * kGroupSize;
        model->setItem(titleRow, 0, new QStandardItem(QStringLiteral("分组 %1（跨 %2 列）")
                                                          .arg(group + 1)
                                                          .arg(kGroupTitleSpan)));
        for (int column = 1; column < kGroupTitleSpan; ++column)
            model->setItem(titleRow, column, new QStandardItem(QString()));
        for (int column = kGroupTitleSpan; column < kColumns; ++column) {
            model->setItem(titleRow, column,
                           new QStandardItem(QStringLiteral("g%1c%2").arg(group + 1).arg(column)));
        }
        for (int offset = 1; offset < kGroupSize; ++offset) {
            const int row = titleRow + offset;
            for (int column = 0; column < kColumns; ++column) {
                model->setItem(row, column,
                               new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
            }
        }
    }
    return model;
}

/// Registers the spans of the example: every group title row is merged, and the
/// row below the first group merges a 2x2 block to show how a merged area behaves.
void applySpans(viv::VirtualTableView *view)
{
    view->clearSpans();
    for (int group = 0; group < 30; ++group)
        view->setSpan(group * kGroupSize, 0, 1, kGroupTitleSpan);
    view->setSpan(kRowSpanAnchor, 1, 2, 2);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: spans (§43)"));
    parser.addHelpOption();
    QCommandLineOption noSpanOption(QStringLiteral("no-spans"),
                                    QStringLiteral("不注册任何合并（对照用）"));
    QCommandLineOption cellModeOption(QStringLiteral("cell-mode"),
                                      QStringLiteral("用 Cell Widget Mode（跨行合并由框架渲染）"));
    QCommandLineOption frozenOption(
        QStringLiteral("frozen"),
        QStringLiteral("冻结左侧列数（冻结后跨 pane 的合并会被裁剪到锚点 pane）"),
        QStringLiteral("count"), QStringLiteral("0"));
    QCommandLineOption hoverOption(
        QStringLiteral("hover"),
        QStringLiteral("合成一次悬停并显示插入指示器：<row>:<column>"), QStringLiteral("spec"));
    QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                      QStringLiteral("把窗口渲染到 PNG 后退出"),
                                      QStringLiteral("path"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(noSpanOption);
    parser.addOption(cellModeOption);
    parser.addOption(frozenOption);
    parser.addOption(hoverOption);
    parser.addOption(snapshotOption);
    parser.addOption(exitOption);
    parser.process(app);

    const bool cellMode = parser.isSet(cellModeOption);
    const int frozenColumns = qBound(0, parser.value(frozenOption).toInt(), kColumns);

    // 模型/适配器先于窗口构造：析构顺序上晚于视图，视图析构时仍能安全 unbind。
    auto *model = buildModel(&app);
    SpanTableAdapter tableAdapter(cellMode);
    SpanCellAdapter cellAdapter;

    QMainWindow window;
    auto *central = new QWidget(&window);
    auto *layout = new QVBoxLayout(central);
    auto *toolbar = new QHBoxLayout;
    layout->addLayout(toolbar);

    auto *view = new viv::VirtualTableView(central);
    layout->addWidget(view, 1);
    window.setCentralWidget(central);

    if (cellMode) {
        view->setCellAdapter(&cellAdapter);
        view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view->setTableAdapter(&tableAdapter);
    }
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(kColumnWidth);
    view->setModel(model);
    if (frozenColumns > 0) {
        QVector<int> frozen;
        for (int column = 0; column < frozenColumns; ++column)
            frozen.append(column);
        view->setFrozenColumns(frozen);
    }
    if (!parser.isSet(noSpanOption))
        applySpans(view);

    auto *spanCheck = new QCheckBox(QStringLiteral("合并单元格"), central);
    spanCheck->setChecked(!parser.isSet(noSpanOption));
    auto *status = new QLabel(central);
    status->setObjectName(QStringLiteral("statusLabel"));
    toolbar->addWidget(spanCheck);
    window.statusBar()->addWidget(status);

    QObject::connect(spanCheck, &QCheckBox::toggled, &window, [view](bool enabled) {
        if (enabled)
            applySpans(view);
        else
            view->clearSpans();
    });

    window.setWindowTitle(QStringLiteral("VirtualItemViews · spans (§43)"));
    window.resize(1100, 620);
    window.show();
    view->flushPendingRelayout();

    const auto updateStatus = [&]() {
        const qsizetype logical = view->model() ? view->model()->rowCount() : 0;
        status->setText(QStringLiteral("%1 行 x %2 列   ·   %3   ·   物化 %4   ·   池 %5")
                            .arg(logical)
                            .arg(view->columnCount())
                            .arg(cellMode ? QStringLiteral("Cell Widget Mode")
                                          : QStringLiteral("Row Widget Mode"))
                            .arg(view->usesItemWidgets() ? view->materializedItemCount()
                                                         : view->materializedCellCount())
                            .arg(view->pooledWidgetCount()));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    updateStatus();

    if (parser.isSet(hoverOption)) {
        const QStringList parts = parser.value(hoverOption).split(QLatin1Char(':'));
        bool ok = parts.size() == 2;
        const int row = ok ? parts.at(0).toInt(&ok) : -1;
        const int column = ok ? parts.at(1).toInt(&ok) : -1;
        if (!ok || row < 0 || column < 0 || column >= view->columnCount()) {
            qWarning("--hover 需要 <row>:<column>");
            return 2;
        }
        QTimer::singleShot(0, &window, [view, row, column]() {
            const QRect rect = view->spanRect(view->model()->index(row, column));
            const QPoint pos = rect.isEmpty() ? QPoint(10, 10) : rect.center();
            QScopedPointer<QMimeData> data(view->model()->mimeData({view->model()->index(row, 0)}));
            view->setDragEnabled(true);
            QDragEnterEvent enter(pos, Qt::CopyAction, data.data(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &enter);
            QDragMoveEvent move(pos, Qt::CopyAction, data.data(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &move);
            const viv::VirtualItemView::DropTarget target = view->dropTargetAt(pos);
            qInfo().noquote() << QStringLiteral("hover(%1,%2) -> anchor=(%3,%4) row=%5 column=%6")
                                     .arg(row)
                                     .arg(column)
                                     .arg(view->anchorIndex(view->model()->index(row, column)).row())
                                     .arg(view->anchorIndex(view->model()->index(row, column)).column())
                                     .arg(target.row)
                                     .arg(target.column);
        });
    }

    if (parser.isSet(snapshotOption)) {
        const QString path = parser.value(snapshotOption);
        QTimer::singleShot(150, &window, [&window, path]() {
            const bool ok = window.grab().save(path);
            qInfo().noquote() << QStringLiteral("snapshot %1: %2")
                                     .arg(path, ok ? QStringLiteral("ok") : QStringLiteral("failed"));
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    const int exitAfter = parser.value(exitOption).toInt();
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter, &app, &QCoreApplication::quit);

    return app.exec();
}
