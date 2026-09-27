// 自定义表头示例（§15/§17-§19）：同一个 VirtualTableView 可以在
//   * 默认的 label 表头（LabelHeaderView：Widget 表头 + 库里自带的、只画 label 的 adapter）、
//   * 本示例的自定义表头（VirtualHeaderView + badge/过滤按钮的 section 控件）与
// 之间切换，表格主体完全不用改——列几何始终只有 HeaderGeometry 一份。
// 默认表头就是 Widget 表头，所以拖动换序、section 过渡、"整列一起动"在默认配置下也有。
//
// Widget 表头只 materialize「可见列 + 横向 overscan + pinned section」，
// 所以 200 列也只创建个位数个 section 控件。
//
// 运行：
//   table_custom_header --exit-after 2000            # 无人值守
//   table_custom_header --widget-header --sections 200
//   table_custom_header --widget-header --move-demo shot.png        # 换序过渡 + 途中截图
//   table_custom_header --widget-header --drag-demo shot.png        # 拖动预览（Esc 取消）+ 截图
//   table_custom_header --widget-header --drag-commit-demo shot.png # 拖动并松手提交 + 途中截图
//
// 表头动画（§23/§24）只有 Widget 表头支持，而且"整列一起动"是**库能力**：
// setColumnFollowsHeaderVisual()（默认开）让 body 的列控件跟着表头 section 的视觉位置走 ——
// 拖动期间整列跟着指针走、邻居平滑让位，松手提交后一起从预览位置收敛；committed 几何始终是
// 唯一事实来源（columnGeometry()、命中测试、滚动条都不受动画影响）。工具条上的复选框切换它，
// `--no-body-animation` 是无人值守的对照。
//   * 关掉它 = 以前的样子：表头在飞，body 在提交时一次到位。
// 演示里 `--move-demo` / `--drag-commit-demo` 的截图时刻是固定延时（按默认 60 列调过）；
// 宽表首屏可能花几百毫秒，想让截图落在过渡中途，把 `--animation` 调大一些即可。

#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/reorderabletablemodel.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <cstdio>

namespace {

/// 合成一次鼠标事件（拖动演示用；示例里不能依赖 QtTest）。
void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

constexpr int kRowHeight = 28;

/// 轻量宽表模型：数据按需生成，行序交给 `viv::ReorderableTableModel`（"拖拽排序基类"：
/// 视图行 → 数据行的顺序表 + `moveRows()`），所以行号条拖动能真的换行序。
class WideModel : public viv::ReorderableTableModel
{
public:
    WideModel(int rows, int columns, QObject *parent = nullptr)
        : viv::ReorderableTableModel(qMax(1, rows), qMax(1, columns), parent)
    {
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("r%1c%2").arg(sourceRow(index.row())).arg(index.column() + 1);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1)
                                             // 行号是这一行的稳定标识（`R7`），拖动换序后不会
                                             // 重新编号：行跟着它的标号一起走，一眼能看出移动生效。
                                             : QStringLiteral("R%1").arg(sourceRow(section) + 1);
    }

    /// 模拟"每列有个待处理数量"，给 widget 表头的 badge 用。
    int pendingCount(int column) const { return (column * 7) % 23; }
};

/// 行控件：每列一个 ColumnHost + 一个 label（与其它表格示例一致）。
class RowWidget : public QWidget
{
public:
    RowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(2, 0, 80, 18);
            m_labels.append(label);
        }
    }

    void bind(const QModelIndex &index)
    {
        for (int column = 0; column < m_labels.size(); ++column)
            m_labels.at(column)->setText(index.siblingAtColumn(column).data().toString());
    }

private:
    QVector<QLabel *> m_labels;
};

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    explicit RowAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new RowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<RowWidget *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(400, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

/// Widget 表头的 section：标题 + 计数 badge + 过滤按钮，全部是真控件。
class SectionHeader : public QWidget
{
public:
    explicit SectionHeader(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 2, 4, 2);
        layout->setSpacing(4);
        m_title = new QLabel(this);
        m_badge = new QLabel(this);
        m_badge->setObjectName(QStringLiteral("badge"));
        m_badge->setAlignment(Qt::AlignCenter);
        m_badge->setMinimumWidth(20);
        m_filter = new QPushButton(QStringLiteral("≡"), this);
        m_filter->setObjectName(QStringLiteral("filterButton"));
        m_filter->setFixedSize(20, 20);
        m_filter->setToolTip(QStringLiteral("过滤（示意：真实项目里可以弹菜单）"));
        layout->addWidget(m_title, 1);
        layout->addWidget(m_badge);
        layout->addWidget(m_filter);
    }

    void bind(int logicalIndex, const QString &title, int pending, bool sorted, Qt::SortOrder order)
    {
        m_title->setText(sorted ? QStringLiteral("%1 %2").arg(title, order == Qt::AscendingOrder
                                                                       ? QStringLiteral("▲")
                                                                       : QStringLiteral("▼"))
                                : title);
        m_badge->setText(QString::number(pending));
        m_badge->setVisible(pending > 0);
    }

private:
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QPushButton *m_filter = nullptr;
};

class WidgetHeaderAdapter : public viv::HeaderWidgetAdapter
{
public:
    WidgetHeaderAdapter() = default;

    /// The header tells the adapter which model the labels come from (and it may change: the
    /// table hands its pane clones the same adapter). Holding a QPointer keeps this safe even if
    /// the model is destroyed first; the downcast happens per call, so no captured pointer
    /// survives a replacement.
    void setLabelModel(QAbstractItemModel *model) override { m_model = model; }

    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new SectionHeader(parent);
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        // dynamic_cast instead of qobject_cast: the example's model carries no Q_OBJECT macro.
        auto *model = dynamic_cast<WideModel *>(m_model.data());
        if (!model)
            return;
        const QString title = model->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
        static_cast<SectionHeader *>(widget)->bind(logicalIndex, title,
                                                  model->pendingCount(logicalIndex), false,
                                                  Qt::AscendingOrder);
        ++bound;
    }

    int created = 0;
    int bound = 0;

private:
    QPointer<QAbstractItemModel> m_model;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: custom (widget) header"));
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("行数"),
                                  QStringLiteral("count"), QStringLiteral("5000"));
    QCommandLineOption sectionsOption(QStringLiteral("sections"), QStringLiteral("列数"),
                                      QStringLiteral("count"), QStringLiteral("60"));
    QCommandLineOption widgetOption(QStringLiteral("widget-header"),
                                    QStringLiteral("启动时使用本示例的自定义表头"
                                                   "（不带 = 库里默认的 label 表头）"));
    QCommandLineOption frozenOption(QStringLiteral("frozen"), QStringLiteral("左侧冻结列数（§31）"),
                                    QStringLiteral("count"), QStringLiteral("0"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    QCommandLineOption moveDemoOption(QStringLiteral("move-demo"),
                                      QStringLiteral("无人值守动画演示：慢速移动一列并在途中截图"),
                                      QStringLiteral("path"));
    QCommandLineOption dragDemoOption(QStringLiteral("drag-demo"),
                                      QStringLiteral("无人值守拖动演示：合成一次拖动并在途中截图"),
                                      QStringLiteral("path"));
    QCommandLineOption dragCommitDemoOption(
        QStringLiteral("drag-commit-demo"),
        QStringLiteral("无人值守拖动演示：合成一次拖动并松手提交，在过渡途中截图"),
        QStringLiteral("path"));
    QCommandLineOption noBodyAnimationOption(
        QStringLiteral("no-body-animation"),
        QStringLiteral("关掉库的“整列一起动”（对照：body 像以前一样在提交时一次到位）"));
    QCommandLineOption animationOption(QStringLiteral("animation"),
                                       QStringLiteral("section 移动动画时长（ms，0 = 关闭）"),
                                       QStringLiteral("ms"), QStringLiteral("300"));
    parser.addOption(rowsOption);
    parser.addOption(sectionsOption);
    parser.addOption(widgetOption);
    parser.addOption(frozenOption);
    parser.addOption(exitOption);
    parser.addOption(moveDemoOption);
    parser.addOption(dragDemoOption);
    parser.addOption(dragCommitDemoOption);
    parser.addOption(noBodyAnimationOption);
    parser.addOption(animationOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    const int columnCount = qMax(1, parser.value(sectionsOption).toInt());

    WideModel model(rowCount, columnCount);
    RowAdapter rowAdapter(columnCount);
    WidgetHeaderAdapter headerAdapter;

    QMainWindow window;
    auto *view = new viv::VirtualTableView(&window);
    view->setTableAdapter(&rowAdapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(120);
    view->setColumnOverscan(1);
    view->setSortingEnabled(true);
    view->setModel(&model);
    // 表头拖动默认关闭（1.0 起）：本示例是表头交互的演示，两个方向都打开——列拖动换序
    // 与行号条拖动换行序（WideModel 继承 viv::ReorderableTableModel，所以行序真的会变）。
    view->setColumnDragEnabled(true);
    view->setVerticalHeaderDragEnabled(true);
    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · custom header"));
    window.resize(1000, 600);

    // 库能力："整列一起动"——拖动与换序过渡期间，body 的整列跟着表头的 section 走。
    view->setColumnFollowsHeaderVisual(!parser.isSet(noBodyAnimationOption));

    // 三种表头：默认的 label 表头（Widget 表头，库里自带）、本示例的自定义表头
    // 两种表头：库里自带的 label 表头，以及本示例的自定义表头（badge + 过滤按钮）。
    const auto useHeader = [view, &headerAdapter, &model](int kind) {
        switch (kind) {
        case 1: {   // 自定义 section 控件
            auto *header = new viv::VirtualHeaderView(Qt::Horizontal);
            header->setAdapter(&headerAdapter);
            header->setLabelModel(&model);
            header->setSortInteractionEnabled(true);
            header->setSectionOverscan(1);
            view->setHorizontalHeader(header);   // 表格会自动按需销毁/接替
            break;
        }
        default:    // 回到库里自带的 label 表头（也是 Widget 表头）
            view->setHorizontalHeader(nullptr);
            break;
        }
    };

    auto *toolbar = window.addToolBar(QStringLiteral("表头"));
    auto *headerKind = new QComboBox(&window);
    headerKind->addItem(QStringLiteral("默认 label 表头"));
    headerKind->addItem(QStringLiteral("自定义表头"));
    toolbar->addWidget(new QLabel(QStringLiteral("表头: "), &window));
    toolbar->addWidget(headerKind);
    auto *bodyCheck = new QCheckBox(QStringLiteral("整列一起动"), &window);
    bodyCheck->setChecked(view->columnFollowsHeaderVisual());
    bodyCheck->setToolTip(QStringLiteral(
        "库能力 setColumnFollowsHeaderVisual()：拖动时整列跟着表头的 section 一起走，\n"
        "松手提交后也从预览位置收敛；关掉它就是以前的样子（body 提交时一次到位）"));
    toolbar->addWidget(bodyCheck);
    QObject::connect(bodyCheck, &QCheckBox::toggled, view,
                     &viv::VirtualTableView::setColumnFollowsHeaderVisual);
    toolbar->addWidget(new QLabel(QStringLiteral("  冻结: "), &window));
    auto *frozen = new QCheckBox(QStringLiteral("前 2 列"), &window);
    toolbar->addWidget(frozen);
    // qOverload: Qt 5 的 currentIndexChanged 还有 (const QString &) 重载。
    QObject::connect(headerKind, qOverload<int>(&QComboBox::currentIndexChanged), view,
                     useHeader);
    QObject::connect(frozen, &QCheckBox::toggled, view, [view, columnCount](bool on) {
        view->setFrozenColumns(on ? QVector<int>({0, qMin(1, columnCount - 1)}) : QVector<int>());
    });
    if (parser.isSet(widgetOption))
        headerKind->setCurrentIndex(1);
    const int frozenCount = qBound(0, parser.value(frozenOption).toInt(), columnCount - 1);
    if (frozenCount > 0) {
        QVector<int> columns;
        for (int column = 0; column < frozenCount; ++column)
            columns.append(column);
        view->setFrozenColumns(columns);
        const QSignalBlocker blocker(frozen);
        frozen->setChecked(true);
    }

    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);
    const auto updateStatus = [&]() {
        const viv::VirtualViewStats stats = view->stats();
        int sectionWidgets = -1;
        if (auto *header = dynamic_cast<viv::VirtualHeaderView *>(view->horizontalHeader()))
            sectionWidgets = int(header->materializedSectionCount());
        status->setText(QStringLiteral("逻辑行: %1   列: %2   实例化行: %3   表头 section 控件: %4   "
                                       "累计创建(行/表头): %5 / %6")
                            .arg(stats.logicalItems)
                            .arg(view->columnCount())
                            .arg(stats.materializedItems)
                            .arg(sectionWidgets < 0
                                     ? QStringLiteral("native（QHeaderView，不做过渡）")
                                     : QString::number(sectionWidgets))
                            .arg(stats.createCount)
                            .arg(headerAdapter.created));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QObject::connect(view, &viv::VirtualTableView::columnGeometryChanged, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    // §23/§24：移动列时表头做视觉过渡，而 body 立刻采用 committed geometry。
    view->setHeaderAnimationDuration(parser.value(animationOption).toInt());
    // 程序化换序默认即时（不播动画）；勾上它才让"移动一列"走 §23 的视觉过渡。
    auto *animateMoves = new QCheckBox(QStringLiteral("换序时过渡"), &window);
    animateMoves->setToolTip(QStringLiteral("程序化换序默认即时；勾选后显式请求表头的视觉过渡"));
    toolbar->addWidget(animateMoves);
    auto *moveAction = toolbar->addAction(QStringLiteral("移动一列"));
    QObject::connect(moveAction, &QAction::triggered, view, [view, columnCount, animateMoves]() {
        const int from = columnCount > 1 ? 1 : 0;
        const int to = qMin(columnCount - 1, from + 3);
        view->moveColumn(from, to, animateMoves->isChecked()
                                       ? viv::VirtualTableView::MoveAnimation::Animate
                                       : viv::VirtualTableView::MoveAnimation::Immediate);
    });

    const QString moveDemoPath = parser.value(moveDemoOption);
    const QString dragDemoPath = parser.value(dragDemoOption);
    const QString dragCommitDemoPath = parser.value(dragCommitDemoOption);
    const int exitAfter = parser.value(exitOption).toInt();

    // 无人值守截图 + 报告：把"整列一起动"这一帧的状态打出来 —— 第 0..4 列的 committed x、
    // 第一行里这些列的 ColumnHost x、以及表头 section 的 x。跟帧时 body 与 header 两行相等。
    const auto reportShot = [&](const QString &path, const char *kind) {
        const QPixmap shot = window.grab();
        const bool saved = shot.save(path);
        QStringList committedX;
        QStringList bodyX;   // 第 0..4 列 host 的 x（viewport 坐标）
        QStringList headerX; // 第 0..4 列 section 控件的 x（换算到 viewport 坐标）
        for (int column = 0; column <= 4; ++column)
            committedX << QString::number(view->columnGeometry(column).viewportX);
        const QList<viv::MaterializedItem> rows = view->materializedItems();
        if (!rows.isEmpty()) {
            QHash<int, int> byColumn;
            for (viv::ColumnHost *host : rows.first().widget->findChildren<viv::ColumnHost *>()) {
                const int x = host->mapTo(rows.first().widget, QPoint(0, 0)).x();
                byColumn.insert(host->logicalColumn(), x);
            }
            for (int column = 0; column <= 4; ++column)
                bodyX << QString::number(byColumn.value(column, -999));
        }
        // 主表头画滚动 pane，冻结 pane 由克隆的渲染器画（§43）：两边的 section 都要看，
        // 否则冻结列会误报成"没有 section"。
        // viewport 坐标：section 的局部 x 加上"表头控件到 viewport"的偏移，用 global 换算最稳。
        const QPoint viewportOrigin = view->viewport()->mapToGlobal(QPoint(0, 0));
        QHash<int, int> sectionXByColumn;
        for (viv::VirtualHeaderView *renderer : view->findChildren<viv::VirtualHeaderView *>()) {
            if (renderer->orientation() != Qt::Horizontal)
                continue;   // 纵向行号条不参与"列的 x"报告
            for (int column : renderer->materializedSections()) {
                if (sectionXByColumn.contains(column))
                    continue;
                QWidget *section = renderer->sectionWidget(column);
                if (section)
                    sectionXByColumn.insert(
                        column, section->mapToGlobal(QPoint(0, 0)).x() - viewportOrigin.x());
            }
        }
        for (int column = 0; column <= 4; ++column)
            headerX << QString::number(sectionXByColumn.value(column, -999));
        std::printf("table_custom_header: %s %s (%dx%d)%s bodyFollowsHeader=%d "
                    "committed0-4=[%s] body0-4=[%s] header0-4=[%s]\n",
                    kind, qPrintable(path), shot.width(), shot.height(), saved ? "" : " FAILED",
                    int(view->columnFollowsHeaderVisual()),
                    qUtf8Printable(committedX.join(QLatin1Char(','))),
                    qUtf8Printable(bodyX.join(QLatin1Char(','))),
                    qUtf8Printable(headerX.join(QLatin1Char(','))));
        std::fflush(stdout);
    };

    if (!dragDemoPath.isEmpty()) {
        // §22/§23 的拖动：按下 → 越过阈值后只动"视觉几何"（被拖的列跟随光标、邻居让出插入位），
        // committed 几何在松手前一个字节都不动。这里在拖动途中截图，然后按 Esc 取消。
        if (headerKind->currentIndex() != 1)
        headerKind->setCurrentIndex(1);
        QTimer::singleShot(0, &window, [view, &window, reportShot, dragDemoPath]() {
            QWidget *header = view->horizontalHeader()->headerWidget();
            const int rowY = qMax(2, header->height() / 2);
            const int pressX = view->columnGeometry(1).viewportX + view->columnWidth(1) / 2;
            const int moveX = view->columnGeometry(4).viewportX + view->columnWidth(4) / 2;
            sendMouse(header, QEvent::MouseButtonPress, QPoint(pressX, rowY), Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(header, QEvent::MouseMove, QPoint(moveX, rowY), Qt::NoButton, Qt::LeftButton);
            QApplication::processEvents();

            reportShot(dragDemoPath, "drag-demo");

            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(header, &escape);
            QCoreApplication::quit();
        });
    } else if (!dragCommitDemoPath.isEmpty()) {
        // 拖动并**提交**（松手）：表头与 body 的整列一起从预览位置收敛到 committed —— 与
        // "移动一列"按钮走的是同一条提交路径，区别只是这次 committed 几何是拖动松手改的。
        if (headerKind->currentIndex() != 1)
        headerKind->setCurrentIndex(1);
        view->setHeaderAnimationDuration(1200);
        QTimer::singleShot(0, &window, [view, &window]() {
            QWidget *header = view->horizontalHeader()->headerWidget();
            const int rowY = qMax(2, header->height() / 2);
            const int pressX = view->columnGeometry(1).viewportX + view->columnWidth(1) / 2;
            const int moveX = view->columnGeometry(4).viewportX + view->columnWidth(4) / 2;
            sendMouse(header, QEvent::MouseButtonPress, QPoint(pressX, rowY), Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(header, QEvent::MouseMove, QPoint(moveX, rowY), Qt::NoButton, Qt::LeftButton);
            sendMouse(header, QEvent::MouseButtonRelease, QPoint(moveX, rowY), Qt::LeftButton,
                      Qt::NoButton);
        });
        QTimer::singleShot(300, &app, [reportShot, dragCommitDemoPath]() {
            reportShot(dragCommitDemoPath, "drag-commit-demo");
            QCoreApplication::quit();
        });
    } else if (!moveDemoPath.isEmpty()) {
        // 慢速动画 + 途中截图：能直接看到 section 与整列一起在飞（"整列一起动"关掉时只有 section）。
        // 截图时刻是固定延时，按默认 60 列调过；宽表首屏更慢，需要把 --animation 调大。
        if (headerKind->currentIndex() != 1)
        headerKind->setCurrentIndex(1); // 只有 widget 表头能做视觉过渡
        view->setHeaderAnimationDuration(1200);
        QTimer::singleShot(0, view, [view, columnCount]() {
            view->moveColumn(1, qMin(columnCount - 1, 4),
                             viv::VirtualTableView::MoveAnimation::Animate);
        });
        QTimer::singleShot(500, &app, [reportShot, moveDemoPath]() {
            reportShot(moveDemoPath, "move-demo");
            QCoreApplication::quit();
        });
    } else if (exitAfter > 0) {
        QTimer::singleShot(exitAfter, &app, [view, &headerAdapter, &model, exitAfter, columnCount]() {
            int sections = -1;
            if (auto *header = dynamic_cast<viv::VirtualHeaderView *>(view->horizontalHeader()))
                sections = int(header->materializedSectionCount());
            std::printf("table_custom_header: columns=%d exit=%dms materializedRows=%lld "
                        "sectionWidgets=%d sectionCreated=%d pooled=%lld\n",
                        columnCount, exitAfter, static_cast<long long>(view->materializedItemCount()),
                        sections, headerAdapter.created,
                        static_cast<long long>(view->pooledWidgetCount()));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    }

    window.show();
    return app.exec();
}
