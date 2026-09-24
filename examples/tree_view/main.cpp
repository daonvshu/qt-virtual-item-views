// 树示例（v0.6 Tree MVP）：设备 -> 通道 -> 测点，共两万多个节点，
// 只实例化"展开后的可见行"；缩进、分支指示、展开/折叠、键盘导航都由框架提供。

#include <virtualitemviews/branchindicator.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <cstdio>

namespace {

struct Node
{
    QString name;
    Node *parent = nullptr;
    QVector<Node *> children;

    ~Node() { qDeleteAll(children); }
};

/// Classic QAbstractItemModel tree: identity is the node pointer.
class DeviceTreeModel : public QAbstractItemModel
{
public:
    explicit DeviceTreeModel(int deviceCount, QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {
        for (int device = 0; device < deviceCount; ++device) {
            auto *node = new Node;
            node->name = QStringLiteral("设备 %1").arg(device);
            for (int channel = 0; channel < 20; ++channel) {
                auto *channelNode = new Node;
                channelNode->name = QStringLiteral("通道 %1").arg(channel);
                channelNode->parent = node;
                for (int point = 0; point < 5; ++point) {
                    auto *pointNode = new Node;
                    pointNode->name = QStringLiteral("测点 %1（%2）")
                                          .arg(point)
                                          .arg((device * 97 + channel * 7 + point) % 4096);
                    pointNode->parent = channelNode;
                    channelNode->children.append(pointNode);
                }
                node->children.append(channelNode);
            }
            m_roots.append(node);
        }
    }

    ~DeviceTreeModel() override { qDeleteAll(m_roots); }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (column != 0)
            return QModelIndex();
        Node *parentNode = nodeFor(parent);
        const QVector<Node *> &children = parentNode ? parentNode->children : m_roots;
        if (row < 0 || row >= children.size())
            return QModelIndex();
        return createIndex(row, column, children.at(row));
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        Node *node = nodeFor(index);
        if (!node || !node->parent)
            return QModelIndex();
        Node *parentNode = node->parent;
        const QVector<Node *> &siblings =
            parentNode->parent ? parentNode->parent->children : m_roots;
        for (int row = 0; row < siblings.size(); ++row) {
            if (siblings.at(row) == parentNode)
                return createIndex(row, 0, parentNode);
        }
        return QModelIndex();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        // The invalid root index has column() == -1, so it must be handled
        // before the column check.
        if (parent.isValid() && parent.column() != 0)
            return 0;
        Node *node = nodeFor(parent);
        return node ? node->children.size() : m_roots.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return 1;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return static_cast<Node *>(index.internalPointer())->name;
    }

    QVariant headerData(int, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return QVariant();
        return QStringLiteral("名称");
    }

private:
    Node *nodeFor(const QModelIndex &index) const
    {
        return index.isValid() ? static_cast<Node *>(index.internalPointer()) : nullptr;
    }

    QVector<Node *> m_roots;
};

class TreeRowWidget : public QWidget
{
public:
    explicit TreeRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("rowLabel"));
        m_label->setGeometry(0, 0, 200, 20);
    }

    void setText(const QString &text) { m_label->setText(text); }

    void relayout(int width)
    {
        m_label->setGeometry(0, 0, qMax(0, width), height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout(width());
    }

private:
    QLabel *m_label = nullptr;
};

/// 自定义分支图标：按 QTreeView::branch 的状态词汇（has-children / has-siblings /
/// adjoins-item / open）分别画不同图形，不需要任何样式表或图片资源。
///   - 祖先格：画连接线（从上方接入、还有兄弟节点就继续向下、非本行格子向右连）
///   - 本行格：有子节点时闭合画实心方块、展开画空心方块；叶子画一个小圆点
class BranchGlyphRenderer : public viv::BranchIndicatorRenderer
{
public:
    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &index, const QRect &cellRect) const override
    {
        Q_UNUSED(index);
        if (cellRect.isEmpty())
            return;

        const QColor line(0x9b, 0xa3, 0xaf);
        const QColor accent(0x2f, 0x6f, 0xed);
        const int centerX = cellRect.left() + cellRect.width() / 2;
        const int centerY = cellRect.center().y();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(line, 1));

        // 连接线：本行格的线从上方接到中点，还有兄弟节点时继续贯穿到格子底部；
        // 祖先格再向右连一条横线（表示下面还有更深的一层）。
        const bool incoming = state.cellDepth > 0;
        if (incoming || state.hasSiblings) {
            painter->drawLine(centerX, incoming ? cellRect.top() : centerY, centerX,
                              state.hasSiblings ? cellRect.bottom() : centerY);
        }
        if (!state.adjoinsItem)
            painter->drawLine(centerX, centerY, cellRect.right() - 2, centerY);

        // 本行格的图标：方块表示还有子节点（展开 = 空心），圆点表示叶子。
        if (state.adjoinsItem) {
            const int size = 9;
            const QRect box(centerX - size / 2, centerY - size / 2, size, size);
            if (state.hasChildren) {
                painter->setPen(QPen(accent, 1));
                painter->setBrush(state.isExpanded ? Qt::NoBrush : QBrush(accent));
                painter->drawRect(box);
            } else {
                painter->setPen(Qt::NoPen);
                painter->setBrush(line);
                painter->drawEllipse(box.adjusted(2, 2, -2, -2));
            }
        }
        painter->restore();
    }
};

class TreeAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new TreeRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<TreeRowWidget *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        static_cast<TreeRowWidget *>(widget)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(300, 26);
    }

    int created = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualItemViews: tree"));
    parser.addHelpOption();
    QCommandLineOption devicesOption(QStringLiteral("devices"), QStringLiteral("顶层设备数"),
                                     QStringLiteral("count"), QStringLiteral("200"));
    QCommandLineOption indentationOption(QStringLiteral("indentation"), QStringLiteral("缩进像素"),
                                         QStringLiteral("pixels"), QStringLiteral("20"));
    QCommandLineOption pixelStepOption(QStringLiteral("wheel-pixels"), QStringLiteral("滚轮每刻度像素"),
                                       QStringLiteral("pixels"), QStringLiteral("48"));
    QCommandLineOption customIconsOption(QStringLiteral("custom-icons"),
                                         QStringLiteral("启动时使用自定义分支图标渲染器"));
    QCommandLineOption snapshotOption(QStringLiteral("snapshot"),
                                      QStringLiteral("渲染视口到 PNG 后退出"), QStringLiteral("file"));
    QCommandLineOption exitOption(QStringLiteral("exit-after"),
                                  QStringLiteral("毫秒后自动退出（0 = 一直运行）"),
                                  QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(devicesOption);
    parser.addOption(indentationOption);
    parser.addOption(pixelStepOption);
    parser.addOption(customIconsOption);
    parser.addOption(snapshotOption);
    parser.addOption(exitOption);
    parser.process(app);

    const int deviceCount = qMax(1, parser.value(devicesOption).toInt());
    DeviceTreeModel model(deviceCount);
    TreeAdapter adapter;
    BranchGlyphRenderer glyphRenderer;

    QMainWindow window;
    // 视图由窗口持有；模型/适配器先声明，生命周期覆盖视图。
    auto *view = new viv::VirtualTreeView(&window);
    view->setAdapter(&adapter);
    view->setUniformItemHeight(26);
    view->setIndentation(parser.value(indentationOption).toInt());
    view->setOverscan(2, 4);
    // 与其它示例一致：滚动全部按像素进行，行高不参与滚动粒度。
    view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
    view->setWheelScrollPixels(parser.value(pixelStepOption).toInt());
    view->setModel(&model);

    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualItemViews · tree (%1 devices)").arg(deviceCount));
    window.resize(760, 560);

    auto *toolbar = window.addToolBar(QStringLiteral("树"));
    auto *collapseAll = new QCheckBox(QStringLiteral("全部折叠"), &window);
    toolbar->addWidget(collapseAll);
    auto *indicators = new QCheckBox(QStringLiteral("分支指示"));
    indicators->setChecked(true);
    toolbar->addWidget(indicators);
    auto *customIcons = new QCheckBox(QStringLiteral("自定义图标"));
    toolbar->addWidget(customIcons);
    toolbar->addWidget(new QLabel(QStringLiteral("  缩进: "), &window));
    auto *indentation = new QSpinBox(&window);
    indentation->setRange(0, 60);
    indentation->setValue(view->indentation());
    toolbar->addWidget(indentation);
    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);

    QObject::connect(collapseAll, &QCheckBox::toggled, view, [view](bool collapsed) {
        if (collapsed)
            view->collapseAll();
        else
            view->expand(view->model()->index(0, 0));
    });
    QObject::connect(indicators, &QCheckBox::toggled, view, [view](bool visible) {
        view->setBranchIndicatorsVisible(visible);
    });
    QObject::connect(customIcons, &QCheckBox::toggled, view, [view, &glyphRenderer](bool custom) {
        // 渲染器由本窗口持有（takeOwnership = false），nullptr 表示回到内置三角箭头。
        view->setBranchIndicatorRenderer(custom ? &glyphRenderer : nullptr);
    });
    if (parser.isSet(customIconsOption))
        customIcons->setChecked(true);
    QObject::connect(indentation, QOverload<int>::of(&QSpinBox::valueChanged), view, [view](int pixels) {
        view->setIndentation(pixels);
    });

    const auto updateStatus = [&]() {
        const viv::VirtualViewStats stats = view->stats();
        status->setText(QStringLiteral("可见行: %1   实例化: %2   池: %3   累计创建: %4   滚动: %5 px")
                            .arg(view->visibleRowCount())
                            .arg(stats.materializedItems)
                            .arg(stats.pooledWidgets)
                            .arg(stats.createCount)
                            .arg(view->verticalOffset()));
    };
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, &window, updateStatus);
    QTimer::singleShot(0, &window, updateStatus);

    const int exitAfter = parser.value(exitOption).toInt();
    const QString snapshotPath = parser.value(snapshotOption);
    if (exitAfter > 0 || !snapshotPath.isEmpty()) {
        // 展开前两个设备，便于无人值守观察。
        view->expand(model.index(0, 0));
        view->expand(model.index(1, 0));
        view->expand(model.index(0, 0, model.index(0, 0)));   // 设备 0 的第一个通道
    }

    if (!snapshotPath.isEmpty()) {
        // 渲染视口到 PNG：无人值守的视觉检查与文档截图都用它。
        QTimer::singleShot(qMax(1, exitAfter), &app, [view, snapshotPath, deviceCount]() {
            const QPixmap shot = view->viewport()->grab();
            const bool saved = shot.save(snapshotPath);
            std::printf("tree_view: snapshot %s (%dx%d)%s devices=%d visibleRows=%lld\n",
                        qPrintable(snapshotPath), shot.width(), shot.height(),
                        saved ? "" : " FAILED", deviceCount,
                        static_cast<long long>(view->visibleRowCount()));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    } else if (exitAfter > 0) {
        // 无人值守运行时输出一行统计，让 headless 冒烟测试能验证可见行映射。
        QTimer::singleShot(exitAfter, &app, [view, deviceCount]() {
            const viv::VirtualViewStats stats = view->stats();
            std::printf("tree_view: devices=%d visibleRows=%lld materialized=%lld created=%llu\n",
                        deviceCount,
                        static_cast<long long>(view->visibleRowCount()),
                        static_cast<long long>(stats.materializedItems),
                        static_cast<unsigned long long>(stats.createCount));
            std::fflush(stdout);
            QCoreApplication::quit();
        });
    }

    window.show();
    return app.exec();
}
