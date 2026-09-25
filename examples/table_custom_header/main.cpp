// 自定义表头示例（§15/§17-§19）：同一个 VirtualTableView 可以在
//   * NativeHeaderView（QHeaderView + QStyle 绘制）与
//   * VirtualHeaderView（每个可见 section 一个真实 QWidget）
// 之间切换，表格主体完全不用改——列几何始终只有 HeaderGeometry 一份。
//
// Widget 表头只 materialize「可见列 + 横向 overscan + pinned section」，
// 所以 200 列也只创建个位数个 section 控件。
//
// 运行：
//   table_custom_header --exit-after 2000            # 无人值守
//   table_custom_header --widget-header --sections 200

#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtableview.h>

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <cstdio>

namespace {

constexpr int kRowHeight = 28;

/// 轻量宽表模型：数据按需生成。
class WideModel : public QAbstractTableModel
{
public:
    WideModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(qMax(1, rows))
        , m_columns(qMax(1, columns))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("r%1c%2").arg(index.row()).arg(index.column() + 1);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1)
                                             : QString::number(section + 1);
    }

    /// 模拟"每列有个待处理数量"，给 widget 表头的 badge 用。
    int pendingCount(int column) const { return (column * 7) % 23; }

private:
    int m_rows = 0;
    int m_columns = 0;
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
    explicit WidgetHeaderAdapter(const WideModel *model)
        : m_model(model)
    {
    }

    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new SectionHeader(parent);
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        const QString title = m_model->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
        static_cast<SectionHeader *>(widget)->bind(logicalIndex, title,
                                                  m_model->pendingCount(logicalIndex), false,
                                                  Qt::AscendingOrder);
        ++bound;
    }

    int created = 0;
    int bound = 0;

private:
    const WideModel *m_model = nullptr;
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
                                    QStringLiteral("启动时使用 QWidget 版表头"));
    QCommandLineOption frozenOption(QStringLiteral("frozen"), QStringLiteral("左侧冻结列数（§31）"),
                                    QStringLiteral("count"), QStringLiteral("0"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    QCommandLineOption moveDemoOption(QStringLiteral("move-demo"),
                                      QStringLiteral("无人值守动画演示：慢速移动一列并在途中截图"),
                                      QStringLiteral("path"));
    QCommandLineOption animationOption(QStringLiteral("animation"),
                                       QStringLiteral("section 移动动画时长（ms，0 = 关闭）"),
                                       QStringLiteral("ms"), QStringLiteral("160"));
    parser.addOption(rowsOption);
    parser.addOption(sectionsOption);
    parser.addOption(widgetOption);
    parser.addOption(frozenOption);
    parser.addOption(exitOption);
    parser.addOption(moveDemoOption);
    parser.addOption(animationOption);
    parser.process(app);

    const int rowCount = qMax(1, parser.value(rowsOption).toInt());
    const int columnCount = qMax(1, parser.value(sectionsOption).toInt());

    WideModel model(rowCount, columnCount);
    RowAdapter rowAdapter(columnCount);
    WidgetHeaderAdapter headerAdapter(&model);

    QMainWindow window;
    auto *view = new viv::VirtualTableView(&window);
    view->setTableAdapter(&rowAdapter);
    view->setUniformItemHeight(kRowHeight);
    view->setDefaultColumnWidth(120);
    view->setColumnOverscan(1);
    view->setSortingEnabled(true);
    view->setModel(&model);
    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · custom header"));
    window.resize(1000, 600);

    const auto useWidgetHeader = [view, &headerAdapter, &model](bool widget) {
        if (widget) {
            auto *header = new viv::VirtualHeaderView(Qt::Horizontal);
            header->setAdapter(&headerAdapter);
            header->setLabelModel(&model);
            header->setSortInteractionEnabled(true);
            header->setSectionOverscan(1);
            view->setHorizontalHeader(header);   // 表格会自动按需销毁/接替
        } else {
            view->setHorizontalHeader(nullptr);  // nullptr = 回到 native 表头
        }
    };

    auto *toolbar = window.addToolBar(QStringLiteral("表头"));
    auto *widgetHeader = new QCheckBox(QStringLiteral("Widget 表头"), &window);
    toolbar->addWidget(widgetHeader);
    toolbar->addWidget(new QLabel(QStringLiteral("  冻结: "), &window));
    auto *frozen = new QCheckBox(QStringLiteral("前 2 列"), &window);
    toolbar->addWidget(frozen);
    QObject::connect(widgetHeader, &QCheckBox::toggled, view, useWidgetHeader);
    QObject::connect(frozen, &QCheckBox::toggled, view, [view, columnCount](bool on) {
        view->setFrozenColumns(on ? QVector<int>({0, qMin(1, columnCount - 1)}) : QVector<int>());
    });
    if (parser.isSet(widgetOption))
        widgetHeader->setChecked(true);
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
                                     ? QStringLiteral("native（不支持过渡）")
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
    const int exitAfter = parser.value(exitOption).toInt();
    if (!moveDemoPath.isEmpty()) {
        // 慢速动画 + 途中截图：能直接看到 section 在飞、而 body 已经在终点位置。
        if (!widgetHeader->isChecked())
            widgetHeader->setChecked(true); // 只有 widget 表头能做视觉过渡
        view->setHeaderAnimationDuration(1200);
        QTimer::singleShot(0, view, [view, columnCount]() {
            view->moveColumn(1, qMin(columnCount - 1, 4),
                             viv::VirtualTableView::MoveAnimation::Animate);
        });
        QTimer::singleShot(500, &app, [&window, view, moveDemoPath]() {
            const QPixmap shot = window.grab();
            const bool saved = shot.save(moveDemoPath);
            std::printf("table_custom_header: move-demo %s (%dx%d)%s committedX1=%d "
                        "committedX4=%d\n",
                        qPrintable(moveDemoPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED", view->columnGeometry(1).viewportX,
                        view->columnGeometry(4).viewportX);
            std::fflush(stdout);
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
