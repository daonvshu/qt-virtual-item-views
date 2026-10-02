#include <virtualitemviews/accessibility.h>
#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/reorderabletablemodel.h>
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QAccessible>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QCursor>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTemporaryFile>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <thread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef interface
#endif

using namespace viv;
using namespace vivtest;

namespace {

QList<QStandardItem *> row(const QString &name)
{
    return {new QStandardItem(name), new QStandardItem(name + QStringLiteral(" type")),
            new QStandardItem(name + QStringLiteral(" state"))};
}

class RowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QWidget(parent); }
    void bindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }
};

class PinWarningCapture
{
public:
    PinWarningCapture()
    {
        active = this;
        previous = qInstallMessageHandler(handleMessage);
    }

    ~PinWarningCapture()
    {
        qInstallMessageHandler(previous);
        active = nullptr;
    }

    QStringList messages;

private:
    static void handleMessage(QtMsgType type, const QMessageLogContext &context,
                              const QString &message)
    {
        if (type == QtWarningMsg
            && message.contains(QStringLiteral("items are pinned, above the configured limit"))) {
            active->messages.append(message);
        } else if (active->previous) {
            active->previous(type, context, message);
        }
    }

    QtMessageHandler previous = nullptr;
    inline static PinWarningCapture *active = nullptr;
};

class SolidBranchRenderer : public BranchIndicatorRenderer
{
public:
    void paintBranch(QPainter *painter, const BranchIndicatorState &state,
                     const QModelIndex &, const QRect &cell) const override
    {
        if (!state.adjoinsItem)
            return;
        painter->fillRect(QRect(cell.center() - QPoint(3, 3), QSize(8, 8)),
                          state.hasChildren ? QColor(10, 180, 210) : QColor(230, 180, 10));
    }
};

class ColorBranchRenderer : public BranchIndicatorRenderer
{
public:
    ColorBranchRenderer(const QColor &branch, const QColor &leaf)
        : branchColor(branch), leafColor(leaf) {}

    void paintBranch(QPainter *painter, const BranchIndicatorState &state,
                     const QModelIndex &, const QRect &cell) const override
    {
        if (!state.adjoinsItem)
            return;
        painter->fillRect(QRect(cell.center() - QPoint(3, 3), QSize(8, 8)),
                          state.hasChildren ? branchColor : leafColor);
    }

    QColor branchColor;
    QColor leafColor;
};

class SelfRemovingBranchRenderer : public BranchIndicatorRenderer
{
public:
    explicit SelfRemovingBranchRenderer(VirtualTreeTableView *view) : view(view) {}

    void paintBranch(QPainter *, const BranchIndicatorState &, const QModelIndex &,
                     const QRect &) const override
    {
        if (!view || invoked)
            return;
        invoked = true;
        view->setBranchIndicatorRenderer(nullptr);
    }

    VirtualTreeTableView *view;
    mutable bool invoked = false;
};

class TreeHeaderAdapter : public HeaderWidgetAdapter
{
public:
    TreeHeaderAdapter(VirtualTreeTableView *view, Qt::Orientation orientation, int owner)
        : view(view), orientation(orientation), owner(owner) {}

    WidgetType sectionType(int) const override
    {
        const WidgetType result = type;
        if (onType) {
            auto callback = std::move(onType);
            callback();
        }
        return result;
    }
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        QPointer<QWidget> widget = new QLabel(parent);
        if (onCreate) {
            auto callback = std::move(onCreate);
            callback();
        }
        return widget.data();
    }
    void setLabelModel(QAbstractItemModel *value) override { labelModel = value; }
    QString text(int logical) const
    {
        if (orientation == Qt::Horizontal)
            return labelModel ? labelModel->headerData(logical, orientation).toString() : QString();
        const QModelIndex node = view->visibilityIndex()->indexAtVisibleRow(logical);
        return QStringLiteral("%1: %2").arg(logical + 1).arg(node.data().toString());
    }
    void bindSection(QWidget *widget, int logical) override
    {
        ++binds;
        widget->setProperty("headerOwner", owner);
        widget->setProperty("logicalSection", logical);
        static_cast<QLabel *>(widget)->setText(text(logical));
        if (onBindWidget) {
            auto callback = std::move(onBindWidget);
            callback(widget);
        }
    }
    void unbindSection(QWidget *widget, int) override
    {
        ++unbinds;
        static_cast<QLabel *>(widget)->clear();
        if (onUnbindWidget) {
            auto callback = std::move(onUnbindWidget);
            callback(widget);
        }
    }

    VirtualTreeTableView *view;
    Qt::Orientation orientation;
    int owner;
    int binds = 0;
    int unbinds = 0;
    std::function<void(QWidget *)> onBindWidget;
    std::function<void(QWidget *)> onUnbindWidget;
    mutable std::function<void()> onType;
    std::function<void()> onCreate;
    WidgetType type = 0;
    QPointer<QAbstractItemModel> labelModel;
};

class ReentrantHeaderAdapter : public TreeHeaderAdapter
{
public:
    ReentrantHeaderAdapter(VirtualTreeTableView *view, Qt::Orientation orientation, int owner)
        : TreeHeaderAdapter(view, orientation, owner) {}

    void bindSection(QWidget *widget, int logical) override
    {
        TreeHeaderAdapter::bindSection(widget, logical);
        if (onBind) {
            auto callback = std::move(onBind);
            callback();
        }
    }

    std::function<void()> onBind;
};

class MisleadingSpanProvider : public TableSpanProvider
{
public:
    explicit MisleadingSpanProvider(const QModelIndex &index) : anchor(index) {}
    TableSpan spanAt(const QModelIndex &index) const override
    {
        return index == anchor ? TableSpan{2, 2} : TableSpan{};
    }
    TableSpan maximumSpan() const override { return TableSpan{2, 2}; }
    QModelIndex anchorOf(const QModelIndex &) const override { return anchor; }

    QPersistentModelIndex anchor;
};

class ReentrantSpanProvider : public TableSpanProvider
{
public:
    ~ReentrantSpanProvider() override
    {
        if (destructions)
            ++*destructions;
    }
    TableSpan spanAt(const QModelIndex &) const override
    {
        fire(0);
        return TableSpan{1, 2};
    }
    QModelIndex anchorOf(const QModelIndex &index) const override
    {
        if (defaultReverse)
            return TableSpanProvider::anchorOf(index);
        const QModelIndex result = stage == 2 ? QModelIndex() : index;
        fire(1);
        return result;
    }
    TableSpan maximumSpan() const override
    {
        fire(2);
        return TableSpan{1, 2};
    }
    void modelStructureChanged() override { fire(3); }

    void fire(int callbackStage) const
    {
        if (!armed || stage != callbackStage)
            return;
        if (skip > 0) {
            --skip;
            return;
        }
        armed = false;
        ++calls;
        const auto action = callback;
        action();
        if (resumptions)
            ++*resumptions;
    }

    int stage = 0;
    bool defaultReverse = false;
    mutable bool armed = false;
    mutable int calls = 0;
    mutable int skip = 0;
    std::function<void()> callback;
    int *destructions = nullptr;
    int *resumptions = nullptr;
};

class PreviewRefreshHookView : public VirtualTreeTableView
{
public:
    std::function<void()> beforeMaskRefresh;

protected:
    void visualColumnGeometryChanged() override
    {
        VirtualTreeTableView::visualColumnGeometryChanged();
        // setRowFollowsHeaderVisual refreshes masks immediately after this hook.
        const auto callback = std::move(beforeMaskRefresh);
        if (callback)
            callback();
    }
};

class PartialPreviewSpanProvider : public TableSpanMap
{
public:
    TableSpan spanAt(const QModelIndex &index) const override
    {
        if (armed && index == projectedAnchor)
            ++anchorQueries;
        return TableSpanMap::spanAt(index);
    }

    QModelIndex anchorOf(const QModelIndex &index) const override
    {
        if (armed && index == triggerIndex) {
            armed = false;
            ++calls;
            queriesBeforeCallback = anchorQueries;
            callback();
        }
        return TableSpanMap::anchorOf(index);
    }

    QPersistentModelIndex projectedAnchor;
    QPersistentModelIndex triggerIndex;
    std::function<void()> callback;
    mutable bool armed = false;
    mutable int anchorQueries = 0;
    mutable int queriesBeforeCallback = 0;
    mutable int calls = 0;
};

class QueryCountingTreeModel : public QStandardItemModel
{
public:
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        ++queries;
        return QStandardItemModel::index(row, column, parent);
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        ++queries;
        return QStandardItemModel::parent(index);
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        ++queries;
        return QStandardItemModel::rowCount(parent);
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        ++queries;
        return QStandardItemModel::columnCount(parent);
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        ++queries;
        return QStandardItemModel::data(index, role);
    }
    mutable quint64 queries = 0;
};

class MeasuredRow : public QWidget
{
public:
    explicit MeasuredRow(QWidget *parent) : QWidget(parent) {}
    QSize sizeHint() const override { return QSize(0, property("hintHeight").toInt()); }
};

class MeasuredRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new MeasuredRow(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        const int height = index.data(Qt::UserRole).toInt();
        widget->setProperty("hintHeight", height > 0 ? height : 28);
        widget->updateGeometry();
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }
};

class ColoredHost : public ColumnHost
{
public:
    explicit ColoredHost(int column, QWidget *parent) : ColumnHost(column, parent) {}

    void setFillColor(const QColor &color)
    {
        m_fillColor = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_fillColor.isValid())
            return;
        QPainter painter(this);
        painter.fillRect(rect(), m_fillColor);
    }

private:
    QColor m_fillColor;
};

class HostedRow : public QWidget
{
public:
    HostedRow(int columns, QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < columns; ++column)
            m_hosts.append(new ColoredHost(column, this));
    }

    ColoredHost *host(int column) const { return m_hosts.at(column); }

private:
    QVector<ColoredHost *> m_hosts;
};

class ParentTypedRowAdapter : public TableWidgetAdapter
{
public:
    explicit ParentTypedRowAdapter(int columns = 3) : m_columns(columns) {}

    WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.parent().data().toString() == QStringLiteral("wide") ? 1 : 0;
    }

    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        auto *widget = new HostedRow(m_columns, parent);
        widget->setProperty("rowType", type);
        return widget;
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }

private:
    int m_columns = 3;
};

class SpanContextRowAdapter : public TableWidgetAdapter
{
public:
    explicit SpanContextRowAdapter(int columns = 3) : m_columns(columns) {}

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new HostedRow(m_columns, parent);
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}

    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }

    void layoutRowWidget(QWidget *, const QModelIndex &rowIndex,
                         const TableRowLayoutContext &context) override
    {
        if (!rowIndex.parent().isValid())
            return;
        const int row = rowIndex.row();
        anchorSpans[row] = context.spans().spanOf(1);
        anchorRects[row] = context.spans().rect(1);
        coveredColumns[row] = context.spans().isCovered(2);
        paneHostAvailable[row] = context.paneHostForColumn(1) != nullptr;
    }

    QHash<int, TableSpan> anchorSpans;
    QHash<int, QRect> anchorRects;
    QHash<int, bool> coveredColumns;
    QHash<int, bool> paneHostAvailable;

private:
    int m_columns = 3;
};

class CellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

class EditorRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        auto *widget = new HostedRow(3, parent);
        auto *editor = new QLineEdit(widget->host(1));
        editor->setGeometry(0, 0, 90, 24);
        return widget;
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        widget->setProperty("boundIndex", QVariant::fromValue(QPersistentModelIndex(index)));
        widget->findChild<QLineEdit *>()->setText(index.siblingAtColumn(1).data().toString());
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }
};

class EditorCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override
    {
        return new QLineEdit(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        widget->setProperty("boundIndex", QVariant::fromValue(QPersistentModelIndex(index)));
        static_cast<QLineEdit *>(widget)->setText(index.data().toString());
    }
};

class PopupRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        auto *widget = new HostedRow(3, parent);
        auto *editor = new QComboBox(widget->host(1));
        editor->addItems({QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("draft")});
        editor->setGeometry(0, 0, 90, 24);
        return widget;
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        widget->setProperty("boundIndex", QVariant::fromValue(QPersistentModelIndex(index)));
        widget->findChild<QComboBox *>()->setCurrentIndex(0);
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }
};

class PopupCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override
    {
        auto *editor = new QComboBox(parent);
        editor->addItems({QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("draft")});
        return editor;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        widget->setProperty("boundIndex", QVariant::fromValue(QPersistentModelIndex(index)));
        static_cast<QComboBox *>(widget)->setCurrentIndex(0);
    }
};

enum class CallbackStage { Type, Create, Bind, Unbind, State, Layout };

struct CallbackProbe
{
    CallbackStage stage = CallbackStage::Bind;
    mutable std::function<void()> action;
    mutable std::function<void(QWidget *)> widgetAction;
    mutable int calls = 0;

    void invoke(CallbackStage current) const
    {
        if (current != stage || !action)
            return;
        auto once = std::move(action);
        action = {};
        ++calls;
        once();
    }

    void invokeWidget(CallbackStage current, QWidget *widget) const
    {
        if (current != stage || !widgetAction)
            return;
        auto once = std::move(widgetAction);
        ++calls;
        once(widget);
    }
};

class CallbackRowAdapter : public TableWidgetAdapter
{
public:
    CallbackRowAdapter(CallbackProbe *probe, int owner) : probe(probe), owner(owner) {}

    WidgetType widgetType(const QModelIndex &) const override
    {
        probe->invoke(CallbackStage::Type);
        return probe->stage == CallbackStage::Create && probe->action ? 1 : 0;
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        auto *widget = new QWidget(parent);
        widget->setProperty("owner", owner);
        probe->invoke(CallbackStage::Create);
        return widget;
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        widget->setProperty("boundText", index.data());
        probe->invoke(CallbackStage::Bind);
        probe->invokeWidget(CallbackStage::Bind, widget);
    }

    void unbindWidget(QWidget *, const QModelIndex &) override
    {
        probe->invoke(CallbackStage::Unbind);
    }

    void visualStateChanged(QWidget *, const QModelIndex &) override
    {
        probe->invoke(CallbackStage::State);
    }

    void layoutRowWidget(QWidget *, const QModelIndex &, const TableRowLayoutContext &) override
    {
        probe->invoke(CallbackStage::Layout);
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 28); }

    CallbackProbe *probe;
    int owner;
};

class CallbackCellAdapter : public CellWidgetAdapter
{
public:
    CallbackCellAdapter(CallbackProbe *probe, int owner) : probe(probe), owner(owner) {}

    WidgetType cellWidgetType(const QModelIndex &) const override
    {
        probe->invoke(CallbackStage::Type);
        return probe->stage == CallbackStage::Create && probe->action ? 1 : 0;
    }

    QWidget *createCellWidget(WidgetType, QWidget *parent) override
    {
        auto *widget = new QLabel(parent);
        widget->setProperty("owner", owner);
        probe->invoke(CallbackStage::Create);
        return widget;
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        widget->setProperty("boundText", index.data());
        probe->invoke(CallbackStage::Bind);
        probe->invokeWidget(CallbackStage::Bind, widget);
    }

    void unbindCellWidget(QWidget *, const QModelIndex &) override
    {
        probe->invoke(CallbackStage::Unbind);
    }

    void visualStateChanged(QWidget *, const QModelIndex &) override
    {
        probe->invoke(CallbackStage::State);
    }

    CallbackProbe *probe;
    int owner;
};

class AdapterDeletingRowAdapter : public CallbackRowAdapter
{
public:
    AdapterDeletingRowAdapter(CallbackProbe *probe, int owner)
        : CallbackRowAdapter(probe, owner) {}

    void bindWidget(QWidget *, const QModelIndex &) override
    {
        // The callback may delete this adapter; keep it as the final operation.
        probe->invoke(CallbackStage::Bind);
    }
};

class AdapterDeletingCellAdapter : public CallbackCellAdapter
{
public:
    AdapterDeletingCellAdapter(CallbackProbe *probe, int owner)
        : CallbackCellAdapter(probe, owner) {}

    void bindCellWidget(QWidget *, const QModelIndex &) override
    {
        // The callback may delete this adapter; keep it as the final operation.
        probe->invoke(CallbackStage::Bind);
    }
};

class NodeNameFilterProxy : public QSortFilterProxyModel
{
public:
    void hideName(const QString &name)
    {
        m_hiddenName = name;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        return sourceModel()->index(row, 0, parent).data().toString() != m_hiddenName;
    }

private:
    QString m_hiddenName;
};

class RootVisibilityProxy : public QSortFilterProxyModel
{
public:
    void setHideFirstRoot(bool hidden)
    {
        m_hideFirstRoot = hidden;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        return sourceParent.isValid() || !m_hideFirstRoot || sourceRow != 0;
    }

private:
    bool m_hideFirstRoot = false;
};

class RecordingSortModel : public QStandardItemModel
{
public:
    void sort(int column, Qt::SortOrder order) override
    {
        ++sortCalls;
        QStandardItemModel::sort(column, order);
    }

    int sortCalls = 0;
};

class DragQueryCallbackModel : public QStandardItemModel
{
public:
    enum class Stage { Flags, DragActions, DropActions, Mime };

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        const Qt::ItemFlags result = QStandardItemModel::flags(index);
        invoke(Stage::Flags);
        return result;
    }

    Qt::DropActions supportedDragActions() const override
    {
        const Qt::DropActions result = stage == Stage::DropActions
            ? Qt::DropActions(Qt::IgnoreAction) : Qt::MoveAction | Qt::CopyAction;
        invoke(Stage::DragActions);
        return result;
    }

    Qt::DropActions supportedDropActions() const override
    {
        invoke(Stage::DropActions);
        return Qt::MoveAction | Qt::CopyAction;
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        ++mimeCalls;
        QMimeData *result = QStandardItemModel::mimeData(indexes);
        lastMime = result;
        invoke(Stage::Mime);
        return result;
    }

    void invoke(Stage current) const
    {
        if (current != stage || !callback)
            return;
        if (skipCalls > 0) {
            --skipCalls;
            return;
        }
        const auto once = std::move(callback);
        callback = {};
        ++callbacks;
        once();
    }

    Stage stage = Stage::Flags;
    mutable std::function<void()> callback;
    mutable int skipCalls = 0;
    mutable int callbacks = 0;
    mutable int mimeCalls = 0;
    mutable QPointer<QMimeData> lastMime;
};

class ParentTypedCellAdapter : public CellWidgetAdapter
{
public:
    WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() == 2 && index.parent().data().toString() == QStringLiteral("wide")
            ? 1 : 0;
    }

    QWidget *createCellWidget(WidgetType type, QWidget *parent) override
    {
        return type == 1 ? static_cast<QWidget *>(new QProgressBar(parent))
                         : static_cast<QWidget *>(new QLabel(parent));
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (auto *progress = qobject_cast<QProgressBar *>(widget)) {
            progress->setRange(0, 100);
            progress->setValue(75);
            progress->setFormat(index.data().toString());
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }
};

class InspectTreeTableView : public VirtualTreeTableView
{
public:
    using VirtualItemView::captureAnchor;
    using VirtualItemView::dragSourceWidget;
    using VirtualTableView::indexAt;
    using VirtualTreeTableView::selectionRange;
};

class DragConstructionObserver : public QObject
{
public:
    std::function<void()> onChildAdded;

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded && onChildAdded)
            onChildAdded();
        return QObject::eventFilter(object, event);
    }
};

class PopupMigrationObserver : public QObject
{
public:
    std::function<void()> onParentChange;
    int changes = 0;

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::ParentAboutToChange && onParentChange) {
            ++changes;
            const auto callback = std::move(onParentChange);
            callback();
        }
        return QObject::eventFilter(object, event);
    }
};

class RecordingDropModel : public QStandardItemModel
{
public:
    QStringList mimeTypes() const override
    {
        return {QStringLiteral("application/x-viv-tree-table-test")};
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::CopyAction | Qt::MoveAction;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int targetRow,
                         int column, const QModelIndex &parent) const override
    {
        const bool allowed = data && data->hasFormat(mimeTypes().first())
            && (action == Qt::CopyAction || action == Qt::MoveAction)
            && targetRow >= 0 && targetRow <= rowCount(parent) && column <= 0;
        if (onCanDrop)
            onCanDrop();
        return allowed;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int targetRow,
                      int column, const QModelIndex &parent) override
    {
        ++dropCalls;
        lastAction = action;
        lastRow = targetRow;
        lastColumn = column;
        lastParent = parent;
        if (onDrop) {
            onDrop();
            return acceptDrop;
        }
        if (!acceptDrop || !canDropMimeData(data, action, targetRow, column, parent))
            return false;
        const QList<QStandardItem *> items = row(QStringLiteral("dropped"));
        if (parent.isValid())
            itemFromIndex(parent)->insertRow(targetRow, items);
        else
            insertRow(targetRow, items);
        return true;
    }

    int dropCalls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    Qt::DropAction lastAction = Qt::IgnoreAction;
    QPersistentModelIndex lastParent;
    bool acceptDrop = true;
    std::function<void()> onCanDrop;
    std::function<void()> onDrop;
};

class CrossParentMoveModel : public QStandardItemModel
{
public:
    QStringList mimeTypes() const override
    {
        return {QStringLiteral("application/x-viv-cross-parent-move")};
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QStandardItemModel::flags(index) | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled;
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        auto *data = new QMimeData;
        if (!indexes.isEmpty() && indexes.first().isValid())
            data->setData(mimeTypes().first(), indexes.first().data().toString().toUtf8());
        return data;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int targetRow,
                         int column, const QModelIndex &parent) const override
    {
        return data && data->hasFormat(mimeTypes().first()) && action == Qt::MoveAction
            && targetRow >= 0 && targetRow <= rowCount(parent) && column <= 0 && parent.isValid();
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int targetRow,
                      int column, const QModelIndex &parent) override
    {
        ++dropCalls;
        lastAction = action;
        lastRow = targetRow;
        lastColumn = column;
        lastParent = parent;
        if (!canDropMimeData(data, action, targetRow, column, parent))
            return false;

        const QString sourceText = QString::fromUtf8(data->data(mimeTypes().first()));
        QStandardItem *source = findItem(invisibleRootItem(), sourceText);
        if (!source || !source->parent() || source->parent() == itemFromIndex(parent))
            return false;
        const QList<QStandardItem *> moved = source->parent()->takeRow(source->row());
        itemFromIndex(parent)->insertRow(targetRow, moved);
        return true;
    }

    int dropCalls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    Qt::DropAction lastAction = Qt::IgnoreAction;
    QPersistentModelIndex lastParent;

private:
    static QStandardItem *findItem(QStandardItem *parent, const QString &text)
    {
        for (int row = 0; row < parent->rowCount(); ++row) {
            QStandardItem *item = parent->child(row, 0);
            if (!item)
                continue;
            if (item->text() == text)
                return item;
            if (QStandardItem *nested = findItem(item, text))
                return nested;
        }
        return nullptr;
    }
};

class RejectingMoveModel : public QStandardItemModel
{
public:
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        ++moveCalls;
        lastSourceParent = sourceParent;
        lastSourceRow = sourceRow;
        lastCount = count;
        lastDestinationParent = destinationParent;
        lastDestinationChild = destinationChild;
        return false;
    }

    int moveCalls = 0;
    int lastSourceRow = -1;
    int lastCount = 0;
    int lastDestinationChild = -1;
    QPersistentModelIndex lastSourceParent;
    QPersistentModelIndex lastDestinationParent;
};

class ColumnMoveModel : public QAbstractItemModel
{
public:
    ColumnMoveModel()
    {
        Item *owner = append(&m_root, QStringLiteral("parent"));
        Item *child = append(owner, QStringLiteral("child"));
        append(child, QStringLiteral("leaf"));
        append(owner, QStringLiteral("sibling"));
        append(&m_root, QStringLiteral("peer"));
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        const Item *owner = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        if (row < 0 || row >= owner->rows.size() || column < 0 || column >= 3)
            return {};
        return createIndex(row, column, owner->rows.at(row).at(column));
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid())
            return {};
        Item *owner = static_cast<Item *>(child.internalPointer())->owner;
        if (owner == &m_root)
            return {};
        const auto &rows = owner->owner->rows;
        for (int row = 0; row < rows.size(); ++row) {
            const int column = rows.at(row).indexOf(owner);
            if (column >= 0)
                return createIndex(row, column, owner);
        }
        return {};
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        const Item *owner = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        return owner->rows.size();
    }

    int columnCount(const QModelIndex & = QModelIndex()) const override { return 3; }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        return index.isValid() && role == Qt::DisplayRole
            ? QVariant(static_cast<Item *>(index.internalPointer())->text) : QVariant();
    }

    bool moveColumns(const QModelIndex &sourceParent, int sourceColumn, int count,
                     const QModelIndex &destinationParent, int destinationColumn) override
    {
        if (sourceParent != destinationParent || count != 1 || sourceColumn < 0
            || sourceColumn >= 3 || destinationColumn < 0 || destinationColumn > 3)
            return false;
        if (!beginMoveColumns(sourceParent, sourceColumn, sourceColumn,
                              destinationParent, destinationColumn))
            return false;
        Item *owner = sourceParent.isValid() ? static_cast<Item *>(sourceParent.internalPointer()) : &m_root;
        for (auto &row : owner->rows) {
            Item *moving = row.takeAt(sourceColumn);
            row.insert(destinationColumn > sourceColumn ? destinationColumn - 1 : destinationColumn, moving);
        }
        endMoveColumns();
        return true;
    }

private:
    struct Item
    {
        QString text;
        Item *owner = nullptr;
        QVector<QVector<Item *>> rows;
        ~Item()
        {
            for (const auto &row : rows) {
                for (Item *item : row)
                    delete item;
            }
        }
    };

    Item *append(Item *owner, const QString &text)
    {
        QVector<Item *> row;
        for (int column = 0; column < 3; ++column) {
            auto *item = new Item;
            item->owner = owner;
            item->text = text + QStringLiteral(" %1").arg(column);
            row.append(item);
        }
        owner->rows.append(row);
        return row.first();
    }

    Item m_root;
};

class SiblingMoveModel : public QAbstractItemModel
{
public:
    explicit SiblingMoveModel(bool withDescendants = false, bool narrowDestination = false,
                              int extraChildren = 0)
    {
        Node *parent = append(&m_root, QStringLiteral("parent"));
        append(parent, QStringLiteral("child 0"));
        Node *moving = append(parent, QStringLiteral("child 1"));
        append(parent, QStringLiteral("child 2"));
        Node *other = append(&m_root, QStringLiteral("other parent"), narrowDestination ? 2 : 3);
        if (withDescendants) {
            append(moving, QStringLiteral("leaf"));
            append(other, QStringLiteral("other child"));
        }
        for (int i = 0; i < extraChildren; ++i) {
            append(parent, QStringLiteral("source extra %1").arg(i));
            append(other, QStringLiteral("destination extra %1").arg(i));
        }
    }

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override
    {
        if (row < 0 || column < 0 || (parent.isValid() && parent.column() != 0))
            return QModelIndex();
        Node *owner = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                       : const_cast<Node *>(&m_root);
        return row < owner->children.size() && column < owner->columns
            ? createIndex(row, column, owner->children.at(row)) : QModelIndex();
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid())
            return QModelIndex();
        Node *owner = static_cast<Node *>(child.internalPointer())->parent;
        if (!owner || owner == &m_root)
            return QModelIndex();
        return createIndex(owner->parent->children.indexOf(owner), 0, owner);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const Node *owner = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                             : &m_root;
        return owner->children.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const Node *owner = parent.isValid() ? static_cast<const Node *>(parent.internalPointer())
                                             : &m_root;
        return owner->columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return static_cast<Node *>(index.internalPointer())->text;
    }

    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if (!sourceParent.isValid() || !destinationParent.isValid() || count != 1
            || sourceRow < 0 || sourceRow >= rowCount(sourceParent)
            || destinationChild < 0 || destinationChild > rowCount(destinationParent))
            return false;
        const QModelIndex movingIndex = index(sourceRow, 0, sourceParent);
        for (QModelIndex ancestor = destinationParent; ancestor.isValid(); ancestor = parent(ancestor)) {
            if (ancestor == movingIndex)
                return false;
        }
        Node *destination = static_cast<Node *>(destinationParent.internalPointer());
        QModelIndexList invalidated;
        for (const QModelIndex &persistent : persistentIndexList()) {
            if (persistent.parent() == sourceParent && persistent.row() == sourceRow
                && persistent.column() >= destination->columns)
                invalidated.append(persistent);
        }
        for (const QModelIndex &index : invalidated)
            changePersistentIndex(index, QModelIndex());
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow,
                           destinationParent, destinationChild))
            return false;
        Node *owner = static_cast<Node *>(sourceParent.internalPointer());
        destination = static_cast<Node *>(destinationParent.internalPointer());
        Node *moving = owner->children.takeAt(sourceRow);
        moving->parent = destination;
        destination->children.insert(sourceParent == destinationParent && destinationChild > sourceRow
                                         ? destinationChild - 1 : destinationChild, moving);
        endMoveRows();
        ++moveCalls;
        return true;
    }

    int moveCalls = 0;

private:
    struct Node
    {
        QString text;
        Node *parent = nullptr;
        int columns = 3;
        QVector<Node *> children;
        ~Node()
        {
            for (Node *child : children)
                delete child;
        }
    };

    Node *append(Node *parent, const QString &text, int columns = 3)
    {
        Node *node = new Node;
        node->text = text;
        node->parent = parent;
        node->columns = columns;
        parent->children.append(node);
        return node;
    }

    Node m_root;
};

void sendHeaderMouse(QWidget *widget, QEvent::Type type, const QPoint &position,
                     Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

bool sendDrop(VirtualTreeTableView *view, const QPoint &position,
              const QMimeData *data, Qt::DropAction action)
{
    QDragEnterEvent enter(position, action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    QDragMoveEvent move(position, action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &move);
    QDropEvent drop(QPointF(position), action, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
    return drop.isAccepted();
}

void configure(VirtualTreeTableView *view, QStandardItemModel *model, bool cells)
{
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    if (cells) {
        view->setCellAdapter(new CellAdapter, true);
        view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view->setTableAdapter(new RowAdapter, true);
    }
    view->setModel(model);
    showView(view, QSize(440, 280));
}

} // namespace

class TestTreeTableViewInteraction : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void branchClickFollowsMovedColumn();
    void frozenColumnAndSiblingSpanStayAligned();
    void expandedFrozenRowsKeepHeadersAndBranchAligned();
    void rowStripDragRejectsCrossParentAndRestoresPreview();
    void rowStripDragMovesSiblingIdentity();
    void rowStripPreviewFollowsBothTreeWidgetModes_data();
    void rowStripPreviewFollowsBothTreeWidgetModes();
    void rowStripPreviewKeepsMergedCellMasks_data();
    void rowStripPreviewKeepsMergedCellMasks();
    void proxySortKeepsNodeStateAndRestoresHeaderLayout();
    void filteringDoesNotTransferNodeStateToAnotherRow();
    void sortingAndFilteringTogetherKeepNodeState_data();
    void sortingAndFilteringTogetherKeepNodeState();
    void modelResetClearsRootCellAndNodeState();
    void parentSpecificCellWidgetsRespectMissingColumnsAndFolding();
    void parentSpecificRowHostsRespectMissingColumnsAndFolding();
    void rowHostsStayClippedInSeparateScrollGroups();
    void depthSpacingKeepsRowHeadersAndHitTestsAligned();
    void customSpacingWidgetsFollowVisibleNodesAndHeaders();
    void customSpacingWidgetsFollowAdvancedPanes_data();
    void customSpacingWidgetsFollowAdvancedPanes();
    void customBranchRendererFollowsAdvancedPanes_data();
    void customBranchRendererFollowsAdvancedPanes();
    void branchRendererCanRemoveItselfDuringPaint_data();
    void branchRendererCanRemoveItselfDuringPaint();
    void wideTreeMaterializesOnlyTheWindow();
    void accessibilityExposesHierarchyAndCells();
    void accessibilityFocusAndHitsFollowIndependentPanes_data();
    void accessibilityFocusAndHitsFollowIndependentPanes();
    void selectedBackgroundExtentKeepsGridLineVisible();
    void rowGridLineAliasesMatchTableGridSettings_data();
    void rowGridLineAliasesMatchTableGridSettings();
    void rootSchemaAndCrossParentSelection();
    void dropTargetsFollowVisibleTreeAndSpacing();
    void dropIndicatorsFollowFrozenPanesAndSpacing();
    void dropIndicatorFollowsMergedCellAnchor_data();
    void dropIndicatorFollowsMergedCellAnchor();
    void dragAutoscrollTracksTreeTargetsAndStops_data();
    void dragAutoscrollTracksTreeTargetsAndStops();
    void modelReceivesDropAndCanRejectIt();
    void crossParentMoveCommitsToTheTargetParent();
    void modelSwitchDuringDropDoesNotSubmitStaleTarget();
    void synchronousAdapterChanges_data();
    void synchronousAdapterChanges();
    void callbackDeletingBoundWidget_data();
    void callbackDeletingBoundWidget();
    void callbackDeletingModelDuringBind_data();
    void callbackDeletingModelDuringBind();
    void callbackDeletingAdapterDuringBind_data();
    void callbackDeletingAdapterDuringBind();
    void callbackDeletingViewDuringBind_data();
    void callbackDeletingViewDuringBind();
    void synchronousGeometryChanges_data();
    void synchronousGeometryChanges();
    void filteringTheRootClearsDescendantState_data();
    void filteringTheRootClearsDescendantState();
    void synchronousModelNotifications_data();
    void synchronousModelNotifications();
    void modelSwitchCallbacksKeepTheNewestModel_data();
    void modelSwitchCallbacksKeepTheNewestModel();
    void headerLayoutSurvivesFileAndViewRecreation_data();
    void headerLayoutSurvivesFileAndViewRecreation();
    void keyboardRespectsHiddenAndMissingColumns_data();
    void keyboardRespectsHiddenAndMissingColumns();
    void selectionPoliciesRespectTreeSchema_data();
    void selectionPoliciesRespectTreeSchema();
    void selectionModifiersRemainNodeBoundAcrossFolding_data();
    void selectionModifiersRemainNodeBoundAcrossFolding();
    void publicExpansionApisKeepCanonicalNodeIdentity_data();
    void publicExpansionApisKeepCanonicalNodeIdentity();
    void publicNotificationsExposeCurrentTreeState_data();
    void publicNotificationsExposeCurrentTreeState();
    void activationSignalsRespectModelIdentity_data();
    void activationSignalsRespectModelIdentity();
    void mouseActivationDistinguishesTreeBranches_data();
    void mouseActivationDistinguishesTreeBranches();
    void activationCallbacksRetireOldRequests_data();
    void activationCallbacksRetireOldRequests();
    void currentChangeCallbacksCancelActivation_data();
    void currentChangeCallbacksCancelActivation();
    void headerVisibilityAndDimensionsKeepTreeGeometry_data();
    void headerVisibilityAndDimensionsKeepTreeGeometry();
    void dragEntryRespectsModelIdentity_data();
    void dragEntryRespectsModelIdentity();
    void dragQueryCallbacksRetireRequests_data();
    void dragQueryCallbacksRetireRequests();
    void dragSourcePinsSurviveIgnoredReturn_data();
    void dragSourcePinsSurviveIgnoredReturn();
    void nativeDragCancelPreservesSourceIdentity_data();
    void nativeDragCancelPreservesSourceIdentity();
    void lifecycleLogTracksTreeWidgets_data();
    void lifecycleLogTracksTreeWidgets();
    void scrollHintsPositionExpandedNodes_data();
    void scrollHintsPositionExpandedNodes();
    void wheelParametersScrollExpandedNodes_data();
    void wheelParametersScrollExpandedNodes();
    void itemWheelStepsAcrossVariableTreeRows_data();
    void itemWheelStepsAcrossVariableTreeRows();
    void pinSoftCapKeepsTreeWidgets_data();
    void pinSoftCapKeepsTreeWidgets();
    void plainAdapterEntryKeepsInstalledTreeAdapters_data();
    void plainAdapterEntryKeepsInstalledTreeAdapters();
    void clearingFrozenColumnsRestoresTreePaneQueries_data();
    void clearingFrozenColumnsRestoresTreePaneQueries();
    void headerResizeSwitchesFollowTreePanes_data();
    void headerResizeSwitchesFollowTreePanes();
    void rowDragAutoModelYieldsToApplicationTree_data();
    void rowDragAutoModelYieldsToApplicationTree();
    void selectionSurvivesIndependentPaneScrolling_data();
    void selectionSurvivesIndependentPaneScrolling();
    void mergedCellsMaskInternalGridPixels_data();
    void mergedCellsMaskInternalGridPixels();
    void editorsAndPinsSurviveScrollingAndFolding_data();
    void editorsAndPinsSurviveScrollingAndFolding();
    void inputMethodCommitSurvivesSpanChangesAndScrolling_data();
    void inputMethodCommitSurvivesSpanChangesAndScrolling();
    void editorsSurviveRuntimeFreezeChanges_data();
    void editorsSurviveRuntimeFreezeChanges();
    void rowHeightsAndScrollAnchorsFollowNodes_data();
    void rowHeightsAndScrollAnchorsFollowNodes();
    void horizontalGeometryAndOverscanStayConsistent_data();
    void horizontalGeometryAndOverscanStayConsistent();
    void accessibilityHandlesSchemaSpansAndVisibility_data();
    void accessibilityHandlesSchemaSpansAndVisibility();
    void spansFollowVisualColumnsAndProxyChanges_data();
    void spansFollowVisualColumnsAndProxyChanges();
    void rowAdaptersReceiveTreeSpanContext();
    void crossPaneTreeSpanStopsAtPaneBoundary_data();
    void crossPaneTreeSpanStopsAtPaneBoundary();
    void offscreenSpanAnchorsStayMaterialized_data();
    void offscreenSpanAnchorsStayMaterialized();
    void invalidProviderAnchorsDoNotCoverOtherNodes_data();
    void invalidProviderAnchorsDoNotCoverOtherNodes();
    void spanProviderCallbacksKeepNewestState_data();
    void spanProviderCallbacksKeepNewestState();
    void previewMaskRefreshCallbacksKeepNewestState_data();
    void previewMaskRefreshCallbacksKeepNewestState();
    void partialPreviewProjectionDropsInvalidatedMasks_data();
    void partialPreviewProjectionDropsInvalidatedMasks();
    void ownedSpanProvidersSurviveNestedReplacement_data();
    void ownedSpanProvidersSurviveNestedReplacement();
    void columnZeroChangesInvalidateOnlyAffectedNodeState_data();
    void columnZeroChangesInvalidateOnlyAffectedNodeState();
    void crossParentMovesPreserveNodeState_data();
    void crossParentMovesPreserveNodeState();
    void crossParentMoveUnderProxyFilter_data();
    void crossParentMoveUnderProxyFilter();
    void crossParentProxyChangesKeepRemoteScrollAnchor_data();
    void crossParentProxyChangesKeepRemoteScrollAnchor();
    void crossParentMoveRespectsDestinationSchema_data();
    void crossParentMoveRespectsDestinationSchema();
    void crossParentMoveCombinesSchemaSpanAndFrozenState_data();
    void crossParentMoveCombinesSchemaSpanAndFrozenState();
    void crossParentMoveOutOfLimitedRootClearsState_data();
    void crossParentMoveOutOfLimitedRootClearsState();
    void crossParentMovePreservesCrossRowSpan_data();
    void crossParentMovePreservesCrossRowSpan();
    void crossParentMoveKeepsRemoteSpanAnchorMaterialized_data();
    void crossParentMoveKeepsRemoteSpanAnchorMaterialized();
    void popupEditorsStayPinnedUntilClosed_data();
    void popupEditorsStayPinnedUntilClosed();
    void popupMenusKeepActionsAcrossPaneMigration_data();
    void popupMenusKeepActionsAcrossPaneMigration();
    void completionPopupKeepsEditorIdentity_data();
    void completionPopupKeepsEditorIdentity();
    void completionSubmissionRetiresView_data();
    void completionSubmissionRetiresView();
    void completionMigrationCallbacksKeepNewestState_data();
    void completionMigrationCallbacksKeepNewestState();
    void largeSelectionKeepsMaterializationBounded_data();
    void largeSelectionKeepsMaterializationBounded();
    void scaleCostsStayBoundedAgainstExistingViews_data();
    void scaleCostsStayBoundedAgainstExistingViews();
    void realtimeAnimationCostsStayWindowBound_data();
    void realtimeAnimationCostsStayWindowBound();
    void realtimeHeaderPreviewCostsStayWindowBound_data();
    void realtimeHeaderPreviewCostsStayWindowBound();
    void animatedBackgroundsPreserveGridPixels_data();
    void animatedBackgroundsPreserveGridPixels();
    void columnMovesKeepOrInvalidateNodeIdentity_data();
    void columnMovesKeepOrInvalidateNodeIdentity();
    void customHeadersFollowTreeGeometry_data();
    void customHeadersFollowTreeGeometry();
    void customHeaderSortingReentersWithNewestModel_data();
    void customHeaderSortingReentersWithNewestModel();
    void flatFrozenRowHeadersRespectPaneRanges();
    void animatedColumnMovesKeepBranchesAndWidgetsAligned_data();
    void animatedColumnMovesKeepBranchesAndWidgetsAligned();
    void animatedColumnMovesKeepAdvancedPanesAndSpansAligned_data();
    void animatedColumnMovesKeepAdvancedPanesAndSpansAligned();
    void customHeaderAdapterReplacementDuringBind();
    void customHeaderAdapterDetachDuringBind();
    void customHeaderDeletingSectionWidget();
    void customHeaderDeletingModelDuringBind();
    void customHeaderDeletingViewDuringBind_data();
    void customHeaderDeletingViewDuringBind();
    void customHeaderDeletingViewDuringUnbind_data();
    void customHeaderDeletingViewDuringUnbind();
    void customHeaderDeletingViewDuringAcquisition_data();
    void customHeaderDeletingViewDuringAcquisition();
    void customHeaderNestedAdapterReplacementDuringBind();
    void paneHeaderDragsKeepGlobalOrderAndWidgetsAligned_data();
    void paneHeaderDragsKeepGlobalOrderAndWidgetsAligned();
};

void TestTreeTableViewInteraction::initTestCase()
{
    installAccessibilityFactory();
}

void TestTreeTableViewInteraction::cleanupTestCase()
{
    removeAccessibilityFactory();
}

void TestTreeTableViewInteraction::branchClickFollowsMovedColumn()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, false);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    view.horizontalHeaderGeometry()->moveSection(0, 2);
    view.flushPendingRelayout();
    const QModelIndex node = model.index(0, 0);
    const ColumnGeometry column = view.columnGeometry(0);
    QVERIFY(column.viewportX > 0);
    const QPoint indicator(column.viewportX + view.indentation() / 2,
                           view.visualRect(node).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(model.index(0, 1)).center()), model.index(0, 1));
}

void TestTreeTableViewInteraction::frozenColumnAndSiblingSpanStayAligned()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("first")));
    parent.first()->appendRow(row(QStringLiteral("second")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    view.setFrozenColumns({0});
    view.setFrozenRows(1);
    view.expand(model.index(0, 0));
    view.setSpan(1, 1, 2, 2);
    view.flushPendingRelayout();

    const QModelIndex first = model.index(0, 1, model.index(0, 0));
    const QModelIndex covered = model.index(1, 2, model.index(0, 0));
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QCOMPARE(view.anchorIndex(covered), first);
    QVERIFY(view.isSpanCovered(covered));
    QVERIFY(view.spanRect(first).height() > view.visualRect(first).height());
    QVERIFY(view.cellWidget(first));
    QVERIFY(!view.cellWidget(covered));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(model.index(0, 0)).center()), model.index(0, 0));
}

void TestTreeTableViewInteraction::expandedFrozenRowsKeepHeadersAndBranchAligned()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto first = row(QStringLiteral("first"));
    first.first()->appendRow(row(QStringLiteral("first child")));
    model.appendRow(first);
    model.appendRow(row(QStringLiteral("middle")));
    auto last = row(QStringLiteral("last"));
    last.first()->appendRow(row(QStringLiteral("last child")));
    model.appendRow(last);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({2});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.flushPendingRelayout();

    const QModelIndex firstRoot = model.index(0, 0);
    const QModelIndex lastRoot = model.index(2, 0);
    const QModelIndex lastChild = model.index(0, 0, lastRoot);
    const auto verifyFrozenRow = [&](const QModelIndex &node, qsizetype row,
                                     ItemPane::Type type) {
        QApplication::processEvents();
        const QVector<ItemPane> rowPanes = view.itemPanes();
        QCOMPARE(rowPanes.size(), 3);
        QRect paneRect;
        for (const ItemPane &pane : rowPanes) {
            if (pane.type == type) {
                paneRect = pane.viewportRect;
                QCOMPARE(pane.firstRow <= row && row <= pane.lastRow, true);
                break;
            }
        }
        QVERIFY(!paneRect.isEmpty());
        const QRect bodyRect = view.visualRect(node);
        QVERIFY(paneRect.contains(bodyRect.center()));
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     view.cellRect(node.siblingAtColumn(1)).center()),
                 node.siblingAtColumn(1));
        const QRect viewportRect = view.viewport()->geometry();
        bool foundHeader = false;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (header->orientation() != Qt::Vertical || !header->isVisible()
                || header->geometry().y() != viewportRect.y() + paneRect.y()
                || header->height() != paneRect.height())
                continue;
            QWidget *section = header->sectionWidget(int(row));
            QVERIFY(section);
            QCOMPARE(header->geometry().y() + section->y(),
                     viewportRect.y() + bodyRect.y());
            auto *label = qobject_cast<LabelHeaderSection *>(section);
            QVERIFY(label);
            QCOMPARE(label->text(), QString::number(row + 1));
            foundHeader = true;
            break;
        }
        QVERIFY(foundHeader);
    };

    verifyFrozenRow(firstRoot, 0, ItemPane::Type::FrozenTop);
    verifyFrozenRow(lastRoot, 2, ItemPane::Type::FrozenBottom);
    const ColumnGeometry firstColumn = view.columnGeometry(0);
    const QPoint firstBranch(firstColumn.viewportX + view.indentation() / 2,
                             view.visualRect(firstRoot).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstBranch);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    verifyFrozenRow(lastRoot, 3, ItemPane::Type::FrozenBottom);

    const QPoint lastBranch(firstColumn.viewportX + view.indentation() / 2,
                            view.visualRect(lastRoot).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, lastBranch);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(5));
    verifyFrozenRow(lastChild, 4, ItemPane::Type::FrozenBottom);

    view.collapse(lastRoot);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    verifyFrozenRow(lastRoot, 3, ItemPane::Type::FrozenBottom);
}

void TestTreeTableViewInteraction::rowStripDragRejectsCrossParentAndRestoresPreview()
{
    RejectingMoveModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child 0")));
    parent.first()->appendRow(row(QStringLiteral("child 1")));
    parent.first()->appendRow(row(QStringLiteral("child 2")));
    model.appendRow(parent);
    model.appendRow(row(QStringLiteral("other parent")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setVerticalHeaderDragEnabled(true);
    const QModelIndex firstParent = model.index(0, 0);
    view.expand(firstParent);
    view.flushPendingRelayout();
    QSignalSpy requestSpy(&view, &VirtualTableView::rowMoveRequested);
    QVERIFY(requestSpy.isValid());
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    QWidget *header = strip->headerWidget();

    const auto dragRow = [&](int from, int to) {
        const int fromY = view.visualRect(view.visibilityIndex()->indexAtVisibleRow(from)).center().y();
        const int toY = view.visualRect(view.visibilityIndex()->indexAtVisibleRow(to)).center().y();
        sendHeaderMouse(header, QEvent::MouseButtonPress, QPoint(4, fromY),
                        Qt::LeftButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, fromY + 2),
                        Qt::NoButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, toY + 10),
                        Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
        sendHeaderMouse(header, QEvent::MouseButtonRelease, QPoint(4, toY + 10),
                        Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    };

    dragRow(1, 3);
    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(model.moveCalls, 1);
    QCOMPARE(model.lastSourceParent, QPersistentModelIndex(firstParent));
    QCOMPARE(model.lastDestinationParent, QPersistentModelIndex(firstParent));
    QCOMPARE(model.lastSourceRow, 0);
    QCOMPARE(model.lastCount, 1);
    QCOMPARE(model.lastDestinationChild, 3);
    QCOMPARE(model.index(0, 0, firstParent).data().toString(), QStringLiteral("child 0"));

    dragRow(3, 4);
    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(model.moveCalls, 1);
    for (int rowNumber : {1, 2, 3, 4}) {
        QWidget *section = strip->sectionWidget(rowNumber);
        QVERIFY(section);
        const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(rowNumber);
        QCOMPARE(strip->geometry().y() + section->y(),
                 view.viewport()->geometry().y() + view.visualRect(node).top());
    }
}

void TestTreeTableViewInteraction::rowStripDragMovesSiblingIdentity()
{
    SiblingMoveModel model;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new RowAdapter, true);
    view.setModel(&model);
    view.setVerticalHeaderDragEnabled(true);
    showView(&view, QSize(440, 280));
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    view.flushPendingRelayout();
    const QPersistentModelIndex moving(model.index(0, 0, parent));
    const QPersistentModelIndex currentCell(model.index(0, 1, parent));
    view.setCurrentIndex(currentCell);
    QSignalSpy requestSpy(&view, &VirtualTableView::rowMoveRequested);
    QSignalSpy movedSpy(&model, &QAbstractItemModel::rowsMoved);
    QVERIFY(requestSpy.isValid());
    QVERIFY(movedSpy.isValid());
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    QWidget *header = strip->headerWidget();
    const int fromY = view.visualRect(moving).center().y();
    const int toY = view.visualRect(model.index(2, 0, parent)).center().y() + 10;
    sendHeaderMouse(header, QEvent::MouseButtonPress, QPoint(4, fromY),
                    Qt::LeftButton, Qt::LeftButton);
    sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, fromY + 2),
                    Qt::NoButton, Qt::LeftButton);
    sendHeaderMouse(header, QEvent::MouseMove, QPoint(4, toY),
                    Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    sendHeaderMouse(header, QEvent::MouseButtonRelease, QPoint(4, toY),
                    Qt::LeftButton, Qt::NoButton);
    view.flushPendingRelayout();
    QApplication::processEvents();

    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(model.moveCalls, 1);
    QCOMPARE(moving.row(), 2);
    QCOMPARE(currentCell.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(currentCell));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(3), QModelIndex(moving));
    QCOMPARE(model.index(0, 0, parent).data().toString(), QStringLiteral("child 1"));
    QCOMPARE(model.index(1, 0, parent).data().toString(), QStringLiteral("child 2"));
    QWidget *section = strip->sectionWidget(3);
    QVERIFY(section);
    QCOMPARE(strip->geometry().y() + section->y(),
             view.viewport()->geometry().y() + view.visualRect(moving).top());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(currentCell).center()), QModelIndex(currentCell));
}

void TestTreeTableViewInteraction::rowStripPreviewFollowsBothTreeWidgetModes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<bool>("cellScope");
    QTest::addColumn<int>("extent");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            for (bool cellScope : {false, true}) {
                for (int extent = 0; extent < 3; ++extent) {
                    const QByteArray name = QByteArray(cells ? "cells" : "rows")
                        + (frozen ? "-frozen" : "-plain")
                        + (cellScope ? "-cell-scope-" : "-row-scope-") + QByteArray::number(extent);
                    QTest::newRow(name.constData()) << cells << frozen << cellScope << extent;
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::rowStripPreviewFollowsBothTreeWidgetModes()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QFETCH(bool, cellScope);
    QFETCH(int, extent);
    SiblingMoveModel model;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new RowAdapter, true);
    }
    view.setModel(&model);
    view.setVerticalHeaderDragEnabled(true);
    view.setHeaderAnimationEnabled(true);
    view.setHeaderAnimationDuration(100);
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    view.setVisualStateBackgroundVisible(true);
    view.setVisualStateAnimationDuration(0);
    view.setVisualStateScope(cellScope ? VirtualTableView::VisualStateScope::Cell
                                       : VirtualTableView::VisualStateScope::Row);
    view.setVisualStateBackgroundExtent(
        static_cast<VirtualTreeTableView::VisualStateBackgroundExtent>(extent));
    const QColor selected(230, 30, 70);
    view.setSelectedBackgroundColor(selected);
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    if (frozen) {
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({2});
    }
    showView(&view, QSize(440, 280));
    const QPersistentModelIndex moving(model.index(0, 0, parent));
    const QPersistentModelIndex neighbour(model.index(1, 0, parent));
    const QPersistentModelIndex currentCell(model.index(0, 1, parent));
    view.setCurrentIndex(currentCell);
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    QSignalSpy requests(&view, &VirtualTableView::rowMoveRequested);
    QSignalSpy stripRequests(strip, &VirtualHeaderView::sectionMoveRequested);
    const auto widgetFor = [&](const QModelIndex &node, int column) {
        return cells ? view.cellWidget(node.siblingAtColumn(column)) : view.widgetForIndex(node);
    };
    const auto widgetY = [&](QWidget *widget) {
        return widget->mapTo(view.viewport(), QPoint()).y();
    };
    const auto verifyRows = [&](bool follows) {
        for (const QPersistentModelIndex &node : {moving, neighbour}) {
            int expected = view.visualRect(node).top();
            if (follows) {
                QVERIFY(strip->sectionVisualX(int(view.visibilityIndex()->visibleRowForIndex(node)),
                                              &expected));
            }
            for (int column = 0; column < (cells ? 3 : 1); ++column) {
                QWidget *widget = widgetFor(node, column);
                QVERIFY(widget);
                QCOMPARE(widgetY(widget), expected);
            }
        }
        if (frozen) {
            for (int visible : {0, 4}) {
                const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(visible);
                for (int column = 0; column < (cells ? 3 : 1); ++column) {
                    QWidget *widget = widgetFor(node, column);
                    QVERIFY(widget);
                    QCOMPARE(widgetY(widget), view.visualRect(node).top());
                }
            }
        }
        int movingY = view.visualRect(moving).top();
        if (follows)
            QVERIFY(strip->sectionVisualX(int(view.visibilityIndex()->visibleRowForIndex(moving)),
                                          &movingY));
        const QImage image = view.grab().toImage();
        const QPoint origin = view.viewport()->pos();
        const int x = view.columnGeometry(0).viewportX;
        const int sampleY = movingY + 3;
        QCOMPARE(image.pixelColor(origin + QPoint(x + 90, sampleY)), selected);
        for (int sampleX : {5, 25}) {
            const bool covered = sampleX == 5 ? extent == 2 : extent != 0;
            if (covered)
                QCOMPARE(image.pixelColor(origin + QPoint(x + sampleX, sampleY)), selected);
            else
                QVERIFY(image.pixelColor(origin + QPoint(x + sampleX, sampleY)) != selected);
        }
        const QPoint marker(x + view.indentation() + 9,
                            movingY + view.visualRect(moving).height() / 2 - 1);
        QCOMPARE(image.pixelColor(origin + marker), QColor(230, 180, 10));
        if (follows && movingY != view.visualRect(moving).top())
            QVERIFY(image.pixelColor(origin + QPoint(x + 90, view.visualRect(moving).top() + 3))
                    != selected);
    };
    const int committed = view.visualRect(moving).top();
    const int grabY = strip->mapFromGlobal(
        view.viewport()->mapToGlobal(view.visualRect(moving).center())).y();
    const int dropY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(model.index(2, 0, parent)).center())).y() + 10;
    const auto beginPreview = [&] {
        sendHeaderMouse(strip, QEvent::MouseButtonPress, QPoint(4, grabY),
                        Qt::LeftButton, Qt::LeftButton);
        sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, grabY + 2),
                        Qt::NoButton, Qt::LeftButton);
        sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, dropY),
                        Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
    };
    QVERIFY(view.rowFollowsHeaderVisual());
    beginPreview();
    verifyRows(true);
    QVERIFY(widgetY(widgetFor(moving, 0)) != committed);
    QTest::qWait(140);
    verifyRows(true);
    QCOMPARE(view.visualRect(moving).top(), committed);
    QCOMPARE(view.currentIndex(), QModelIndex(currentCell));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(currentCell).center()),
             QModelIndex(currentCell));
    view.setRowFollowsHeaderVisual(false);
    QVERIFY(!view.rowFollowsHeaderVisual());
    verifyRows(false);
    view.setRowFollowsHeaderVisual(true);
    verifyRows(true);
    QTest::keyClick(strip, Qt::Key_Escape);
    verifyRows(false);
    QCOMPARE(requests.size(), 0);
    QCOMPARE(model.moveCalls, 0);
    beginPreview();
    sendHeaderMouse(strip, QEvent::MouseButtonRelease, QPoint(4, dropY),
                    Qt::LeftButton, Qt::NoButton);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(stripRequests.size(), 1);
    QCOMPARE(stripRequests.first().at(0).toInt(), 1);
    QCOMPARE(stripRequests.first().at(1).toInt(), 3);
    QCOMPARE(requests.size(), 1);
    QCOMPARE(model.moveCalls, 1);
    QCOMPARE(moving.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(currentCell));
    verifyRows(false);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(currentCell).center()),
             QModelIndex(currentCell));
}

void TestTreeTableViewInteraction::rowStripPreviewKeepsMergedCellMasks_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<int>("spacing");
    QTest::addColumn<int>("spanRows");
    QTest::addColumn<int>("customGapFlags");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            for (int spacing : {0, 6}) {
                for (int spanRows : {1, 2}) {
                    const QByteArray name = QByteArray(cells ? "cells" : "rows")
                        + (frozen ? "-frozen-" : "-plain-") + QByteArray::number(spacing)
                        + "-" + QByteArray::number(spanRows);
                    QTest::newRow(name.constData()) << cells << frozen << spacing << spanRows << -1;
                    if (spacing > 0) {
                        for (int flags = 0; flags < 4; ++flags) {
                            const QByteArray customName = name + "-custom-" + QByteArray::number(flags);
                            QTest::newRow(customName.constData())
                                << cells << frozen << spacing << spanRows << flags;
                        }
                    }
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::rowStripPreviewKeepsMergedCellMasks()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QFETCH(int, spacing);
    QFETCH(int, spanRows);
    QFETCH(int, customGapFlags);
    SiblingMoveModel model;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new RowAdapter, true);
    }
    view.setModel(&model);
    showView(&view, QSize(440, 280));
    view.setRowSpacing(spacing);
    view.setColumnSpacing(spacing);
    const QColor rowGapColor(220, 190, 20);
    const QColor columnGapColor(170, 40, 200);
    if (customGapFlags >= 0) {
        view.setRowSpacingFactory([rowGapColor](const QModelIndex &, QWidget *parent) {
            auto *label = new QLabel(parent);
            label->setObjectName(QStringLiteral("previewCustomRowGap"));
            label->setStyleSheet(QStringLiteral("background-color: %1;").arg(rowGapColor.name()));
            return label;
        });
        view.setColumnSpacingFactory([columnGapColor](int, QWidget *parent) {
            auto *label = new QLabel(parent);
            label->setObjectName(QStringLiteral("previewCustomColumnGap"));
            label->setStyleSheet(QStringLiteral("background-color: %1;").arg(columnGapColor.name()));
            return label;
        });
        view.setVerticalSpacingLineThroughRowSpacing((customGapFlags & 1) != 0);
        view.setHorizontalSpacingLineThroughColumnSpacing((customGapFlags & 2) != 0);
    }
    view.setVerticalHeaderDragEnabled(true);
    view.setHeaderAnimationEnabled(true);
    view.setHeaderAnimationDuration(100);
    view.setVisualStateScope(VirtualTableView::VisualStateScope::Cell);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    view.setVisualStateBackgroundVisible(true);
    view.setVisualStateAnimationDuration(0);
    const QColor selected(230, 30, 70);
    const QColor vertical(15, 125, 40);
    const QColor horizontal(30, 60, 220);
    view.setSelectedBackgroundColor(selected);
    view.setVerticalGridLineColor(vertical);
    view.setHorizontalGridLineColor(horizontal);
    view.setHorizontalGridLinesVisible(true);
    view.setVerticalGridLinesVisible(true);
    view.setHorizontalGridLineWidth(1);
    view.setVerticalGridLineWidth(1);
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    if (frozen) {
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.setFrozenColumns({0});
    }
    const QPersistentModelIndex anchor(model.index(0, 1, parent));
    const QPersistentModelIndex covered(model.index(0, 2, parent));
    view.setSpan(1, 1, spanRows, 2);
    view.setCurrentIndex(anchor);
    view.flushPendingRelayout();
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    if (customGapFlags >= 0) {
        QVERIFY(!view.findChildren<QLabel *>(QStringLiteral("previewCustomRowGap")).isEmpty());
        QVERIFY(!view.findChildren<QLabel *>(QStringLiteral("previewCustomColumnGap")).isEmpty());
    }
    const QRect committed = view.spanRect(anchor);
    QCOMPARE(view.anchorIndex(covered), QModelIndex(anchor));
    QCOMPARE(view.spanAt(anchor).rowSpan, spanRows);
    if (cells) {
        QVERIFY(view.cellWidget(anchor));
        QVERIFY(!view.cellWidget(covered));
    }
    const int grabY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(anchor).center())).y();
    const int dropY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(model.index(2, 0, parent)).center())).y() + 10;
    const auto beginPreview = [&] {
        sendHeaderMouse(strip, QEvent::MouseButtonPress, QPoint(4, grabY),
                        Qt::LeftButton, Qt::LeftButton);
        sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, grabY + 2),
                        Qt::NoButton, Qt::LeftButton);
        sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, dropY),
                        Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
    };
    const auto verify = [&](bool follows, bool oldLineRestored) {
        int visualY = view.visualRect(anchor).top();
        if (follows)
            QVERIFY(strip->sectionVisualX(int(view.visibilityIndex()->visibleRowForIndex(anchor)),
                                          &visualY));
        QWidget *widget = cells ? view.cellWidget(anchor)
                               : view.widgetForIndex(QModelIndex(anchor).siblingAtColumn(0));
        QVERIFY(widget);
        QCOMPARE(widget->mapTo(view.viewport(), QPoint()).y(), visualY);
        if (cells) {
            QCOMPARE(widget->height(), view.spanRect(anchor).height());
            QVERIFY(!view.cellWidget(covered));
        }
        const QImage image = view.grab().toImage();
        const QPoint origin = view.viewport()->pos();
        const int verticalX = view.columnGeometry(1).viewportX + view.columnWidth(1) - 1;
        QCOMPARE(image.pixelColor(origin + QPoint(verticalX, visualY + 4)), selected);
        if (customGapFlags >= 0) {
            const QRect parentRect = view.visualRect(parent);
            const int columnEnd = view.columnGeometry(1).viewportX + view.columnWidth(1);
            const QColor verticalGap = image.pixelColor(
                origin + QPoint(columnEnd - 1, parentRect.bottom() + 3));
            const QColor horizontalGap = image.pixelColor(
                origin + QPoint(columnEnd + 2, parentRect.bottom() + 1));
            if (customGapFlags & 1)
                QCOMPARE(verticalGap, vertical);
            else
                QCOMPARE(verticalGap, view.palette().color(QPalette::Base));
            if (customGapFlags & 2)
                QCOMPARE(horizontalGap, horizontal);
            else
                QCOMPARE(horizontalGap, columnGapColor);
            QCOMPARE(image.pixelColor(origin + QPoint(
                         view.columnGeometry(1).viewportX + 50, parentRect.bottom() + 3)),
                     rowGapColor);
        }
        if (oldLineRestored) {
            QCOMPARE(image.pixelColor(origin + QPoint(verticalX, committed.top() + 4)), vertical);
            if (spanRows == 2) {
                const int boundaryY = committed.top() + view.visualRect(anchor).height()
                    + (spacing > 0 ? 0 : -1);
                QCOMPARE(image.pixelColor(origin + QPoint(
                             view.columnGeometry(1).viewportX + 50, boundaryY)), horizontal);
            }
        }
        QCOMPARE(view.currentIndex(), QModelIndex(anchor));
        QCOMPARE(view.anchorIndex(covered), QModelIndex(anchor));
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     QPoint(view.columnGeometry(2).viewportX + 8,
                            view.visualRect(anchor).top() + 3)), QModelIndex(anchor));
    };
    verify(false, false);
    beginPreview();
    QTest::qWait(140);
    QCOMPARE(view.spanRect(anchor), committed);
    int previewY = 0;
    QVERIFY(strip->sectionVisualX(1, &previewY));
    QVERIFY(previewY > committed.bottom());
    verify(true, true);
    view.setRowFollowsHeaderVisual(false);
    verify(false, false);
    view.setRowFollowsHeaderVisual(true);
    verify(true, true);
    QTest::keyClick(strip, Qt::Key_Escape);
    verify(false, false);
    QCOMPARE(model.moveCalls, 0);
    beginPreview();
    sendHeaderMouse(strip, QEvent::MouseButtonRelease, QPoint(4, dropY),
                    Qt::LeftButton, Qt::NoButton);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(model.moveCalls, 1);
    QCOMPARE(anchor.row(), 2);
    QCOMPARE(view.spanRect(anchor).height(), view.visualRect(anchor).height());
    verify(false, false);
}

void TestTreeTableViewInteraction::proxySortKeepsNodeStateAndRestoresHeaderLayout()
{
    QStandardItemModel source;
    source.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                      QStringLiteral("State")});
    auto parent = row(QStringLiteral("Zulu"));
    parent.first()->appendRow(row(QStringLiteral("nested")));
    source.appendRow(parent);
    source.appendRow(row(QStringLiteral("Alpha")));
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setDynamicSortFilter(true);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new RowAdapter, true);
    view.setModel(&proxy);
    showView(&view, QSize(440, 280));
    const QPersistentModelIndex zulu(proxy.mapFromSource(source.index(0, 0)));
    const QPersistentModelIndex child(proxy.index(0, 1, zulu));
    view.expand(zulu);
    view.setCurrentIndex(child);
    view.selectionModel()->select(child, QItemSelectionModel::ClearAndSelect);
    QVERIFY(view.isExpanded(zulu));
    QVERIFY(view.selectionModel()->isSelected(child));

    view.sortByColumn(0, Qt::AscendingOrder);
    view.flushPendingRelayout();
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("Alpha"));
    QCOMPARE(zulu.row(), 1);
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(zulu), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(child), qsizetype(2));
    QCOMPARE(view.currentIndex(), QModelIndex(child));
    QVERIFY(view.selectionModel()->isSelected(child));
    QVERIFY(view.isExpanded(zulu));

    view.setColumnWidth(1, 135);
    view.horizontalHeaderGeometry()->moveSection(0, 2);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({2});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.flushPendingRelayout();
    const QByteArray state = view.saveHeaderState();
    QVERIFY(!state.isEmpty());

    VirtualTreeTableView restored;
    restored.setUniformItemHeight(28);
    restored.setDefaultColumnWidth(100);
    restored.setTableAdapter(new RowAdapter, true);
    restored.setModel(&proxy);
    showView(&restored, QSize(440, 280));
    QSignalSpy sortSpy(&restored, &VirtualTableView::sortIndicatorRequested);
    QVERIFY(sortSpy.isValid());
    QVERIFY(restored.restoreHeaderState(state));
    QCOMPARE(sortSpy.count(), 0);
    QCOMPARE(restored.columnWidth(1), 135);
    QCOMPARE(restored.horizontalHeaderGeometry()->visualIndex(0), 2);
    QCOMPARE(restored.horizontalHeaderGeometry()->sortIndicatorSection(), 0);
    QCOMPARE(restored.horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::AscendingOrder);
    QCOMPARE(restored.frozenColumns(), QVector<int>({0}));
    QCOMPARE(restored.frozenRightColumns(), QVector<int>({2}));
    QCOMPARE(restored.frozenRows(), 1);
    QCOMPARE(restored.frozenBottomRows(), 1);
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("Alpha"));
}

void TestTreeTableViewInteraction::filteringDoesNotTransferNodeStateToAnotherRow()
{
    QStandardItemModel source;
    auto first = row(QStringLiteral("first"));
    first.first()->appendRow(row(QStringLiteral("first child")));
    source.appendRow(first);
    auto second = row(QStringLiteral("second"));
    second.first()->appendRow(row(QStringLiteral("second child")));
    source.appendRow(second);

    RootVisibilityProxy proxy;
    proxy.setSourceModel(&source);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setTableAdapter(new RowAdapter, true);
    view.setModel(&proxy);
    showView(&view, QSize(440, 280));

    const QPersistentModelIndex firstNode(proxy.mapFromSource(source.index(0, 0)));
    const QPersistentModelIndex firstChild(proxy.index(0, 1, firstNode));
    const QPersistentModelIndex secondNode(proxy.mapFromSource(source.index(1, 0)));
    view.expand(firstNode);
    view.setCurrentIndex(firstChild);
    view.setItemPinned(firstChild);
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QVERIFY(view.isExpanded(firstNode));
    QVERIFY(view.isItemPinned(firstChild));

    proxy.setHideFirstRoot(true);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0), QModelIndex(secondNode));
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(!view.isItemPinned(firstChild));
    QVERIFY(!view.isExpanded(secondNode));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(secondNode).center()), QModelIndex(secondNode));

    proxy.setHideFirstRoot(false);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0),
             proxy.mapFromSource(source.index(0, 0)));
    QVERIFY(!view.isExpanded(proxy.mapFromSource(source.index(0, 0))));
    QVERIFY(!view.currentIndex().isValid());
}

void TestTreeTableViewInteraction::sortingAndFilteringTogetherKeepNodeState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::sortingAndFilteringTogetherKeepNodeState()
{
    QFETCH(bool, cells);
    QStandardItemModel source;
    auto root = row(QStringLiteral("root"));
    const QStringList names = {QStringLiteral("delta"), QStringLiteral("alpha"),
                               QStringLiteral("charlie"), QStringLiteral("bravo")};
    for (const QString &name : names) {
        auto child = row(name);
        child.first()->appendRow(row(name + QStringLiteral(" leaf")));
        root.first()->appendRow(child);
    }
    source.appendRow(root);

    NodeNameFilterProxy proxy;
    proxy.setSourceModel(&source);
    proxy.setDynamicSortFilter(true);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&proxy);
    const QPersistentModelIndex proxyRoot(proxy.index(0, 0));
    view.setRootIndex(proxyRoot);
    view.expand(proxyRoot);
    proxy.sort(0, Qt::AscendingOrder);
    showView(&view, QSize(440, 320));

    auto nodeNamed = [&](const QString &name) {
        for (int rowIndex = 0; rowIndex < proxy.rowCount(proxyRoot); ++rowIndex) {
            const QModelIndex candidate = proxy.index(rowIndex, 0, proxyRoot);
            if (candidate.data().toString() == name)
                return candidate;
        }
        return QModelIndex();
    };
    QPersistentModelIndex target(nodeNamed(QStringLiteral("charlie")));
    QVERIFY(target.isValid());
    view.expand(target);
    const QPersistentModelIndex targetCell(QModelIndex(target).siblingAtColumn(1));
    view.setCurrentIndex(targetCell);
    view.selectionModel()->select(targetCell, QItemSelectionModel::ClearAndSelect);
    view.setItemPinned(targetCell);
    view.flushPendingRelayout();
    settle();
    QPointer<QWidget> widget(cells ? view.cellWidget(targetCell) : view.widgetForIndex(target));
    QVERIFY(widget);

    const auto verifyTarget = [&]() {
        const QModelIndex currentTarget = nodeNamed(QStringLiteral("charlie"));
        QVERIFY(currentTarget.isValid());
        const QModelIndex currentCell = currentTarget.siblingAtColumn(1);
        QCOMPARE(view.currentIndex(), currentCell);
        QVERIFY(view.selectionModel()->isSelected(currentCell));
        QVERIFY(view.isExpanded(currentTarget));
        QVERIFY(view.isItemPinned(currentCell));
        QVERIFY(view.visibilityIndex()->visibleRowForIndex(currentTarget) >= 0);
        QVERIFY(widget);
        QCOMPARE(cells ? view.cellWidget(currentCell) : view.widgetForIndex(currentTarget),
                 widget.data());
        QCOMPARE(view.indexForWidget(widget), cells ? currentCell : currentTarget);
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     view.cellRect(currentCell).center()), currentCell);
    };

    verifyTarget();
    proxy.hideName(QStringLiteral("alpha"));
    proxy.sort(0, Qt::DescendingOrder);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(proxy.rowCount(proxyRoot), 3);
    verifyTarget();

    proxy.hideName(QString());
    proxy.sort(0, Qt::AscendingOrder);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(proxy.rowCount(proxyRoot), 4);
    verifyTarget();
}

void TestTreeTableViewInteraction::modelResetClearsRootCellAndNodeState()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto root = row(QStringLiteral("root"));
    auto branch = row(QStringLiteral("branch"));
    branch.first()->appendRow(row(QStringLiteral("old leaf")));
    root.first()->appendRow(branch);
    model.appendRow(root);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    const QModelIndex rootIndex = model.index(0, 0);
    const QModelIndex branchIndex = model.index(0, 0, rootIndex);
    const QPersistentModelIndex oldCell(model.index(0, 1, branchIndex));
    view.setRootIndex(rootIndex);
    view.expand(branchIndex);
    view.setCurrentIndex(oldCell);
    view.setItemPinned(oldCell);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(view.cellWidget(oldCell));
    QVERIFY(view.isItemPinned(oldCell));
    QVERIFY(view.selectionModel()->isSelected(oldCell));

    model.clear();
    view.flushPendingRelayout();
    QVERIFY(!oldCell.isValid());
    QVERIFY(!view.rootIndex().isValid());
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
    QCOMPARE(view.columnCount(), 0);
    QCOMPARE(view.materializedCellCount(), qsizetype(0));
    QCOMPARE(view.visibilityIndex()->expandedCount(), qsizetype(0));

    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    model.appendRow(row(QStringLiteral("new root")));
    view.flushPendingRelayout();
    const QModelIndex freshCell = model.index(0, 1);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0), model.index(0, 0));
    QCOMPARE(view.columnCount(), 3);
    QVERIFY(view.cellWidget(freshCell));
    QVERIFY(!view.isItemPinned(freshCell));
    QVERIFY(!view.selectionModel()->isSelected(freshCell));
    QVERIFY(!view.currentIndex().isValid());
}

void TestTreeTableViewInteraction::parentSpecificCellWidgetsRespectMissingColumnsAndFolding()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto narrow = row(QStringLiteral("narrow"));
    narrow.first()->appendRow(new QStandardItem(QStringLiteral("narrow child")));
    model.appendRow(narrow);
    auto wide = row(QStringLiteral("wide"));
    wide.first()->appendRow(row(QStringLiteral("wide child")));
    model.appendRow(wide);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(160);
    view.setColumnOverscan(0);
    view.setCellAdapter(new ParentTypedCellAdapter, true);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&model);
    showView(&view, QSize(320, 260));
    const QModelIndex narrowRoot = model.index(0, 0);
    const QModelIndex wideRoot = model.index(1, 0);
    const QModelIndex narrowChild = model.index(0, 0, narrowRoot);
    const QModelIndex wideCell = model.index(0, 2, wideRoot);
    view.expand(narrowRoot);
    view.expand(wideRoot);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    QVERIFY(!model.index(0, 2, narrowRoot).isValid());
    QVERIFY(!view.cellWidget(wideCell));

    view.setHorizontalOffset(220);
    view.flushPendingRelayout();
    QVERIFY(view.horizontalOffset() > 0);
    auto *progress = qobject_cast<QProgressBar *>(view.cellWidget(wideCell));
    QVERIFY(progress);
    QCOMPARE(progress->format(), QStringLiteral("wide child state"));
    const QPoint absentCellPoint(view.cellRect(wideCell).center().x(),
                                 view.visualRect(narrowChild).center().y());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(absentCellPoint), QModelIndex());

    view.collapse(wideRoot);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    QVERIFY(!view.cellWidget(wideCell));
    view.expand(wideRoot);
    view.flushPendingRelayout();
    QVERIFY(qobject_cast<QProgressBar *>(view.cellWidget(wideCell)));
}

void TestTreeTableViewInteraction::parentSpecificRowHostsRespectMissingColumnsAndFolding()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto narrow = row(QStringLiteral("narrow"));
    narrow.first()->appendRow(new QStandardItem(QStringLiteral("narrow child")));
    model.appendRow(narrow);
    auto wide = row(QStringLiteral("wide"));
    wide.first()->appendRow(row(QStringLiteral("wide child")));
    model.appendRow(wide);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(160);
    view.setTableAdapter(new ParentTypedRowAdapter, true);
    view.setModel(&model);
    showView(&view, QSize(320, 260));
    const QModelIndex narrowRoot = model.index(0, 0);
    const QModelIndex wideRoot = model.index(1, 0);
    const QModelIndex narrowChild = model.index(0, 0, narrowRoot);
    const QModelIndex wideChild = model.index(0, 0, wideRoot);
    const QModelIndex wideCell = model.index(0, 2, wideRoot);
    view.expand(narrowRoot);
    view.expand(wideRoot);
    view.flushPendingRelayout();
    auto *narrowRow = static_cast<HostedRow *>(view.widgetForIndex(narrowChild));
    auto *wideRow = static_cast<HostedRow *>(view.widgetForIndex(wideChild));
    QVERIFY(narrowRow);
    QVERIFY(wideRow);
    QCOMPARE(narrowRow->property("rowType").toInt(), 0);
    QCOMPARE(wideRow->property("rowType").toInt(), 1);
    QVERIFY(narrowRow->host(2)->isHidden());
    QVERIFY(wideRow->host(2)->isHidden());
    QVERIFY(narrowRow->host(0)->mapTo(view.viewport(), QPoint()).x()
            > view.cellRect(narrowChild).x());

    view.setHorizontalOffset(220);
    view.flushPendingRelayout();
    QVERIFY(view.horizontalOffset() > 0);
    QVERIFY(narrowRow->host(2)->isHidden());
    QVERIFY(wideRow->host(2)->isVisible());
    QCOMPARE(wideRow->host(2)->mapTo(view.viewport(), QPoint()).x(),
             view.cellRect(wideCell).x());

    view.collapse(wideRoot);
    view.flushPendingRelayout();
    QVERIFY(!view.widgetForIndex(wideChild));
    view.expand(wideRoot);
    view.flushPendingRelayout();
    auto *rebound = static_cast<HostedRow *>(view.widgetForIndex(wideChild));
    QVERIFY(rebound);
    QCOMPARE(rebound->property("rowType").toInt(), 1);
    QVERIFY(rebound->host(2)->isVisible());
}

void TestTreeTableViewInteraction::rowHostsStayClippedInSeparateScrollGroups()
{
    QStandardItemModel model;
    QList<QStandardItem *> rootRow;
    QList<QStandardItem *> childRow;
    for (int column = 0; column < 10; ++column) {
        rootRow.append(new QStandardItem(QStringLiteral("root %1").arg(column)));
        childRow.append(new QStandardItem(QStringLiteral("child %1").arg(column)));
    }
    rootRow.first()->appendRow(childRow);
    model.appendRow(rootRow);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new ParentTypedRowAdapter(10), true);
    view.setModel(&model);
    showView(&view, QSize(900, 260));
    const auto pane = [](const QVector<int> &columns, PaneScroll scroll, int group) {
        TablePaneSpec spec;
        spec.logicalColumns = columns;
        spec.scroll = scroll;
        spec.scrollGroup = group;
        return spec;
    };
    view.setPanes({pane({0}, PaneScroll::Frozen, 0),
                   pane({1, 2, 3}, PaneScroll::Scrollable, 0),
                   pane({4}, PaneScroll::Frozen, 0),
                   pane({5, 6, 7, 8, 9}, PaneScroll::Scrollable, 1)});
    const QModelIndex root = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, root);
    view.expand(root);
    view.flushPendingRelayout();
    QCOMPARE(view.panes().size(), 4);
    QCOMPARE(view.scrollGroups(), QVector<int>({0, 1}));
    QVERIFY(view.maximumHorizontalOffset(1) > 0);

    auto *rowWidget = static_cast<HostedRow *>(view.widgetForIndex(child));
    QVERIFY(rowWidget);
    const QList<QWidget *> clipHosts = rowWidget->findChildren<QWidget *>(
        QStringLiteral("vivPaneClipHost"), Qt::FindDirectChildrenOnly);
    QCOMPARE(clipHosts.size(), 2);
    QVERIFY(rowWidget->host(1)->parentWidget() != rowWidget->host(5)->parentWidget());
    QCOMPARE(rowWidget->host(4)->parentWidget(), static_cast<QWidget *>(rowWidget));
    const int firstGroupX = rowWidget->host(1)->mapTo(view.viewport(), QPoint()).x();
    const int frozenX = rowWidget->host(4)->mapTo(view.viewport(), QPoint()).x();
    const int secondGroupX = rowWidget->host(5)->mapTo(view.viewport(), QPoint()).x();
    QCOMPARE(firstGroupX, view.cellRect(model.index(0, 1, root)).x());
    QCOMPARE(secondGroupX, view.cellRect(model.index(0, 5, root)).x());

    const qint64 step = qMin<qint64>(40, view.maximumHorizontalOffset(1));
    view.setHorizontalOffset(1, step);
    view.flushPendingRelayout();
    QCOMPARE(view.horizontalOffset(1), step);
    QCOMPARE(view.horizontalOffset(0), qint64(0));
    QCOMPARE(rowWidget->host(1)->mapTo(view.viewport(), QPoint()).x(), firstGroupX);
    QCOMPARE(rowWidget->host(4)->mapTo(view.viewport(), QPoint()).x(), frozenX);
    QCOMPARE(rowWidget->host(5)->mapTo(view.viewport(), QPoint()).x(),
             secondGroupX - int(step));
    QCOMPARE(rowWidget->host(5)->mapTo(view.viewport(), QPoint()).x(),
             view.cellRect(model.index(0, 5, root)).x());

    rowWidget->host(4)->hide();
    const int y = view.visualRect(child).center().y();
    QVERIFY(rowWidget->host(5)->isVisible());
    const QRect hostRect(rowWidget->host(5)->mapTo(view.viewport(), QPoint()),
                         rowWidget->host(5)->size());
    const QRect overFrozen = hostRect.intersected(view.panes().at(2).viewportRect);
    const QRect inScrollPane = hostRect.intersected(view.panes().at(3).viewportRect);
    QVERIFY(!overFrozen.isEmpty());
    QVERIFY(!inScrollPane.isEmpty());
    const QPoint frozenSample(overFrozen.center().x(), y);
    const QPoint scrollingSample(inScrollPane.center().x(), y);
    const QImage baseline = view.viewport()->grab().toImage();
    const QColor red(233, 40, 60);
    rowWidget->host(5)->setFillColor(red);
    const QImage painted = view.viewport()->grab().toImage();
    QCOMPARE(painted.pixelColor(scrollingSample), red);
    QCOMPARE(painted.pixelColor(frozenSample), baseline.pixelColor(frozenSample));
}

void TestTreeTableViewInteraction::depthSpacingKeepsRowHeadersAndHitTestsAligned()
{
    QStandardItemModel model;
    auto rootRow = row(QStringLiteral("root"));
    auto childRow = row(QStringLiteral("child"));
    childRow.first()->appendRow(row(QStringLiteral("grandchild")));
    rootRow.first()->appendRow(childRow);
    model.appendRow(rootRow);
    model.appendRow(row(QStringLiteral("sibling")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(4);
    view.setDepthRowSpacing(0, 8);
    view.setDepthRowSpacing(1, 13);
    view.setDepthRowSpacing(2, 21);
    const QModelIndex root = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, root);
    const QModelIndex grandchild = model.index(0, 0, child);
    const QModelIndex sibling = model.index(1, 0);
    view.expand(root);
    view.expand(child);
    view.flushPendingRelayout();

    auto *header = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(header);
    const auto verify = [&](const QVector<QModelIndex> &nodes,
                            const QVector<int> &gaps) {
        QCOMPARE(view.visibleRowCount(), qsizetype(nodes.size()));
        QCOMPARE(view.verticalHeaderGeometry()->sectionCount(), nodes.size());
        for (int rowNumber = 0; rowNumber < nodes.size(); ++rowNumber) {
            const QRect body = view.visualRect(nodes.at(rowNumber));
            QVERIFY(body.isValid());
            QWidget *section = header->sectionWidget(rowNumber);
            QVERIFY(section);
            QCOMPARE(header->geometry().y() + section->y(),
                     view.viewport()->geometry().y() + body.y());
            QCOMPARE(section->height(), body.height());
            if (rowNumber + 1 < nodes.size()) {
                const int gap = gaps.at(rowNumber);
                QCOMPARE(view.verticalHeaderGeometry()->sectionSpacingAfter(rowNumber), gap);
                QCOMPARE(view.visualRect(nodes.at(rowNumber + 1)).y() - body.bottom() - 1, gap);
                if (gap > 0) {
                    const QPoint gapPoint(view.cellRect(nodes.at(rowNumber)).center().x(),
                                          body.bottom() + 1);
                    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(gapPoint),
                             QModelIndex());
                }
            }
        }
    };
    verify({root, child, grandchild, sibling}, {8, 13, 21});

    view.setDepthRowSpacing(1, 0);
    view.flushPendingRelayout();
    verify({root, child, grandchild, sibling}, {8, 0, 21});

    view.collapse(root);
    view.flushPendingRelayout();
    verify({root, sibling}, {8});
}

void TestTreeTableViewInteraction::customSpacingWidgetsFollowVisibleNodesAndHeaders()
{
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentRow);
    model.appendRow(row(QStringLiteral("sibling")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(8);
    view.setDepthRowSpacing(1, 12);
    view.setColumnSpacing(9);
    view.setRowSpacingFactory([](const QModelIndex &, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("rowGapContent"));
        return label;
    }, [](QWidget *widget, const QModelIndex &index) {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    });
    view.setColumnSpacingFactory([](int column, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("columnGapContent"));
        label->setProperty("logicalColumn", column);
        return label;
    });
    view.setHeaderColumnSpacingFactory([](int column, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("headerGapContent"));
        label->setProperty("logicalColumn", column);
        return label;
    });
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    view.flushPendingRelayout();

    const auto rowGaps = view.findChildren<QLabel *>(QStringLiteral("rowGapContent"));
    bool foundParent = false;
    bool foundChild = false;
    for (QLabel *label : rowGaps) {
        if (!label->isVisible())
            continue;
        const QModelIndex node = label->text() == QStringLiteral("parent") ? parent : child;
        if (label->text() != QStringLiteral("parent")
            && label->text() != QStringLiteral("child"))
            continue;
        const QRect body = view.visualRect(node);
        const QPoint gapTop = view.viewport()->mapFromGlobal(
            label->parentWidget()->mapToGlobal(QPoint()));
        QCOMPARE(gapTop.y(), body.bottom() + 1);
        QCOMPARE(label->parentWidget()->height(),
                 node == parent ? 8 : 12);
        foundParent |= node == parent;
        foundChild |= node == child;
    }
    QVERIFY(foundParent);
    QVERIFY(foundChild);

    for (int column = 0; column < 2; ++column) {
        QLabel *bodyGap = nullptr;
        QLabel *headerGap = nullptr;
        for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("columnGapContent"))) {
            if (label->isVisible() && label->property("logicalColumn").toInt() == column)
                bodyGap = label;
        }
        for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("headerGapContent"))) {
            if (label->isVisible() && label->property("logicalColumn").toInt() == column)
                headerGap = label;
        }
        QVERIFY(bodyGap);
        QVERIFY(headerGap);
        const int end = view.columnGeometry(column).viewportX + view.columnWidth(column);
        const QRect bodyRect(view.viewport()->mapFromGlobal(bodyGap->mapToGlobal(QPoint())),
                             bodyGap->size());
        const QRect headerRect(view.viewport()->mapFromGlobal(headerGap->mapToGlobal(QPoint())),
                               headerGap->size());
        QVERIFY(bodyRect.left() >= end - view.verticalGridLineWidth());
        QVERIFY(bodyRect.right() < end + view.columnSpacing());
        QVERIFY(bodyRect.top() >= 0);
        QVERIFY(headerRect.bottom() < 0);
    }

    view.collapse(parent);
    view.flushPendingRelayout();
    for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("rowGapContent")))
        QVERIFY(!label->isVisible() || label->text() != QStringLiteral("child"));
}

void TestTreeTableViewInteraction::customSpacingWidgetsFollowAdvancedPanes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("customHeaders");
    for (bool cells : {false, true}) {
        for (bool customHeaders : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + (customHeaders ? "custom" : "default");
            QTest::newRow(name.constData()) << cells << customHeaders;
        }
    }
}

void TestTreeTableViewInteraction::customSpacingWidgetsFollowAdvancedPanes()
{
    QFETCH(bool, cells);
    QFETCH(bool, customHeaders);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int column = 3; column < 6; ++column)
        parentItems.append(new QStandardItem(QStringLiteral("parent-%1").arg(column)));
    auto childItems = row(QStringLiteral("child"));
    for (int column = 3; column < 6; ++column)
        childItems.append(new QStandardItem(QStringLiteral("child-%1").arg(column)));
    parentItems.first()->appendRow(childItems);
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("sibling")));

    VirtualTreeTableView view;
    if (customHeaders) {
        auto *horizontal = new VirtualHeaderView(Qt::Horizontal);
        horizontal->setAdapter(new TreeHeaderAdapter(&view, Qt::Horizontal, 1), true);
        auto *vertical = new VirtualHeaderView(Qt::Vertical);
        vertical->setAdapter(new TreeHeaderAdapter(&view, Qt::Vertical, 2), true);
        view.setHorizontalHeader(horizontal);
        view.setVerticalHeader(vertical);
    }
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setColumnOverscan(0);
    view.setRowSpacing(8);
    view.setDepthRowSpacing(1, 12);
    view.setColumnSpacing(9);
    view.setFrozenColumns({0});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2}, PaneScroll::Scrollable, 0},
                   {{3, 4, 5}, PaneScroll::Scrollable, 1}});
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    view.setRowSpacingFactory([](const QModelIndex &, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("advancedRowGap"));
        return label;
    }, [](QWidget *widget, const QModelIndex &index) {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    });
    view.setColumnSpacingFactory([](int column, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("advancedColumnGap"));
        label->setProperty("logicalColumn", column);
        return label;
    });
    view.setHeaderColumnSpacingFactory([](int column, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("advancedHeaderGap"));
        label->setProperty("logicalColumn", column);
        return label;
    });
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(6), true);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    showView(&view, QSize(620, 320));
    view.flushPendingRelayout();
    settle();

    int visibleRowGaps = 0;
    for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("advancedRowGap"))) {
        if (!label->isVisible() || (label->text() != QStringLiteral("parent")
                                    && label->text() != QStringLiteral("child")))
            continue;
        const QModelIndex node = label->text() == QStringLiteral("parent") ? parent : child;
        const QRect body = view.visualRect(node);
        const QPoint top = view.viewport()->mapFromGlobal(label->parentWidget()->mapToGlobal(QPoint()));
        QCOMPARE(top.y(), body.bottom() + 1);
        QCOMPARE(label->parentWidget()->height(), node == parent ? 8 : 12);
        ++visibleRowGaps;
    }
    QCOMPARE(visibleRowGaps, 2);

    for (int column : {1, 3}) {
        QLabel *bodyGap = nullptr;
        QLabel *headerGap = nullptr;
        for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("advancedColumnGap"))) {
            if (label->isVisible() && label->property("logicalColumn").toInt() == column)
                bodyGap = label;
        }
        for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("advancedHeaderGap"))) {
            if (label->isVisible() && label->property("logicalColumn").toInt() == column)
                headerGap = label;
        }
        QVERIFY(bodyGap);
        QVERIFY(headerGap);
        const int end = view.columnGeometry(column).viewportX + view.columnWidth(column);
        const QRect bodyRect(view.viewport()->mapFromGlobal(bodyGap->mapToGlobal(QPoint())),
                             bodyGap->size());
        QVERIFY(bodyRect.left() >= end - view.verticalGridLineWidth());
        QVERIFY(bodyRect.right() < end + view.columnSpacing());
        QVERIFY(view.viewport()->mapFromGlobal(headerGap->mapToGlobal(QPoint())).y() < 0);
    }

    const qint64 offset = qMin<qint64>(20, view.maximumHorizontalOffset(1));
    view.setHorizontalOffset(1, offset);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.horizontalOffset(1), offset);
    QLabel *scrolledGap = nullptr;
    for (QLabel *label : view.findChildren<QLabel *>(QStringLiteral("advancedColumnGap"))) {
        if (label->isVisible() && label->property("logicalColumn").toInt() == 3)
            scrolledGap = label;
    }
    QVERIFY(scrolledGap);
    const QRect scrolledRect(view.viewport()->mapFromGlobal(scrolledGap->mapToGlobal(QPoint())),
                             scrolledGap->size());
    const int scrolledEnd = view.columnGeometry(3).viewportX + view.columnWidth(3);
    QVERIFY(scrolledRect.left() >= scrolledEnd - view.verticalGridLineWidth());
    view.setHorizontalGridLinesVisible(true);
    view.setVerticalGridLinesVisible(true);
    view.setHorizontalGridLineWidth(1);
    view.setVerticalGridLineWidth(1);
    for (int palette = 0; palette < 2; ++palette) {
        const QColor horizontal = palette == 0 ? QColor(17, 131, 229) : QColor(210, 40, 130);
        const QColor vertical = palette == 0 ? QColor(19, 173, 53) : QColor(160, 70, 230);
        view.setHorizontalGridLineColor(horizontal);
        view.setVerticalGridLineColor(vertical);
        for (bool verticalThrough : {false, true}) {
            for (bool horizontalThrough : {false, true}) {
                view.setVerticalSpacingLineThroughRowSpacing(verticalThrough);
                view.setHorizontalSpacingLineThroughColumnSpacing(horizontalThrough);
                view.flushPendingRelayout();
                settle();
                const QImage image = view.grab().toImage();
                const QPoint origin = view.viewport()->pos();
                const QRect body = view.visualRect(child);
                for (int column : {1, 3}) {
                    const int end = view.columnGeometry(column).viewportX + view.columnWidth(column);
                    const QPoint verticalGap(end - 1, body.bottom() + 3);
                    const QPoint horizontalGap(end + 2, body.bottom() + 1);
                    if (verticalThrough)
                        QCOMPARE(image.pixelColor(origin + verticalGap), vertical);
                    else
                        QVERIFY(image.pixelColor(origin + verticalGap) != vertical);
                    if (horizontalThrough)
                        QCOMPARE(image.pixelColor(origin + horizontalGap), horizontal);
                    else
                        QVERIFY(image.pixelColor(origin + horizontalGap) != horizontal);
                    QCOMPARE(image.pixelColor(origin + QPoint(end - 1, body.top() + 4)), vertical);
                    QCOMPARE(image.pixelColor(origin + QPoint(end - 20, body.bottom() + 1)), horizontal);
                }
                const QPoint branch(view.columnGeometry(0).viewportX + view.indentation() + 9,
                                    body.center().y());
                QCOMPARE(image.pixelColor(origin + branch), QColor(230, 180, 10));
            }
        }
    }
}

void TestTreeTableViewInteraction::customBranchRendererFollowsAdvancedPanes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("customHeaders");
    for (bool cells : {false, true}) {
        for (bool customHeaders : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + (customHeaders ? "custom" : "default");
            QTest::newRow(name.constData()) << cells << customHeaders;
        }
    }
}

void TestTreeTableViewInteraction::customBranchRendererFollowsAdvancedPanes()
{
    QFETCH(bool, cells);
    QFETCH(bool, customHeaders);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int column = 3; column < 6; ++column)
        parentItems.append(new QStandardItem(QStringLiteral("parent-%1").arg(column)));
    auto childItems = row(QStringLiteral("child"));
    for (int column = 3; column < 6; ++column)
        childItems.append(new QStandardItem(QStringLiteral("child-%1").arg(column)));
    parentItems.first()->appendRow(childItems);
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("tail")));

    VirtualTreeTableView view;
    if (customHeaders) {
        auto *horizontal = new VirtualHeaderView(Qt::Horizontal);
        horizontal->setAdapter(new TreeHeaderAdapter(&view, Qt::Horizontal, 1), true);
        auto *vertical = new VirtualHeaderView(Qt::Vertical);
        vertical->setAdapter(new TreeHeaderAdapter(&view, Qt::Vertical, 2), true);
        view.setHorizontalHeader(horizontal);
        view.setVerticalHeader(vertical);
    }
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setColumnOverscan(0);
    view.setFrozenColumns({0});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2}, PaneScroll::Scrollable, 0},
                   {{3, 4, 5}, PaneScroll::Scrollable, 1}});
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(6), true);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    const QColor branchA(20, 190, 220);
    const QColor leafA(240, 170, 20);
    const QColor branchB(190, 40, 160);
    const QColor leafB(40, 220, 100);
    view.setBranchIndicatorRenderer(new ColorBranchRenderer(branchA, leafA), true);
    showView(&view, QSize(620, 300));
    view.flushPendingRelayout();
    settle();

    const auto marker = [&](const QModelIndex &node) {
        return view.viewport()->pos()
            + QPoint(view.columnGeometry(0).viewportX + view.itemDepth(node) * view.indentation() + 9,
                     view.visualRect(node).center().y());
    };
    const auto pixel = [&](const QPoint &point) {
        return view.grab().toImage().pixelColor(point);
    };
    QCOMPARE(pixel(marker(parent)), branchA);
    QCOMPARE(pixel(marker(child)), leafA);

    view.setHorizontalOffset(1, qMin<qint64>(20, view.maximumHorizontalOffset(1)));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(pixel(marker(parent)), branchA);
    view.setBranchIndicatorRenderer(new ColorBranchRenderer(branchB, leafB), true);
    settle();
    QCOMPARE(pixel(marker(parent)), branchB);
    QCOMPARE(pixel(marker(child)), leafB);

    view.setBranchIndicatorsVisible(false);
    settle();
    QVERIFY(pixel(marker(parent)) != branchB);
    QVERIFY(pixel(marker(child)) != leafB);
    view.setBranchIndicatorsVisible(true);
    view.setBranchIndicatorRenderer(nullptr);
    settle();
    QVERIFY(pixel(marker(parent)) != branchB);
    QVERIFY(pixel(marker(child)) != leafB);

    view.setBranchIndicatorRenderer(new ColorBranchRenderer(branchA, leafA), true);
    settle();
    QCOMPARE(pixel(marker(parent)), branchA);
    view.collapse(parent);
    QVERIFY(!view.isExpanded(parent));
    view.expand(parent);
    QVERIFY(view.isExpanded(parent));
    settle();
    QCOMPARE(pixel(marker(child)), leafA);
    view.setRowSpacing(4);
    view.setDepthRowSpacing(1, 6);
    view.setColumnSpacing(4);
    const QModelIndex spanAnchor = child.siblingAtColumn(1);
    view.setSpan(int(view.visibilityIndex()->visibleRowForIndex(child)), 1, 1, 2);
    view.setCurrentIndex(spanAnchor);
    view.selectionModel()->select(spanAnchor, QItemSelectionModel::ClearAndSelect);
    for (int layout = 0; layout < 3; ++layout) {
        view.setCurrentIndex(spanAnchor);
        view.selectionModel()->select(spanAnchor, QItemSelectionModel::ClearAndSelect);
        if (layout == 0) {
            view.setPanes({{{0}, PaneScroll::Frozen, 0},
                           {{1, 2}, PaneScroll::Scrollable, 0},
                           {{3, 4, 5}, PaneScroll::Scrollable, 1}});
        } else if (layout == 1) {
            view.setPanes({{{1}, PaneScroll::Frozen, 0},
                           {{0, 2}, PaneScroll::Scrollable, 0},
                           {{3, 4, 5}, PaneScroll::Scrollable, 1}});
        } else {
            view.setPanes({{{0}, PaneScroll::Frozen, 0},
                           {{1, 2}, PaneScroll::Scrollable, 0},
                           {{3, 4}, PaneScroll::Scrollable, 1},
                           {{5}, PaneScroll::Frozen, 0}});
        }
        view.setFrozenRows(layout == 0 ? 0 : layout == 1 ? 2 : 1);
        view.setFrozenBottomRows(layout == 0 ? 0 : 1);
        view.setHorizontalOffset(0, 0);
        view.setHorizontalOffset(1, qMin<qint64>(20, view.maximumHorizontalOffset(1)));
        view.flushPendingRelayout();
        settle();
        QCOMPARE(pixel(marker(parent)), branchA);
        QCOMPARE(pixel(marker(child)), leafA);
        QCOMPARE(view.currentIndex(), spanAnchor);
        QVERIFY(view.selectionModel()->isSelected(spanAnchor));
        const QModelIndex covered = child.siblingAtColumn(2);
        QCOMPARE(view.anchorIndex(covered), layout == 1 ? covered : spanAnchor);
        const QRect span = view.spanRect(spanAnchor);
        QVERIFY(!span.isEmpty());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     QPoint(span.left() + 5, span.center().y())), spanAnchor);
        if (layout != 1)
            QVERIFY(!static_cast<const VirtualItemView &>(view).indexAt(span.center()).isValid());
        if (cells) {
            QVERIFY(view.cellWidget(spanAnchor));
            QCOMPARE(view.indexForWidget(view.cellWidget(spanAnchor)), spanAnchor);
        } else {
            QVERIFY(view.widgetForIndex(child));
            QCOMPARE(view.indexForWidget(view.widgetForIndex(child)), child);
        }
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          marker(parent) - view.viewport()->pos());
        QVERIFY(!view.isExpanded(parent));
        view.expand(parent);
        view.flushPendingRelayout();
        settle();
        QCOMPARE(pixel(marker(child)), leafA);
        QCOMPARE(view.anchorIndex(covered), layout == 1 ? covered : spanAnchor);
    }
}

void TestTreeTableViewInteraction::branchRendererCanRemoveItselfDuringPaint_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::branchRendererCanRemoveItselfDuringPaint()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int column = 1; column < 4; ++column)
        parentItems.append(new QStandardItem(QStringLiteral("parent-%1").arg(column)));
    auto childItems = row(QStringLiteral("child"));
    for (int column = 1; column < 4; ++column)
        childItems.append(new QStandardItem(QStringLiteral("child-%1").arg(column)));
    parentItems.first()->appendRow(childItems);
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("tail")));

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(90);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({3});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2}, PaneScroll::Scrollable, 0},
                   {{3}, PaneScroll::Frozen, 0}});
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(4), true);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    view.setBranchIndicatorRenderer(new SelfRemovingBranchRenderer(&view), true);
    showView(&view, QSize(520, 300));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.branchIndicatorRenderer(), nullptr);

    const QColor branchColor(80, 190, 240);
    const QColor leafColor(240, 170, 80);
    view.setBranchIndicatorRenderer(new ColorBranchRenderer(branchColor, leafColor), true);
    settle();
    const auto marker = [&](const QModelIndex &node) {
        return view.viewport()->pos()
            + QPoint(view.columnGeometry(0).viewportX + view.itemDepth(node) * view.indentation()
                         + 9,
                     view.visualRect(node).center().y());
    };
    const QImage image = view.grab().toImage();
    QVERIFY(image.pixelColor(marker(parent)) == branchColor);
    QVERIFY(image.pixelColor(marker(child)) == leafColor);
}

void TestTreeTableViewInteraction::wideTreeMaterializesOnlyTheWindow()
{
    QStandardItemModel model;
    for (int rowNumber = 0; rowNumber < 600; ++rowNumber) {
        QList<QStandardItem *> items;
        for (int column = 0; column < 12; ++column)
            items.append(new QStandardItem(QStringLiteral("%1:%2").arg(rowNumber).arg(column)));
        if (rowNumber == 0) {
            for (int child = 0; child < 300; ++child) {
                QList<QStandardItem *> children;
                for (int column = 0; column < 12; ++column)
                    children.append(new QStandardItem(QStringLiteral("child %1:%2").arg(child).arg(column)));
                items.first()->appendRow(children);
            }
        }
        model.appendRow(items);
    }

    VirtualTreeTableView view;
    configure(&view, &model, true);
    QCOMPARE(view.visibleRowCount(), qsizetype(600));
    QVERIFY(view.materializedCellCount() > 0);
    QVERIFY(view.materializedCellCount() < 300);

    view.expand(model.index(0, 0));
    view.setVerticalOffset(2000);
    view.setHorizontalOffset(400);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(900));
    QVERIFY(view.materializedCellCount() > 0);
    QVERIFY(view.materializedCellCount() < 300);
}

void TestTreeTableViewInteraction::accessibilityExposesHierarchyAndCells()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, true);
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();

    QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(interface);
    QCOMPARE(interface->role(), QAccessible::Tree);
    auto *table = static_cast<QAccessibleTableInterface *>(
        interface->interface_cast(QAccessible::TableInterface));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->columnCount(), 3);
    QAccessibleInterface *childCell = table->cellAt(1, 1);
    QVERIFY(childCell);
    QCOMPARE(childCell->text(QAccessible::Name), QStringLiteral("child type"));
    QAccessibleInterface *parentNode = interface->child(0);
    QVERIFY(parentNode);
    QCOMPARE(parentNode->role(), QAccessible::TreeItem);
    QVERIFY(parentNode->childCount() > 0);
}

void TestTreeTableViewInteraction::accessibilityFocusAndHitsFollowIndependentPanes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::accessibilityFocusAndHitsFollowIndependentPanes()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int column = 3; column < 6; ++column)
        parentItems.append(new QStandardItem(QStringLiteral("parent-%1").arg(column)));
    for (int i = 0; i < 30; ++i) {
        auto items = row(QStringLiteral("child-%1").arg(i));
        for (int column = 3; column < 6; ++column)
            items.append(new QStandardItem(QStringLiteral("value-%1-%2").arg(i).arg(column)));
        parentItems.first()->appendRow(items);
    }
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(2, 0, parent);
    const QModelIndex target = node.siblingAtColumn(3);
    view.expand(parent);
    view.setDefaultColumnWidth(120);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2}, PaneScroll::Scrollable, 0},
                   {{3, 4}, PaneScroll::Scrollable, 1},
                   {{5}, PaneScroll::Frozen, 0}});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    showView(&view, QSize(620, 340));
    view.setCurrentIndex(target);
    view.setHorizontalOffset(0, 20);
    view.setHorizontalOffset(1, 25);
    view.flushPendingRelayout();
    settle();
    QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(interface);
    auto *table = interface->tableInterface();
    QVERIFY(table);
    QAccessibleInterface *parentInterface = interface->child(0);
    QVERIFY(parentInterface);
    QCOMPARE(parentInterface->text(QAccessible::Name), QStringLiteral("parent"));
    QAccessibleInterface *nodeInterface = parentInterface->focusChild();
    QVERIFY(nodeInterface);
    QCOMPARE(nodeInterface->text(QAccessible::Name), QStringLiteral("child-2"));
    QAccessibleInterface *cellInterface = nodeInterface->focusChild();
    QVERIFY(cellInterface);
    QCOMPARE(cellInterface, table->cellAt(3, 3));
    QCOMPARE(interface->focusChild(), cellInterface);
    QCOMPARE(cellInterface->text(QAccessible::Name), target.data().toString());
    QVERIFY(!cellInterface->rect().isEmpty());
    const QPoint hit = cellInterface->rect().center();
    QCOMPARE(interface->childAt(hit.x(), hit.y()), parentInterface);
    QCOMPARE(parentInterface->childAt(hit.x(), hit.y()), nodeInterface);
    QCOMPARE(nodeInterface->childAt(hit.x(), hit.y()), cellInterface);
    const QRect pane = view.panes().at(view.paneIndexOfColumn(3)).viewportRect;
    const QRect globalPane(view.viewport()->mapToGlobal(pane.topLeft()), pane.size());
    QVERIFY(globalPane.contains(cellInterface->rect()));
    const QPoint outside = view.viewport()->mapToGlobal(QPoint(-1, -1));
    QVERIFY(!interface->childAt(outside.x(), outside.y()));
    view.collapse(parent);
    view.flushPendingRelayout();
    QVERIFY(cellInterface->state().offscreen);
    QVERIFY(!nodeInterface->focusChild());
    view.expand(parent);
    view.setCurrentIndex(target);
    view.flushPendingRelayout();
    QCOMPARE(nodeInterface->focusChild(), cellInterface);
}

void TestTreeTableViewInteraction::selectedBackgroundExtentKeepsGridLineVisible()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("first")));
    parent.first()->appendRow(row(QStringLiteral("second")));
    model.appendRow(parent);

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setVisualStateAnimationDuration(0);
    view.setVisualStateBackgroundVisible(true);
    view.setSelectedBackgroundColor(QColor(230, 30, 70));
    view.setHorizontalGridLineColor(QColor(15, 125, 40));
    view.setVerticalGridLinesVisible(false);
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();
    const QModelIndex child = model.index(0, 0, model.index(0, 0));
    const QRect rowRect = view.visualRect(child);
    QVERIFY(rowRect.height() > 2);
    const int y = rowRect.center().y();
    const QImage baseline = view.viewport()->grab().toImage();

    view.setCurrentIndex(child);
    QVERIFY(view.visualState(child).selected);
    const QColor selected(230, 30, 70);
    const QColor line(15, 125, 40);
    using Extent = VirtualTreeTableView::VisualStateBackgroundExtent;
    for (const Extent extent : {Extent::NodeOnly, Extent::NodeAndIcon, Extent::FullWidth}) {
        view.setVisualStateBackgroundExtent(extent);
        const QImage image = view.viewport()->grab().toImage();
        QCOMPARE(image.pixelColor(45, y), selected);
        QCOMPARE(image.pixelColor(25, y), extent == Extent::NodeOnly
                     ? baseline.pixelColor(25, y) : selected);
        QCOMPARE(image.pixelColor(5, y), extent == Extent::FullWidth
                     ? selected : baseline.pixelColor(5, y));
        const QImage full = view.grab().toImage();
        QCOMPARE(full.pixelColor(view.viewport()->geometry().topLeft()
                                     + QPoint(70, rowRect.bottom())), line);
    }
}

void TestTreeTableViewInteraction::rowGridLineAliasesMatchTableGridSettings_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::rowGridLineAliasesMatchTableGridSettings()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parent = row(QStringLiteral("parent"));
    parent.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parent);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QColor color(17, 93, 181);
    view.setRowGridLinesVisible(false);
    view.setRowGridLineWidth(3);
    view.setRowGridLineColor(color);
    view.setRowGridLineExtent(VirtualTreeTableView::RowGridLineExtent::NodeAndIcon);
    QCOMPARE(view.rowGridLinesVisible(), false);
    QCOMPARE(view.horizontalGridLinesVisible(), false);
    QCOMPARE(view.rowGridLineWidth(), 3);
    QCOMPARE(view.horizontalGridLineWidth(), 3);
    QCOMPARE(view.rowGridLineColor(), color);
    QCOMPARE(view.horizontalGridLineColor(), color);
    QCOMPARE(view.rowGridLineExtent(), VirtualTreeTableView::RowGridLineExtent::NodeAndIcon);
    view.setHorizontalGridLinesVisible(true);
    view.setHorizontalGridLineWidth(1);
    const QColor replacement(205, 71, 33);
    view.setHorizontalGridLineColor(replacement);
    QCOMPARE(view.rowGridLinesVisible(), true);
    QCOMPARE(view.rowGridLineWidth(), 1);
    QCOMPARE(view.rowGridLineColor(), replacement);
}

void TestTreeTableViewInteraction::rootSchemaAndCrossParentSelection()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto firstRoot = row(QStringLiteral("first root"));
    firstRoot.first()->appendRow({new QStandardItem(QStringLiteral("narrow")),
                                  new QStandardItem(QStringLiteral("type"))});
    model.appendRow(firstRoot);
    auto secondRoot = row(QStringLiteral("second root"));
    secondRoot.first()->appendRow(row(QStringLiteral("wide")));
    model.appendRow(secondRoot);

    InspectTreeTableView view;
    configure(&view, &model, true);
    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    const QModelIndex narrow = model.index(0, 0, first);
    const QModelIndex wide = model.index(0, 0, second);
    view.expand(first);
    view.expand(second);
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    const QList<QModelIndex> cells = view.selectionRange(narrow.siblingAtColumn(1),
                                                         wide.siblingAtColumn(2)).indexes();
    QCOMPARE(cells.size(), 5);
    QVERIFY(cells.contains(narrow.siblingAtColumn(1)));
    QVERIFY(cells.contains(second.siblingAtColumn(2)));
    QVERIFY(cells.contains(wide.siblingAtColumn(2)));

    view.setRootIndex(first);
    view.flushPendingRelayout();
    QCOMPARE(view.columnCount(), 2);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    view.setColumnWidth(1, 135);
    const QByteArray narrowState = view.saveHeaderState();
    view.setRootIndex(QModelIndex());
    view.flushPendingRelayout();
    QCOMPARE(view.columnCount(), 3);
    QVERIFY(!view.restoreHeaderState(narrowState));
    view.setRootIndex(first);
    view.setColumnWidth(1, 90);
    QVERIFY(view.restoreHeaderState(narrowState));
    QCOMPARE(view.columnWidth(1), 135);
}

void TestTreeTableViewInteraction::dropTargetsFollowVisibleTreeAndSpacing()
{
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto firstRoot = row(QStringLiteral("first root"));
    firstRoot.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(firstRoot);
    model.appendRow(row(QStringLiteral("second root")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(8);
    const QModelIndex root = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, root);
    view.expand(root);
    view.flushPendingRelayout();
    const QRect rootRect = view.visualRect(root);
    const QRect childRect = view.visualRect(child);
    const auto beforeRoot = view.dropTargetAt(QPoint(50, rootRect.top() + 1));
    QVERIFY(!beforeRoot.parent.isValid());
    QCOMPARE(beforeRoot.row, 0);
    QVERIFY(!beforeRoot.ontoItem);

    const auto intoRoot = view.dropTargetAt(QPoint(50, rootRect.center().y()));
    QCOMPARE(intoRoot.parent, root);
    QCOMPARE(intoRoot.row, 1);
    QVERIFY(intoRoot.ontoItem);
    QVERIFY(!view.dropIndicatorRect(intoRoot).isEmpty());

    const auto beforeChild = view.dropTargetAt(QPoint(50, rootRect.bottom() + 2));
    QCOMPARE(beforeChild.parent, root);
    QCOMPARE(beforeChild.row, 0);
    QVERIFY(!beforeChild.ontoItem);
    const auto afterChild = view.dropTargetAt(QPoint(50, childRect.bottom() - 1));
    QCOMPARE(afterChild.parent, root);
    QCOMPARE(afterChild.row, 1);
    QVERIFY(!afterChild.ontoItem);

    const QRect last = view.visualRect(model.index(1, 0));
    const auto trailing = view.dropTargetAt(QPoint(50, last.bottom() + 5));
    QVERIFY(!trailing.parent.isValid());
    QCOMPARE(trailing.row, 2);
    QVERIFY(trailing.trailing);
}

void TestTreeTableViewInteraction::dropIndicatorsFollowFrozenPanesAndSpacing()
{
    RecordingDropModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    for (int i = 0; i < 12; ++i)
        model.appendRow(row(QStringLiteral("root %1").arg(i)));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setRowSpacing(8);
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.setVerticalOffset(56);
    view.flushPendingRelayout();

    const QRect topPane = view.itemPaneRect(ItemPane::Type::FrozenTop);
    const QRect scrollPane = view.itemPaneRect(ItemPane::Type::Scrollable);
    const QRect bottomPane = view.itemPaneRect(ItemPane::Type::FrozenBottom);
    QVERIFY(!topPane.isEmpty());
    QVERIFY(!scrollPane.isEmpty());
    QVERIFY(!bottomPane.isEmpty());

    const QPoint probe(40, topPane.top() + 1);
    const auto topTarget = view.dropTargetAt(probe);
    QVERIFY(topTarget.isValid());
    QVERIFY(!topTarget.parent.isValid());
    QCOMPARE(topTarget.row, 0);
    const auto lineCenterY = [](const QRect &line) { return line.top() + line.height() / 2; };
    QCOMPARE(lineCenterY(view.dropIndicatorRect(topTarget)), topPane.top());

    int scrollRow = -1;
    QRect scrollRowRect;
    for (int rowIndex = 1; rowIndex < model.rowCount() - 1; ++rowIndex) {
        const QRect candidate = view.visualRect(model.index(rowIndex, 0));
        if (scrollPane.contains(candidate.center())) {
            scrollRow = rowIndex;
            scrollRowRect = candidate;
            break;
        }
    }
    QVERIFY(scrollRow >= 1);
    const auto scrollTarget = view.dropTargetAt(
        QPoint(40, scrollRowRect.top() + 1));
    QVERIFY(scrollTarget.isValid());
    QVERIFY(!scrollTarget.parent.isValid());
    QCOMPARE(scrollTarget.row, scrollRow);
    QCOMPARE(lineCenterY(view.dropIndicatorRect(scrollTarget)), scrollRowRect.top());

    const int gapY = scrollRowRect.bottom() + 4;
    const auto gapTarget = view.dropTargetAt(QPoint(40, gapY));
    QVERIFY(gapTarget.isValid());
    QVERIFY(!gapTarget.parent.isValid());
    QCOMPARE(gapTarget.row, scrollRow + 1);
    QVERIFY(view.dropIndicatorRect(gapTarget).top() >= scrollPane.top());
    QVERIFY(view.dropIndicatorRect(gapTarget).bottom() <= scrollPane.bottom());

    const QModelIndex last = model.index(model.rowCount() - 1, 0);
    const QRect bottomRow = view.visualRect(last);
    QVERIFY(bottomPane.contains(bottomRow.center()));
    const auto bottomTarget = view.dropTargetAt(QPoint(40, bottomRow.top() + 1));
    QVERIFY(bottomTarget.isValid());
    QVERIFY(!bottomTarget.parent.isValid());
    QCOMPARE(bottomTarget.row, model.rowCount() - 1);
    QCOMPARE(lineCenterY(view.dropIndicatorRect(bottomTarget)), bottomPane.top());
}

void TestTreeTableViewInteraction::dropIndicatorFollowsMergedCellAnchor_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::dropIndicatorFollowsMergedCellAnchor()
{
    QFETCH(bool, cells);
    RecordingDropModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State"), QStringLiteral("Extra")});
    model.appendRow(row(QStringLiteral("first")));
    model.appendRow(row(QStringLiteral("merged")));
    model.appendRow(row(QStringLiteral("last")));

    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    view.setColumnSpacing(6);
    auto *spans = new TableSpanMap;
    const QModelIndex anchor = model.index(1, 1);
    spans->setSpan(anchor, 1, 2);
    view.setSpanProvider(spans, true);
    view.flushPendingRelayout();

    const ColumnGeometry coveredColumn = view.columnGeometry(2);
    QVERIFY(coveredColumn.isValid());
    QVERIFY(coveredColumn.width > 0);
    const int coveredX = coveredColumn.viewportX + coveredColumn.width / 2;
    const auto target = view.dropTargetAt(
        QPoint(coveredX, view.visualRect(model.index(1, 0)).top() + 1));
    QVERIFY(target.isValid());
    QCOMPARE(target.row, 1);
    QCOMPARE(target.column, anchor.column());
    QVERIFY(!target.ontoItem);

    const QRect merged = view.spanRect(anchor);
    const QRect indicator = view.dropIndicatorRect(target);
    QVERIFY(!merged.isEmpty());
    QVERIFY(!indicator.isEmpty());
    QCOMPARE(indicator.x(), merged.x());
    QCOMPARE(indicator.width(), merged.width());
    QCOMPARE(indicator.top() + indicator.height() / 2,
             view.visualRect(model.index(1, 0)).top());
}

void TestTreeTableViewInteraction::dragAutoscrollTracksTreeTargetsAndStops_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::dragAutoscrollTracksTreeTargetsAndStops()
{
    QFETCH(bool, cells);
    RecordingDropModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 100; ++i)
        parentItems.first()->appendRow(row(QStringLiteral("child %1").arg(i)));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.expand(model.index(0, 0));
    view.setDragEnabled(true);
    view.setDropIndicatorShown(true);
    view.flushPendingRelayout();
    QMimeData data;
    data.setData(model.mimeTypes().first(), QByteArrayLiteral("payload"));
    const QPoint bottom(50, view.viewport()->height() - 2);
    QDragEnterEvent enter(bottom, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    const auto move = [&](const QPoint &position, const QMimeData *mime) {
        QDragMoveEvent event(position, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
        return event.isAccepted();
    };
    const auto initial = view.dropTargetAt(bottom);
    QVERIFY(move(bottom, &data));
    QTRY_VERIFY_WITH_TIMEOUT(view.verticalOffset() >= 120, 2000);
    const auto scrolled = view.dropTargetAt(bottom);
    QVERIFY(scrolled.parent.isValid());
    QCOMPARE(scrolled.parent.model(), &model);
    QVERIFY(scrolled.row >= 0);
    QVERIFY(scrolled.row <= model.rowCount(scrolled.parent));
    QVERIFY(scrolled.parent != initial.parent || scrolled.row > initial.row);
    QWidget *indicator = view.viewport()->findChild<QWidget *>(QStringLiteral("vivDropIndicator"));
    QVERIFY(indicator);
    QVERIFY(indicator->isVisible());
    QCOMPARE(indicator->geometry(), view.dropIndicatorRect(scrolled));

    QDragLeaveEvent leave;
    QApplication::sendEvent(view.viewport(), &leave);
    const qint64 stopped = view.verticalOffset();
    QTest::qWait(120);
    QCOMPARE(view.verticalOffset(), stopped);
    QVERIFY(!indicator->isVisible());

    QDragEnterEvent reenter(bottom, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &reenter);
    QVERIFY(move(QPoint(50, 1), &data));
    QTRY_VERIFY_WITH_TIMEOUT(view.verticalOffset() < stopped, 2000);
    QMimeData unsupported;
    QVERIFY(!move(bottom, &unsupported));
    const qint64 rejected = view.verticalOffset();
    QTest::qWait(120);
    QCOMPARE(view.verticalOffset(), rejected);
    QVERIFY(!indicator->isVisible());

    QVERIFY(move(bottom, &data));
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    view.setModel(&replacement);
    const qint64 replaced = view.verticalOffset();
    QTest::qWait(120);
    QCOMPARE(view.verticalOffset(), replaced);
    QVERIFY(!indicator->isVisible());
    for (QTimer *timer : view.findChildren<QTimer *>()) {
        if (timer->interval() == 40)
            QVERIFY(!timer->isActive());
    }
}

void TestTreeTableViewInteraction::modelReceivesDropAndCanRejectIt()
{
    RecordingDropModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto firstRoot = row(QStringLiteral("first root"));
    firstRoot.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(firstRoot);
    model.appendRow(row(QStringLiteral("second root")));

    VirtualTreeTableView view;
    configure(&view, &model, false);
    view.setDragEnabled(true);
    const QModelIndex root = model.index(0, 0);
    view.expand(root);
    view.flushPendingRelayout();
    int droppedSignals = 0;
    QObject::connect(&view, &VirtualItemView::itemDropped, &view,
                     [&](const QModelIndex &, int, int, Qt::DropAction) { ++droppedSignals; });
    QMimeData data;
    data.setData(model.mimeTypes().first(), QByteArrayLiteral("new child"));

    const QPoint intoRoot(50, view.visualRect(root).center().y());
    QVERIFY(sendDrop(&view, intoRoot, &data, Qt::CopyAction));
    QCOMPARE(model.dropCalls, 1);
    QCOMPARE(model.lastParent, QPersistentModelIndex(root));
    QCOMPARE(model.lastRow, 1);
    QCOMPARE(model.lastColumn, -1);
    QCOMPARE(model.lastAction, Qt::CopyAction);
    QCOMPARE(droppedSignals, 1);
    QCOMPARE(model.rowCount(root), 2);
    QCOMPARE(view.visibleRowCount(), qsizetype(4));

    model.acceptDrop = false;
    const QPoint intoSecond(50, view.visualRect(model.index(1, 0)).center().y());
    QVERIFY(!sendDrop(&view, intoSecond, &data, Qt::MoveAction));
    QCOMPARE(model.dropCalls, 2);
    QCOMPARE(model.lastParent, QPersistentModelIndex(model.index(1, 0)));
    QCOMPARE(model.lastAction, Qt::MoveAction);
    QCOMPARE(droppedSignals, 1);
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
}

void TestTreeTableViewInteraction::crossParentMoveCommitsToTheTargetParent()
{
    CrossParentMoveModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                     QStringLiteral("State")});
    auto first = row(QStringLiteral("first parent"));
    first.first()->appendRow(row(QStringLiteral("moving child")));
    auto second = row(QStringLiteral("second parent"));
    second.first()->appendRow(row(QStringLiteral("existing child")));
    model.appendRow(first);
    model.appendRow(second);

    InspectTreeTableView view;
    configure(&view, &model, false);
    view.setDragEnabled(true);
    const QModelIndex firstIndex = model.index(0, 0);
    const QModelIndex secondIndex = model.index(1, 0);
    const QModelIndex source = model.index(0, 0, firstIndex);
    view.expand(firstIndex);
    view.expand(secondIndex);
    view.flushPendingRelayout();

    QScopedPointer<QMimeData> data(model.mimeData({source}));
    QVERIFY(data);
    const QPoint target = view.visualRect(secondIndex).center();
    QVERIFY(sendDrop(&view, target, data.data(), Qt::MoveAction));

    QCOMPARE(model.dropCalls, 1);
    QCOMPARE(model.lastAction, Qt::MoveAction);
    QCOMPARE(model.lastParent, QPersistentModelIndex(secondIndex));
    QCOMPARE(model.lastRow, 1);
    QCOMPARE(model.lastColumn, -1);
    QCOMPARE(model.rowCount(firstIndex), 0);
    QCOMPARE(model.rowCount(secondIndex), 2);
    QCOMPARE(model.index(0, 0, secondIndex).data().toString(), QStringLiteral("existing child"));
    QCOMPARE(model.index(1, 0, secondIndex).data().toString(), QStringLiteral("moving child"));

    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    const QRect movedRect = view.visualRect(model.index(1, 0, secondIndex));
    QVERIFY(!movedRect.isEmpty());
    const QModelIndex hit = view.indexAt(QPoint(movedRect.left() + 5, movedRect.center().y()));
    QCOMPARE(hit.siblingAtColumn(0).data().toString(), QStringLiteral("moving child"));
}

void TestTreeTableViewInteraction::modelSwitchDuringDropDoesNotSubmitStaleTarget()
{
    RecordingDropModel oldModel;
    oldModel.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                        QStringLiteral("State")});
    oldModel.appendRow(row(QStringLiteral("old parent")));
    RecordingDropModel newModel;
    newModel.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                        QStringLiteral("State")});
    newModel.appendRow(row(QStringLiteral("new parent")));

    VirtualTreeTableView view;
    configure(&view, &oldModel, false);
    view.setDragEnabled(true);
    QMimeData data;
    data.setData(oldModel.mimeTypes().first(), QByteArrayLiteral("child"));
    const QPoint target(50, view.visualRect(oldModel.index(0, 0)).center().y());
    int droppedSignals = 0;
    QObject::connect(&view, &VirtualItemView::itemDropped, &view,
                     [&](const QModelIndex &, int, int, Qt::DropAction) { ++droppedSignals; });

    QDragEnterEvent firstEnter(target, Qt::CopyAction, &data, Qt::LeftButton,
                               Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &firstEnter);
    QVERIFY(firstEnter.isAccepted());
    oldModel.onCanDrop = [&] { view.setModel(&newModel); };
    QDragMoveEvent move(target, Qt::CopyAction, &data, Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(!move.isAccepted());
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&newModel));

    view.setModel(&oldModel);
    oldModel.onCanDrop = {};
    QDragEnterEvent secondEnter(target, Qt::CopyAction, &data, Qt::LeftButton,
                                Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &secondEnter);
    QVERIFY(secondEnter.isAccepted());
    QDragMoveEvent firstMove(target, Qt::CopyAction, &data, Qt::LeftButton,
                             Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &firstMove);
    QVERIFY(firstMove.isAccepted());
    oldModel.onCanDrop = [&] { view.setModel(&newModel); };
    QDropEvent firstDrop(QPointF(target), Qt::CopyAction, &data, Qt::LeftButton,
                         Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &firstDrop);
    QVERIFY(!firstDrop.isAccepted());
    QCOMPARE(oldModel.dropCalls, 0);
    QCOMPARE(newModel.dropCalls, 0);
    QCOMPARE(droppedSignals, 0);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&newModel));

    view.setModel(&oldModel);
    oldModel.onCanDrop = {};
    QDragEnterEvent thirdEnter(target, Qt::CopyAction, &data, Qt::LeftButton,
                               Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &thirdEnter);
    QVERIFY(thirdEnter.isAccepted());
    QDragMoveEvent secondMove(target, Qt::CopyAction, &data, Qt::LeftButton,
                              Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &secondMove);
    QVERIFY(secondMove.isAccepted());
    oldModel.onDrop = [&] { view.setModel(&newModel); };
    QDropEvent secondDrop(QPointF(target), Qt::CopyAction, &data, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &secondDrop);
    QVERIFY(!secondDrop.isAccepted());
    QCOMPARE(oldModel.dropCalls, 1);
    QCOMPARE(newModel.dropCalls, 0);
    QCOMPARE(droppedSignals, 0);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&newModel));
}

void TestTreeTableViewInteraction::synchronousAdapterChanges_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("change");
    const char *stages[] = {"type", "create", "bind", "unbind", "state"};
    const char *changes[] = {"model", "root", "adapter", "mode"};
    for (bool cells : {false, true}) {
        for (int stage = 0; stage < 5; ++stage) {
            for (int change = 0; change < 4; ++change) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + stages[stage] + "-" + changes[change];
                QTest::newRow(name.constData()) << cells << stage << change;
            }
        }
    }
}

void TestTreeTableViewInteraction::synchronousAdapterChanges()
{
    QFETCH(bool, cells);
    QFETCH(int, stage);
    QFETCH(int, change);
    QStandardItemModel original;
    QStandardItemModel replacement;
    for (QStandardItemModel *model : {&original, &replacement}) {
        for (int parent = 0; parent < 2; ++parent) {
            auto items = row(QStringLiteral("%1 parent %2")
                                 .arg(model == &original ? "old" : "new").arg(parent));
            for (int child = 0; child < 60; ++child)
                items.first()->appendRow(row(QStringLiteral("child %1:%2").arg(parent).arg(child)));
            model->appendRow(items);
        }
    }
    CallbackProbe probe;
    CallbackProbe passive;
    probe.stage = static_cast<CallbackStage>(stage);
    CallbackRowAdapter rowAdapter(&probe, 1);
    CallbackCellAdapter cellAdapter(&probe, 2);
    CallbackRowAdapter nextRowAdapter(&passive, 3);
    CallbackCellAdapter nextCellAdapter(&passive, 4);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    if (cells)
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&original);
    showView(&view, QSize(440, 280));
    view.expand(original.index(0, 0));
    view.flushPendingRelayout();

    const QPersistentModelIndex newRoot(original.index(1, 0));
    probe.action = [&] {
        switch (change) {
        case 0: view.setModel(&replacement); break;
        case 1: view.setRootIndex(newRoot); break;
        case 2:
            if (cells)
                view.setCellAdapter(&nextCellAdapter);
            else
                view.setTableAdapter(&nextRowAdapter);
            break;
        case 3:
            view.setMaterializationMode(cells ? VirtualTableView::MaterializationMode::RowWidgets
                                             : VirtualTableView::MaterializationMode::CellWidgets);
            break;
        }
    };
    // Recycle the existing window, then materialize fresh nodes. The one-shot
    // callback also catches unbind while the outer root change is in progress.
    view.setRootIndex(original.index(0, 0));
    settle();
    view.flushPendingRelayout();
    QCOMPARE(probe.calls, 1);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(change == 0 ? &replacement : &original));
    const QModelIndex expectedRoot = change == 0 ? QModelIndex()
        : change == 1 ? QModelIndex(newRoot) : original.index(0, 0);
    QCOMPARE(view.rootIndex(), expectedRoot);
    const bool finalCells = change == 3 ? !cells : cells;
    QCOMPARE(view.materializationMode(), finalCells
                 ? VirtualTableView::MaterializationMode::CellWidgets
                 : VirtualTableView::MaterializationMode::RowWidgets);
    const int expectedOwner = change == 2 ? (finalCells ? 4 : 3) : (finalCells ? 2 : 1);
    const auto verifyWindow = [&] {
        int checked = 0;
        for (qsizetype visible = 0; visible < view.visibleRowCount(); ++visible) {
            const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(visible);
            for (int column = 0; column < (finalCells ? view.columnCount() : 1); ++column) {
                const QModelIndex index = node.siblingAtColumn(column);
                QWidget *widget = finalCells ? view.cellWidget(index) : view.widgetForIndex(index);
                if (!widget)
                    continue;
                ++checked;
                QCOMPARE(widget->property("owner").toInt(), expectedOwner);
                QCOMPARE(widget->property("boundText"), index.data());
            }
        }
        QVERIFY(checked > 0);
    };
    verifyWindow();
    view.setVerticalOffset(900);
    view.flushPendingRelayout();
    verifyWindow();
    view.setVerticalOffset(0);
    view.flushPendingRelayout();
    verifyWindow();
}

void TestTreeTableViewInteraction::callbackDeletingBoundWidget_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::callbackDeletingBoundWidget()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parent = row(QStringLiteral("parent"));
    for (int child = 0; child < 80; ++child)
        parent.first()->appendRow(row(QStringLiteral("child-%1").arg(child)));
    model.appendRow(parent);

    CallbackProbe probe;
    CallbackRowAdapter rowAdapter(&probe, 1);
    CallbackCellAdapter cellAdapter(&probe, 2);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    if (cells)
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&model);
    const QModelIndex parentIndex = model.index(0, 0);
    view.expand(parentIndex);
    showView(&view, QSize(440, 280));
    view.flushPendingRelayout();
    settle();

    probe.widgetAction = [](QWidget *widget) { delete widget; };
    view.setRootIndex(parentIndex);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(probe.calls, 1);

    int materialized = 0;
    for (qsizetype visible = 0; visible < view.visibleRowCount(); ++visible) {
        const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(visible);
        if (!node.isValid())
            continue;
        QWidget *widget = cells ? view.cellWidget(node) : view.widgetForIndex(node);
        if (widget)
            ++materialized;
    }
    QVERIFY(materialized > 0);

    view.setRootIndex(QModelIndex());
    view.flushPendingRelayout();
    settle();
    QVERIFY(view.widgetForIndex(model.index(0, 0)) || view.cellWidget(model.index(0, 0)));
}

void TestTreeTableViewInteraction::callbackDeletingModelDuringBind_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::callbackDeletingModelDuringBind()
{
    QFETCH(bool, cells);
    QPointer<QStandardItemModel> model = new QStandardItemModel;
    auto parent = row(QStringLiteral("parent"));
    for (int child = 0; child < 100; ++child)
        parent.first()->appendRow(row(QStringLiteral("child-%1").arg(child)));
    model->appendRow(parent);

    CallbackProbe probe;
    CallbackRowAdapter rowAdapter(&probe, 1);
    CallbackCellAdapter cellAdapter(&probe, 2);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    if (cells)
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(model.data());
    const QModelIndex parentIndex = model->index(0, 0);
    view.expand(parentIndex);
    showView(&view, QSize(440, 280));
    view.flushPendingRelayout();
    settle();

    probe.action = [&model] {
        delete model.data();
    };
    view.setVerticalOffset(700);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(probe.calls, 1);
    QVERIFY(model.isNull());
    QVERIFY(view.model() == nullptr);
    QCOMPARE(view.materializedItemCount(), qsizetype(0));
    QCOMPARE(view.materializedCellCount(), qsizetype(0));
}

void TestTreeTableViewInteraction::callbackDeletingAdapterDuringBind_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::callbackDeletingAdapterDuringBind()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parent = row(QStringLiteral("parent"));
    for (int child = 0; child < 100; ++child)
        parent.first()->appendRow(row(QStringLiteral("child-%1").arg(child)));
    model.appendRow(parent);

    CallbackProbe probe;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    if (cells) {
        view.setTableAdapter(new AdapterDeletingRowAdapter(&probe, 1), false);
        view.setCellAdapter(new AdapterDeletingCellAdapter(&probe, 2), true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new AdapterDeletingRowAdapter(&probe, 1), true);
        view.setCellAdapter(new AdapterDeletingCellAdapter(&probe, 2), false);
    }
    view.setModel(&model);
    const QModelIndex parentIndex = model.index(0, 0);
    view.expand(parentIndex);
    showView(&view, QSize(440, 280));
    view.flushPendingRelayout();
    settle();

    probe.action = [&view, cells] {
        if (cells)
            view.setCellAdapter(nullptr, false);
        else
            view.setTableAdapter(nullptr, false);
    };
    view.setRootIndex(parentIndex);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(probe.calls, 1);
    if (cells)
        QVERIFY(view.cellAdapter() == nullptr);
    else
        QVERIFY(view.tableAdapter() == nullptr);
    QCOMPARE(view.materializedItemCount(), qsizetype(0));
    QCOMPARE(view.materializedCellCount(), qsizetype(0));

    if (cells) {
        CallbackCellAdapter replacement(&probe, 3);
        view.setCellAdapter(&replacement);
        view.flushPendingRelayout();
        settle();
        QVERIFY(view.cellAdapter() == &replacement);
        QVERIFY(view.materializedCellCount() > 0);
    } else {
        CallbackRowAdapter replacement(&probe, 3);
        view.setTableAdapter(&replacement);
        view.flushPendingRelayout();
        settle();
        QVERIFY(view.tableAdapter() == &replacement);
        QVERIFY(view.materializedItemCount() > 0);
    }
}

void TestTreeTableViewInteraction::callbackDeletingViewDuringBind_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::callbackDeletingViewDuringBind()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parent = row(QStringLiteral("parent"));
    for (int child = 0; child < 80; ++child)
        parent.first()->appendRow(row(QStringLiteral("child-%1").arg(child)));
    model.appendRow(parent);

    CallbackProbe probe;
    AdapterDeletingRowAdapter rowAdapter(&probe, 1);
    AdapterDeletingCellAdapter cellAdapter(&probe, 2);
    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    view->setTableAdapter(&rowAdapter);
    view->setCellAdapter(&cellAdapter);
    if (cells)
        view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view->setModel(&model);
    const QModelIndex parentIndex = model.index(0, 0);
    view->expand(parentIndex);
    showView(view.data(), QSize(440, 280));
    view->flushPendingRelayout();
    settle();

    probe.action = [&view] { delete view.data(); };
    view->setRootIndex(parentIndex);
    QVERIFY(view.isNull());
    QCOMPARE(probe.calls, 1);
}

void TestTreeTableViewInteraction::synchronousGeometryChanges_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("change");
    const char *stages[] = {"gap-create", "gap-bind", "row-layout"};
    const char *changes[] = {"model", "root", "factory", "mode"};
    for (bool cells : {false, true}) {
        for (int stage = 0; stage < (cells ? 2 : 3); ++stage) {
            for (int change = 0; change < 4; ++change) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + stages[stage] + "-" + changes[change];
                QTest::newRow(name.constData()) << cells << stage << change;
            }
        }
    }
}

void TestTreeTableViewInteraction::synchronousGeometryChanges()
{
    QFETCH(bool, cells);
    QFETCH(int, stage);
    QFETCH(int, change);
    QStandardItemModel original;
    QStandardItemModel replacement;
    for (QStandardItemModel *model : {&original, &replacement}) {
        for (int parent = 0; parent < 2; ++parent) {
            const QString prefix = QStringLiteral("%1-%2-")
                .arg(model == &original ? "old" : "new").arg(parent);
            auto items = row(prefix + QStringLiteral("parent"));
            for (int child = 0; child < 40; ++child)
                items.first()->appendRow(row(prefix + QString::number(child)));
            model->appendRow(items);
        }
    }
    CallbackProbe probe;
    probe.stage = stage == 0 ? CallbackStage::Create
        : stage == 1 ? CallbackStage::Bind : CallbackStage::Layout;
    CallbackProbe passive;
    CallbackRowAdapter rowAdapter(stage == 2 ? &probe : &passive, 1);
    CallbackCellAdapter cellAdapter(&passive, 2);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    if (cells)
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&original);
    view.expand(original.index(0, 0));
    showView(&view, QSize(440, 280));
    view.setRowSpacing(7);
    const QPersistentModelIndex targetRoot(original.index(1, 0));
    const auto nextFactory = [](const QModelIndex &, QWidget *parent) {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("reentryGap"));
        label->setProperty("factory", 2);
        return label;
    };
    const auto nextBinder = [](QWidget *widget, const QModelIndex &index) {
        widget->setProperty("boundNode", QVariant::fromValue(QPersistentModelIndex(index)));
    };
    probe.action = [&] {
        switch (change) {
        case 0: view.setModel(&replacement); break;
        case 1: view.setRootIndex(targetRoot); break;
        case 2: view.setRowSpacingFactory(nextFactory, nextBinder); break;
        case 3:
            view.setMaterializationMode(cells ? VirtualTableView::MaterializationMode::RowWidgets
                                             : VirtualTableView::MaterializationMode::CellWidgets);
            break;
        }
    };
    if (stage == 2) {
        view.setRowSpacingFactory(nextFactory, nextBinder);
        view.setColumnWidth(1, 120);
    } else {
        view.setRowSpacingFactory([&](const QModelIndex &, QWidget *parent) -> QWidget * {
            QPointer<QLabel> label = new QLabel(parent);
            label->setObjectName(QStringLiteral("reentryGap"));
            label->setProperty("factory", 1);
            probe.invoke(CallbackStage::Create);
            return label.data();
        }, [&](QWidget *widget, const QModelIndex &index) {
            nextBinder(widget, index);
            probe.invoke(CallbackStage::Bind);
        });
    }
    settle();
    view.flushPendingRelayout();
    QCOMPARE(probe.calls, 1);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(change == 0 ? &replacement : &original));
    QCOMPARE(view.rootIndex(), change == 1 ? QModelIndex(targetRoot) : QModelIndex());
    const bool finalCells = change == 3 ? !cells : cells;
    QCOMPARE(view.materializationMode(), finalCells
                 ? VirtualTableView::MaterializationMode::CellWidgets
                 : VirtualTableView::MaterializationMode::RowWidgets);
    for (int offset : {0, 700, 0}) {
        view.setVerticalOffset(offset);
        view.flushPendingRelayout();
        settle();
        int checked = 0;
        for (QLabel *gap : view.findChildren<QLabel *>(QStringLiteral("reentryGap"))) {
            if (!gap->isVisible())
                continue;
            ++checked;
            const QModelIndex node = gap->property("boundNode").value<QPersistentModelIndex>();
            QVERIFY(node.isValid());
            QCOMPARE(node.model(), static_cast<const QAbstractItemModel *>(view.model()));
            QVERIFY(view.visibilityIndex()->isVisible(node));
            if (change == 2 || stage == 2)
                QCOMPARE(gap->property("factory").toInt(), 2);
            const QPoint top = view.viewport()->mapFromGlobal(gap->parentWidget()->mapToGlobal(QPoint()));
            QCOMPARE(top.y(), view.visualRect(node).bottom() + 1);
            QCOMPARE(gap->parentWidget()->height(), 7);
        }
        QVERIFY(checked > 0);
        const QModelIndex hit = static_cast<const VirtualItemView &>(view).indexAt(QPoint(150, 14));
        QVERIFY(hit.isValid());
        QWidget *widget = finalCells ? view.cellWidget(hit) : view.widgetForIndex(hit.siblingAtColumn(0));
        QVERIFY(widget);
        QCOMPARE(widget->property("boundText"), (finalCells ? hit : hit.siblingAtColumn(0)).data());
    }
}

void TestTreeTableViewInteraction::filteringTheRootClearsDescendantState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::filteringTheRootClearsDescendantState()
{
    QFETCH(bool, cells);
    QStandardItemModel source;
    auto first = row(QStringLiteral("filtered root"));
    auto branch = row(QStringLiteral("branch"));
    branch.first()->appendRow(row(QStringLiteral("leaf")));
    first.first()->appendRow(branch);
    source.appendRow(first);
    source.appendRow(row(QStringLiteral("survivor")));
    RootVisibilityProxy proxy;
    proxy.setSourceModel(&source);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(new RowAdapter, true);
    view.setCellAdapter(new CellAdapter, true);
    if (cells)
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&proxy);
    const QPersistentModelIndex root(proxy.index(0, 0));
    const QPersistentModelIndex child(proxy.index(0, 0, root));
    const QPersistentModelIndex leaf(proxy.index(0, 1, child));
    view.setRootIndex(root);
    view.expand(child);
    view.setCurrentIndex(leaf);
    view.setItemPinned(leaf);
    showView(&view, QSize(440, 280));
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(view.isItemPinned(leaf));
    proxy.setHideFirstRoot(true);
    settle();
    view.flushPendingRelayout();
    QVERIFY(!root.isValid());
    QVERIFY(!view.rootIndex().isValid());
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());
    QCOMPARE(view.visibilityIndex()->expandedCount(), qsizetype(0));
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0).data().toString(), QStringLiteral("survivor"));
    const QModelIndex survivor = proxy.index(0, 1);
    QVERIFY(!view.isItemPinned(survivor));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(survivor).center()), survivor);
    proxy.setHideFirstRoot(false);
    settle();
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(!view.isExpanded(proxy.index(0, 0)));
    QVERIFY(!view.isItemPinned(proxy.index(0, 1, proxy.index(0, 0))));
    QVERIFY(!view.currentIndex().isValid());
}

void TestTreeTableViewInteraction::synchronousModelNotifications_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("notification");
    QTest::addColumn<int>("change");
    const char *notifications[] = {"row-remove", "column-insert", "column-remove", "reset",
                                   "data-bind", "data-state", "selection-state"};
    const char *changes[] = {"model", "root", "adapter", "mode"};
    for (bool cells : {false, true}) {
        for (int notification = 0; notification < 7; ++notification) {
            for (int change = 0; change < 4; ++change) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + notifications[notification] + "-" + changes[change];
                QTest::newRow(name.constData()) << cells << notification << change;
            }
        }
    }
}

void TestTreeTableViewInteraction::synchronousModelNotifications()
{
    QFETCH(bool, cells);
    QFETCH(int, notification);
    QFETCH(int, change);
    QStandardItemModel original;
    QStandardItemModel replacement;
    const auto populate = [](QStandardItemModel *model, const QString &prefix) {
        for (int parent = 0; parent < 2; ++parent) {
            auto items = row(prefix + QStringLiteral(" parent %1").arg(parent));
            for (int child = 0; child < 40; ++child)
                items.first()->appendRow(row(prefix + QStringLiteral(" child %1:%2").arg(parent).arg(child)));
            model->appendRow(items);
        }
    };
    populate(&original, QStringLiteral("old"));
    populate(&replacement, QStringLiteral("new"));
    CallbackProbe probe;
    CallbackProbe passive;
    probe.stage = notification < 4 ? CallbackStage::Unbind
        : notification == 4 ? CallbackStage::Bind : CallbackStage::State;
    CallbackRowAdapter rowAdapter(&probe, 1);
    CallbackCellAdapter cellAdapter(&probe, 2);
    CallbackRowAdapter nextRowAdapter(&passive, 3);
    CallbackCellAdapter nextCellAdapter(&passive, 4);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    if (cells)
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&original);
    const QPersistentModelIndex parent(original.index(0, 0));
    const QPersistentModelIndex targetRoot(original.index(1, 0));
    const QPersistentModelIndex child(original.index(0, 0, parent));
    view.expand(parent);
    showView(&view, QSize(440, 280));
    probe.action = [&] {
        switch (change) {
        case 0: view.setModel(&replacement); break;
        case 1: view.setRootIndex(targetRoot); break;
        case 2:
            if (cells)
                view.setCellAdapter(&nextCellAdapter);
            else
                view.setTableAdapter(&nextRowAdapter);
            break;
        case 3:
            view.setMaterializationMode(cells ? VirtualTableView::MaterializationMode::RowWidgets
                                             : VirtualTableView::MaterializationMode::CellWidgets);
            break;
        }
    };
    switch (notification) {
    case 0: QVERIFY(original.removeRows(0, 1, parent)); break;
    case 1: QVERIFY(original.insertColumns(1, 1)); break;
    case 2: QVERIFY(original.removeColumns(1, 1)); break;
    case 3:
        original.clear();
        populate(&original, QStringLiteral("reset"));
        break;
    case 4:
    case 5: QVERIFY(original.setData(child, QStringLiteral("updated"))); break;
    case 6: view.setCurrentIndex(child); break;
    }
    settle();
    view.flushPendingRelayout();
    QCOMPARE(probe.calls, 1);
    QAbstractItemModel *expectedModel = change == 0 ? &replacement : &original;
    QCOMPARE(view.model(), expectedModel);
    QCOMPARE(view.selectionModel()->model(), expectedModel);
    const QModelIndex expectedRoot = change == 1 && notification != 3
        ? QModelIndex(targetRoot) : QModelIndex();
    QCOMPARE(view.rootIndex(), expectedRoot);
    QCOMPARE(view.columnCount(), expectedModel->columnCount(expectedRoot));
    QCOMPARE(view.horizontalHeaderGeometry()->sectionCount(), view.columnCount());
    const bool finalCells = change == 3 ? !cells : cells;
    QCOMPARE(view.materializationMode(), finalCells
                 ? VirtualTableView::MaterializationMode::CellWidgets
                 : VirtualTableView::MaterializationMode::RowWidgets);
    if (change == 0 || notification == 3) {
        QCOMPARE(view.visibilityIndex()->expandedCount(), qsizetype(0));
        QVERIFY(!view.currentIndex().isValid());
        QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());
    }
    const int expectedOwner = change == 2 ? (finalCells ? 4 : 3) : (finalCells ? 2 : 1);
    for (int offset : {0, 500, 0}) {
        view.setVerticalOffset(offset);
        view.flushPendingRelayout();
        settle();
        int checked = 0;
        for (qsizetype visible = 0; visible < view.visibleRowCount(); ++visible) {
            const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(visible);
            QVERIFY(node.isValid());
            QCOMPARE(node.model(), static_cast<const QAbstractItemModel *>(expectedModel));
            QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), visible);
            for (int column = 0; column < (finalCells ? view.columnCount() : 1); ++column) {
                const QModelIndex index = node.siblingAtColumn(column);
                if (!index.isValid())
                    continue;
                QWidget *widget = finalCells ? view.cellWidget(index) : view.widgetForIndex(index);
                if (!widget)
                    continue;
                ++checked;
                QCOMPARE(widget->property("owner").toInt(), expectedOwner);
                QCOMPARE(widget->property("boundText"), index.data());
            }
        }
        QVERIFY(checked > 0);
    }
}

void TestTreeTableViewInteraction::modelSwitchCallbacksKeepTheNewestModel_data()
{
    QTest::addColumn<int>("trigger");
    QTest::addColumn<bool>("destroyRequested");
    const char *triggers[] = {"selection-model", "sort-api", "sort-header"};
    for (int trigger = 0; trigger < 3; ++trigger) {
        for (bool destroyRequested : {false, true}) {
            const QByteArray name = QByteArray(triggers[trigger])
                + (destroyRequested ? "-destroy" : "-replace");
            QTest::newRow(name.constData()) << trigger << destroyRequested;
        }
    }
}

void TestTreeTableViewInteraction::modelSwitchCallbacksKeepTheNewestModel()
{
    QFETCH(int, trigger);
    QFETCH(bool, destroyRequested);
    RecordingSortModel original;
    RecordingSortModel replacement;
    original.appendRow(row(QStringLiteral("original")));
    replacement.appendRow(row(QStringLiteral("replacement")));
    QPointer<RecordingSortModel> requested = new RecordingSortModel;
    requested->setParent(&original);
    requested->appendRow(row(QStringLiteral("requested")));
    VirtualTreeTableView view;
    configure(&view, &original, true);
    int callbacks = 0;
    if (trigger == 0) {
        connect(&view, &VirtualItemView::selectionModelChanged, &view,
                [&](QItemSelectionModel *) {
            if (callbacks != 0)
                return;
            ++callbacks;
            if (destroyRequested)
                delete requested.data();
            view.setModel(&replacement);
        });
        view.setModel(requested);
    } else {
        view.setModel(requested);
        view.setSortingEnabled(true);
        connect(&view, &VirtualTableView::sortIndicatorRequested, &view,
                [&](int, Qt::SortOrder) {
            ++callbacks;
            if (destroyRequested)
                delete requested.data();
            view.setModel(&replacement);
        });
        if (trigger == 1)
            view.sortByColumn(0, Qt::DescendingOrder);
        else
            view.setSortIndicator(0, Qt::DescendingOrder);
    }
    settle();
    view.flushPendingRelayout();
    QCOMPARE(callbacks, 1);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(view.selectionModel()->model(), static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(view.columnCount(), 3);
    QCOMPARE(view.visibleRowCount(), qsizetype(1));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0), replacement.index(0, 0));
    QCOMPARE(replacement.sortCalls, 0);
    if (requested)
        QCOMPARE(requested->sortCalls, 0);
    auto *label = qobject_cast<QLabel *>(view.cellWidget(replacement.index(0, 1)));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("replacement type"));
    delete requested.data();
}

void TestTreeTableViewInteraction::headerLayoutSurvivesFileAndViewRecreation_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::headerLayoutSurvivesFileAndViewRecreation()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto rootRow = row(QStringLiteral("root"));
    for (int child = 0; child < 4; ++child) {
        QList<QStandardItem *> items;
        for (int column = 0; column < 6; ++column)
            items.append(new QStandardItem(QStringLiteral("%1:%2").arg(child).arg(column)));
        rootRow.first()->appendRow(items);
    }
    model.appendRow(rootRow);
    const QModelIndex root = model.index(0, 0);
    QTemporaryFile stateFile;
    QVERIFY(stateFile.open());
    const QString path = stateFile.fileName();
    stateFile.close();
    QByteArray saved;
    QVector<int> widths;
    QVector<int> order;
    {
        VirtualTreeTableView view;
        configure(&view, &model, cells);
        view.setRootIndex(root);
        view.setColumnWidth(0, 85);
        view.setColumnWidth(2, 110);
        view.setColumnWidth(5, 75);
        view.setColumnHidden(4, true);
        view.horizontalHeaderGeometry()->moveSection(2, 0);
        view.setColumnStretchFactor(1, 1);
        view.setColumnStretchFactor(3, 2);
        view.setStretchLastColumn(true);
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({5});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.setSortIndicator(2, Qt::DescendingOrder);
        view.flushPendingRelayout();
        for (int column = 0; column < 6; ++column) {
            widths.append(view.columnWidth(column));
            order.append(view.horizontalHeaderGeometry()->logicalIndex(column));
        }
        saved = view.saveHeaderState();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(saved), qint64(saved.size()));
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray loaded = file.readAll();
    QCOMPARE(loaded, saved);
    VirtualTreeTableView restored;
    configure(&restored, &model, cells);
    const QByteArray narrowState = restored.saveHeaderState();
    QVERIFY(!restored.restoreHeaderState(loaded));
    QCOMPARE(restored.saveHeaderState(), narrowState);
    restored.setRootIndex(root);
    QVERIFY(restored.restoreHeaderState(loaded));
    restored.flushPendingRelayout();
    for (int column = 0; column < 6; ++column) {
        QCOMPARE(restored.columnWidth(column), widths.at(column));
        QCOMPARE(restored.horizontalHeaderGeometry()->logicalIndex(column), order.at(column));
        QCOMPARE(restored.isColumnHidden(column), column == 4);
    }
    QCOMPARE(restored.columnStretchFactor(1), qreal(1));
    QCOMPARE(restored.columnStretchFactor(3), qreal(2));
    QVERIFY(restored.stretchLastColumn());
    QCOMPARE(restored.frozenColumns(), QVector<int>({0}));
    QCOMPARE(restored.frozenRightColumns(), QVector<int>({5}));
    QCOMPARE(restored.frozenRows(), 1);
    QCOMPARE(restored.frozenBottomRows(), 1);
    QCOMPARE(restored.horizontalHeaderGeometry()->sortIndicatorSection(), 2);
    QCOMPARE(restored.horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::DescendingOrder);
    QCOMPARE(restored.saveHeaderState(), loaded);
    const QModelIndex cell = model.index(0, 0, root);
    QCOMPARE(static_cast<const VirtualItemView &>(restored).indexAt(restored.cellRect(cell).center()), cell);
}

void TestTreeTableViewInteraction::keyboardRespectsHiddenAndMissingColumns_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("wholeRows");
    for (bool cells : {false, true}) {
        for (bool wholeRows : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + (wholeRows ? "row-selection" : "cell-selection");
            QTest::newRow(name.constData()) << cells << wholeRows;
        }
    }
}

void TestTreeTableViewInteraction::keyboardRespectsHiddenAndMissingColumns()
{
    QFETCH(bool, cells);
    QFETCH(bool, wholeRows);
    QStandardItemModel model;
    auto firstRow = row(QStringLiteral("first"));
    firstRow.first()->appendRow({new QStandardItem(QStringLiteral("narrow")),
                                new QStandardItem(QStringLiteral("narrow type"))});
    model.appendRow(firstRow);
    auto secondRow = row(QStringLiteral("second"));
    secondRow.first()->appendRow(row(QStringLiteral("wide")));
    model.appendRow(secondRow);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    view.setSelectionBehavior(wholeRows ? VirtualItemView::SelectionBehavior::SelectRows
                                       : VirtualItemView::SelectionBehavior::SelectItems);
    view.setColumnHidden(1, true);
    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    const QModelIndex narrow = model.index(0, 0, first);
    const QModelIndex wide = model.index(0, 0, second);
    view.setCurrentIndex(first);
    QTest::keyClick(&view, Qt::Key_Right, wholeRows ? Qt::NoModifier : Qt::AltModifier);
    QVERIFY(view.isExpanded(first));
    QTest::keyClick(&view, Qt::Key_Right, wholeRows ? Qt::NoModifier : Qt::AltModifier);
    QCOMPARE(view.currentIndex(), narrow);
    QTest::keyClick(&view, Qt::Key_Left, wholeRows ? Qt::NoModifier : Qt::AltModifier);
    QCOMPARE(view.currentIndex(), first);
    QTest::keyClick(&view, Qt::Key_Left, wholeRows ? Qt::NoModifier : Qt::AltModifier);
    QVERIFY(!view.isExpanded(first));
    view.expand(first);
    view.expand(second);
    if (!wholeRows) {
        view.setCurrentIndex(first);
        QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(view.currentIndex(), first.siblingAtColumn(2));
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(view.currentIndex(), narrow);
        QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(view.currentIndex(), narrow);
    }
    view.setCurrentIndex(narrow);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), second);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), wide);
    const QList<QModelIndex> selected = view.selectionModel()->selectedIndexes();
    QCOMPARE(selected.size(), wholeRows ? 8 : 3);
    for (const QModelIndex &index : selected) {
        QVERIFY(index.isValid());
        QVERIFY(index.column() < qMin(view.columnCount(), model.columnCount(index.parent())));
        QVERIFY(index.siblingAtColumn(0) == narrow || index.siblingAtColumn(0) == second
                || index.siblingAtColumn(0) == wide);
    }
    QTest::keyClick(&view, Qt::Key_Space, Qt::ControlModifier);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), wholeRows ? 5 : 2);
    view.setColumnHidden(0, true);
    view.setCurrentIndex(second.siblingAtColumn(2));
    QTest::keyClick(&view, Qt::Key_Left, Qt::AltModifier);
    QVERIFY(!view.isExpanded(second));
    QTest::keyClick(&view, Qt::Key_Right, Qt::AltModifier);
    QVERIFY(view.isExpanded(second));
    view.setCurrentIndex(wide);
    view.setRootIndex(first);
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.columnCount(), 2);
}

void TestTreeTableViewInteraction::headerResizeSwitchesFollowTreePanes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<bool>("customHeaders");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            for (bool customHeaders : {false, true}) {
                const QByteArray name = QByteArray(cells ? "cells" : "rows")
                    + (frozen ? "-frozen" : "-plain")
                    + (customHeaders ? "-custom" : "-default");
                QTest::newRow(name.constData()) << cells << frozen << customHeaders;
            }
        }
    }
}

void TestTreeTableViewInteraction::headerResizeSwitchesFollowTreePanes()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QFETCH(bool, customHeaders);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("tail")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    if (customHeaders) {
        auto *horizontal = new VirtualHeaderView(Qt::Horizontal);
        horizontal->setAdapter(new TreeHeaderAdapter(&view, Qt::Horizontal, 1), true);
        auto *vertical = new VirtualHeaderView(Qt::Vertical);
        vertical->setAdapter(new TreeHeaderAdapter(&view, Qt::Vertical, 2), true);
        view.setHorizontalHeader(horizontal);
        view.setVerticalHeader(vertical);
    }
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({2});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    const QModelIndex tail = model.index(1, 0);
    view.expand(parent);
    view.flushPendingRelayout();
    settle();
    const auto dragEdge = [&](Qt::Orientation orientation, int logical, int delta) {
        QPointer<VirtualHeaderView> header;
        QWidget *section = nullptr;
        for (VirtualHeaderView *candidate : view.findChildren<VirtualHeaderView *>()) {
            QWidget *widget = candidate->sectionWidget(logical);
            if (candidate->orientation() == orientation && widget
                && widget->isVisible() && !widget->visibleRegion().isEmpty()) {
                header = candidate;
                section = widget;
                break;
            }
        }
        QVERIFY(header);
        const QPoint origin = section->mapTo(header, QPoint());
        const QPoint edge = orientation == Qt::Horizontal
            ? origin + QPoint(section->width() - 1, section->height() / 2)
            : origin + QPoint(section->width() / 2, section->height() - 1);
        const QPoint end = edge + (orientation == Qt::Horizontal ? QPoint(delta, 0)
                                                                 : QPoint(0, delta));
        sendHeaderMouse(header, QEvent::MouseButtonPress, edge, Qt::LeftButton, Qt::LeftButton);
        QVERIFY(header);
        sendHeaderMouse(header, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        QVERIFY(header);
        sendHeaderMouse(header, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        view.flushPendingRelayout();
        settle();
    };
    const auto verifySwitches = [&](bool enabled) {
        QCOMPARE(view.isColumnResizeEnabled(), enabled);
        QCOMPARE(view.isVerticalHeaderResizeEnabled(), enabled);
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>())
            QCOMPARE(header->isSectionResizeEnabled(), enabled);
    };
    verifySwitches(true);
    for (int column = 0; column < 3; ++column) {
        const int width = view.columnWidth(column);
        dragEdge(Qt::Horizontal, column, 12);
        QCOMPARE(view.columnWidth(column), width + 12);
    }
    for (int visible = 0; visible < 3; ++visible) {
        dragEdge(Qt::Vertical, visible, 7);
        QCOMPARE(view.rowHeight(visible), 35);
        QVERIFY(view.hasExplicitRowHeight(visible));
    }
    view.setColumnResizeEnabled(false);
    view.setVerticalHeaderResizeEnabled(false);
    verifySwitches(false);
    for (int column = 0; column < 3; ++column) {
        const int width = view.columnWidth(column);
        dragEdge(Qt::Horizontal, column, 13);
        QCOMPARE(view.columnWidth(column), width);
    }
    for (int visible = 0; visible < 3; ++visible) {
        dragEdge(Qt::Vertical, visible, 13);
        QCOMPARE(view.rowHeight(visible), 35);
    }
    view.setColumnWidth(1, 123);
    view.setRowHeight(1, 41);
    view.flushPendingRelayout();
    QCOMPARE(view.columnWidth(1), 123);
    QCOMPARE(view.rowHeight(1), 41);
    view.setColumnResizeEnabled(true);
    view.setVerticalHeaderResizeEnabled(true);
    verifySwitches(true);
    dragEdge(Qt::Horizontal, 1, -4);
    dragEdge(Qt::Vertical, 1, -4);
    QCOMPARE(view.columnWidth(1), 119);
    QCOMPARE(view.rowHeight(1), 37);
    QCOMPARE(view.visualRect(child).height(), 37);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(child.siblingAtColumn(1)).center()), child.siblingAtColumn(1));
    const QPoint marker(view.columnGeometry(0).viewportX + view.indentation() + 9,
                        view.visualRect(child).center().y());
    QCOMPARE(view.grab().toImage().pixelColor(view.viewport()->pos() + marker), QColor(230, 180, 10));
    view.collapse(parent);
    view.flushPendingRelayout();
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(tail), qsizetype(1));
    QCOMPARE(view.rowHeight(1), 35);
    view.expand(parent);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(1), 37);
    QCOMPARE(view.rowHeight(2), 35);
}

void TestTreeTableViewInteraction::rowDragAutoModelYieldsToApplicationTree_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::rowDragAutoModelYieldsToApplicationTree()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new RowAdapter, true);
    }
    QVERIFY(!view.model());
    view.setColumnDragEnabled(true);
    QVERIFY(view.isColumnDragEnabled());
    QVERIFY(!view.model());
    view.setVerticalHeaderDragEnabled(true);
    QVERIFY(view.isVerticalHeaderDragEnabled());
    auto *internal = dynamic_cast<ReorderableTableModel *>(view.model());
    QVERIFY(internal);
    QCOMPARE(internal->rowCount(), 0);
    QCOMPARE(view.columnCount(), 0);
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
    view.setModel(&model);
    showView(&view, QSize(440, 280));
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 1, parent);
    view.expand(parent);
    view.setVerticalHeaderDragEnabled(false);
    view.setVerticalHeaderDragEnabled(true);
    view.flushPendingRelayout();
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&model));
    QCOMPARE(view.columnCount(), 3);
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(child).center()), child);
    view.setModel(nullptr);
    view.setVerticalHeaderDragEnabled(false);
    view.setVerticalHeaderDragEnabled(true);
    QVERIFY(!view.model());
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
}

void TestTreeTableViewInteraction::dragSourcePinsSurviveIgnoredReturn_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("alreadyPinned");
    QTest::addColumn<bool>("changePin");
    for (bool cells : {false, true}) {
        for (bool pinned : {false, true}) {
            for (bool change : {false, true}) {
                const QByteArray name = QByteArray(cells ? "cells" : "rows")
                    + (pinned ? "-pinned" : "-unpinned")
                    + (change ? "-change" : "-keep");
                QTest::newRow(name.constData()) << cells << pinned << change;
            }
        }
    }
}

void TestTreeTableViewInteraction::dragSourcePinsSurviveIgnoredReturn()
{
    if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
        QSKIP("Ignored-return source lifetime is tested with the offscreen backend.");
    QFETCH(bool, cells);
    QFETCH(bool, alreadyPinned);
    QFETCH(bool, changePin);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 120; ++i)
        parentItems.first()->appendRow(row(QStringLiteral("child-%1").arg(i)));
    model.appendRow(parentItems);
    InspectTreeTableView view;
    configure(&view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    const QPersistentModelIndex source(model.index(0, 1, parent));
    view.expand(parent);
    view.setOverscan(0, 0);
    view.setDragEnabled(true);
    view.setDragDropActions(Qt::MoveAction);
    view.setMoveRemovesSourceRows(true);
    view.setItemPinned(source, alreadyPinned);
    view.flushPendingRelayout();
    QPointer<QWidget> widget(cells ? view.cellWidget(source)
        : view.widgetForIndex(QModelIndex(source).siblingAtColumn(0)));
    QVERIFY(widget);
    QCOMPARE(view.dragSourceWidget(source), widget.data());
    if (cells) {
        QVERIFY(view.cellWidget(model.index(0, 0, parent)) != widget.data());
        QVERIFY(!view.isItemPinned(model.index(0, 0, parent)));
    }
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy dropped(&view, &VirtualItemView::itemDropped);
    bool observed = false;
    bool temporaryPin = false;
    bool nestedDragRejected = false;
    bool keptWhileScrolled = false;
    bool keptWhileFolded = false;
    DragConstructionObserver observer;
    observer.onChildAdded = [&]() {
        if (observed)
            return;
        observed = true;
        temporaryPin = view.isItemPinned(source);
        nestedDragRejected = view.startDrag(source) == Qt::IgnoreAction
            && view.isItemPinned(source);
        if (changePin)
            view.setItemPinned(source, !alreadyPinned);
        view.scrollTo(model.index(80, 0, parent), VirtualItemView::PositionAtTop);
        view.flushPendingRelayout();
        keptWhileScrolled = widget && view.dragSourceWidget(source) == widget.data();
        view.collapse(parent);
        view.flushPendingRelayout();
        // Collapsed nodes have no visible-row mapping, but their pinned widget must remain.
        keptWhileFolded = widget && (cells ? view.cellWidget(source)
            : view.widgetForIndex(QModelIndex(source).siblingAtColumn(0))) == widget.data();
    };
    view.installEventFilter(&observer);
    QTimer::singleShot(0, &view, []() { QDrag::cancel(); });
    const Qt::DropAction result = view.startDrag(source);
    view.removeEventFilter(&observer);
    QCOMPARE(result, Qt::IgnoreAction);
    QVERIFY(observed);
    QVERIFY(temporaryPin);
    QVERIFY(nestedDragRejected);
    QVERIFY(keptWhileScrolled);
    QVERIFY(keptWhileFolded);
    QCOMPARE(view.isItemPinned(source), changePin ? !alreadyPinned : alreadyPinned);
    QCOMPARE(model.rowCount(parent), 120);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(dropped.count(), 0);
    view.setItemPinned(source, false);
    view.flushPendingRelayout();
    QVERIFY(!(cells ? view.cellWidget(source)
        : view.widgetForIndex(QModelIndex(source).siblingAtColumn(0))));
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestTreeTableViewInteraction::nativeDragCancelPreservesSourceIdentity_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("alreadyPinned");
    for (bool cells : {false, true}) {
        for (bool pinned : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (pinned ? "-pinned" : "-unpinned");
            QTest::newRow(name.constData()) << cells << pinned;
        }
    }
}

void TestTreeTableViewInteraction::nativeDragCancelPreservesSourceIdentity()
{
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
        QSKIP("This test requires the native Windows drag event loop.");
    QFETCH(bool, cells);
    QFETCH(bool, alreadyPinned);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 120; ++i)
        parentItems.first()->appendRow(row(QStringLiteral("child-%1").arg(i)));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setWindowTitle(QStringLiteral("VirtualTreeTableView - Native Drag Acceptance"));
    view.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QVERIFY(QTest::qWaitForWindowActive(&view));
    const QModelIndex parent = model.index(0, 0);
    const QPersistentModelIndex source(model.index(0, 1, parent));
    view.expand(parent);
    view.setCurrentIndex(source);
    view.setFocus();
    view.setOverscan(0, 0);
    view.setDragEnabled(true);
    view.setDragDropActions(Qt::MoveAction);
    view.setDefaultDropAction(Qt::MoveAction);
    view.setMoveRemovesSourceRows(true);
    view.setItemPinned(source, alreadyPinned);
    view.flushPendingRelayout();
    const auto sourceWidget = [&]() {
        return cells ? view.cellWidget(source)
                     : view.widgetForIndex(QModelIndex(source).siblingAtColumn(0));
    };
    QPointer<QWidget> widget(sourceWidget());
    QVERIFY(widget);
    qRegisterMetaType<Qt::DropAction>();
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy dropped(&view, &VirtualItemView::itemDropped);
    bool callbackRan = false;
    bool activeDragSeen = false;
    bool sourcePinned = false;
    bool scrolledIdentityKept = false;
    bool foldedIdentityKept = false;
    QTimer cancelTimer;
    cancelTimer.setSingleShot(true);
    cancelTimer.setTimerType(Qt::PreciseTimer);
    const auto cancelDrag = [&]() {
        if (callbackRan)
            return;
        callbackRan = true;
        QDrag *drag = view.findChild<QDrag *>();
        activeDragSeen = drag && drag->mimeData() && !drag->mimeData()->formats().isEmpty();
        sourcePinned = view.isItemPinned(source);
        view.scrollTo(model.index(80, 0, parent), VirtualItemView::PositionAtTop);
        view.flushPendingRelayout();
        scrolledIdentityKept = widget && sourceWidget() == widget.data();
        view.collapse(parent);
        view.flushPendingRelayout();
        foldedIdentityKept = widget && sourceWidget() == widget.data();
        QDrag::cancel();
    };
    connect(&cancelTimer, &QTimer::timeout, &view, cancelDrag);
#ifdef Q_OS_WIN
    const HWND nativeWindow = reinterpret_cast<HWND>(view.winId());
    view.raise();
    ShowWindow(nativeWindow, SW_SHOWNORMAL);
    SetForegroundWindow(nativeWindow);
    const int foregroundWait = qBound(3000,
        qEnvironmentVariableIntValue("VIV_NATIVE_FOREGROUND_WAIT_MS"), 60000);
    if (!QTest::qWaitFor([&]() { return GetForegroundWindow() == nativeWindow; }, foregroundWait))
        QSKIP("The test window cannot acquire the native foreground; native drag is unverified.");
    const QPoint cursorPosition = view.viewport()->mapToGlobal(view.cellRect(source).center());
    QCursor::setPos(cursorPosition);
    const POINT nativePoint{cursorPosition.x(), cursorPosition.y()};
    QCOMPARE(GetAncestor(WindowFromPoint(nativePoint), GA_ROOT), nativeWindow);
    INPUT press = {};
    press.type = INPUT_MOUSE;
    press.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    QCOMPARE(SendInput(1, &press, sizeof(INPUT)), UINT(1));
    static std::function<void()> nativeCancel;
    nativeCancel = cancelDrag;
    const UINT_PTR nativeTimer = SetTimer(nativeWindow,
        reinterpret_cast<UINT_PTR>(&nativeCancel), 100,
        [](HWND window, UINT, UINT_PTR timer, DWORD) {
            KillTimer(window, timer);
            if (nativeCancel)
                nativeCancel();
        });
    std::atomic<bool> dragReturned{false};
    std::atomic<bool> escapePosted{false};
    std::thread escapeThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!dragReturned.load()) {
            if (GetForegroundWindow() == nativeWindow) {
                INPUT escape[2] = {};
                escape[0].type = INPUT_KEYBOARD;
                escape[0].ki.wVk = VK_ESCAPE;
                escape[1] = escape[0];
                escape[1].ki.dwFlags = KEYEVENTF_KEYUP;
                escapePosted.store(SendInput(2, escape, sizeof(INPUT)) == 2);
            }
        }
        INPUT release = {};
        release.type = INPUT_MOUSE;
        release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &release, sizeof(INPUT));
    });
#endif
    cancelTimer.start(100);
    const Qt::DropAction result = view.startDrag(source);
#ifdef Q_OS_WIN
    dragReturned.store(true);
    escapeThread.join();
    KillTimer(nativeWindow, nativeTimer);
    nativeCancel = {};
    QVERIFY(nativeTimer);
    qInfo() << "native-drag Escape posted" << escapePosted.load();
#endif
    QVERIFY(callbackRan);
    QVERIFY(activeDragSeen);
    QCOMPARE(result, Qt::IgnoreAction);
    QVERIFY(sourcePinned);
    QVERIFY(scrolledIdentityKept);
    QVERIFY(foldedIdentityKept);
    QVERIFY(source.isValid());
    QCOMPARE(model.rowCount(parent), 120);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(dropped.count(), 0);
    QCOMPARE(view.isItemPinned(source), alreadyPinned);
    view.setItemPinned(source, false);
    view.flushPendingRelayout();
    QVERIFY(!sourceWidget());
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestTreeTableViewInteraction::pinSoftCapKeepsTreeWidgets_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::pinSoftCapKeepsTreeWidgets()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 240; ++i)
        parentItems.first()->appendRow(row(QStringLiteral("child %1").arg(i)));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setFocus();
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    view.flushPendingRelayout();
    const auto widgetFor = [&](const QModelIndex &cell) {
        return cells ? view.cellWidget(cell) : view.widgetForIndex(cell.siblingAtColumn(0));
    };
    QList<QModelIndex> pins;
    for (int row = 80; row < 84; ++row)
        pins.append(model.index(row, 1, parent));
    PinWarningCapture warnings;
    view.setMaxPinnedItems(1);
    QCOMPARE(view.maxPinnedItems(), 1);
    view.setItemPinned(pins.at(0));
    QCOMPARE(warnings.messages.size(), 0);
    view.setItemPinned(pins.at(1));
    QCOMPARE(warnings.messages.size(), 1);
    QVERIFY(warnings.messages.first().contains(QStringLiteral("2 items are pinned")));
    view.setItemPinned(pins.at(2));
    view.setItemPinned(pins.at(2));
    QCOMPARE(warnings.messages.size(), 1);
    view.setMaxPinnedItems(0);
    QCOMPARE(view.maxPinnedItems(), 0);
    view.setItemPinned(pins.at(3));
    view.setMaxPinnedItems(-1);
    QCOMPARE(view.maxPinnedItems(), -1);
    QCOMPARE(warnings.messages.size(), 1);
    view.setMaxPinnedItems(1);
    QCOMPARE(warnings.messages.size(), 2);
    QVERIFY(warnings.messages.last().contains(QStringLiteral("4 items are pinned")));
    view.flushPendingRelayout();
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(4));
    QVector<QPointer<QWidget>> widgets;
    for (const QModelIndex &pin : pins) {
        QVERIFY(view.isItemPinned(pin));
        QVERIFY(widgetFor(pin));
        widgets.append(widgetFor(pin));
    }
    view.scrollTo(model.index(150, 0, parent), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    view.collapse(parent);
    view.flushPendingRelayout();
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(4));
    QCOMPARE(warnings.messages.size(), 2);
    for (int i = 0; i < pins.size(); ++i) {
        QVERIFY(widgets.at(i));
        QCOMPARE(widgetFor(pins.at(i)), widgets.at(i).data());
        QVERIFY(widgets.at(i)->visibleRegion().isEmpty());
        view.unpinWidget(widgets.at(i));
    }
    view.flushPendingRelayout();
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
    for (const QModelIndex &pin : pins) {
        QVERIFY(!view.isItemPinned(pin));
        QVERIFY(!widgetFor(pin));
    }
}

void TestTreeTableViewInteraction::plainAdapterEntryKeepsInstalledTreeAdapters_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::plainAdapterEntryKeepsInstalledTreeAdapters()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    TestAdapter rejected(28);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    if (cells)
        view.setTableAdapter(new RowAdapter, true);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 1, parent);
    view.expand(parent);
    view.flushPendingRelayout();
    TableWidgetAdapter *const installedRows = view.tableAdapter();
    CellWidgetAdapter *const installedCells = view.cellAdapter();
    QPointer<QWidget> widget(cells ? view.cellWidget(child)
                                  : view.widgetForIndex(child.siblingAtColumn(0)));
    QVERIFY(widget);
    QTest::ignoreMessage(QtWarningMsg,
        "VirtualTableView::setAdapter(): the adapter is not a TableWidgetAdapter; the "
        "installed adapter is kept (a table creates and lays out its rows through "
        "TableWidgetAdapter)");
    static_cast<VirtualItemView &>(view).setAdapter(&rejected);
    view.flushPendingRelayout();
    QCOMPARE(view.tableAdapter(), installedRows);
    QCOMPARE(view.adapter(), static_cast<WidgetAdapter *>(installedRows));
    QCOMPARE(view.cellAdapter(), installedCells);
    QCOMPARE(view.usesItemWidgets(), !cells);
    QCOMPARE(rejected.createdCount(), 0);
    QCOMPARE(rejected.bindCount(), 0);
    QVERIFY(widget);
    QCOMPARE(cells ? view.cellWidget(child) : view.widgetForIndex(child.siblingAtColumn(0)),
             widget.data());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(child).center()), child);
    view.collapse(parent);
    view.flushPendingRelayout();
    view.expand(parent);
    view.flushPendingRelayout();
    QVERIFY(cells ? view.cellWidget(child) : view.widgetForIndex(child.siblingAtColumn(0)));
    QCOMPARE(rejected.createdCount(), 0);
    QCOMPARE(rejected.bindCount(), 0);
}

void TestTreeTableViewInteraction::clearingFrozenColumnsRestoresTreePaneQueries_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::clearingFrozenColumnsRestoresTreePaneQueries()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("tail")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setDefaultColumnWidth(160);
    view.setFrozenColumns({0});
    view.setFrozenRightColumns({2});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 1, parent);
    view.expand(parent);
    view.setCurrentIndex(child);
    view.setHorizontalOffset(17);
    view.flushPendingRelayout();
    QCOMPARE(view.frozenColumns(), QVector<int>{0});
    QCOMPARE(view.frozenRightColumns(), QVector<int>{2});
    QCOMPARE(view.paneTypeForColumn(0), TablePane::Type::FrozenLeft);
    QCOMPARE(view.paneTypeForColumn(2), TablePane::Type::FrozenRight);
    QCOMPARE(view.columnGeometry(0).viewportX, 0);
    view.clearFrozenColumns();
    view.flushPendingRelayout();
    QVERIFY(view.frozenColumns().isEmpty());
    QVERIFY(view.frozenRightColumns().isEmpty());
    QVERIFY(view.paneSpecs().isEmpty());
    QCOMPARE(view.panes().size(), 1);
    QCOMPARE(view.panes().first().type, TablePane::Type::Scrollable);
    QCOMPARE(view.scrollGroups(), QVector<int>{0});
    QCOMPARE(view.primaryScrollGroup(), 0);
    for (int column = 0; column < 3; ++column) {
        QVERIFY(!view.isColumnFrozen(column));
        QCOMPARE(view.paneIndexOfColumn(column), 0);
        QCOMPARE(view.paneTypeForColumn(column), TablePane::Type::Scrollable);
    }
    QCOMPARE(view.columnGeometry(0).viewportX, -17);
    QCOMPARE(view.currentIndex(), child);
    QVERIFY(view.isExpanded(parent));
    QCOMPARE(view.frozenRows(), 1);
    QCOMPARE(view.frozenBottomRows(), 1);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(child).center()), child);
    const QPoint marker(view.columnGeometry(0).viewportX + view.indentation() + 9,
                        view.visualRect(child).center().y());
    QCOMPARE(view.grab().toImage().pixelColor(view.viewport()->pos() + marker), QColor(230, 180, 10));
    QSignalSpy geometry(&view, &VirtualTableView::columnGeometryChanged);
    view.clearFrozenColumns();
    view.flushPendingRelayout();
    QCOMPARE(geometry.count(), 0);
    view.setColumnHidden(2, true);
    view.flushPendingRelayout();
    QCOMPARE(view.paneIndexOfColumn(2), -1);
    view.setColumnHidden(2, false);
    view.flushPendingRelayout();
    QCOMPARE(view.paneIndexOfColumn(2), 0);
    QVERIFY(!view.isColumnFrozen(2));

    view.setFrozenColumns({0});
    view.setFrozenRightColumns({2});
    const QVector<TablePaneSpec> explicitPanes = {
        {{0}, PaneScroll::Frozen, 0},
        {{1}, PaneScroll::Scrollable, 7},
        {{2}, PaneScroll::Frozen, 0}};
    view.setPanes(explicitPanes);
    view.flushPendingRelayout();
    QVERIFY(view.frozenColumns().isEmpty());
    QVERIFY(view.frozenRightColumns().isEmpty());
    QCOMPARE(view.primaryScrollGroup(), 7);
    QCOMPARE(view.scrollGroups(), QVector<int>{7});
    QVERIFY(view.isColumnFrozen(0));
    QVERIFY(view.isColumnFrozen(2));
    const QVector<TablePane> explicitGeometry = view.panes();
    const QRect explicitChild = view.cellRect(child);
    geometry.clear();
    view.clearFrozenColumns();
    view.flushPendingRelayout();
    QCOMPARE(geometry.count(), 0);
    QCOMPARE(view.paneSpecs(), explicitPanes);
    QCOMPARE(view.panes(), explicitGeometry);
    QCOMPARE(view.cellRect(child), explicitChild);

    view.setFrozenColumns({1});
    view.setFrozenRightColumns({0});
    view.clearFrozenColumns();
    view.flushPendingRelayout();
    QVERIFY(view.frozenColumns().isEmpty());
    QVERIFY(view.frozenRightColumns().isEmpty());
    QCOMPARE(view.paneSpecs(), explicitPanes);
    QCOMPARE(view.panes(), explicitGeometry);
    const QRect explicitHit = view.cellRect(child).intersected(explicitGeometry.at(1).viewportRect);
    QVERIFY(!explicitHit.isEmpty());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(explicitHit.center()), child);
    view.setPanes({});
    view.flushPendingRelayout();
    QVERIFY(view.paneSpecs().isEmpty());
    QCOMPARE(view.panes().size(), 1);
    QCOMPARE(view.primaryScrollGroup(), 0);
    QCOMPARE(view.scrollGroups(), QVector<int>{0});
    for (int column = 0; column < 3; ++column) {
        QVERIFY(!view.isColumnFrozen(column));
        QCOMPARE(view.paneIndexOfColumn(column), 0);
        QCOMPARE(view.paneTypeForColumn(column), TablePane::Type::Scrollable);
    }
    QCOMPARE(view.currentIndex(), child);
    QVERIFY(view.isExpanded(parent));
    QCOMPARE(view.frozenRows(), 1);
    QCOMPARE(view.frozenBottomRows(), 1);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(child).center()), child);
    const QPoint restoredMarker(view.columnGeometry(0).viewportX + view.indentation() + 9,
                                view.visualRect(child).center().y());
    QCOMPARE(view.grab().toImage().pixelColor(view.viewport()->pos() + restoredMarker),
             QColor(230, 180, 10));
    geometry.clear();
    view.setPanes({});
    view.clearFrozenColumns();
    view.flushPendingRelayout();
    QCOMPARE(geometry.count(), 0);
}

void TestTreeTableViewInteraction::scrollHintsPositionExpandedNodes_data()
{
    rowHeightsAndScrollAnchorsFollowNodes_data();
}

void TestTreeTableViewInteraction::scrollHintsPositionExpandedNodes()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 240; ++i)
        parentItems.first()->appendRow(row(QStringLiteral("child %1").arg(i)));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    view.setDepthRowSpacing(1, 5);
    const QModelIndex target = model.index(80, 1, parent);
    const qsizetype visible = view.visibilityIndex()->visibleRowForIndex(target);
    view.setRowHeight(visible, 44);
    view.flushPendingRelayout();
    for (VirtualItemView::ScrollHint hint : {VirtualItemView::EnsureVisible,
             VirtualItemView::PositionAtTop, VirtualItemView::PositionAtBottom,
             VirtualItemView::PositionAtCenter}) {
        view.setVerticalOffset(0);
        view.scrollTo(target, hint);
        view.flushPendingRelayout();
        const QRect pane = view.itemPaneRect(ItemPane::Type::Scrollable);
        const QRect rect = view.visualRect(target);
        QCOMPARE(rect.height(), 44);
        QVERIFY(rect.top() >= pane.top());
        QVERIFY(rect.bottom() <= pane.bottom());
        if (hint == VirtualItemView::PositionAtTop)
            QCOMPARE(rect.top(), pane.top());
        if (hint == VirtualItemView::PositionAtBottom)
            QCOMPARE(rect.bottom(), pane.bottom());
        if (hint == VirtualItemView::PositionAtCenter)
            QVERIFY(qAbs(rect.center().y() - pane.center().y()) <= 1);
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     view.cellRect(target).center()), target);
        auto *header = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
        QVERIFY(header);
        QWidget *section = header->sectionWidget(int(visible));
        QVERIFY(section);
        QCOMPARE(section->mapTo(&view, QPoint()).y() - view.viewport()->y(), rect.top());
        const qint64 offset = view.verticalOffset();
        view.scrollTo(target, VirtualItemView::EnsureVisible);
        QCOMPARE(view.verticalOffset(), offset);
    }
    view.setVerticalOffset(view.maximumVerticalOffset());
    view.scrollTo(target, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    QCOMPARE(view.visualRect(target).top(), view.itemPaneRect(ItemPane::Type::Scrollable).top());
    const qint64 offset = view.verticalOffset();
    QStandardItemModel foreign;
    foreign.appendRow(row(QStringLiteral("foreign")));
    view.scrollTo(QModelIndex(), VirtualItemView::PositionAtTop);
    view.scrollTo(foreign.index(0, 0), VirtualItemView::PositionAtTop);
    QCOMPARE(view.verticalOffset(), offset);
    if (frozen) {
        view.scrollTo(parent, VirtualItemView::PositionAtBottom);
        view.scrollTo(model.index(239, 0, parent), VirtualItemView::PositionAtTop);
        QCOMPARE(view.verticalOffset(), offset);
    }
    view.collapse(parent);
    view.flushPendingRelayout();
    const qint64 foldedOffset = view.verticalOffset();
    view.scrollTo(target, VirtualItemView::PositionAtCenter);
    QCOMPARE(view.verticalOffset(), foldedOffset);
}

void TestTreeTableViewInteraction::itemWheelStepsAcrossVariableTreeRows_data()
{
    rowHeightsAndScrollAnchorsFollowNodes_data();
}

void TestTreeTableViewInteraction::itemWheelStepsAcrossVariableTreeRows()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 240; ++i) {
        auto items = row(QStringLiteral("child %1").arg(i));
        if (i == 80) {
            for (int grandchild = 0; grandchild < 3; ++grandchild)
                items.first()->appendRow(row(QStringLiteral("grandchild %1").arg(grandchild)));
        }
        parentItems.first()->appendRow(items);
    }
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setRowSpacing(3);
    view.setDepthRowSpacing(1, 5);
    view.setDepthRowSpacing(2, 9);
    if (frozen) {
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex branch = model.index(80, 0, parent);
    view.expand(parent);
    view.expand(branch);
    const QModelIndex first = model.index(0, 0, branch);
    const QModelIndex second = model.index(1, 0, branch);
    const QModelIndex third = model.index(2, 0, branch);
    const QModelIndex sibling = model.index(81, 0, parent);
    const QModelIndex nodes[] = {branch, first, second, third, sibling};
    const int heights[] = {44, 19, 55, 27, 31};
    for (int i = 0; i < 5; ++i)
        view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(nodes[i]), heights[i]);
    view.setCurrentIndex(branch.siblingAtColumn(1));
    view.setWheelScrollItems(2);
    view.scrollTo(branch, VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    const qint64 start = view.verticalOffset();
    const int paneTop = view.itemPaneRect(ItemPane::Type::Scrollable).top();
    const auto sendWheel = [&](int angle, const QPoint &pixel = QPoint()) {
        QWheelEvent event(QPointF(20, 80), QPointF(view.viewport()->mapToGlobal(QPoint(20, 80))),
                          pixel, QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &event);
        QVERIFY(event.isAccepted());
        view.flushPendingRelayout();
    };
    sendWheel(-120);
    QCOMPARE(view.verticalOffset(), start + 44 + 5 + 19 + 9);
    QCOMPARE(view.visualRect(second).top(), paneTop);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(second.siblingAtColumn(1)).center()), second.siblingAtColumn(1));
    sendWheel(120);
    QCOMPARE(view.verticalOffset(), start);
    QCOMPARE(view.visualRect(branch).top(), paneTop);
    sendWheel(-240);
    QCOMPARE(view.visualRect(sibling).top(), paneTop);
    QCOMPARE(view.verticalOffset(), start + 44 + 5 + 19 + 9 + 55 + 9 + 27 + 9);
    sendWheel(240);
    QCOMPARE(view.verticalOffset(), start);
    sendWheel(-120, QPoint(0, -4));
    QCOMPARE(view.verticalOffset(), start + 4);
    sendWheel(-120);
    QCOMPARE(view.visualRect(second).top(), paneTop - 4);
    sendWheel(120);
    QCOMPARE(view.verticalOffset(), start + 4);
    QCOMPARE(view.currentIndex(), branch.siblingAtColumn(1));
    view.setVerticalOffset(start);
    view.flushPendingRelayout();
    const int partialReference = frozen ? 28 : 44;
    sendWheel(-60);
    QCOMPARE(view.verticalOffset(), start + partialReference);
    sendWheel(60);
    QCOMPARE(view.verticalOffset(), start);
    QWheelEvent tiny(QPointF(20, 80), QPointF(view.viewport()->mapToGlobal(QPoint(20, 80))),
                     QPoint(), QPoint(0, -1), Qt::NoButton, Qt::NoModifier,
                     Qt::NoScrollPhase, false);
    tiny.setAccepted(false);
    QApplication::sendEvent(view.viewport(), &tiny);
    QVERIFY(!tiny.isAccepted());
    QCOMPARE(view.verticalOffset(), start);
    view.setVerticalOffset(start + 46);
    view.flushPendingRelayout();
    QCOMPARE(view.visualRect(first).top(), paneTop + 3);
    QVERIFY(!static_cast<const VirtualItemView &>(view).indexAt(QPoint(150, paneTop + 1)).isValid());
    sendWheel(-120);
    QCOMPARE(view.verticalOffset(), start + 46 + 44 + 5 + 19 + 9);
    QCOMPARE(view.visualRect(second).top(), paneTop - 46);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(QPoint(150, paneTop + 2)),
             second.siblingAtColumn(1));
    sendWheel(120);
    QCOMPARE(view.verticalOffset(), start + 46);
    QCOMPARE(view.currentIndex(), branch.siblingAtColumn(1));
    view.collapse(branch);
    view.scrollTo(branch, VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    sendWheel(-120);
    QCOMPARE(view.visualRect(model.index(82, 0, parent)).top(), paneTop);
    view.setWheelScrollItems(std::numeric_limits<int>::max());
    sendWheel(-120);
    QCOMPARE(view.verticalOffset(), view.maximumVerticalOffset());
    sendWheel(120);
    QCOMPARE(view.verticalOffset(), qint64(0));
    QStandardItemModel fullyFrozen;
    auto frozenParent = row(QStringLiteral("frozen parent"));
    frozenParent.first()->appendRow(row(QStringLiteral("frozen child")));
    fullyFrozen.appendRow(frozenParent);
    fullyFrozen.appendRow(row(QStringLiteral("frozen tail")));
    view.setFrozenRows(0);
    view.setFrozenBottomRows(0);
    view.setModel(&fullyFrozen);
    view.expand(fullyFrozen.index(0, 0));
    const QModelIndex frozenChild = fullyFrozen.index(0, 1, fullyFrozen.index(0, 0));
    view.setCurrentIndex(frozenChild);
    for (const QPair<int, int> counts : {qMakePair(3, 0), qMakePair(0, 3), qMakePair(1, 2)}) {
        view.setFrozenRows(0);
        view.setFrozenBottomRows(0);
        view.setFrozenRows(counts.first);
        view.setFrozenBottomRows(counts.second);
        view.flushPendingRelayout();
        QCOMPARE(view.frozenRows() + view.frozenBottomRows(), qsizetype(3));
        QCOMPARE(view.maximumVerticalOffset(), qint64(0));
        for (int angle : {-120, 120, -60, 60}) {
            sendWheel(angle);
            QCOMPARE(view.verticalOffset(), qint64(0));
        }
        sendWheel(-120, QPoint(0, -7));
        sendWheel(120, QPoint(0, 7));
        QCOMPARE(view.verticalOffset(), qint64(0));
        QCOMPARE(view.currentIndex(), frozenChild);
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(frozenChild).center()),
                 frozenChild);
    }
    view.setModel(nullptr);
}

void TestTreeTableViewInteraction::wheelParametersScrollExpandedNodes_data()
{
    rowHeightsAndScrollAnchorsFollowNodes_data();
}

void TestTreeTableViewInteraction::wheelParametersScrollExpandedNodes()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 240; ++i) {
        auto items = row(QStringLiteral("child %1").arg(i));
        for (int column = 3; column < 12; ++column)
            items.append(new QStandardItem(QStringLiteral("column %1").arg(column)));
        parentItems.first()->appendRow(items);
    }
    for (int column = 3; column < 12; ++column)
        parentItems.append(new QStandardItem(QStringLiteral("column %1").arg(column)));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    const QModelIndex target = model.index(80, 3, parent);
    view.scrollTo(target, VirtualItemView::PositionAtCenter);
    view.flushPendingRelayout();
    const auto sendWheel = [&](const QPoint &pixel, const QPoint &angle,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                              const QPoint &position = QPoint(20, 80)) {
        QWheelEvent event(QPointF(position), QPointF(view.viewport()->mapToGlobal(position)),
                          pixel, angle, Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &event);
        QVERIFY(event.isAccepted());
        view.flushPendingRelayout();
    };
    view.setWheelScrollPixels(43);
    QCOMPARE(view.wheelScrollPixels(), 43);
    QCOMPARE(view.wheelScrollMode(), VirtualItemView::WheelScrollMode::Pixels);
    qint64 offset = view.verticalOffset();
    sendWheel(QPoint(), QPoint(0, -120));
    QCOMPARE(view.verticalOffset(), offset + 43);
    sendWheel(QPoint(), QPoint(0, 120));
    QCOMPARE(view.verticalOffset(), offset);
    sendWheel(QPoint(0, -7), QPoint(0, -120));
    QCOMPARE(view.verticalOffset(), offset + 7);
    view.setWheelScrollItems(2);
    QCOMPARE(view.wheelScrollItems(), 2);
    QCOMPARE(view.wheelScrollMode(), VirtualItemView::WheelScrollMode::Items);
    offset = view.verticalOffset();
    sendWheel(QPoint(), QPoint(0, -120));
    QCOMPARE(view.verticalOffset(), offset + 56);
    sendWheel(QPoint(), QPoint(0, 120));
    QCOMPARE(view.verticalOffset(), offset);
    sendWheel(QPoint(0, -9), QPoint());
    QCOMPARE(view.verticalOffset(), offset + 9);
    view.setWheelScrollPixels(-1);
    QCOMPARE(view.wheelScrollPixels(), 1);
    view.setWheelScrollItems(0);
    QCOMPARE(view.wheelScrollItems(), 1);
    view.setWheelScrollMode(VirtualItemView::WheelScrollMode::Pixels);
    QCOMPARE(view.wheelScrollMode(), VirtualItemView::WheelScrollMode::Pixels);
    view.setHorizontalWheelPixels(37);
    QCOMPARE(view.horizontalWheelPixels(), 37);
    const qint64 vertical = view.verticalOffset();
    sendWheel(QPoint(), QPoint(-120, 0));
    QCOMPARE(view.horizontalOffset(), qint64(37));
    sendWheel(QPoint(-19, 0), QPoint(-120, 0));
    QCOMPARE(view.horizontalOffset(), qint64(56));
    sendWheel(QPoint(), QPoint(0, -120), Qt::ShiftModifier);
    QCOMPARE(view.horizontalOffset(), qint64(93));
    sendWheel(QPoint(0, -11), QPoint(), Qt::ShiftModifier);
    QCOMPARE(view.horizontalOffset(), qint64(104));
    QCOMPARE(view.verticalOffset(), vertical);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(target).center()), target);
    view.setHorizontalWheelPixels(0);
    QCOMPARE(view.horizontalWheelPixels(), 1);
    view.setHorizontalOffset(0);
    sendWheel(QPoint(), QPoint(120, 0));
    QCOMPARE(view.horizontalOffset(), qint64(0));
    view.setVerticalOffset(0);
    sendWheel(QPoint(), QPoint(0, 120));
    QCOMPARE(view.verticalOffset(), qint64(0));

    view.setHorizontalWheelPixels(37);
    view.scrollTo(target, VirtualItemView::PositionAtCenter);
    const QModelIndex secondaryCell = model.index(80, 6, parent);
    view.setCurrentIndex(secondaryCell);
    for (const QPair<int, int> groups : {qMakePair(7, 2), qMakePair(2, 7)}) {
        view.setPanes({{{0}, PaneScroll::Frozen, 0},
                       {{1, 2, 3, 4, 5}, PaneScroll::Scrollable, groups.first},
                       {{6, 7, 8, 9, 10}, PaneScroll::Scrollable, groups.second},
                       {{11}, PaneScroll::Frozen, 0}});
        view.setHorizontalOffset(0);
        view.setHorizontalOffset(groups.second, 29);
        view.flushPendingRelayout();
        QCOMPARE(view.primaryScrollGroup(), groups.first);
        QCOMPARE(view.scrollGroups(), (QVector<int>{2, 7}));
        QVERIFY(view.maximumHorizontalOffset(groups.first) >= 55);
        QVERIFY(view.maximumHorizontalOffset(groups.second) >= 29);
        const qint64 verticalBefore = view.verticalOffset();
        const QRect secondaryBefore = view.cellRect(secondaryCell);
        const int leftBefore = view.columnGeometry(0).viewportX;
        const int rightBefore = view.columnGeometry(11).viewportX;
        const QVector<TablePane> panes = view.panes();
        QCOMPARE(panes.size(), 4);
        for (const TablePane &pane : panes) {
            const QPoint position(pane.viewportRect.center().x(), secondaryBefore.center().y());
            sendWheel(QPoint(), QPoint(-120, 0), Qt::NoModifier, position);
            QCOMPARE(view.horizontalOffset(), qint64(37));
            QCOMPARE(view.horizontalOffset(groups.first), qint64(37));
            sendWheel(QPoint(-11, 0), QPoint(-120, 0), Qt::NoModifier, position);
            QCOMPARE(view.horizontalOffset(groups.first), qint64(48));
            sendWheel(QPoint(0, -7), QPoint(), Qt::ShiftModifier, position);
            QCOMPARE(view.horizontalOffset(groups.first), qint64(55));
            QCOMPARE(view.horizontalOffset(groups.second), qint64(29));
            QCOMPARE(view.verticalOffset(), verticalBefore);
            QCOMPARE(view.columnGeometry(0).viewportX, leftBefore);
            QCOMPARE(view.columnGeometry(11).viewportX, rightBefore);
            QCOMPARE(view.cellRect(secondaryCell), secondaryBefore);
            QCOMPARE(view.currentIndex(), secondaryCell);
            const QRect hitRect = secondaryBefore.intersected(panes.at(2).viewportRect);
            QVERIFY(!hitRect.isEmpty());
            QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(hitRect.center()),
                     secondaryCell);
            sendWheel(QPoint(55, 0), QPoint(), Qt::NoModifier, position);
            QCOMPARE(view.horizontalOffset(groups.first), qint64(0));
        }
    }
}

void TestTreeTableViewInteraction::lifecycleLogTracksTreeWidgets_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::lifecycleLogTracksTreeWidgets()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    for (int i = 0; i < 600; ++i)
        parentItems.first()->appendRow(row(QStringLiteral("child %1").arg(i)));
    model.appendRow(parentItems);
    VirtualTreeTableView view;
    QVERIFY(!view.isLifecycleLoggingEnabled());
    view.setLifecycleLoggingEnabled(true);
    configure(&view, &model, cells);
    view.setFocus();
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();
    const auto hasEvent = [&view](const QString &prefix) {
        for (const QString &entry : view.lifecycleLog()) {
            if (entry.startsWith(prefix))
                return true;
        }
        return false;
    };
    QVERIFY(hasEvent(cells ? QStringLiteral("create cell type=")
                           : QStringLiteral("create type=")));
    QVERIFY(hasEvent(cells ? QStringLiteral("bind cell row=0 column=1")
                           : QStringLiteral("bind row=0")));
    const QModelIndex child = model.index(0, 1, model.index(0, 0));
    view.clearLifecycleLog();
    QVERIFY(view.lifecycleLog().isEmpty());
    QVERIFY(view.isLifecycleLoggingEnabled());
    model.setData(child, QStringLiteral("changed child"));
    QVERIFY(hasEvent(cells ? QStringLiteral("rebind cell row=0 column=1")
                           : QStringLiteral("rebind row=0")));
    view.setItemPinned(child);
    QVERIFY(hasEvent(QStringLiteral("pin row=0")));
    view.setItemPinned(child, false);
    QVERIFY(hasEvent(QStringLiteral("unpin row=0")));
    QSignalSpy updates(&view, &VirtualItemView::virtualizationUpdated);
    for (int step = 0; step < 40; ++step) {
        view.verticalScrollBar()->setValue((step % 2 == 0 ? 120 : 300) * 28);
        view.flushPendingRelayout();
    }
    QVERIFY(updates.count() > 0);
    QCOMPARE(view.lifecycleLog().size(), VirtualItemView::kLifecycleLogCapacity);
    QVERIFY(hasEvent(cells ? QStringLiteral("reuse cell type=")
                           : QStringLiteral("reuse type=")));
    QVERIFY(hasEvent(cells ? QStringLiteral("bind cell row=")
                           : QStringLiteral("bind row=")));
    QVERIFY(hasEvent(cells ? QStringLiteral("unbind cell row=")
                           : QStringLiteral("unbind row=")));
    QVERIFY(hasEvent(cells ? QStringLiteral("recycle cell type=")
                           : QStringLiteral("recycle type=")));
    view.setLifecycleLoggingEnabled(false);
    QVERIFY(!view.isLifecycleLoggingEnabled());
    QVERIFY(view.lifecycleLog().isEmpty());
    view.verticalScrollBar()->setValue(0);
    view.flushPendingRelayout();
    view.collapse(model.index(0, 0));
    view.flushPendingRelayout();
    QVERIFY(view.lifecycleLog().isEmpty());
    view.setLifecycleLoggingEnabled(true);
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();
    QVERIFY(!view.lifecycleLog().isEmpty());
    QVERIFY(view.lifecycleLog().size() <= VirtualItemView::kLifecycleLogCapacity);
}

void TestTreeTableViewInteraction::dragEntryRespectsModelIdentity_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::dragEntryRespectsModelIdentity()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    QStandardItemModel foreign;
    foreign.appendRow(row(QStringLiteral("foreign")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.expand(model.index(0, 0));
    const QModelIndex cell = model.index(0, 2, model.index(0, 0));
    QVERIFY(!view.canStartDrag(cell));
    view.setDragEnabled(true);
    QVERIFY(view.canStartDrag(cell));
    QVERIFY(!view.canStartDrag(QModelIndex()));
    QVERIFY(!view.canStartDrag(foreign.index(0, 2)));
    QCOMPARE(view.startDrag(foreign.index(0, 2)), Qt::IgnoreAction);
    QStandardItem *item = model.itemFromIndex(cell);
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    QVERIFY(!view.canStartDrag(cell));
    QCOMPARE(view.startDrag(cell), Qt::IgnoreAction);
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
    for (Qt::DropAction action : {Qt::CopyAction, Qt::MoveAction, Qt::LinkAction}) {
        view.setDragDropActions(action);
        QCOMPARE(view.dragDropActions(), Qt::DropActions(action));
        QVERIFY(view.canStartDrag(cell));
    }
    view.setDragDropActions(Qt::IgnoreAction);
    QVERIFY(view.canStartDrag(cell));
    view.setDragEnabled(false);
    QVERIFY(!view.canStartDrag(cell));
    view.setModel(nullptr);
    view.setDragEnabled(true);
    QVERIFY(!view.canStartDrag(cell));
}

void TestTreeTableViewInteraction::dragQueryCallbacksRetireRequests_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("scenario");
    const char *names[] = {
        "flags-can-model", "flags-start-root", "flags-start-delete",
        "flags-selection-model", "flags-selection-delete", "drag-can-model",
        "drag-actions-delete", "drag-start-root", "drop-actions-root",
        "drop-start-delete", "mime-start-model", "mime-start-delete"
    };
    for (bool cells : {false, true}) {
        for (int scenario = 0; scenario < int(sizeof(names) / sizeof(names[0])); ++scenario) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-") + names[scenario];
            QTest::newRow(name.constData()) << cells << scenario;
        }
    }
}

void TestTreeTableViewInteraction::dragQueryCallbacksRetireRequests()
{
    QFETCH(bool, cells);
    QFETCH(int, scenario);
    using Stage = DragQueryCallbackModel::Stage;
    DragQueryCallbackModel model;
    auto items = row(QStringLiteral("parent"));
    items.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(items);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    configure(view, &model, cells);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex cell(model.index(0, 2, parent));
    view->expand(parent);
    view->setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    view->setDragEnabled(true);
    if (scenario == 3 || scenario == 4)
        view->setCurrentIndex(cell);

    model.stage = scenario < 5 ? Stage::Flags : scenario < 8 ? Stage::DragActions
        : scenario < 10 ? Stage::DropActions : Stage::Mime;
    model.skipCalls = scenario == 3 || scenario == 4 || scenario == 7 ? 1 : 0;
    const bool destroy = scenario == 2 || scenario == 4 || scenario == 6
        || scenario == 9 || scenario == 11;
    const bool changeRoot = scenario == 1 || scenario == 7 || scenario == 8;
    model.callback = [&]() {
        if (destroy)
            delete view.data();
        else if (changeRoot)
            view->setRootIndex(parent);
        else
            view->setModel(&replacement);
    };
    if (scenario == 0 || scenario == 5)
        QVERIFY(!view->canStartDrag(cell));
    else if (scenario == 6 || scenario == 8)
        QCOMPARE(view->dragDropActions(), Qt::DropActions(Qt::IgnoreAction));
    else
        QCOMPARE(view->startDrag(cell), Qt::IgnoreAction);

    QCOMPARE(model.callbacks, 1);
    QCOMPARE(model.mimeCalls, scenario >= 10 ? 1 : 0);
    QVERIFY(!model.lastMime);
    if (destroy) {
        QVERIFY(!view);
    } else {
        QVERIFY(view);
        QCOMPARE(view->model(), changeRoot ? static_cast<QAbstractItemModel *>(&model)
                                          : static_cast<QAbstractItemModel *>(&replacement));
        QCOMPARE(view->rootIndex(), changeRoot ? QModelIndex(parent) : QModelIndex());
        view->flushPendingRelayout();
        QCOMPARE(view->visibleRowCount(), qsizetype(1));
        const QModelIndex next = view->model()->index(0, 2, view->rootIndex());
        QVERIFY(view->canStartDrag(next));
        QCOMPARE(view->stats().pinnedWidgets, qsizetype(0));
        QCOMPARE(static_cast<const VirtualItemView &>(*view).indexAt(view->cellRect(next).center()), next);
        delete view.data();
    }
}

void TestTreeTableViewInteraction::headerVisibilityAndDimensionsKeepTreeGeometry_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::headerVisibilityAndDimensionsKeepTreeGeometry()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("tail")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setFrozenColumns({0});
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    for (int dimension = 0; dimension < 2; ++dimension) {
        const int height = dimension == 0 ? 39 : 22;
        const int width = dimension == 0 ? 67 : 41;
        view.setHeaderHeight(height);
        view.setVerticalHeaderWidth(width);
        QCOMPARE(view.headerHeight(), height);
        QCOMPARE(view.verticalHeaderWidth(), width);
        for (bool horizontal : {false, true}) {
            for (bool vertical : {false, true}) {
                view.setHorizontalHeaderVisible(horizontal);
                view.setVerticalHeaderVisible(vertical);
                view.flushPendingRelayout();
                settle();
                QCOMPARE(view.isHorizontalHeaderVisible(), horizontal);
                QCOMPARE(view.isVerticalHeaderVisible(), vertical);
                QCOMPARE(view.isVerticalHeaderShown(), vertical);
                QCOMPARE(view.viewport()->x(), view.frameWidth() + (vertical ? width : 0));
                QCOMPARE(view.viewport()->y(), view.frameWidth() + (horizontal ? height : 0));
                if (horizontal)
                    QCOMPARE(view.horizontalHeader()->headerWidget()->height(), height);
                if (vertical)
                    QCOMPARE(view.verticalHeader()->headerWidget()->width(), width);
                const QRect cell = view.cellRect(child.siblingAtColumn(1));
                QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(cell.center()),
                         child.siblingAtColumn(1));
                const QPoint marker(view.columnGeometry(0).viewportX + view.indentation() + 9,
                                    view.visualRect(child).center().y());
                QCOMPARE(view.grab().toImage().pixelColor(view.viewport()->pos() + marker),
                         QColor(230, 180, 10));
            }
        }
    }
    view.setHeaderHeight(-1);
    view.setVerticalHeaderWidth(-1);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.headerHeight(), 0);
    QCOMPARE(view.verticalHeaderWidth(), 0);
    QCOMPARE(view.viewport()->pos(), QPoint(view.frameWidth(), view.frameWidth()));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(child.siblingAtColumn(1)).center()), child.siblingAtColumn(1));
}

void TestTreeTableViewInteraction::activationSignalsRespectModelIdentity_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("noSelection");
    for (bool cells : {false, true}) {
        for (bool noSelection : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (noSelection ? "-no-selection" : "-single-selection");
            QTest::newRow(name.constData()) << cells << noSelection;
        }
    }
}

void TestTreeTableViewInteraction::activationSignalsRespectModelIdentity()
{
    QFETCH(bool, cells);
    QFETCH(bool, noSelection);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentItems);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    view.setSelectionMode(noSelection ? VirtualItemView::SelectionMode::NoSelection
                                      : VirtualItemView::SelectionMode::SingleSelection);
    view.expand(model.index(0, 0));
    const QModelIndex cell = model.index(0, 2, model.index(0, 0));
    QSignalSpy clicked(&view, &VirtualItemView::clicked);
    QSignalSpy activated(&view, &VirtualItemView::activated);
    QSignalSpy doubleClicked(&view, &VirtualItemView::doubleClicked);
    view.activateIndex(cell);
    QCOMPARE(view.currentIndex(), cell);
    QCOMPARE(clicked.size(), 1);
    QCOMPARE(activated.size(), 1);
    QCOMPARE(clicked.first().first().value<QModelIndex>(), cell);
    QCOMPARE(activated.first().first().value<QModelIndex>(), cell);
    QCOMPARE(doubleClicked.size(), 0);
    QCOMPARE(view.selectionModel()->isSelected(cell), !noSelection);
    view.activateIndex(QModelIndex());
    view.activateIndex(replacement.index(0, 0));
    QCOMPARE(view.currentIndex(), cell);
    QCOMPARE(clicked.size(), 1);
    QCOMPARE(activated.size(), 1);
    connect(&view, &VirtualItemView::clicked, &view,
            [&view, &replacement](const QModelIndex &) { view.setModel(&replacement); });
    view.activateIndex(cell);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(clicked.size(), 2);
    QCOMPARE(activated.size(), 1);
}

void TestTreeTableViewInteraction::mouseActivationDistinguishesTreeBranches_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::mouseActivationDistinguishesTreeBranches()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto items = row(QStringLiteral("parent"));
    items.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(items);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex cell = model.index(0, 1);
    QSignalSpy clicked(&view, &VirtualItemView::clicked);
    QSignalSpy doubled(&view, &VirtualItemView::doubleClicked);
    QSignalSpy activated(&view, &VirtualItemView::activated);
    QSignalSpy expanded(&view, &VirtualTreeTableView::expanded);
    const QPoint branch(view.columnGeometry(0).viewportX + view.indentation() / 2,
                        view.visualRect(parent).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, branch);
    QVERIFY(view.isExpanded(parent));
    QCOMPARE(expanded.size(), 1);
    QCOMPARE(clicked.size(), 0);
    QCOMPARE(activated.size(), 0);
    sendHeaderMouse(view.viewport(), QEvent::MouseButtonDblClick, branch,
                    Qt::LeftButton, Qt::LeftButton);
    QVERIFY(view.isExpanded(parent));
    QCOMPARE(expanded.size(), 1);
    QCOMPARE(doubled.size(), 1);
    QCOMPARE(activated.size(), 1);
    QCOMPARE(doubled.first().first().value<QModelIndex>(), parent);
    sendHeaderMouse(view.viewport(), QEvent::MouseButtonDblClick, view.cellRect(cell).center(),
                    Qt::LeftButton, Qt::LeftButton);
    QVERIFY(!view.isExpanded(parent));
    QCOMPARE(doubled.size(), 2);
    QCOMPARE(activated.size(), 2);
    QCOMPARE(doubled.last().first().value<QModelIndex>(), cell);
    QCOMPARE(activated.last().first().value<QModelIndex>(), cell);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      view.cellRect(cell).center());
    QCOMPARE(clicked.size(), 1);
    QCOMPARE(clicked.first().first().value<QModelIndex>(), cell);
    QCOMPARE(activated.size(), 2);
}

void TestTreeTableViewInteraction::activationCallbacksRetireOldRequests_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("trigger");
    QTest::addColumn<bool>("destroyView");
    const char *names[] = {"double-clicked", "api-clicked", "api-activated",
                           "branch-press", "content-double"};
    for (bool cells : {false, true}) {
        for (int trigger = 0; trigger < 5; ++trigger) {
            for (bool destroyView : {false, true}) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + names[trigger] + (destroyView ? "-delete" : "-model");
                QTest::newRow(name.constData()) << cells << trigger << destroyView;
            }
        }
    }
}

void TestTreeTableViewInteraction::activationCallbacksRetireOldRequests()
{
    QFETCH(bool, cells);
    QFETCH(int, trigger);
    QFETCH(bool, destroyView);
    QStandardItemModel model;
    auto items = row(QStringLiteral("parent"));
    items.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(items);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    configure(view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex cell = model.index(0, 1);
    if (trigger < 3)
        view->expand(parent);
    QSignalSpy clicked(view, &VirtualItemView::clicked);
    QSignalSpy doubled(view, &VirtualItemView::doubleClicked);
    QSignalSpy activated(view, &VirtualItemView::activated);
    int calls = 0;
    const auto change = [&] {
        ++calls;
        if (destroyView)
            delete view.data();
        else
            view->setModel(&replacement);
    };
    if (trigger == 0)
        connect(view, &VirtualItemView::doubleClicked, this, change);
    else if (trigger == 1)
        connect(view, &VirtualItemView::clicked, this, change);
    else if (trigger == 2)
        connect(view, &VirtualItemView::activated, this, change);
    else
        connect(view, &VirtualTreeTableView::expanded, this, change);
    if (trigger == 1 || trigger == 2) {
        view->activateIndex(cell);
    } else {
        const QPoint position = trigger == 3
            ? QPoint(view->columnGeometry(0).viewportX + view->indentation() / 2,
                     view->visualRect(parent).center().y())
            : view->cellRect(cell).center();
        sendHeaderMouse(view->viewport(), trigger == 3 ? QEvent::MouseButtonPress
                                                       : QEvent::MouseButtonDblClick,
                        position, Qt::LeftButton, Qt::LeftButton);
    }
    QCOMPARE(calls, 1);
    QCOMPARE(doubled.size(), trigger == 0 ? 1 : 0);
    QCOMPARE(clicked.size(), trigger == 1 || trigger == 2 ? 1 : 0);
    QCOMPARE(activated.size(), trigger == 2 ? 1 : 0);
    if (destroyView) {
        QVERIFY(!view);
    } else {
        QVERIFY(view);
        QCOMPARE(view->model(), static_cast<QAbstractItemModel *>(&replacement));
        view->flushPendingRelayout();
        QCOMPARE(view->visibleRowCount(), qsizetype(1));
        QVERIFY(!view->currentIndex().isValid());
        delete view.data();
    }
}

void TestTreeTableViewInteraction::currentChangeCallbacksCancelActivation_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("policy");
    QTest::addColumn<int>("operation");
    QTest::addColumn<bool>("ownedSelection");
    QTest::addColumn<bool>("destroyView");
    const char *policies[] = {"rows", "items", "none"};
    const char *operations[] = {"current", "activate", "clear"};
    for (bool cells : {false, true}) {
        for (int policy = 0; policy < 3; ++policy) {
            for (int operation = 0; operation < 3; ++operation) {
                for (bool ownedSelection : {false, true}) {
                    for (bool destroyView : {false, true}) {
                        const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                            + policies[policy] + "-" + operations[operation]
                            + (ownedSelection ? "-owned" : "-external")
                            + (destroyView ? "-delete" : "-model");
                        QTest::newRow(name.constData())
                            << cells << policy << operation << ownedSelection << destroyView;
                    }
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::currentChangeCallbacksCancelActivation()
{
    QFETCH(bool, cells);
    QFETCH(int, policy);
    QFETCH(int, operation);
    QFETCH(bool, ownedSelection);
    QFETCH(bool, destroyView);
    QStandardItemModel model;
    auto items = row(QStringLiteral("parent"));
    items.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(items);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    QItemSelectionModel selection(&model);
    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    configure(view, &model, cells);
    if (!ownedSelection)
        view->setSelectionModel(&selection);
    const QPointer<QItemSelectionModel> activeSelection = view->selectionModel();
    view->setSelectionBehavior(policy == 0 ? VirtualItemView::SelectionBehavior::SelectRows
                                           : VirtualItemView::SelectionBehavior::SelectItems);
    view->setSelectionMode(policy == 2 ? VirtualItemView::SelectionMode::NoSelection
                                      : VirtualItemView::SelectionMode::SingleSelection);
    view->expand(model.index(0, 0));
    const QModelIndex cell = model.index(0, 2, model.index(0, 0));
    if (operation == 2)
        view->setCurrentIndex(cell);
    QSignalSpy clicked(view, &VirtualItemView::clicked);
    QSignalSpy activated(view, &VirtualItemView::activated);
    int callbacks = 0;
    connect(activeSelection, &QItemSelectionModel::currentChanged, this,
            [&](const QModelIndex &current, const QModelIndex &) {
        if (callbacks != 0 || (operation == 2 ? current.isValid() : current != cell))
            return;
        ++callbacks;
        if (destroyView)
            delete view.data();
        else
            view->setModel(&replacement);
    });
    if (operation == 1)
        view->activateIndex(cell);
    else
        view->setCurrentIndex(operation == 2 ? QModelIndex() : cell);
    QCOMPARE(callbacks, 1);
    QCOMPARE(clicked.size(), 0);
    QCOMPARE(activated.size(), 0);
    QVERIFY(activeSelection);
    if (ownedSelection)
        QVERIFY(!activeSelection->parent());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(activeSelection.isNull(), ownedSelection);
    if (destroyView) {
        QVERIFY(!view);
    } else {
        QVERIFY(view);
        QCOMPARE(view->model(), static_cast<QAbstractItemModel *>(&replacement));
        QVERIFY(!view->currentIndex().isValid());
        QVERIFY(view->selectionModel()->selectedIndexes().isEmpty());
        view->flushPendingRelayout();
        const QModelIndex next = replacement.index(0, 1);
        view->setCurrentIndex(next);
        QCOMPARE(view->currentIndex(), next);
        QCOMPARE(static_cast<const VirtualItemView &>(*view).indexAt(view->cellRect(next).center()),
                 next);
        delete view.data();
    }
}

void TestTreeTableViewInteraction::publicExpansionApisKeepCanonicalNodeIdentity_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::publicExpansionApisKeepCanonicalNodeIdentity()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    auto branchItems = row(QStringLiteral("branch"));
    branchItems.first()->appendRow(row(QStringLiteral("leaf")));
    parentItems.first()->appendRow(branchItems);
    parentItems.first()->appendRow(row(QStringLiteral("sibling")));
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("other")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex branch = model.index(0, 0, parent);
    const QModelIndex leaf = model.index(0, 0, branch);
    QSignalSpy expanded(&view, &VirtualTreeTableView::expanded);
    QSignalSpy collapsed(&view, &VirtualTreeTableView::collapsed);
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(view.hasChildren(parent.siblingAtColumn(2)));
    QVERIFY(!view.hasChildren(leaf));
    view.expandRecursively(parent.siblingAtColumn(2));
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(5));
    QVERIFY(view.isExpanded(parent.siblingAtColumn(1)));
    QVERIFY(view.isExpanded(branch.siblingAtColumn(2)));
    QCOMPARE(view.itemDepth(leaf.siblingAtColumn(1)), 2);
    QCOMPARE(expanded.size(), 1);
    QCOMPARE(expanded.first().first().value<QModelIndex>(), parent);
    view.collapseAll();
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(!view.isExpanded(parent));
    QVERIFY(!view.isExpanded(branch));
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(leaf), qsizetype(-1));
    QVERIFY(cells ? !view.cellWidget(leaf) : !view.widgetForIndex(leaf));
    view.setExpanded(parent.siblingAtColumn(1), true);
    view.setExpanded(parent.siblingAtColumn(2), true);
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    QCOMPARE(expanded.size(), 2);
    view.toggleExpanded(branch.siblingAtColumn(2));
    QCOMPARE(view.visibleRowCount(), qsizetype(5));
    QCOMPARE(expanded.last().first().value<QModelIndex>(), branch);
    view.setExpanded(branch.siblingAtColumn(1), false);
    QCOMPARE(view.visibleRowCount(), qsizetype(4));
    QCOMPARE(collapsed.last().first().value<QModelIndex>(), branch);
    view.toggleExpanded(parent.siblingAtColumn(1));
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(collapsed.last().first().value<QModelIndex>(), parent);
    view.setRootIndex(parent);
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(view.itemDepth(branch), 0);
    view.setExpanded(branch, true);
    QCOMPARE(view.visibleRowCount(), qsizetype(3));
    const BranchIndicatorState own = view.branchState(leaf.siblingAtColumn(2));
    QCOMPARE(own.itemDepth, 1);
    QCOMPARE(own.cellDepth, 1);
    QVERIFY(own.adjoinsItem);
    QVERIFY(!own.hasChildren);
    const BranchIndicatorState ancestor = view.branchState(leaf, 0);
    QCOMPARE(ancestor.cellDepth, 0);
    QVERIFY(!ancestor.adjoinsItem);
    QVERIFY(ancestor.hasChildren);
    QVERIFY(ancestor.isExpanded);
    QCOMPARE(view.branchState(leaf, 100).cellDepth, 1);
    view.collapseAll();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(!view.isExpanded(branch));
}

void TestTreeTableViewInteraction::publicNotificationsExposeCurrentTreeState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::publicNotificationsExposeCurrentTreeState()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto items = row(QStringLiteral("parent"));
    items.first()->appendRow(row(QStringLiteral("first-child")));
    items.first()->appendRow(row(QStringLiteral("second-child")));
    model.appendRow(items);
    model.appendRow(row(QStringLiteral("other")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex child(model.index(1, 0, parent));
    QVector<qsizetype> notifiedCounts;
    connect(&view, &VirtualTreeTableView::visibleRowsChanged, &view, [&]() {
        notifiedCounts.append(view.visibleRowCount());
        QCOMPARE(view.verticalHeaderGeometry()->sectionCount(), view.visibleRowCount());
        for (qsizetype visibleRow = 0; visibleRow < view.visibleRowCount(); ++visibleRow) {
            const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(visibleRow);
            QVERIFY(node.isValid());
            QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), visibleRow);
        }
    });
    view.expand(parent);
    QCOMPARE(notifiedCounts, QVector<qsizetype>{4});
    QModelIndex resizedNode;
    int notifiedHeight = 0;
    int heightNotifications = 0;
    connect(&view, &VirtualTableView::rowHeightChanged, &view,
            [&](qsizetype visibleRow, int height) {
        ++heightNotifications;
        resizedNode = view.visibilityIndex()->indexAtVisibleRow(visibleRow);
        notifiedHeight = height;
        QCOMPARE(view.rowHeight(visibleRow), height);
        QVERIFY(view.hasExplicitRowHeight(visibleRow));
    });
    const qsizetype childRow = view.visibilityIndex()->visibleRowForIndex(child);
    view.setRowHeight(childRow, 47);
    QCOMPARE(resizedNode, QModelIndex(child));
    QCOMPARE(notifiedHeight, 47);
    QCOMPARE(heightNotifications, 1);
    view.setRowHeight(childRow, 47);
    view.setRowHeight(-1, 99);
    view.setRowHeight(view.visibleRowCount(), 99);
    QCOMPARE(heightNotifications, 1);
    view.collapse(parent);
    view.expand(parent);
    QCOMPARE(notifiedCounts, (QVector<qsizetype>{4, 2, 4}));
    QCOMPARE(view.rowHeight(view.visibilityIndex()->visibleRowForIndex(child)), 47);
    QCOMPARE(heightNotifications, 1);

    view.setColumnWidth(0, 600);
    view.flushPendingRelayout();
    QSignalSpy offsets(&view, &VirtualTableView::horizontalOffsetChanged);
    connect(&view, &VirtualTableView::horizontalOffsetChanged, &view,
            [&](qint64 offset) { QCOMPARE(view.horizontalOffset(), offset); });
    QVERIFY(view.maximumHorizontalOffset() > 20);
    view.setHorizontalOffset(20);
    QCOMPARE(offsets.count(), 1);
    QCOMPARE(offsets.last().first().toLongLong(), qint64(20));
    view.setHorizontalOffset(view.primaryScrollGroup(), 20);
    QCOMPARE(offsets.count(), 1);
    view.setHorizontalOffset(view.maximumHorizontalOffset() + 100);
    QCOMPARE(offsets.count(), 2);
    QCOMPARE(offsets.last().first().toLongLong(), view.maximumHorizontalOffset());

    QItemSelectionModel external(&model);
    int selectionNotifications = 0;
    connect(&view, &VirtualItemView::selectionModelChanged, &view,
            [&](QItemSelectionModel *selection) {
        ++selectionNotifications;
        QCOMPARE(selection, &external);
        QCOMPARE(view.selectionModel(), selection);
    });
    view.setSelectionModel(&external);
    view.setSelectionModel(&external);
    QCOMPARE(selectionNotifications, 1);
    view.setCurrentIndex(QModelIndex(child).siblingAtColumn(2));
    QCOMPARE(external.currentIndex(), QModelIndex(child).siblingAtColumn(2));
    view.setRootIndex(parent);
    QCOMPARE(notifiedCounts.last(), qsizetype(2));
    QCOMPARE(view.rowHeight(view.visibilityIndex()->visibleRowForIndex(child)), 47);
}

void TestTreeTableViewInteraction::selectionPoliciesRespectTreeSchema_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("wholeRows");
    QTest::addColumn<int>("policy");
    for (bool cells : {false, true}) {
        for (bool wholeRows : {false, true}) {
            for (int policy = 0; policy < 4; ++policy) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + (wholeRows ? "row-selection-" : "cell-selection-")
                    + QByteArray::number(policy);
                QTest::newRow(name.constData()) << cells << wholeRows << policy;
            }
        }
    }
}

void TestTreeTableViewInteraction::selectionPoliciesRespectTreeSchema()
{
    QFETCH(bool, cells);
    QFETCH(bool, wholeRows);
    QFETCH(int, policy);
    QStandardItemModel model;
    auto firstItems = row(QStringLiteral("first"));
    firstItems.first()->appendRow({new QStandardItem(QStringLiteral("narrow")),
                                  new QStandardItem(QStringLiteral("type"))});
    model.appendRow(firstItems);
    auto secondItems = row(QStringLiteral("second"));
    secondItems.first()->appendRow(row(QStringLiteral("wide")));
    model.appendRow(secondItems);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setSelectionBehavior(wholeRows ? VirtualItemView::SelectionBehavior::SelectRows
                                       : VirtualItemView::SelectionBehavior::SelectItems);
    const VirtualItemView::SelectionMode policies[] = {
        VirtualItemView::SelectionMode::NoSelection,
        VirtualItemView::SelectionMode::SingleSelection,
        VirtualItemView::SelectionMode::MultiSelection,
        VirtualItemView::SelectionMode::ExtendedSelection
    };
    view.setSelectionMode(policies[policy]);
    const QModelIndex first = model.index(0, 1);
    const QModelIndex second = model.index(1, 1);
    const QModelIndex narrow = model.index(0, 1, first.siblingAtColumn(0));
    const QModelIndex wide = model.index(0, 1, second.siblingAtColumn(0));
    view.expand(first.siblingAtColumn(0));
    view.expand(second.siblingAtColumn(0));
    view.flushPendingRelayout();
    settle();
    const auto verifySelection = [&](const QList<QModelIndex> &items) {
        QSet<QModelIndex> expected;
        for (const QModelIndex &item : items) {
            const int from = wholeRows ? 0 : item.column();
            const int to = wholeRows ? qMin(view.columnCount(), model.columnCount(item.parent()))
                                    : item.column() + 1;
            for (int column = from; column < to; ++column)
                expected.insert(item.siblingAtColumn(column));
        }
        const QList<QModelIndex> indexes = view.selectionModel()->selectedIndexes();
        QSet<QModelIndex> actual;
        for (const QModelIndex &index : indexes)
            actual.insert(index);
        QCOMPARE(actual, expected);
    };
    const auto click = [&](const QModelIndex &index, Qt::KeyboardModifiers modifiers) {
        const QRect rect = view.cellRect(index);
        QVERIFY(!rect.isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, rect.center());
        QCOMPARE(view.currentIndex(), index);
    };
    click(first, Qt::NoModifier);
    verifySelection(policy == 0 ? QList<QModelIndex>() : QList<QModelIndex>{first});
    click(wide, Qt::ControlModifier);
    verifySelection(policy == 0 ? QList<QModelIndex>()
                    : policy == 1 ? QList<QModelIndex>{wide} : QList<QModelIndex>{first, wide});
    click(first, Qt::ControlModifier);
    verifySelection(policy == 0 ? QList<QModelIndex>()
                    : policy == 1 ? QList<QModelIndex>{first} : QList<QModelIndex>{wide});
    view.setSelectionMode(VirtualItemView::SelectionMode::SingleSelection);
    verifySelection({first});
    view.setSelectionMode(VirtualItemView::SelectionMode::NoSelection);
    verifySelection({});
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), narrow);
    verifySelection({});
    view.setSelectionMode(VirtualItemView::SelectionMode::SingleSelection);
    verifySelection({narrow});
    QTest::keyClick(&view, Qt::Key_Down, Qt::ControlModifier);
    QCOMPARE(view.currentIndex(), second);
    verifySelection({second});
    QTest::keyClick(&view, Qt::Key_Space);
    verifySelection({});
    QTest::keyClick(&view, Qt::Key_Space);
    verifySelection({second});
}

void TestTreeTableViewInteraction::selectionModifiersRemainNodeBoundAcrossFolding_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("wholeRows");
    for (bool cells : {false, true}) {
        for (bool wholeRows : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + (wholeRows ? "row-selection" : "cell-selection");
            QTest::newRow(name.constData()) << cells << wholeRows;
        }
    }
}

void TestTreeTableViewInteraction::selectionModifiersRemainNodeBoundAcrossFolding()
{
    QFETCH(bool, cells);
    QFETCH(bool, wholeRows);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("child a")));
    parentItems.first()->appendRow(row(QStringLiteral("child b")));
    model.appendRow(parentItems);
    model.appendRow(row(QStringLiteral("sibling")));
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    view.setSelectionBehavior(wholeRows ? VirtualItemView::SelectionBehavior::SelectRows
                                        : VirtualItemView::SelectionBehavior::SelectItems);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex childA = model.index(0, 1, parent);
    const QModelIndex childB = model.index(1, 1, parent);
    const QModelIndex sibling = model.index(1, 1);
    view.expand(parent);
    view.flushPendingRelayout();
    view.setCurrentIndex(childA);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), childB);
    const QSet<QPersistentModelIndex> beforeFold = [&] {
        QSet<QPersistentModelIndex> result;
        for (const QModelIndex &index : view.selectionModel()->selectedIndexes())
            result.insert(index);
        return result;
    }();
    QVERIFY(beforeFold.contains(childA));
    QVERIFY(beforeFold.contains(childB));
    QVERIFY(!beforeFold.contains(sibling));
    view.collapse(parent);
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(view.currentIndex(), childB);
    QSet<QPersistentModelIndex> folded;
    for (const QModelIndex &index : view.selectionModel()->selectedIndexes())
        folded.insert(index);
    QCOMPARE(folded, beforeFold);
    view.expand(parent);
    view.flushPendingRelayout();
    QCOMPARE(view.currentIndex(), childB);
    QSet<QPersistentModelIndex> restored;
    for (const QModelIndex &index : view.selectionModel()->selectedIndexes())
        restored.insert(index);
    QCOMPARE(restored, beforeFold);
    QVERIFY(!restored.contains(sibling));
    QTest::keyClick(&view, Qt::Key_Down, Qt::ControlModifier);
    QCOMPARE(view.currentIndex(), sibling);
    QSet<QPersistentModelIndex> afterCtrlMove;
    for (const QModelIndex &index : view.selectionModel()->selectedIndexes())
        afterCtrlMove.insert(index);
    QCOMPARE(afterCtrlMove, beforeFold);
}

void TestTreeTableViewInteraction::selectionSurvivesIndependentPaneScrolling_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("wholeRows");
    for (bool cells : {false, true}) {
        for (bool wholeRows : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + (wholeRows ? "row-selection" : "cell-selection");
            QTest::newRow(name.constData()) << cells << wholeRows;
        }
    }
}

void TestTreeTableViewInteraction::selectionSurvivesIndependentPaneScrolling()
{
    QFETCH(bool, cells);
    QFETCH(bool, wholeRows);
    QStandardItemModel model;
    for (int rowIndex = 0; rowIndex < 6; ++rowIndex) {
        auto items = row(QStringLiteral("row-%1").arg(rowIndex));
        items.append(new QStandardItem(QStringLiteral("type-%1").arg(rowIndex)));
        items.append(new QStandardItem(QStringLiteral("state-%1").arg(rowIndex)));
        items.append(new QStandardItem(QStringLiteral("value-%1").arg(rowIndex)));
        items.append(new QStandardItem(QStringLiteral("tail-%1").arg(rowIndex)));
        model.appendRow(items);
    }
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    view.setSelectionBehavior(wholeRows ? VirtualItemView::SelectionBehavior::SelectRows
                                        : VirtualItemView::SelectionBehavior::SelectItems);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2}, PaneScroll::Scrollable, 0},
                   {{3, 4}, PaneScroll::Scrollable, 1}});
    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();

    const QModelIndex current = model.index(2, 3);
    view.setCurrentIndex(current);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(view.currentIndex(), model.index(4, 3));
    const QModelIndexList selectedIndexes = view.selectionModel()->selectedIndexes();
    QSet<QPersistentModelIndex> selected;
    for (const QModelIndex &index : selectedIndexes)
        selected.insert(index);
    QVERIFY(selected.size() >= (wholeRows ? 3 : 3));
    for (const QPersistentModelIndex &index : selected)
        QVERIFY(index.isValid());

    const qint64 secondaryOffset = qMin<qint64>(20, view.maximumHorizontalOffset(1));
    view.setHorizontalOffset(1, secondaryOffset);
    const qint64 primaryOffset = qMin<qint64>(20, view.maximumHorizontalOffset(0));
    view.setHorizontalOffset(primaryOffset);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.currentIndex(), model.index(4, 3));
    for (const QPersistentModelIndex &index : selected)
        QVERIFY(view.selectionModel()->isSelected(index));
    const int pane = view.paneIndexOfColumn(3);
    QVERIFY(pane >= 0);
    const QRect visible = view.cellRect(view.currentIndex())
        .intersected(view.panes().at(pane).viewportRect)
        .intersected(view.viewport()->rect());
    if (!visible.isEmpty())
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(visible.center()),
                 view.currentIndex());
}

void TestTreeTableViewInteraction::mergedCellsMaskInternalGridPixels_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("spacing");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<bool>("animated");
    QTest::addColumn<int>("anchorColumn");
    QTest::addColumn<int>("extent");
    for (bool cells : {false, true}) {
        for (int spacing : {0, 6}) {
            for (bool frozen : {false, true}) {
                for (bool animated : {false, true}) {
                    for (int anchorColumn : {0, 1}) {
                        for (int extent = anchorColumn == 0 ? 0 : 2; extent < 3; ++extent) {
                            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                                + QByteArray::number(spacing) + (frozen ? "-frozen" : "-scrollable")
                                + (animated ? "-animated" : "-static")
                                + "-column-" + QByteArray::number(anchorColumn)
                                + "-extent-" + QByteArray::number(extent);
                            QTest::newRow(name.constData()) << cells << spacing << frozen << animated
                                                           << anchorColumn << extent;
                        }
                    }
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::mergedCellsMaskInternalGridPixels()
{
    QFETCH(bool, cells);
    QFETCH(int, spacing);
    QFETCH(bool, frozen);
    QFETCH(bool, animated);
    QFETCH(int, anchorColumn);
    QFETCH(int, extent);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    auto firstRow = row(QStringLiteral("first"));
    firstRow.first()->appendRow(row(QStringLiteral("descendant")));
    parentRow.first()->appendRow(firstRow);
    parentRow.first()->appendRow(row(QStringLiteral("second")));
    parentRow.first()->appendRow(row(QStringLiteral("last")));
    model.appendRow(parentRow);
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    view.setRowSpacing(spacing);
    view.setColumnSpacing(spacing);
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    view.setVisualStateScope(VirtualTableView::VisualStateScope::Cell);
    view.setVisualStateAnimationDuration(0);
    view.setVisualStateBackgroundVisible(true);
    view.setVisualStateBackgroundExtent(
        static_cast<VirtualTreeTableView::VisualStateBackgroundExtent>(extent));
    const QColor selected(211, 31, 77);
    const QColor hover(29, 191, 113);
    const QColor horizontal(17, 131, 229);
    const QColor vertical(19, 173, 53);
    view.setSelectedBackgroundColor(selected);
    view.setHoverBackgroundColor(hover);
    view.setHorizontalGridLineColor(horizontal);
    view.setVerticalGridLineColor(vertical);
    view.setHorizontalGridLinesVisible(true);
    view.setVerticalGridLinesVisible(true);
    view.setHorizontalGridLineWidth(1);
    view.setVerticalGridLineWidth(1);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex first = model.index(0, 0, parent);
    const QModelIndex anchor = first.siblingAtColumn(anchorColumn);
    const QModelIndex covered = model.index(1, anchorColumn + 1, parent);
    const int outsideColumn = anchorColumn == 0 ? 2 : 0;
    view.expand(parent);
    if (frozen) {
        view.setFrozenColumns(anchorColumn == 0 ? QVector<int>{0, 1} : QVector<int>{0});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    view.setSpan(1, anchorColumn, 2, 2);
    if (animated) {
        view.flushPendingRelayout();
        QCursor::setPos(view.viewport()->mapToGlobal(QPoint(400, 220)));
        sendHeaderMouse(view.viewport(), QEvent::MouseMove, QPoint(400, 220), Qt::NoButton, Qt::NoButton);
        settle();
        const QRect firstRect = view.visualRect(first);
        const int insideX = view.columnGeometry(anchorColumn).viewportX + 50;
        const int boundaryY = firstRect.bottom() + (spacing > 0 ? 1 : 0);
        const int verticalX = view.columnGeometry(anchorColumn).viewportX + view.columnWidth(anchorColumn) - 1;
        QVector<QPoint> points{
            QPoint(insideX, firstRect.top() + 4),
            QPoint(view.columnGeometry(anchorColumn + 1).viewportX + 40,
                   view.visualRect(covered.siblingAtColumn(0)).top() + 4),
            QPoint(insideX, boundaryY),
            QPoint(verticalX, firstRect.top() + 4)};
        QVector<bool> excluded(points.size(), false);
        if (anchorColumn == 0) {
            for (const int y : {firstRect.top() + 2,
                               view.visualRect(covered.siblingAtColumn(0)).top() + 2}) {
                points.append(QPoint(view.columnGeometry(0).viewportX + 5, y));
                excluded.append(extent != 2);
                points.append(QPoint(view.columnGeometry(0).viewportX + view.indentation() + 5, y));
                excluded.append(extent == 0);
            }
        }
        const QPoint origin = view.viewport()->geometry().topLeft();
        const QImage baseline = view.grab().toImage();
        QVERIFY(!view.visualState(anchor).hovered);
        view.setVisualStateAnimationDuration(1000);
        QVERIFY(!view.visualState(anchor).hovered);
        const auto advance = [&](int time) {
            bool found = false;
            for (QVariantAnimation *animation : view.findChildren<QVariantAnimation *>()) {
                if (animation->duration() != 1000 || animation->state() == QAbstractAnimation::Stopped)
                    continue;
                found = true;
                if (animation->state() == QAbstractAnimation::Running)
                    animation->pause();
                animation->setCurrentTime(time);
            }
            QVERIFY(found);
            settle();
        };
        const auto verifyFrame = [&](qreal hoverProgress, qreal selectedProgress) {
            const QImage actual = view.grab().toImage();
            for (int i = 0; i < points.size(); ++i) {
                const QPoint point = points.at(i);
                QImage reference(1, 1, QImage::Format_ARGB32_Premultiplied);
                reference.fill(baseline.pixelColor(origin + point));
                QPainter painter(&reference);
                painter.setOpacity(excluded.at(i) ? 0.0 : hoverProgress);
                painter.fillRect(reference.rect(), hover);
                painter.setOpacity(excluded.at(i) ? 0.0 : selectedProgress);
                painter.fillRect(reference.rect(), selected);
                painter.end();
                QCOMPARE(actual.pixelColor(origin + point), reference.pixelColor(0, 0));
            }
            QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(points.at(0)), anchor);
            QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(points.at(1)), anchor);
            const QPoint outside(view.columnGeometry(outsideColumn).viewportX + 70, boundaryY);
            QCOMPARE(actual.pixelColor(origin + outside), horizontal);
        };
        QCursor::setPos(view.viewport()->mapToGlobal(points.at(1)));
        sendHeaderMouse(view.viewport(), QEvent::MouseMove, points.at(1), Qt::NoButton, Qt::NoButton);
        settle();
        QVERIFY(view.visualState(anchor).hovered);
        advance(500);
        QVERIFY(view.visualState(anchor).hoverProgress > 0.4);
        QVERIFY(view.visualState(anchor).hoverProgress < 0.6);
        verifyFrame(view.visualState(anchor).hoverProgress, 0.0);
        advance(1000);
        verifyFrame(1.0, 0.0);
        view.setCurrentIndex(anchor);
        settle();
        advance(500);
        QVERIFY(view.visualState(anchor).selectedProgress > 0.4);
        QVERIFY(view.visualState(anchor).selectedProgress < 0.6);
        verifyFrame(1.0, view.visualState(anchor).selectedProgress);
        advance(1000);
        verifyFrame(1.0, 1.0);
        view.setVisualStateAnimationDuration(0);
    }
    view.setCurrentIndex(anchor);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.anchorIndex(covered), anchor);
    const QPoint coveredPoint(view.columnGeometry(anchorColumn + 1).viewportX + 40,
                              view.visualRect(covered.siblingAtColumn(0)).center().y());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(coveredPoint), anchor);
    if (cells) {
        QVERIFY(view.cellWidget(anchor));
        QVERIFY(!view.cellWidget(covered));
    }
    const QRect firstRect = view.visualRect(first);
    const int boundaryY = firstRect.bottom() + (spacing > 0 ? 1 : 0);
    const int insideX = view.columnGeometry(anchorColumn).viewportX + 50;
    const int outsideX = view.columnGeometry(outsideColumn).viewportX + 70;
    const int verticalX = view.columnGeometry(anchorColumn).viewportX + view.columnWidth(anchorColumn) - 1;
    const QPoint origin = view.viewport()->geometry().topLeft();
    const QImage merged = view.grab().toImage();
    QCOMPARE(merged.pixelColor(origin + QPoint(insideX, boundaryY)), selected);
    QCOMPARE(merged.pixelColor(origin + QPoint(outsideX, boundaryY)), horizontal);
    QCOMPARE(merged.pixelColor(origin + QPoint(verticalX, firstRect.top() + 4)), selected);
    view.expand(first);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanRect(anchor).size(), QSize(view.columnWidth(anchorColumn), firstRect.height()));
    QCOMPARE(view.anchorIndex(covered), covered);
    const QImage expanded = view.grab().toImage();
    QCOMPARE(expanded.pixelColor(origin + QPoint(insideX, boundaryY)), horizontal);
    QCOMPARE(expanded.pixelColor(origin + QPoint(verticalX, firstRect.top() + 4)), vertical);
    view.collapse(first);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.anchorIndex(covered), anchor);
    const QImage collapsed = view.grab().toImage();
    QCOMPARE(collapsed.pixelColor(origin + QPoint(insideX, boundaryY)), selected);
    view.setFrozenRows(2);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanRect(anchor).size(), QSize(view.columnWidth(anchorColumn), firstRect.height()));
    QCOMPARE(view.anchorIndex(covered), covered);
    const QImage topBoundary = view.grab().toImage();
    QCOMPARE(topBoundary.pixelColor(origin + QPoint(verticalX, view.visualRect(first).top() + 4)),
             vertical);
    view.setFrozenRows(0);
    view.setFrozenBottomRows(2);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.anchorIndex(covered), covered);
    QCOMPARE(view.spanRect(anchor).size(), QSize(view.columnWidth(anchorColumn), firstRect.height()));
    const QImage bottomBoundary = view.grab().toImage();
    const QRect scrollingFirst = view.visualRect(first);
    QCOMPARE(bottomBoundary.pixelColor(origin + QPoint(verticalX, scrollingFirst.top() + 4)), vertical);
    QCOMPARE(bottomBoundary.pixelColor(origin + QPoint(insideX,
                 scrollingFirst.bottom() + (spacing > 0 ? 1 : 0))), horizontal);
    const QPoint separateHit(view.columnGeometry(anchorColumn + 1).viewportX + 40,
                             view.visualRect(covered.siblingAtColumn(0)).center().y());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(separateHit), covered);
    if (cells)
        QVERIFY(view.cellWidget(covered));
    view.setFrozenRows(frozen ? 1 : 0);
    view.setFrozenBottomRows(frozen ? 1 : 0);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.anchorIndex(covered), anchor);
    const QRect restoredFirst = view.visualRect(first);
    const QImage restored = view.grab().toImage();
    QCOMPARE(restored.pixelColor(origin + QPoint(insideX,
                 restoredFirst.bottom() + (spacing > 0 ? 1 : 0))), selected);
    QCOMPARE(restored.pixelColor(origin + QPoint(verticalX, restoredFirst.top() + 4)), selected);
    const QPoint restoredHit(view.columnGeometry(anchorColumn + 1).viewportX + 40,
                             view.visualRect(covered.siblingAtColumn(0)).center().y());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(restoredHit), anchor);
    if (cells) {
        QVERIFY(view.cellWidget(anchor));
        QVERIFY(!view.cellWidget(covered));
    }
}

void TestTreeTableViewInteraction::editorsAndPinsSurviveScrollingAndFolding_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<bool>("merged");
    QTest::addColumn<bool>("explicitPin");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            for (bool merged : {false, true}) {
                for (bool explicitPin : {false, true}) {
                    const QByteArray name = QByteArray(cells ? "cells" : "rows")
                        + (frozen ? "-frozen" : "-plain")
                        + (merged ? "-span" : "-single")
                        + (explicitPin ? "-pin" : "-focus");
                    QTest::newRow(name.constData()) << cells << frozen << merged << explicitPin;
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::editorsAndPinsSurviveScrollingAndFolding()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QFETCH(bool, merged);
    QFETCH(bool, explicitPin);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("edited-child")));
    parentRow.first()->appendRow(row(QStringLiteral("sibling")));
    model.appendRow(parentRow);
    for (int i = 0; i < 200; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    EditorRowAdapter rowAdapter;
    EditorCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    const QModelIndex binding = cells ? cell : node;
    view.expand(parent);
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({2});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    if (merged)
        view.setSpan(1, 1, 2, 2);
    showView(&view, QSize(440, 280));
    const auto owningWidget = [&](const QModelIndex &index) {
        return cells ? view.cellWidget(index) : view.widgetForIndex(index.siblingAtColumn(0));
    };
    QPointer<QWidget> widget(owningWidget(cell));
    QVERIFY(widget);
    QPointer<QLineEdit> editor(cells ? qobject_cast<QLineEdit *>(widget.data())
                                   : widget->findChild<QLineEdit *>());
    QVERIFY(editor);
    QCOMPARE(view.indexForWidget(widget), binding);
    const QString draft = QStringLiteral("uncommitted draft");
    editor->setText(draft);
    if (explicitPin) {
        view.setFocus();
        view.pinWidget(widget);
        QVERIFY(view.isItemPinned(cell));
    } else {
        view.activateWindow();
        editor->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::focusWidget(), editor.data());
    }
    view.scrollTo(model.index(100, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(widget);
    QCOMPARE(owningWidget(cell), widget.data());
    QCOMPARE(widget->property("boundIndex").value<QPersistentModelIndex>(),
             QPersistentModelIndex(binding));
    QCOMPARE(editor->text(), draft);
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
    if (!explicitPin)
        QCOMPARE(QApplication::focusWidget(), editor.data());
    view.collapse(parent);
    view.scrollTo(parent, VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), qsizetype(-1));
    QVERIFY(widget);
    QCOMPARE(owningWidget(cell), widget.data());
    QCOMPARE(editor->text(), draft);
    QVERIFY(widget->visibleRegion().isEmpty());
    if (!explicitPin)
        QCOMPARE(QApplication::focusWidget(), editor.data());
    view.expand(parent);
    view.scrollTo(node, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(owningWidget(cell), widget.data());
    QCOMPARE(editor->text(), draft);
    QVERIFY(editor->isVisible());
    QVERIFY(view.viewport()->rect().contains(editor->mapTo(view.viewport(), editor->rect().center())));
    if (!explicitPin)
        QCOMPARE(QApplication::focusWidget(), editor.data());
    if (explicitPin)
        view.unpinWidget(widget);
    else
        editor->clearFocus();
    view.setFocus();
    view.scrollTo(model.index(150, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(!owningWidget(cell));
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
    QVERIFY(!view.isItemPinned(cell));
    if (explicitPin) {
        const QModelIndex offscreen = model.index(60, 1);
        QVERIFY(!owningWidget(offscreen));
        view.setItemPinned(offscreen);
        view.flushPendingRelayout();
        QWidget *pinned = owningWidget(offscreen);
        QVERIFY(pinned);
        QCOMPARE(view.indexForWidget(pinned), cells ? offscreen : offscreen.siblingAtColumn(0));
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
        view.unpinWidget(pinned);
        view.flushPendingRelayout();
        QVERIFY(!owningWidget(offscreen));
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
    }
}

void TestTreeTableViewInteraction::inputMethodCommitSurvivesSpanChangesAndScrolling_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("covered");
    QTest::newRow("rows-anchor") << false << false;
    QTest::newRow("cells-anchor") << true << false;
    QTest::newRow("rows-covered") << false << true;
    QTest::newRow("cells-covered") << true << true;
}

void TestTreeTableViewInteraction::inputMethodCommitSurvivesSpanChangesAndScrolling()
{
    QFETCH(bool, cells);
    QFETCH(bool, covered);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("editor")));
    model.appendRow(parentItems);
    for (int i = 0; i < 120; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    EditorRowAdapter rowAdapter;
    EditorCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    view.expand(parent);
    showView(&view, QSize(440, 280));
    QPointer<QWidget> widget(cells ? view.cellWidget(cell) : view.widgetForIndex(node));
    QVERIFY(widget);
    QPointer<QLineEdit> editor(cells ? qobject_cast<QLineEdit *>(widget.data())
                                   : widget->findChild<QLineEdit *>());
    QVERIFY(editor);
    view.activateWindow();
    editor->setText(QStringLiteral("draft:"));
    editor->setCursorPosition(editor->text().size());
    editor->setFocus(Qt::MouseFocusReason);
    settle();
    QCOMPARE(QApplication::focusWidget(), editor.data());
    QInputMethodEvent preedit(QStringLiteral("pending"), {});
    QApplication::sendEvent(editor.data(), &preedit);
    QCOMPARE(editor->text(), QStringLiteral("draft:"));

    view.setSpan(1, covered ? 0 : 1, 1, 2);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(cells ? view.cellWidget(cell) : view.widgetForIndex(node), widget.data());
    QCOMPARE(QApplication::focusWidget(), editor.data());
    QCOMPARE(editor->text(), QStringLiteral("draft:"));
    QCOMPARE(view.isSpanCovered(cell), covered);
    if (covered) {
        QCOMPARE(view.anchorIndex(cell), node);
        QVERIFY(view.cellRect(cell).isEmpty());
        QVERIFY(editor->visibleRegion().isEmpty());
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
    }
    view.scrollTo(model.index(100, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(editor);
    QCOMPARE(QApplication::focusWidget(), editor.data());
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("committed"));
    QApplication::sendEvent(editor.data(), &commit);
    QCOMPARE(editor->text(), QStringLiteral("draft:committed"));
    view.clearSpans();
    view.scrollTo(node, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(cells ? view.cellWidget(cell) : view.widgetForIndex(node), widget.data());
    QCOMPARE(editor->text(), QStringLiteral("draft:committed"));
    QCOMPARE(QApplication::focusWidget(), editor.data());
    QVERIFY(!view.isSpanCovered(cell));
    QVERIFY(!editor->visibleRegion().isEmpty());
    editor->clearFocus();
    view.setFocus();
    view.scrollTo(model.index(110, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(cells ? !view.cellWidget(cell) : !view.widgetForIndex(node));
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestTreeTableViewInteraction::editorsSurviveRuntimeFreezeChanges_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("explicitPin");
    for (bool cells : {false, true}) {
        for (bool explicitPin : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (explicitPin ? "-pin" : "-focus");
            QTest::newRow(name.constData()) << cells << explicitPin;
        }
    }
}

void TestTreeTableViewInteraction::editorsSurviveRuntimeFreezeChanges()
{
    QFETCH(bool, cells);
    QFETCH(bool, explicitPin);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("editor")));
    model.appendRow(parentRow);
    for (int i = 0; i < 120; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    EditorRowAdapter rowAdapter;
    EditorCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    const QModelIndex binding = cells ? cell : node;
    view.expand(parent);
    showView(&view, QSize(440, 280));
    const auto owningWidget = [&]() {
        return cells ? view.cellWidget(cell) : view.widgetForIndex(node);
    };
    QPointer<QWidget> widget(owningWidget());
    QVERIFY(widget);
    QPointer<QLineEdit> editor(cells ? qobject_cast<QLineEdit *>(widget.data())
                                   : widget->findChild<QLineEdit *>());
    QVERIFY(editor);
    const QString draft = QStringLiteral("draft across pane changes");
    editor->setText(draft);
    if (explicitPin) {
        view.setFocus();
        view.pinWidget(widget);
    } else {
        view.activateWindow();
        editor->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::focusWidget(), editor.data());
    }
    for (int state = 0; state < 3; ++state) {
        if (state == 1)
            view.scrollTo(model.index(80, 0), VirtualItemView::PositionAtTop);
        if (state == 2)
            view.collapse(parent);
        for (int layout = 0; layout < 3; ++layout) {
            view.setFrozenColumns(layout == 1 ? QVector<int>{0, 1} : QVector<int>{});
            view.setFrozenRightColumns(layout == 2 ? QVector<int>{1, 2} : QVector<int>{});
            view.setFrozenRows(layout == 1 ? 2 : 0);
            view.setFrozenBottomRows(layout == 2 ? 2 : 0);
            view.flushPendingRelayout();
            settle();
            QVERIFY(widget);
            QVERIFY(editor);
            QCOMPARE(owningWidget(), widget.data());
            QCOMPARE(view.indexForWidget(widget), binding);
            QCOMPARE(widget->property("boundIndex").value<QPersistentModelIndex>(),
                     QPersistentModelIndex(binding));
            QCOMPARE(editor->text(), draft);
            if (explicitPin || state > 0)
                QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
            if (!explicitPin)
                QCOMPARE(QApplication::focusWidget(), editor.data());
            if (state == 2)
                QVERIFY(widget->visibleRegion().isEmpty());
        }
    }
    view.setFrozenColumns({});
    view.setFrozenRightColumns({});
    view.setFrozenRows(0);
    view.setFrozenBottomRows(0);
    view.expand(parent);
    view.scrollTo(node, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(owningWidget(), widget.data());
    QCOMPARE(editor->text(), draft);
    QVERIFY(!editor->visibleRegion().isEmpty());
    if (explicitPin)
        view.unpinWidget(widget);
    else
        editor->clearFocus();
    view.setFocus();
    view.scrollTo(model.index(100, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(!owningWidget());
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestTreeTableViewInteraction::rowHeightsAndScrollAnchorsFollowNodes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (frozen ? "-frozen" : "-plain");
            QTest::newRow(name.constData()) << cells << frozen;
        }
    }
}

void TestTreeTableViewInteraction::rowHeightsAndScrollAnchorsFollowNodes()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    auto firstRow = row(QStringLiteral("first"));
    firstRow.first()->appendRow(row(QStringLiteral("grandchild")));
    parentRow.first()->appendRow(firstRow);
    auto targetRow = row(QStringLiteral("target"));
    targetRow.first()->setData(43, Qt::UserRole);
    parentRow.first()->appendRow(targetRow);
    model.appendRow(parentRow);
    for (int i = 0; i < 180; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    MeasuredRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    InspectTreeTableView view;
    view.setEstimatedItemHeight(28);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setAutoMeasureItemHeight(true);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setRowSpacing(3);
    view.setDepthRowSpacing(1, 7);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex first = model.index(0, 0, parent);
    const QModelIndex target = model.index(1, 0, parent);
    view.expand(parent);
    if (frozen) {
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.setFrozenColumns({0});
    }
    showView(&view, QSize(440, 350));
    const auto verifyTarget = [&](int expected) {
        view.scrollTo(target, VirtualItemView::EnsureVisible);
        view.flushPendingRelayout();
        settle();
        const qsizetype visible = view.visibilityIndex()->visibleRowForIndex(target);
        QVERIFY(visible >= 0);
        QCOMPARE(view.rowHeight(visible), expected);
        QCOMPARE(view.verticalHeaderGeometry()->storedSectionSize(int(visible)), expected);
        const QRect body = view.visualRect(target);
        QCOMPARE(body.height(), expected);
        auto *header = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
        QVERIFY(header);
        QWidget *section = header->sectionWidget(int(visible));
        QVERIFY(section);
        QCOMPARE(section->height(), expected);
        QCOMPARE(header->geometry().y() + section->y(), view.viewport()->geometry().y() + body.y());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(target).center()), target);
    };
    verifyTarget(cells ? 28 : 43);
    view.setRowHeight(2, 66);
    verifyTarget(66);
    QVERIFY(view.hasExplicitRowHeight(2));
    view.expand(first);
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(target), qsizetype(3));
    verifyTarget(66);
    QVERIFY(view.hasExplicitRowHeight(3));
    QVERIFY(!view.hasExplicitRowHeight(2));
    view.collapse(first);
    verifyTarget(66);
    view.collapse(parent);
    view.expand(parent);
    verifyTarget(66);
    if (!cells) {
        view.setRowSizePolicy(VirtualTableView::RowSizePolicy::MeasuredWins);
        verifyTarget(43);
        QVERIFY(view.hasExplicitRowHeight(2));
        model.setData(target, 57, Qt::UserRole);
        verifyTarget(57);
        view.setRowSizePolicy(VirtualTableView::RowSizePolicy::ExplicitWins);
        view.setRowHeight(2, 66);
        verifyTarget(66);
        model.setData(target, 61, Qt::UserRole);
        verifyTarget(66);
    }
    view.clearRowHeight(2);
    verifyTarget(cells ? 28 : 61);
    QVERIFY(!view.hasExplicitRowHeight(2));
    const QModelIndex offscreen = model.index(80, 0);
    const qsizetype offscreenRow = view.visibilityIndex()->visibleRowForIndex(offscreen);
    view.setRowHeight(offscreenRow, 59);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(offscreenRow), 59);
    QVERIFY(cells ? !view.cellWidget(offscreen) : !view.widgetForIndex(offscreen));
    view.clearRowHeight(offscreenRow);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(offscreenRow), 28);
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionSize(int(offscreenRow)), 28);
    const QPersistentModelIndex anchor(model.index(100, 0));
    view.scrollTo(anchor, VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    const int anchorY = view.visualRect(anchor).top();
    for (int i = 0; i < 5; ++i) {
        view.collapse(parent);
        view.flushPendingRelayout();
        settle();
        QCOMPARE(view.visualRect(anchor).top(), anchorY);
        view.expand(parent);
        view.expand(first);
        view.flushPendingRelayout();
        settle();
        QCOMPARE(view.visualRect(anchor).top(), anchorY);
        view.collapse(first);
    }
    model.insertRow(0, row(QStringLiteral("inserted-before-anchor")));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.visualRect(anchor).top(), anchorY);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(anchor).center()),
             QModelIndex(anchor));
    const auto verifyAnchor = [&]() {
        view.flushPendingRelayout();
        settle();
        QVERIFY(anchor.isValid());
        QCOMPARE(view.visualRect(anchor).top(), anchorY);
        const qsizetype visible = view.visibilityIndex()->visibleRowForIndex(anchor);
        QVERIFY(visible >= 0);
        QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(visible), QModelIndex(anchor));
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(anchor).center()),
                 QModelIndex(anchor));
    };
    QVERIFY(model.removeRow(0));
    verifyAnchor();
    QVERIFY(model.removeRows(20, 7));
    verifyAnchor();
    QVERIFY(model.removeRows(anchor.row() + 20, 5));
    verifyAnchor();
    QVERIFY(model.removeRow(0));
    verifyAnchor();
    const QPersistentModelIndex successor(model.index(anchor.row() + 1, 0));
    const QModelIndex anchorCell = QModelIndex(anchor).siblingAtColumn(1);
    const qsizetype deletedVisibleRow = view.visibilityIndex()->visibleRowForIndex(anchor);
    view.setRowHeight(deletedVisibleRow, 47);
    view.setCurrentIndex(anchorCell);
    view.selectionModel()->select(anchorCell, QItemSelectionModel::ClearAndSelect);
    view.setItemPinned(anchorCell);
    view.setSpan(int(deletedVisibleRow), 1, 1, 2);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(QModelIndex(view.captureAnchor().index), QModelIndex(anchor));
    const qint64 offsetBeforeDeletion = view.verticalOffset();
    const int deletedY = view.visualRect(anchor).top();
    QVERIFY(model.removeRow(anchor.row()));
    view.flushPendingRelayout();
    settle();
    QVERIFY(!anchor.isValid());
    QVERIFY(successor.isValid());
    QCOMPARE(view.verticalOffset(), offsetBeforeDeletion);
    QCOMPARE(view.visualRect(successor).top(), deletedY);
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(successor), deletedVisibleRow);
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(deletedVisibleRow), QModelIndex(successor));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(successor).center()),
             QModelIndex(successor));
    QVERIFY(!view.isItemPinned(successor));
    QVERIFY(!view.hasExplicitRowHeight(deletedVisibleRow));
    QVERIFY(!view.selectionModel()->isSelected(QModelIndex(successor).siblingAtColumn(1)));
    QCOMPARE(view.spanAt(QModelIndex(successor).siblingAtColumn(1)), TableSpan{});
    QCOMPARE(view.anchorIndex(QModelIndex(successor).siblingAtColumn(2)),
             QModelIndex(successor).siblingAtColumn(2));
}

void TestTreeTableViewInteraction::horizontalGeometryAndOverscanStayConsistent_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (frozen ? "-frozen" : "-plain");
            QTest::newRow(name.constData()) << cells << frozen;
        }
    }
}

void TestTreeTableViewInteraction::horizontalGeometryAndOverscanStayConsistent()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    const auto makeRow = [](const QString &name, int columns) {
        QList<QStandardItem *> items;
        for (int column = 0; column < columns; ++column)
            items.append(new QStandardItem(name + QString::number(column)));
        return items;
    };
    QStandardItemModel model;
    auto wide = makeRow(QStringLiteral("wide"), 8);
    wide.first()->appendRow(makeRow(QStringLiteral("wide-child"), 8));
    model.appendRow(wide);
    auto narrow = makeRow(QStringLiteral("narrow"), 8);
    narrow.first()->appendRow(makeRow(QStringLiteral("narrow-child"), 4));
    model.appendRow(narrow);
    model.appendRow(makeRow(QStringLiteral("last"), 8));
    ParentTypedRowAdapter rowAdapter(8);
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setIndentation(20);
    view.setColumnMinimumWidth(20);
    view.setColumnMaximumWidth(1000);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex wideNode = model.index(0, 0, model.index(0, 0));
    const QModelIndex narrowNode = model.index(0, 0, model.index(1, 0));
    view.expand(model.index(0, 0));
    view.expand(model.index(1, 0));
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({7});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    showView(&view, QSize(600, 300));
    const auto verify = [&]() {
        view.flushPendingRelayout();
        settle();
        const QVector<int> columns = view.visibleColumnLogicalIndexes();
        for (const QModelIndex &node : {wideNode, narrowNode}) {
            for (int column = 0; column < 8; ++column) {
                const QModelIndex index = node.siblingAtColumn(column);
                const bool exists = index.isValid() && !view.isColumnHidden(column);
                if (cells) {
                    QVERIFY2(bool(view.cellWidget(index)) == (exists && columns.contains(column)),
                             qPrintable(QStringLiteral("node=%1 column=%2 offset=%3 overscan=%4 exists=%5 wanted=%6 actual=%7")
                                 .arg(node.data().toString()).arg(column).arg(view.horizontalOffset())
                                 .arg(view.columnOverscan()).arg(exists).arg(columns.contains(column))
                                 .arg(bool(view.cellWidget(index)))));
                } else {
                    auto *widget = static_cast<HostedRow *>(view.widgetForIndex(node));
                    QVERIFY(widget);
                    if (!exists) {
                        QVERIFY(!widget->host(column)->isVisible());
                        continue;
                    }
                }
                if (!exists || !columns.contains(column))
                    continue;
                const int paneIndex = view.paneIndexOfColumn(column);
                QVERIFY(paneIndex >= 0);
                const QRect rect = view.cellRect(index);
                const QRect visible = rect.intersected(view.panes().at(paneIndex).viewportRect)
                    .intersected(view.viewport()->rect());
                if (visible.width() > (column == 0 ? 45 : 4)) {
                    QPoint sample = visible.center();
                    if (column == 0)
                        sample.setX(qMax(sample.x(), rect.left() + 42));
                    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(sample), index);
                }
                if (visible.isEmpty())
                    continue;
                QWidget *widget = cells ? view.cellWidget(index)
                    : static_cast<HostedRow *>(view.widgetForIndex(node))->host(column);
                QVERIFY(widget);
                QRect expected = rect;
                if (column == 0)
                    expected.adjust(40, 0, 0, 0);
                QCOMPARE(QRect(widget->mapTo(view.viewport(), QPoint()), widget->size()), expected);
            }
        }
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Horizontal)
                continue;
            for (int column : columns) {
                QWidget *section = header->sectionWidget(column);
                if (!section || section->visibleRegion().isEmpty())
                    continue;
                QCOMPARE(section->width(), view.columnWidth(column));
                QCOMPARE(section->mapTo(&view, QPoint()).x() - view.viewport()->x(),
                         view.columnGeometry(column).viewportX);
            }
        }
    };
    view.setColumnWidth(1, 1);
    QCOMPARE(view.columnWidth(1), 20);
    view.setColumnWidth(1, 2000);
    QCOMPARE(view.columnWidth(1), 1000);
    for (int column = 5; column < 8; ++column)
        view.setColumnHidden(column, true);
    view.setColumnWidth(0, 80);
    view.setColumnWidth(1, 60);
    view.setColumnWidth(4, 70);
    view.setColumnStretchFactor(2, 1);
    view.setColumnStretchFactor(3, 2);
    const auto verifyRatio = [&]() {
        verify();
        const int leftover = view.viewport()->width() - 210;
        QCOMPARE(view.columnWidth(2) + view.columnWidth(3), leftover);
        QVERIFY(qAbs(2 * view.columnWidth(2) - view.columnWidth(3)) <= 2);
        QCOMPARE(view.maximumHorizontalOffset(), qint64(0));
    };
    verifyRatio();
    view.resize(740, 300);
    settle();
    verifyRatio();
    view.setColumnStretchFactor(2, 0);
    view.setColumnStretchFactor(3, 0);
    for (int column = 0; column < 5; ++column)
        view.setColumnWidth(column, 70);
    view.setStretchLastColumn(true);
    verify();
    QCOMPARE(view.columnWidth(4), view.viewport()->width() - 280);
    QCOMPARE(view.columnGeometry(4).viewportX + view.columnWidth(4), view.viewport()->width());
    view.setStretchLastColumn(false);
    for (int column = 0; column < 8; ++column) {
        view.setColumnHidden(column, false);
        view.setColumnWidth(column, 160);
    }
    view.resize(440, 300);
    verify();
    const QVector<int> tight = view.visibleColumnLogicalIndexes();
    view.setColumnOverscan(2);
    verify();
    const QVector<int> widened = view.visibleColumnLogicalIndexes();
    QVERIFY(widened.size() > tight.size());
    for (int column : tight)
        QVERIFY(widened.contains(column));
    view.setColumnOverscan(0);
    view.setHorizontalOffset(view.maximumHorizontalOffset());
    verify();
    QVERIFY(view.horizontalOffset() > 0);
    view.setColumnHidden(2, true);
    view.moveColumn(0, 6);
    verify();
    const QRect missingRect(view.columnGeometry(6).viewportX, view.visualRect(narrowNode).y(),
                            view.columnWidth(6), view.visualRect(narrowNode).height());
    const QRect missingVisible = missingRect.intersected(view.viewport()->rect())
        .intersected(view.panes().at(view.paneIndexOfColumn(6)).viewportRect);
    if (!missingVisible.isEmpty())
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(missingVisible.center()), QModelIndex());
    view.collapse(model.index(0, 0));
    view.flushPendingRelayout();
    if (cells)
        QVERIFY(!view.cellWidget(wideNode.siblingAtColumn(6)));
    else
        QVERIFY(!view.widgetForIndex(wideNode));
    view.expand(model.index(0, 0));
    verify();
}

void TestTreeTableViewInteraction::accessibilityHandlesSchemaSpansAndVisibility_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (frozen ? "-frozen" : "-plain");
            QTest::newRow(name.constData()) << cells << frozen;
        }
    }
}

void TestTreeTableViewInteraction::accessibilityHandlesSchemaSpansAndVisibility()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QStandardItemModel source;
    auto outer = row(QStringLiteral("outer"));
    auto wide = row(QStringLiteral("wide"));
    for (const QString &name : {QStringLiteral("wide-first"), QStringLiteral("wide-second")}) {
        auto items = row(name);
        items.append(new QStandardItem(name + QStringLiteral(" extra3")));
        items.append(new QStandardItem(name + QStringLiteral(" extra4")));
        wide.first()->appendRow(items);
    }
    auto narrow = row(QStringLiteral("narrow"));
    narrow.first()->appendRow(new QStandardItem(QStringLiteral("narrow-leaf")));
    outer.first()->appendRow(wide);
    outer.first()->appendRow(narrow);
    for (int i = 0; i < 50; ++i)
        outer.first()->appendRow(row(QStringLiteral("tail-%1").arg(i)));
    source.appendRow(outer);
    NodeNameFilterProxy proxy;
    proxy.setSourceModel(&source);
    RowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&proxy);
    const QModelIndex root = proxy.index(0, 0);
    const QPersistentModelIndex wideNode(proxy.index(0, 0, root));
    const QPersistentModelIndex narrowNode(proxy.index(1, 0, root));
    const QPersistentModelIndex first(proxy.index(0, 0, wideNode));
    view.setRootIndex(root);
    view.expand(wideNode);
    view.expand(narrowNode);
    if (frozen) {
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({2});
    }
    showView(&view, QSize(440, 300));
    QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(interface);
    auto *table = interface->tableInterface();
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 3);
    QCOMPARE(table->rowCount(), 55);
    QVERIFY(!table->cellAt(1, 3));
    QVERIFY(!table->cellAt(4, 1));
    QAccessibleInterface *wideItem = interface->child(0);
    QVERIFY(wideItem);
    QCOMPARE(wideItem->text(QAccessible::Name), QStringLiteral("wide"));
    QVERIFY(wideItem->state().expanded);
    QAccessibleInterface *firstItem = nullptr;
    for (int i = 0; i < wideItem->childCount(); ++i) {
        QAccessibleInterface *child = wideItem->child(i);
        if (child && child->role() == QAccessible::TreeItem
            && child->text(QAccessible::Name) == QStringLiteral("wide-first"))
            firstItem = child;
    }
    QVERIFY(firstItem);
    QCOMPARE(firstItem->parent(), wideItem);
    QCOMPARE(wideItem->child(wideItem->indexOfChild(firstItem)), firstItem);
    QVERIFY(table->selectRow(1));
    QCOMPARE(table->selectedCellCount(), 3);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 3);
    QVERIFY(!view.selectionModel()->isSelected(QModelIndex(first).siblingAtColumn(3)));
    QVERIFY(table->selectRow(4));
    QCOMPARE(table->selectedCellCount(), 4);
    QCOMPARE(table->selectedRows(), QList<int>({1, 4}));
    QVERIFY(table->unselectRow(1));
    QCOMPARE(table->selectedCellCount(), 1);
    view.selectionModel()->clearSelection();
    QVERIFY(table->selectColumn(2));
    QCOMPARE(table->selectedCellCount(), table->rowCount() - 1);
    QCOMPARE(table->selectedColumns(), QList<int>({2}));
    QVERIFY(table->unselectColumn(2));
    QCOMPARE(table->selectedCellCount(), 0);
    view.setSpan(1, 1, 2, 2);
    view.flushPendingRelayout();
    QAccessibleInterface *anchor = table->cellAt(1, 1);
    QVERIFY(anchor);
    auto *span = anchor->tableCellInterface();
    QVERIFY(span);
    QCOMPARE(span->rowIndex(), 1);
    QCOMPARE(span->columnIndex(), 1);
    QCOMPARE(span->rowExtent(), 2);
    QCOMPARE(span->columnExtent(), frozen ? 1 : 2);
    QCOMPARE(table->cellAt(2, frozen ? 1 : 2), anchor);
    QVERIFY(!anchor->state().offscreen);
    QVERIFY(!anchor->rect().isEmpty());
    view.setColumnHidden(1, true);
    view.flushPendingRelayout();
    QVERIFY(anchor->state().invisible);
    view.setColumnHidden(1, false);
    view.collapse(wideNode);
    view.flushPendingRelayout();
    QVERIFY(wideItem->state().collapsed);
    QCOMPARE(span->rowIndex(), -1);
    QVERIFY(anchor->state().offscreen);
    view.expand(wideNode);
    view.scrollTo(first, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    QCOMPARE(span->rowIndex(), 1);
    QVERIFY(!anchor->state().offscreen);
    proxy.hideName(QStringLiteral("wide-first"));
    view.flushPendingRelayout();
    QCOMPARE(table->rowCount(), 54);
    QVERIFY(anchor->state().invalid);
    QCOMPARE(table->cellAt(1, 1)->text(QAccessible::Name), QStringLiteral("wide-second type"));
    proxy.hideName(QString());
    view.flushPendingRelayout();
    view.setRootIndex(narrowNode);
    view.flushPendingRelayout();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->columnCount(), 1);
    QVERIFY(table->selectRow(0));
    QCOMPARE(table->selectedCellCount(), 1);
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 1);
    QVERIFY(!table->selectColumn(1));
    view.setSelectionMode(VirtualItemView::SelectionMode::NoSelection);
    QVERIFY(!table->selectRow(0));
}

void TestTreeTableViewInteraction::spansFollowVisualColumnsAndProxyChanges_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::spansFollowVisualColumnsAndProxyChanges()
{
    QFETCH(bool, cells);
    const auto makeRow = [](const QString &name, int columns) {
        QList<QStandardItem *> items;
        for (int column = 0; column < columns; ++column)
            items.append(new QStandardItem(name + QString::number(column)));
        return items;
    };
    QStandardItemModel source;
    auto wide = makeRow(QStringLiteral("wide"), 5);
    for (const QString &name : {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")})
        wide.first()->appendRow(makeRow(name, 5));
    source.appendRow(wide);
    auto narrow = makeRow(QStringLiteral("narrow"), 5);
    narrow.first()->appendRow(makeRow(QStringLiteral("narrow-child"), 3));
    source.appendRow(narrow);
    NodeNameFilterProxy proxy;
    proxy.setSourceModel(&source);
    ParentTypedRowAdapter rowAdapter(5);
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(90);
    view.setColumnSpacing(6);
    view.setRowSpacing(6);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&proxy);
    const QPersistentModelIndex wideNode(proxy.index(0, 0));
    const QPersistentModelIndex narrowNode(proxy.index(1, 0));
    const QPersistentModelIndex first(proxy.index(0, 0, wideNode));
    const QPersistentModelIndex second(proxy.index(1, 0, wideNode));
    const QPersistentModelIndex narrowChild(proxy.index(0, 0, narrowNode));
    const QPersistentModelIndex anchor(QModelIndex(first).siblingAtColumn(1));
    view.expand(wideNode);
    view.expand(narrowNode);
    auto *spans = new TableSpanMap;
    spans->setSpan(anchor, 2, 3);
    spans->setSpan(QModelIndex(narrowChild).siblingAtColumn(1), 1, 3);
    view.setSpanProvider(spans, true);
    showView(&view, QSize(620, 330));
    const auto verifyCovered = [&](const QModelIndex &cell, const QModelIndex &expected) {
        view.flushPendingRelayout();
        settle();
        QCOMPARE(view.anchorIndex(cell), expected);
        const int paneIndex = view.paneIndexOfColumn(cell.column());
        QVERIFY(paneIndex >= 0);
        const QRect rect(view.columnGeometry(cell.column()).viewportX,
                         view.visualRect(cell.siblingAtColumn(0)).top(),
                         view.columnWidth(cell.column()), 28);
        const QRect visible = rect.intersected(view.panes().at(paneIndex).viewportRect)
            .intersected(view.viewport()->rect());
        QVERIFY(!visible.isEmpty());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(visible.center()), expected);
        if (cells) {
            QVERIFY(view.cellWidget(expected));
            QCOMPARE(bool(view.cellWidget(cell)), cell == expected);
        } else {
            auto *widget = static_cast<HostedRow *>(view.widgetForIndex(cell.siblingAtColumn(0)));
            QVERIFY(widget);
            QCOMPARE(widget->host(cell.column())->isVisible(), cell == expected);
        }
    };
    QCOMPARE(view.spanRect(anchor).size(), QSize(282, 62));
    verifyCovered(QModelIndex(second).siblingAtColumn(3), anchor);
    view.moveColumn(4, 2);
    verifyCovered(QModelIndex(second).siblingAtColumn(4), anchor);
    verifyCovered(QModelIndex(first).siblingAtColumn(3), QModelIndex(first).siblingAtColumn(3));
    QCOMPARE(view.spanRect(anchor).width(), 282);
    const QModelIndex narrowAnchor = QModelIndex(narrowChild).siblingAtColumn(1);
    QCOMPARE(view.spanRect(narrowAnchor).width(), 90);
    verifyCovered(QModelIndex(narrowChild).siblingAtColumn(2), QModelIndex(narrowChild).siblingAtColumn(2));
    view.setColumnHidden(4, true);
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(anchor).width(), 186);
    QCOMPARE(view.spanRect(narrowAnchor).width(), 186);
    verifyCovered(QModelIndex(second).siblingAtColumn(2), anchor);
    view.setColumnHidden(4, false);
    view.setPanes({{{0}, PaneScroll::Frozen, 0}, {{1, 4}, PaneScroll::Scrollable, 0},
                   {{2}, PaneScroll::Frozen, 0}, {{3}, PaneScroll::Scrollable, 1}});
    view.flushPendingRelayout();
    QCOMPARE(view.spanRect(anchor).width(), 186);
    verifyCovered(QModelIndex(second).siblingAtColumn(4), anchor);
    verifyCovered(QModelIndex(second).siblingAtColumn(2), QModelIndex(second).siblingAtColumn(2));
    view.setPanes({});
    proxy.sort(0, Qt::DescendingOrder);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(first.row(), 2);
    QCOMPARE(view.spanRect(anchor).height(), 28);
    verifyCovered(QModelIndex(second).siblingAtColumn(1), QModelIndex(second).siblingAtColumn(1));
    proxy.hideName(QStringLiteral("first0"));
    view.flushPendingRelayout();
    QVERIFY(!anchor.isValid());
    QCOMPARE(spans->count(), 1);
    proxy.hideName(QString());
    view.flushPendingRelayout();
    settle();
    const QModelIndex restored = proxy.index(2, 1, wideNode);
    QCOMPARE(restored.data().toString(), QStringLiteral("first1"));
    QCOMPARE(view.spanAt(restored), TableSpan());
    verifyCovered(restored, restored);
    QCOMPARE(view.spanRect(restored).size(), QSize(90, 28));
}

void TestTreeTableViewInteraction::rowAdaptersReceiveTreeSpanContext()
{
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("child 0")));
    parentRow.first()->appendRow(row(QStringLiteral("child 1")));
    model.appendRow(parentRow);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    auto *adapter = new SpanContextRowAdapter;
    view.setTableAdapter(adapter, true);
    view.setModel(&model);
    view.setFrozenColumns({0});
    view.expand(model.index(0, 0));
    view.setSpan(1, 1, 2, 2);
    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();

    const TableSpan anchorSpan = adapter->anchorSpans.value(0);
    QCOMPARE(anchorSpan.rowSpan, 2);
    QCOMPARE(anchorSpan.columnSpan, 2);
    QVERIFY(!adapter->anchorRects.value(0).isEmpty());
    QVERIFY(adapter->paneHostAvailable.value(0));
    QVERIFY(adapter->coveredColumns.value(1));
    QVERIFY(adapter->paneHostAvailable.value(1));
    QCOMPARE(adapter->anchorSpans.value(1), TableSpan());
    QVERIFY(adapter->anchorRects.value(1).isEmpty());
}

void TestTreeTableViewInteraction::crossPaneTreeSpanStopsAtPaneBoundary_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::crossPaneTreeSpanStopsAtPaneBoundary()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto items = row(QStringLiteral("root"));
    items.append(new QStandardItem(QStringLiteral("root extra 3")));
    items.append(new QStandardItem(QStringLiteral("root extra 4")));
    model.appendRow(items);

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setColumnSpacing(4);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(5), true);
    }
    view.setModel(&model);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2}, PaneScroll::Scrollable, 0},
                   {{3, 4}, PaneScroll::Scrollable, 1}});
    view.setSpan(0, 1, 1, 4);
    showView(&view, QSize(760, 260));
    view.flushPendingRelayout();
    settle();

    const QModelIndex anchor = model.index(0, 1);
    const QModelIndex nextPane = model.index(0, 3);
    const int paneIndex = view.paneIndexOfColumn(1);
    QVERIFY(paneIndex >= 0);
    const QRect merged = view.spanRect(anchor);
    QVERIFY(!merged.isEmpty());
    QVERIFY(merged.right() <= view.panes().at(paneIndex).viewportRect.right());
    QVERIFY(!view.isSpanCovered(nextPane));
    QCOMPARE(view.anchorIndex(nextPane), nextPane);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.cellRect(nextPane).center()), nextPane);
    if (cells) {
        QVERIFY(view.cellWidget(anchor));
        QVERIFY(view.cellWidget(nextPane));
    } else {
        auto *rowWidget = static_cast<HostedRow *>(view.widgetForIndex(model.index(0, 0)));
        QVERIFY(rowWidget);
        QVERIFY(rowWidget->host(1)->isVisible());
        QVERIFY(rowWidget->host(3)->isVisible());
    }
}

void TestTreeTableViewInteraction::offscreenSpanAnchorsStayMaterialized_data()
{
    QTest::addColumn<int>("spacing");
    QTest::addColumn<bool>("frozen");
    for (int spacing : {0, 6}) {
        for (bool frozen : {false, true}) {
            const QByteArray name = QByteArray::number(spacing)
                + (frozen ? "-frozen" : "-plain");
            QTest::newRow(name.constData()) << spacing << frozen;
        }
    }
}

void TestTreeTableViewInteraction::offscreenSpanAnchorsStayMaterialized()
{
    QFETCH(int, spacing);
    QFETCH(bool, frozen);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    for (int i = 0; i < 160; ++i)
        parentRow.first()->appendRow(row(QStringLiteral("child-%1").arg(i)));
    model.appendRow(parentRow);
    model.appendRow(row(QStringLiteral("tail")));
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setRowSpacing(spacing);
    view.setColumnSpacing(spacing);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    view.setCellAdapter(new CellAdapter, true);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex anchor = model.index(0, 1, parent);
    view.expand(parent);
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({2});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    view.setSpan(1, 1, 100, 2);
    showView(&view, QSize(440, 280));
    for (int middle : {40, 75}) {
        const QModelIndex covered = model.index(middle, 1, parent);
        view.scrollTo(covered.siblingAtColumn(0), VirtualItemView::PositionAtTop);
        view.flushPendingRelayout();
        settle();
        QVERIFY(view.visualRect(anchor.siblingAtColumn(0)).bottom() < 0);
        QCOMPARE(view.anchorIndex(covered), anchor);
        QVERIFY(!view.cellWidget(covered));
        auto *content = qobject_cast<QLabel *>(view.cellWidget(anchor));
        QVERIFY(content);
        QCOMPARE(content->text(), anchor.data().toString());
        QVERIFY(!content->visibleRegion().isEmpty());
        const QRect merged = view.spanRect(anchor);
        QCOMPARE(merged.height(), 100 * 28 + 99 * spacing);
        QCOMPARE(merged.width(), frozen ? 100 : 200 + spacing);
        const QRect actual(content->mapTo(view.viewport(), QPoint()), content->size());
        QCOMPARE(actual, merged);
        const QRect target(view.columnGeometry(1).viewportX,
                           view.visualRect(covered.siblingAtColumn(0)).top(), 100, 28);
        const QRect visible = target.intersected(view.viewport()->rect())
            .intersected(view.itemPaneRect(ItemPane::Type::Scrollable));
        QVERIFY(!visible.isEmpty());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(visible.center()), anchor);
        QVERIFY(view.stats().materializedItems < 60);
    }
    view.scrollTo(model.index(130, 0, parent), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(!view.cellWidget(anchor));
    const QModelIndex ordinary = model.index(40, 1, parent);
    view.scrollTo(ordinary.siblingAtColumn(0), VirtualItemView::PositionAtTop);
    view.clearSpans();
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.anchorIndex(ordinary), ordinary);
    QVERIFY(view.cellWidget(ordinary));
    QVERIFY(!view.cellWidget(anchor));
    QCOMPARE(view.cellRect(ordinary).size(), QSize(100, 28));
}

void TestTreeTableViewInteraction::ownedSpanProvidersSurviveNestedReplacement_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("finish");
    for (bool cells : {false, true}) {
        for (int stage = 0; stage < 7; ++stage) {
            for (int finish = 0; finish < 3; ++finish) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + QByteArray::number(stage) + "-finish-" + QByteArray::number(finish);
                QTest::newRow(name.constData()) << cells << stage << finish;
            }
        }
    }
}

void TestTreeTableViewInteraction::ownedSpanProvidersSurviveNestedReplacement()
{
    QFETCH(bool, cells);
    QFETCH(int, stage);
    QFETCH(int, finish);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentRow);
    int oldDestructions = 0;
    int nestedDestructions = 0;
    int oldResumptions = 0;
    int nestedResumptions = 0;
    TableSpanMap finalProvider;
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    const QModelIndex queried = model.index(0, 1, parent);
    auto *provider = new ReentrantSpanProvider;
    provider->destructions = &oldDestructions;
    provider->resumptions = &oldResumptions;
    view.setSpanProvider(provider, true);
    view.flushPendingRelayout();
    settle();
    provider->stage = stage == 4 || stage == 5 ? 0 : stage == 6 ? 2 : stage;
    provider->defaultReverse = stage >= 5;
    provider->skip = stage == 4 ? 1 : 0;
    provider->callback = [&]() {
        auto *nested = new ReentrantSpanProvider;
        nested->destructions = &nestedDestructions;
        nested->resumptions = &nestedResumptions;
        view.setSpanProvider(nested, true);
        QCOMPARE(oldDestructions, 0);
        nested->callback = [&]() {
            view.setSpanProvider(nullptr);
            QCOMPARE(nestedDestructions, 0);
            QCOMPARE(oldDestructions, 0);
        };
        nested->armed = true;
        QCOMPARE(view.spanAt(queried), TableSpan{});
        QCOMPARE(nestedDestructions, 1);
        QCOMPARE(nestedResumptions, 1);
        if (finish == 0)
            view.setSpanProvider(&finalProvider);
        else if (finish == 1)
            view.setSpanProvider(provider, true);
        else
            view.setSpanProvider(provider);
        QCOMPARE(oldDestructions, 0);
    };
    provider->armed = true;
    if (stage == 0)
        QCOMPARE(view.spanAt(queried), TableSpan{});
    else if (stage == 3)
        model.appendRow(row(QStringLiteral("inserted")));
    else if (stage == 4)
        QVERIFY(view.spanRect(queried).isEmpty());
    else
        QVERIFY(!view.anchorIndex(queried).isValid());
    QCOMPARE(oldResumptions, 1);
    QCOMPARE(oldDestructions, finish == 0 ? 1 : 0);
    QCOMPARE(view.spanProvider(), finish == 0 ? static_cast<TableSpanProvider *>(&finalProvider)
                                            : static_cast<TableSpanProvider *>(provider));
    view.setSpanProvider(nullptr);
    QCOMPARE(oldDestructions, 1);
    QCOMPARE(nestedDestructions, 1);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&model));
    QCOMPARE(view.visibleRowCount(), qsizetype(stage == 3 ? 3 : 2));
    QCOMPARE(view.anchorIndex(queried), queried);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(queried).center()), queried);
    QVERIFY(cells ? view.cellWidget(queried) : view.widgetForIndex(queried.siblingAtColumn(0)));
}

void TestTreeTableViewInteraction::spanProviderCallbacksKeepNewestState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("action");
    for (bool cells : {false, true}) {
        for (int stage = 0; stage < 7; ++stage) {
            for (int action = 0; action < 4; ++action) {
                const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                    + QByteArray::number(stage) + "-action-" + QByteArray::number(action);
                QTest::newRow(name.constData()) << cells << stage << action;
            }
        }
    }
}

void TestTreeTableViewInteraction::spanProviderCallbacksKeepNewestState()
{
    QFETCH(bool, cells);
    QFETCH(int, stage);
    QFETCH(int, action);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("child")));
    model.appendRow(parentRow);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    ReentrantSpanProvider provider;
    TableSpanMap replacementProvider;
    VirtualTreeTableView view;
    configure(&view, &model, cells);
    const QModelIndex parent = model.index(0, 0);
    view.expand(parent);
    view.setSpanProvider(&provider);
    view.flushPendingRelayout();
    settle();
    const QModelIndex queried = model.index(0, 1, parent);
    provider.stage = stage == 4 || stage == 5 ? 0 : stage == 6 ? 2 : stage;
    provider.defaultReverse = stage >= 5;
    provider.skip = stage == 4 ? 1 : 0;
    provider.callback = [&]() {
        if (action == 0)
            view.setModel(&replacement);
        else if (action == 1)
            view.setRootIndex(parent);
        else if (action == 2)
            view.setSpanProvider(&replacementProvider);
        else {
            model.clear();
            model.appendRow(row(QStringLiteral("reset")));
        }
    };
    provider.armed = true;
    if (stage == 0) {
        QCOMPARE(view.spanAt(queried), TableSpan{});
    } else if (stage == 1 || stage == 2 || stage >= 5) {
        QVERIFY(!view.anchorIndex(queried).isValid());
    } else if (stage == 3) {
        model.appendRow(row(QStringLiteral("inserted")));
    } else {
        QVERIFY(view.spanRect(queried).isEmpty());
    }
    QCOMPARE(provider.calls, 1);
    QCOMPARE(view.spanProvider(), action == 2 ? static_cast<TableSpanProvider *>(&replacementProvider)
                                            : static_cast<TableSpanProvider *>(&provider));
    QCOMPARE(view.model(), action == 0 ? static_cast<QAbstractItemModel *>(&replacement)
                                      : static_cast<QAbstractItemModel *>(&model));
    QCOMPARE(view.rootIndex(), action == 1 ? parent : QModelIndex());
    view.setSpanProvider(nullptr);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.model(), action == 0 ? static_cast<QAbstractItemModel *>(&replacement)
                                      : static_cast<QAbstractItemModel *>(&model));
    QCOMPARE(view.rootIndex(), action == 1 ? parent : QModelIndex());
    QCOMPARE(view.columnCount(), 3);
    QCOMPARE(view.visibleRowCount(), qsizetype(action == 2 ? (stage == 3 ? 3 : 2) : 1));
    QCOMPARE(view.horizontalHeaderGeometry()->sectionCount(), 3);
    for (qsizetype r = 0; r < view.visibleRowCount(); ++r) {
        const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(r);
        QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), r);
        for (int c = 0; c < 3; ++c) {
            const QModelIndex cell = node.siblingAtColumn(c);
            QVERIFY(cell.isValid());
            QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(cell).center()), cell);
            if (cells)
                QVERIFY(view.cellWidget(cell));
            else
                QVERIFY(view.widgetForIndex(node));
        }
    }
}

void TestTreeTableViewInteraction::previewMaskRefreshCallbacksKeepNewestState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("action");
    for (bool cells : {false, true}) {
        for (int action = 0; action < 5; ++action) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + QByteArray::number(action);
            QTest::newRow(name.constData()) << cells << action;
        }
    }
}

void TestTreeTableViewInteraction::previewMaskRefreshCallbacksKeepNewestState()
{
    QFETCH(bool, cells);
    QFETCH(int, action);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    for (int i = 0; i < 5; ++i)
        parentRow.first()->appendRow(row(QStringLiteral("child-%1").arg(i)));
    model.appendRow(parentRow);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    ReentrantSpanProvider provider;
    TableSpanMap replacementProvider;
    RowAdapter rowAdapter;
    CellAdapter cellAdapter;
    PreviewRefreshHookView view;
    configure(&view, &model, cells);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    view.setMaterializationMode(cells ? VirtualTableView::MaterializationMode::CellWidgets
                                     : VirtualTableView::MaterializationMode::RowWidgets);
    view.setVerticalHeaderDragEnabled(true);
    view.setHeaderAnimationDuration(0);
    view.setVerticalGridLinesVisible(true);
    view.setHorizontalGridLinesVisible(true);
    view.setVerticalGridLineColor(QColor(10, 210, 30));
    const QPersistentModelIndex parent(model.index(0, 0));
    view.expand(parent);
    view.setSpanProvider(&provider);
    view.flushPendingRelayout();
    settle();
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    const QModelIndex moving = model.index(1, 0, parent);
    const int grabY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(moving).center())).y();
    const int dropY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(model.index(3, 0, parent)).center())).y() + 10;
    sendHeaderMouse(strip, QEvent::MouseButtonPress, QPoint(4, grabY),
                    Qt::LeftButton, Qt::LeftButton);
    sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, grabY + 2),
                    Qt::NoButton, Qt::LeftButton);
    sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, dropY),
                    Qt::NoButton, Qt::LeftButton);
    settle();
    QVERIFY(strip->hasVisualSectionGeometry());
    int previewY = 0;
    QVERIFY(strip->sectionVisualX(3, &previewY));
    QVERIFY(previewY != view.visualRect(model.index(2, 0, parent)).top());
    view.setRowFollowsHeaderVisual(false);
    provider.stage = 1;
    provider.callback = [&]() {
        if (action == 0)
            view.setModel(&replacement);
        else if (action == 1)
            view.setRootIndex(parent);
        else if (action == 2)
            view.setSpanProvider(&replacementProvider);
        else if (action == 3) {
            model.clear();
            model.appendRow(row(QStringLiteral("reset")));
        } else {
            view.setMaterializationMode(cells ? VirtualTableView::MaterializationMode::RowWidgets
                                             : VirtualTableView::MaterializationMode::CellWidgets);
        }
    };
    bool refreshArmed = false;
    view.beforeMaskRefresh = [&]() {
        refreshArmed = true;
        provider.armed = true;
    };
    view.setRowFollowsHeaderVisual(true);
    QVERIFY(refreshArmed);
    QCOMPARE(provider.calls, 1);
    QCOMPARE(view.model(), action == 0 ? static_cast<QAbstractItemModel *>(&replacement)
                                      : static_cast<QAbstractItemModel *>(&model));
    QCOMPARE(view.rootIndex(), action == 1 ? QModelIndex(parent) : QModelIndex());
    QCOMPARE(view.spanProvider(), action == 2 ? static_cast<TableSpanProvider *>(&replacementProvider)
                                            : static_cast<TableSpanProvider *>(&provider));
    QCOMPARE(view.usesItemWidgets(), action == 4 ? cells : !cells);
    view.setSpanProvider(nullptr);
    view.setRowFollowsHeaderVisual(false);
    QTest::keyClick(strip, Qt::Key_Escape);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.visibleRowCount(), qsizetype(action == 0 || action == 3 ? 1
                                             : action == 1 ? 5 : 6));
    const QImage image = view.grab().toImage();
    for (qsizetype r = 0; r < view.visibleRowCount(); ++r) {
        const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(r);
        const QModelIndex cell = node.siblingAtColumn(1);
        QCOMPARE(view.anchorIndex(cell), cell);
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(cell).center()), cell);
        QVERIFY(view.usesItemWidgets() ? view.widgetForIndex(node) : view.cellWidget(cell));
        const QPoint gridPoint(view.columnGeometry(1).viewportX
                                   + view.columnGeometry(1).width - 1,
                               view.visualRect(node).center().y());
        QCOMPARE(image.pixelColor(view.viewport()->pos() + gridPoint), QColor(10, 210, 30));
    }
}

void TestTreeTableViewInteraction::partialPreviewProjectionDropsInvalidatedMasks_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("replaceProvider");
    QTest::addColumn<bool>("replacementSpan");
    for (bool cells : {false, true}) {
        for (bool replacementSpan : {false, true}) {
            const QByteArray suffix = replacementSpan ? "-new-span" : "";
            QTest::newRow((QByteArray(cells ? "cells-replace" : "rows-replace") + suffix).constData())
                << cells << true << replacementSpan;
            QTest::newRow((QByteArray(cells ? "cells-clear" : "rows-clear") + suffix).constData())
                << cells << false << replacementSpan;
        }
    }
}

void TestTreeTableViewInteraction::partialPreviewProjectionDropsInvalidatedMasks()
{
    QFETCH(bool, cells);
    QFETCH(bool, replaceProvider);
    QFETCH(bool, replacementSpan);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    for (int i = 0; i < 5; ++i)
        parentRow.first()->appendRow(row(QStringLiteral("child-%1").arg(i)));
    model.appendRow(parentRow);
    PartialPreviewSpanProvider provider;
    TableSpanMap replacement;
    PreviewRefreshHookView view;
    configure(&view, &model, cells);
    view.setVerticalHeaderDragEnabled(true);
    view.setHeaderAnimationDuration(0);
    view.setHorizontalGridLinesVisible(true);
    view.setHorizontalGridLineColor(QColor(30, 60, 220));
    const QModelIndex parent = model.index(0, 0);
    const QPersistentModelIndex anchor(model.index(0, 1, parent));
    provider.projectedAnchor = anchor;
    provider.triggerIndex = model.index(3, 0, parent);
    provider.setSpan(anchor, 2, 2);
    view.expand(parent);
    view.setSpanProvider(&provider);
    view.flushPendingRelayout();
    settle();
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    const QModelIndex moving = model.index(2, 0, parent);
    const int grabY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(moving).center())).y();
    const int dropY = strip->mapFromGlobal(view.viewport()->mapToGlobal(
        view.visualRect(model.index(4, 0, parent)).center())).y() + 10;
    sendHeaderMouse(strip, QEvent::MouseButtonPress, QPoint(4, grabY),
                    Qt::LeftButton, Qt::LeftButton);
    sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, grabY + 2),
                    Qt::NoButton, Qt::LeftButton);
    sendHeaderMouse(strip, QEvent::MouseMove, QPoint(4, dropY),
                    Qt::NoButton, Qt::LeftButton);
    settle();
    int previewY = 0;
    QVERIFY(strip->sectionVisualX(4, &previewY));
    QVERIFY(previewY != view.visualRect(model.index(3, 0, parent)).top());
    const QPoint point(view.columnGeometry(1).viewportX + 50,
                       view.visualRect(model.index(0, 0, parent)).bottom());
    const QPoint pointInView = view.viewport()->pos() + point;
    const QPoint newSpanPoint(view.viewport()->pos() + QPoint(
        view.columnGeometry(0).viewportX + 70, point.y()));
    const auto lineContains = [&](const QPoint &sample) {
        for (QWidget *line : view.findChildren<QWidget *>(QStringLiteral("vivRowGridLine"))) {
            if (!line->isVisible() || !line->geometry().contains(sample))
                continue;
            const QPoint local = sample - line->pos();
            return line->mask().isEmpty() || line->mask().contains(local);
        }
        return false;
    };
    QVERIFY(!lineContains(pointInView));
    QVERIFY(lineContains(newSpanPoint));
    view.setRowFollowsHeaderVisual(false);
    provider.callback = [&]() {
        if (replaceProvider) {
            if (replacementSpan)
                replacement.setSpan(model.index(0, 0, parent), 2, 1);
            view.setSpanProvider(&replacement);
        } else {
            view.clearSpans();
            if (replacementSpan)
                view.setSpan(1, 0, 2, 1);
        }
    };
    view.beforeMaskRefresh = [&]() { provider.armed = true; };
    view.setRowFollowsHeaderVisual(true);
    QCOMPARE(provider.calls, 1);
    QVERIFY(provider.queriesBeforeCallback >= 2);
    QCOMPARE(view.spanProvider(), replaceProvider ? static_cast<TableSpanProvider *>(&replacement)
                                                 : static_cast<TableSpanProvider *>(&provider));
    // Inspect this frame before clearing the provider, cancelling or processing events.
    QVERIFY(lineContains(pointInView));
    QCOMPARE(lineContains(newSpanPoint), !replacementSpan);
    const QImage immediate = view.grab().toImage();
    QCOMPARE(immediate.pixelColor(pointInView), QColor(30, 60, 220));
    if (replacementSpan)
        QVERIFY(immediate.pixelColor(newSpanPoint) != QColor(30, 60, 220));
    const QModelIndex formerlyCovered = QModelIndex(anchor).siblingAtColumn(2);
    QCOMPARE(view.anchorIndex(formerlyCovered), formerlyCovered);
    QTest::keyClick(strip, Qt::Key_Escape);
    view.flushPendingRelayout();
    settle();
    QVERIFY(lineContains(pointInView));
    QCOMPARE(lineContains(newSpanPoint), !replacementSpan);
    QCOMPARE(view.grab().toImage().pixelColor(pointInView), QColor(30, 60, 220));
}

void TestTreeTableViewInteraction::invalidProviderAnchorsDoNotCoverOtherNodes_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::invalidProviderAnchorsDoNotCoverOtherNodes()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto firstParent = row(QStringLiteral("first-parent"));
    auto firstChild = row(QStringLiteral("anchor"));
    firstChild.first()->appendRow(row(QStringLiteral("grandchild")));
    firstParent.first()->appendRow(firstChild);
    firstParent.first()->appendRow(row(QStringLiteral("sibling")));
    model.appendRow(firstParent);
    auto otherParent = row(QStringLiteral("other-parent"));
    otherParent.first()->appendRow(row(QStringLiteral("other-child")));
    model.appendRow(otherParent);
    VirtualTreeTableView view;
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex other = model.index(1, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex anchor = node.siblingAtColumn(1);
    const QModelIndex sibling = model.index(1, 2, parent);
    const QModelIndex unrelated = model.index(0, 1, other);
    view.expand(parent);
    view.expand(other);
    view.setSpanProvider(new MisleadingSpanProvider(anchor), true);
    showView(&view, QSize(440, 330));
    QCOMPARE(view.anchorIndex(sibling), anchor);
    const auto verifyOrdinary = [&](const QModelIndex &cell) {
        QCOMPARE(view.anchorIndex(cell), cell);
        QCOMPARE(view.cellRect(cell).size(), QSize(100, 28));
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(cell).center()), cell);
        if (cells) {
            QVERIFY(view.cellWidget(cell));
        } else {
            auto *widget = static_cast<HostedRow *>(view.widgetForIndex(cell.siblingAtColumn(0)));
            QVERIFY(widget);
            QVERIFY(widget->host(cell.column())->isVisible());
        }
    };
    verifyOrdinary(unrelated);
    view.expand(node);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanAt(anchor), TableSpan());
    verifyOrdinary(model.index(0, 1, node));
    verifyOrdinary(sibling);
    verifyOrdinary(unrelated);
    view.collapse(node);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.anchorIndex(sibling), anchor);
    view.collapse(parent);
    view.flushPendingRelayout();
    settle();
    verifyOrdinary(unrelated);
}

void TestTreeTableViewInteraction::columnZeroChangesInvalidateOnlyAffectedNodeState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("insert");
    QTest::addColumn<bool>("global");
    for (bool cells : {false, true}) {
        for (bool insert : {false, true}) {
            for (bool global : {false, true}) {
                const QByteArray name = QByteArray(cells ? "cells" : "rows")
                    + (insert ? "-insert" : "-remove") + (global ? "-global" : "-children");
                QTest::newRow(name.constData()) << cells << insert << global;
            }
        }
    }
}

void TestTreeTableViewInteraction::columnZeroChangesInvalidateOnlyAffectedNodeState()
{
    QFETCH(bool, cells);
    QFETCH(bool, insert);
    QFETCH(bool, global);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    auto childRow = row(QStringLiteral("child"));
    childRow.first()->appendRow(row(QStringLiteral("leaf")));
    parentRow.first()->appendRow(childRow);
    parentRow.first()->appendRow(row(QStringLiteral("sibling")));
    model.appendRow(parentRow);
    model.appendRow(row(QStringLiteral("unaffected")));
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex node(model.index(0, 0, parent));
    const QPersistentModelIndex cell(QModelIndex(node).siblingAtColumn(1));
    const QPersistentModelIndex peer(model.index(1, 0));
    const QPersistentModelIndex peerCell(QModelIndex(peer).siblingAtColumn(1));
    if (global)
        view.setRootIndex(parent);
    else
        view.expand(parent);
    view.expand(node);
    showView(&view, QSize(440, 330));
    const qsizetype nodeRow = view.visibilityIndex()->visibleRowForIndex(node);
    QVERIFY(nodeRow >= 0);
    view.setRowHeight(nodeRow, 63);
    view.setItemPinned(cell);
    view.setCurrentIndex(cell);
    if (!global) {
        const qsizetype peerRow = view.visibilityIndex()->visibleRowForIndex(peer);
        QVERIFY(peerRow >= 0);
        view.setRowHeight(peerRow, 57);
        view.setItemPinned(peerCell);
        view.selectionModel()->select(peerCell, QItemSelectionModel::Select);
    }
    view.flushPendingRelayout();
    settle();
    QVERIFY(view.isExpanded(node));
    QVERIFY(view.isItemPinned(cell));
    QVERIFY(view.hasExplicitRowHeight(nodeRow));
    const QModelIndex changedParent = global ? QModelIndex() : QModelIndex(parent);
    if (insert)
        QVERIFY(model.insertColumn(0, changedParent));
    else
        QVERIFY(model.removeColumn(0, changedParent));
    view.flushPendingRelayout();
    settle();
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(!view.isItemPinned(cell));
    QVERIFY(!view.isExpanded(node));
    QCOMPARE(view.columnCount(), global ? (insert ? 4 : 2) : 3);
    QCOMPARE(view.horizontalHeaderGeometry()->sectionCount(), view.columnCount());
    QVERIFY(!view.rootIndex().isValid());
    if (global) {
        QCOMPARE(view.visibleRowCount(), qsizetype(2));
        QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
        QCOMPARE(view.visibilityIndex()->expandedCount(), qsizetype(0));
    } else {
        QCOMPARE(view.visibleRowCount(), qsizetype(4));
        QVERIFY(view.isExpanded(parent));
        QCOMPARE(view.visibilityIndex()->expandedCount(), qsizetype(1));
        QVERIFY(view.isItemPinned(peerCell));
        const qsizetype peerRow = view.visibilityIndex()->visibleRowForIndex(peer);
        QCOMPARE(view.rowHeight(peerRow), 57);
        QVERIFY(view.hasExplicitRowHeight(peerRow));
        QCOMPARE(view.selectionModel()->selectedIndexes(), QModelIndexList{peerCell});
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
    }
    for (qsizetype visible = 0; visible < view.visibleRowCount(); ++visible) {
        const QModelIndex current = view.visibilityIndex()->indexAtVisibleRow(visible);
        QVERIFY(current.isValid());
        QCOMPARE(current.column(), 0);
        QCOMPARE(view.visibilityIndex()->visibleRowForIndex(current), visible);
        if (!global && current == peer)
            continue;
        QVERIFY(!view.hasExplicitRowHeight(visible));
        QCOMPARE(view.rowHeight(visible), 28);
        const QModelIndex target = current.siblingAtColumn(1);
        QVERIFY(target.isValid());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(target).center()), target);
        if (cells) {
            auto *widget = qobject_cast<QLabel *>(view.cellWidget(target));
            QVERIFY(widget);
            QCOMPARE(widget->text(), target.data().toString());
        } else {
            auto *widget = static_cast<HostedRow *>(view.widgetForIndex(current));
            QVERIFY(widget);
            QVERIFY(widget->host(1)->isVisible());
        }
    }
}

void TestTreeTableViewInteraction::crossParentMovesPreserveNodeState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("destinationExpanded");
    for (bool cells : {false, true}) {
        for (bool expanded : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (expanded ? "-expanded" : "-folded");
            QTest::newRow(name.constData()) << cells << expanded;
        }
    }
}

void TestTreeTableViewInteraction::crossParentMovesPreserveNodeState()
{
    QFETCH(bool, cells);
    QFETCH(bool, destinationExpanded);
    SiblingMoveModel model(true);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex source(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    const QPersistentModelIndex moving(model.index(1, 0, source));
    const QPersistentModelIndex cell(QModelIndex(moving).siblingAtColumn(1));
    const QPersistentModelIndex leaf(model.index(0, 0, moving));
    const QPersistentModelIndex leafCell(QModelIndex(leaf).siblingAtColumn(1));
    view.expand(source);
    view.expand(moving);
    if (destinationExpanded)
        view.expand(destination);
    showView(&view, QSize(440, 480));
    view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(moving), 63);
    view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(leaf), 41);
    view.setSpan(int(view.visibilityIndex()->visibleRowForIndex(moving)), 1, 1, 2);
    view.setCurrentIndex(cell);
    view.selectionModel()->select(leafCell, QItemSelectionModel::Select);
    view.setItemPinned(cell);
    view.flushPendingRelayout();
    settle();
    QPointer<QWidget> widget(cells ? view.cellWidget(cell) : view.widgetForIndex(moving));
    QVERIFY(widget);
    QVERIFY(model.moveRows(source, moving.row(), 1, destination, 0));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(moving.parent(), QModelIndex(destination));
    QCOMPARE(moving.row(), 0);
    QCOMPARE(leaf.parent(), QModelIndex(moving));
    QVERIFY(view.isExpanded(moving));
    QVERIFY(view.isItemPinned(cell));
    QVERIFY(view.selectionModel()->isSelected(cell));
    QVERIFY(view.selectionModel()->isSelected(leafCell));
    QVERIFY(widget);
    QCOMPARE(cells ? view.cellWidget(cell) : view.widgetForIndex(moving), widget.data());
    QCOMPARE(view.indexForWidget(widget), cells ? QModelIndex(cell) : QModelIndex(moving));
    if (!destinationExpanded) {
        QCOMPARE(view.visibilityIndex()->visibleRowForIndex(moving), qsizetype(-1));
        QVERIFY(!view.currentIndex().isValid());
        QVERIFY(widget->visibleRegion().isEmpty());
        view.expand(destination);
        view.flushPendingRelayout();
        settle();
    } else {
        QCOMPARE(view.currentIndex(), QModelIndex(cell));
    }
    const auto verifyVisibleState = [&]() {
        QVERIFY(view.isExpanded(moving));
        const qsizetype movingRow = view.visibilityIndex()->visibleRowForIndex(moving);
        const qsizetype leafRow = view.visibilityIndex()->visibleRowForIndex(leaf);
        QVERIFY(movingRow >= 0);
        QCOMPARE(leafRow, movingRow + 1);
        QCOMPARE(view.rowHeight(movingRow), 63);
        QCOMPARE(view.rowHeight(leafRow), 41);
        QVERIFY(view.hasExplicitRowHeight(movingRow));
        QVERIFY(view.hasExplicitRowHeight(leafRow));
        QCOMPARE(view.spanAt(cell), (TableSpan{1, 2}));
        QCOMPARE(view.spanRect(cell).size(), QSize(200, 63));
        QCOMPARE(view.anchorIndex(QModelIndex(moving).siblingAtColumn(2)), QModelIndex(cell));
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.spanRect(cell).center()),
                 QModelIndex(cell));
        QCOMPARE(view.verticalHeaderGeometry()->sectionSize(int(movingRow)), 63);
        QCOMPARE(view.verticalHeaderGeometry()->sectionSize(int(leafRow)), 41);
        QCOMPARE(cells ? view.cellWidget(cell) : view.widgetForIndex(moving), widget.data());
        QVERIFY(!widget->visibleRegion().isEmpty());
        QVERIFY(view.isItemPinned(cell));
        QVERIFY(view.selectionModel()->isSelected(cell));
        QVERIFY(view.selectionModel()->isSelected(leafCell));
    };
    verifyVisibleState();
    QVERIFY(model.moveRows(destination, moving.row(), 1, source, 0));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(moving.parent(), QModelIndex(source));
    QCOMPARE(moving.row(), 0);
    verifyVisibleState();
    QCOMPARE(model.moveCalls, 2);
    const QModelIndex displaced = model.index(1, 0, source);
    const qsizetype displacedRow = view.visibilityIndex()->visibleRowForIndex(displaced);
    QCOMPARE(displaced.data().toString(), QStringLiteral("child 0"));
    QVERIFY(!view.hasExplicitRowHeight(displacedRow));
    QCOMPARE(view.rowHeight(displacedRow), 28);
    QVERIFY(!view.isItemPinned(displaced));
    QVERIFY(!view.selectionModel()->isSelected(displaced.siblingAtColumn(1)));
}

void TestTreeTableViewInteraction::crossParentMoveUnderProxyFilter_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("sorted");
    QTest::newRow("rows") << false << false;
    QTest::newRow("cells") << true << false;
    QTest::newRow("rows-sorted") << false << true;
    QTest::newRow("cells-sorted") << true << true;
}

void TestTreeTableViewInteraction::crossParentMoveUnderProxyFilter()
{
    QFETCH(bool, cells);
    QFETCH(bool, sorted);
    SiblingMoveModel source(true);
    NodeNameFilterProxy proxy;
    proxy.setSourceModel(&source);
    proxy.hideName(QStringLiteral("child 0"));
    if (sorted)
        proxy.sort(0, Qt::AscendingOrder);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&proxy);

    const QPersistentModelIndex sourceParent(source.index(0, 0));
    const QPersistentModelIndex sourceDestination(source.index(1, 0));
    const QPersistentModelIndex sourceMoving(source.index(1, 0, sourceParent));
    const QModelIndex parent = proxy.mapFromSource(sourceParent);
    const QModelIndex destination = proxy.mapFromSource(sourceDestination);
    const QModelIndex moving = proxy.mapFromSource(sourceMoving);
    const QModelIndex movingCell = moving.siblingAtColumn(1);
    QVERIFY(parent.isValid());
    QVERIFY(destination.isValid());
    QVERIFY(moving.isValid());
    QCOMPARE(moving.row(), 0); // child 0 is filtered out
    view.expand(parent);
    view.expand(destination);
    view.expand(moving);
    showView(&view, QSize(440, 360));
    const QPersistentModelIndex sourceLeaf(source.index(0, 0, sourceMoving));
    const QModelIndex leafCell = proxy.mapFromSource(sourceLeaf).siblingAtColumn(1);
    view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(moving), 47);
    view.setSpan(int(view.visibilityIndex()->visibleRowForIndex(moving)), 1, 1, 2);
    view.setCurrentIndex(movingCell);
    view.selectionModel()->select(movingCell, QItemSelectionModel::Select);
    view.selectionModel()->select(leafCell, QItemSelectionModel::Select);
    view.setItemPinned(movingCell);
    view.flushPendingRelayout();
    settle();
    QPointer<QWidget> widget(cells ? view.cellWidget(movingCell) : view.widgetForIndex(moving));
    QVERIFY(widget);

    QVERIFY(source.moveRows(sourceParent, sourceMoving.row(), 1, sourceDestination, 0));
    view.flushPendingRelayout();
    settle();

    const QModelIndex moved = proxy.mapFromSource(sourceMoving);
    const QModelIndex movedCell = moved.siblingAtColumn(1);
    QVERIFY(moved.isValid());
    QCOMPARE(moved.parent(), proxy.mapFromSource(sourceDestination));
    QCOMPARE(moved.row(), 0);
    QCOMPARE(view.currentIndex(), movedCell);
    QVERIFY(view.selectionModel()->isSelected(movedCell));
    QVERIFY(view.isItemPinned(movedCell));
    QVERIFY(view.visibilityIndex()->visibleRowForIndex(moved) >= 0);
    QVERIFY(widget);
    QCOMPARE(cells ? view.cellWidget(movedCell) : view.widgetForIndex(moved), widget.data());
    QCOMPARE(view.indexForWidget(widget), cells ? movedCell : moved);
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(movedCell).center()),
             movedCell);
    const auto verifyCombinedState = [&]() {
        const QModelIndex currentNode = proxy.mapFromSource(sourceMoving);
        const QModelIndex currentCell = currentNode.siblingAtColumn(1);
        const QModelIndex currentLeaf = proxy.mapFromSource(sourceLeaf);
        const qsizetype visible = view.visibilityIndex()->visibleRowForIndex(currentNode);
        QVERIFY(visible >= 0);
        QCOMPARE(currentNode.parent(), proxy.mapFromSource(sourceDestination));
        QCOMPARE(view.currentIndex(), currentCell);
        QVERIFY(view.isExpanded(currentNode));
        QVERIFY(view.isItemPinned(currentCell));
        QVERIFY(view.selectionModel()->isSelected(currentCell));
        QVERIFY(view.selectionModel()->isSelected(currentLeaf.siblingAtColumn(1)));
        QCOMPARE(view.visibilityIndex()->visibleRowForIndex(currentLeaf), visible + 1);
        QCOMPARE(view.rowHeight(visible), 47);
        QVERIFY(view.hasExplicitRowHeight(visible));
        QCOMPARE(view.anchorIndex(currentNode.siblingAtColumn(2)), currentCell);
        QCOMPARE(cells ? view.cellWidget(currentCell) : view.widgetForIndex(currentNode), widget.data());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(currentCell).center()),
                 currentCell);
    };
    verifyCombinedState();
    if (sorted) {
        proxy.sort(0, Qt::DescendingOrder);
        view.flushPendingRelayout();
        settle();
        verifyCombinedState();
        proxy.hideName(QStringLiteral("other child"));
        proxy.sort(0, Qt::AscendingOrder);
        view.flushPendingRelayout();
        settle();
        verifyCombinedState();
        proxy.hideName(QString());
        proxy.sort(0, Qt::DescendingOrder);
        view.flushPendingRelayout();
        settle();
        verifyCombinedState();
    }
}

void TestTreeTableViewInteraction::crossParentProxyChangesKeepRemoteScrollAnchor_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (frozen ? "-frozen" : "-plain");
            QTest::newRow(name.constData()) << cells << frozen;
        }
    }
}

void TestTreeTableViewInteraction::crossParentProxyChangesKeepRemoteScrollAnchor()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    SiblingMoveModel source(true, false, 180);
    NodeNameFilterProxy proxy;
    proxy.setSourceModel(&source);
    proxy.sort(0, Qt::AscendingOrder);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    InspectTreeTableView view;
    view.setUniformItemHeight(28);
    view.setAutoMeasureItemHeight(false);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&proxy);
    const QPersistentModelIndex sourceParent(source.index(0, 0));
    const QPersistentModelIndex destination(source.index(1, 0));
    const QPersistentModelIndex sourceAnchor(source.index(53, 0, sourceParent));
    view.expand(proxy.mapFromSource(sourceParent));
    view.expand(proxy.mapFromSource(destination));
    if (frozen) {
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.setFrozenColumns({0});
    }
    showView(&view, QSize(440, 350));
    const QModelIndex initialNode = proxy.mapFromSource(sourceAnchor);
    const QModelIndex initialCell = initialNode.siblingAtColumn(1);
    view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(initialNode), 47);
    view.setSpan(int(view.visibilityIndex()->visibleRowForIndex(initialNode)), 1, 1, 2);
    view.setCurrentIndex(initialCell);
    view.selectionModel()->select(initialCell, QItemSelectionModel::Select);
    view.setItemPinned(initialCell);
    view.flushPendingRelayout();
    settle();
    view.scrollTo(initialNode, VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    const int anchorY = view.visualRect(initialNode).top();
    QCOMPARE(QModelIndex(view.captureAnchor().index), initialNode);
    QVERIFY(view.visibilityIndex()->visibleRowForIndex(initialNode) > 100);
    QPointer<QWidget> widget(cells ? view.cellWidget(initialCell) : view.widgetForIndex(initialNode));
    QVERIFY(widget);
    const auto verifyAnchor = [&]() {
        view.flushPendingRelayout();
        settle();
        const QModelIndex node = proxy.mapFromSource(sourceAnchor);
        const QModelIndex cell = node.siblingAtColumn(1);
        const qsizetype visible = view.visibilityIndex()->visibleRowForIndex(node);
        QVERIFY(node.isValid());
        QVERIFY(visible >= 0);
        QCOMPARE(view.visualRect(node).top(), anchorY);
        QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(visible), node);
        QCOMPARE(view.currentIndex(), cell);
        QVERIFY(view.selectionModel()->isSelected(cell));
        QVERIFY(view.isItemPinned(cell));
        QCOMPARE(view.rowHeight(visible), 47);
        QVERIFY(view.hasExplicitRowHeight(visible));
        QCOMPARE(view.anchorIndex(node.siblingAtColumn(2)), cell);
        QCOMPARE(cells ? view.cellWidget(cell) : view.widgetForIndex(node), widget.data());
        QVERIFY(widget);
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(cell).center()), cell);
    };
    QVERIFY(source.moveRows(sourceParent, sourceAnchor.row(), 1, destination, 90));
    verifyAnchor();
    QCOMPARE(sourceAnchor.parent(), QModelIndex(destination));
    proxy.sort(0, Qt::DescendingOrder);
    verifyAnchor();
    proxy.hideName(QStringLiteral("destination extra 90"));
    proxy.sort(0, Qt::AscendingOrder);
    verifyAnchor();
    proxy.hideName(QString());
    proxy.sort(0, Qt::DescendingOrder);
    verifyAnchor();
    QVERIFY(source.moveRows(destination, sourceAnchor.row(), 1, sourceParent, 53));
    verifyAnchor();
    QCOMPARE(sourceAnchor.parent(), QModelIndex(sourceParent));
    proxy.sort(0, Qt::AscendingOrder);
    verifyAnchor();
}

void TestTreeTableViewInteraction::crossParentMoveRespectsDestinationSchema_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::crossParentMoveRespectsDestinationSchema()
{
    QFETCH(bool, cells);
    SiblingMoveModel model(true, true);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);

    const QPersistentModelIndex source(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    const QPersistentModelIndex moving(model.index(1, 0, source));
    const QPersistentModelIndex sourceCell(QModelIndex(moving).siblingAtColumn(2));
    QVERIFY(sourceCell.isValid());
    QVERIFY(!model.index(0, 2, destination).isValid());

    view.expand(source);
    view.expand(destination);
    showView(&view, QSize(440, 320));
    view.setCurrentIndex(sourceCell);
    view.selectionModel()->select(sourceCell, QItemSelectionModel::Select);
    view.setItemPinned(sourceCell);
    view.flushPendingRelayout();
    settle();
    QPointer<QWidget> widget(cells ? view.cellWidget(sourceCell) : view.widgetForIndex(moving));
    QVERIFY(widget);

    QVERIFY(model.moveRows(source, moving.row(), 1, destination, 0));
    view.flushPendingRelayout();
    settle();

    QCOMPARE(moving.parent(), QModelIndex(destination));
    QCOMPARE(moving.row(), 0);
    QVERIFY(!model.index(0, 2, destination).isValid());
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(!view.selectionModel()->isSelected(sourceCell));
    QVERIFY(!view.isItemPinned(sourceCell));
    if (cells)
        QVERIFY(!view.cellWidget(sourceCell));
    else
        QVERIFY(widget);
    QVERIFY(view.visibilityIndex()->visibleRowForIndex(moving) >= 0);
    const QModelIndex surviving = QModelIndex(moving).siblingAtColumn(1);
    QVERIFY(surviving.isValid());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(surviving).center()),
             surviving);
    if (cells)
        QVERIFY(view.cellWidget(surviving) != nullptr);
    else
        QCOMPARE(view.indexForWidget(widget), QModelIndex(moving));
}

void TestTreeTableViewInteraction::crossParentMoveCombinesSchemaSpanAndFrozenState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::crossParentMoveCombinesSchemaSpanAndFrozenState()
{
    QFETCH(bool, cells);
    SiblingMoveModel model(true, true);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setColumnSpacing(4);
    view.setRowSpacing(4);
    view.setFrozenColumns({0});
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);

    const QPersistentModelIndex source(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    const QPersistentModelIndex moving(model.index(0, 0, source));
    const QPersistentModelIndex movingCell(QModelIndex(moving).siblingAtColumn(1));
    view.expand(source);
    view.expand(destination);
    showView(&view, QSize(560, 360));
    view.setCurrentIndex(movingCell);
    view.selectionModel()->select(movingCell, QItemSelectionModel::Select);
    view.setItemPinned(movingCell);
    auto *spans = new TableSpanMap;
    spans->setSpan(movingCell, 2, 2);
    view.setSpanProvider(spans, true);
    view.flushPendingRelayout();
    settle();
    QPointer<QWidget> widget(cells ? view.cellWidget(movingCell)
                                   : view.widgetForIndex(moving));
    QVERIFY(widget);
    QCOMPARE(view.spanAt(movingCell), (TableSpan{2, 2}));

    QVERIFY(model.moveRows(source, moving.row(), 1, destination, 0));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(moving.parent(), QModelIndex(destination));
    QCOMPARE(moving.row(), 0);
    QVERIFY(movingCell.isValid());
    QVERIFY(!QModelIndex(moving).siblingAtColumn(2).isValid());
    QCOMPARE(view.currentIndex(), QModelIndex(movingCell));
    QVERIFY(view.selectionModel()->isSelected(movingCell));
    QVERIFY(view.isItemPinned(movingCell));
    QCOMPARE(view.anchorIndex(movingCell), QModelIndex(movingCell));
    QCOMPARE(view.spanRect(movingCell).width(), view.columnWidth(1));
    QVERIFY(widget);
    QCOMPARE(cells ? view.cellWidget(movingCell) : view.widgetForIndex(moving), widget.data());
    QCOMPARE(view.indexForWidget(widget), cells ? QModelIndex(movingCell) : QModelIndex(moving));

    QVERIFY(model.moveRows(destination, moving.row(), 1, source, 0));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(moving.parent(), QModelIndex(source));
    QVERIFY(QModelIndex(moving).siblingAtColumn(2).isValid());
    QCOMPARE(view.currentIndex(), QModelIndex(movingCell));
    QVERIFY(view.isItemPinned(movingCell));
    QCOMPARE(view.spanAt(movingCell), (TableSpan{2, 2}));
    QVERIFY(view.spanRect(movingCell).width() >= view.columnWidth(1) + view.columnWidth(2));
    QVERIFY(widget);
    QCOMPARE(cells ? view.cellWidget(movingCell) : view.widgetForIndex(moving), widget.data());
}

void TestTreeTableViewInteraction::crossParentMoveOutOfLimitedRootClearsState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::crossParentMoveOutOfLimitedRootClearsState()
{
    QFETCH(bool, cells);
    SiblingMoveModel model(true);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex source(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    const QPersistentModelIndex moving(model.index(1, 0, source));
    const QPersistentModelIndex cell(QModelIndex(moving).siblingAtColumn(1));
    view.setRootIndex(source);
    view.expand(moving);
    showView(&view, QSize(440, 320));
    view.setCurrentIndex(cell);
    view.selectionModel()->select(cell, QItemSelectionModel::Select);
    view.setItemPinned(cell);
    view.flushPendingRelayout();
    settle();
    QPointer<QWidget> widget(cells ? view.cellWidget(cell) : view.widgetForIndex(moving));
    QVERIFY(widget);
    QVERIFY(view.visibilityIndex()->isVisible(moving));

    QVERIFY(model.moveRows(source, moving.row(), 1, destination, 0));
    view.flushPendingRelayout();
    settle();

    QVERIFY(view.rootIndex().isValid());
    QVERIFY(!view.visibilityIndex()->isVisible(moving));
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QVERIFY(!view.currentIndex().isValid());
    QVERIFY(!view.selectionModel()->isSelected(cell));
    QVERIFY(!view.isItemPinned(cell));
    if (cells)
        QVERIFY(!view.cellWidget(cell));
    else
        QVERIFY(!view.widgetForIndex(moving));
}

void TestTreeTableViewInteraction::crossParentMovePreservesCrossRowSpan_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::crossParentMovePreservesCrossRowSpan()
{
    QFETCH(bool, cells);
    SiblingMoveModel model(true);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setColumnSpacing(0);
    view.setRowSpacing(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex source(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    const QPersistentModelIndex anchorNode(model.index(0, 0, source));
    const QPersistentModelIndex anchor(QModelIndex(anchorNode).siblingAtColumn(1));
    view.expand(source);
    view.expand(destination);
    auto *spans = new TableSpanMap;
    spans->setSpan(anchor, 2, 2);
    view.setSpanProvider(spans, true);
    showView(&view, QSize(440, 320));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanAt(anchor), (TableSpan{2, 2}));

    QVERIFY(model.moveRows(source, anchorNode.row(), 1, destination, 0));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(anchorNode.parent(), QModelIndex(destination));
    QCOMPARE(anchorNode.row(), 0);
    QCOMPARE(view.spanAt(anchor), (TableSpan{2, 2}));
    const QModelIndex covered = model.index(1, 1, destination);
    QVERIFY(covered.isValid());
    QCOMPARE(view.anchorIndex(covered), QModelIndex(anchor));
    const QRect merged = view.spanRect(anchor);
    QVERIFY(merged.height() > view.rowHeight(view.visibilityIndex()->visibleRowForIndex(anchor)));
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(merged.topLeft() + QPoint(8, 8)),
             QModelIndex(anchor));
    if (cells) {
        QVERIFY(view.cellWidget(anchor));
        QVERIFY(!view.cellWidget(covered));
    } else {
        auto *rowWidget = static_cast<HostedRow *>(view.widgetForIndex(anchorNode));
        QVERIFY(rowWidget);
        QVERIFY(rowWidget->host(1)->isVisible());
    }

    view.collapse(destination);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanAt(anchor), TableSpan{});
    view.expand(destination);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanAt(anchor), (TableSpan{2, 2}));
}

void TestTreeTableViewInteraction::crossParentMoveKeepsRemoteSpanAnchorMaterialized_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::crossParentMoveKeepsRemoteSpanAnchorMaterialized()
{
    QFETCH(bool, cells);
    SiblingMoveModel model(true, false, 160);
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex source(model.index(0, 0));
    const QPersistentModelIndex destination(model.index(1, 0));
    const QPersistentModelIndex anchorNode(model.index(0, 0, source));
    const QPersistentModelIndex anchor(QModelIndex(anchorNode).siblingAtColumn(1));
    view.expand(source);
    view.expand(destination);
    auto *spans = new TableSpanMap;
    spans->setSpan(anchor, 100, 2);
    view.setSpanProvider(spans, true);
    showView(&view, QSize(440, 280));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.spanAt(anchor), (TableSpan{100, 2}));
    QVERIFY(view.visibilityIndex()->isVisible(anchorNode));

    QVERIFY(model.moveRows(source, anchorNode.row(), 1, destination, 0));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(anchorNode.parent(), QModelIndex(destination));
    QCOMPARE(anchorNode.row(), 0);
    QCOMPARE(view.spanAt(anchor), (TableSpan{100, 2}));

    const QModelIndex covered = model.index(75, 1, destination);
    QVERIFY(covered.isValid());
    view.scrollTo(covered.siblingAtColumn(0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(view.visualRect(anchorNode).bottom() < 0);
    QCOMPARE(view.anchorIndex(covered), QModelIndex(anchor));
    QVERIFY(!view.cellWidget(covered));
    const QRect merged = view.spanRect(anchor);
    QCOMPARE(merged.height(), 100 * 28);
    QVERIFY(!merged.isEmpty());
    QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                 view.visualRect(covered.siblingAtColumn(0)).center()), QModelIndex(anchor));
    if (cells) {
        QVERIFY(view.cellWidget(anchor));
        QVERIFY(!view.cellWidget(anchor)->visibleRegion().isEmpty());
    } else {
        auto *rowWidget = static_cast<HostedRow *>(view.widgetForIndex(anchorNode));
        QVERIFY(rowWidget);
        QVERIFY(rowWidget->host(1));
    }
    QVERIFY(view.stats().materializedItems < 80);
}

void TestTreeTableViewInteraction::popupEditorsStayPinnedUntilClosed_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<bool>("migrate");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            for (bool migrate : {false, true}) {
                const QByteArray name = QByteArray(cells ? "cells" : "rows")
                    + (frozen ? "-frozen" : "-plain") + (migrate ? "-migrate" : "-fixed");
                QTest::newRow(name.constData()) << cells << frozen << migrate;
            }
        }
    }
}

void TestTreeTableViewInteraction::popupEditorsStayPinnedUntilClosed()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QFETCH(bool, migrate);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("edited")));
    model.appendRow(parentRow);
    for (int i = 0; i < 120; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    PopupRowAdapter rowAdapter;
    PopupCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    const QModelIndex binding = cells ? cell : node;
    view.expand(parent);
    if (frozen) {
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({2});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    showView(&view, QSize(440, 280));
    const auto owningWidget = [&]() {
        return cells ? view.cellWidget(cell) : view.widgetForIndex(node);
    };
    QPointer<QWidget> widget(owningWidget());
    QVERIFY(widget);
    QPointer<QComboBox> editor(cells ? qobject_cast<QComboBox *>(widget.data())
                                   : widget->findChild<QComboBox *>());
    QVERIFY(editor);
    editor->setCurrentIndex(2);
    view.activateWindow();
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QTest::qWaitForWindowActive(&view));
    }
    editor->setFocus(Qt::MouseFocusReason);
    editor->showPopup();
    settle();
    // Native styles can activate the popup only after the combo's opening animation.
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget(), 2000);
    QPointer<QWidget> popup(QApplication::activePopupWidget());
    QVERIFY(popup);
    QVERIFY(popup->isVisible());
    QWidget *owner = popup;
    while (owner && owner != editor)
        owner = owner->parentWidget();
    QCOMPARE(owner, editor.data());
    view.setFocus();
    QCOMPARE(QApplication::activePopupWidget(), popup.data());
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(&view));
    const QModelIndex highlighted = editor->model()->index(1, editor->modelColumn(), editor->rootModelIndex());
    editor->view()->setCurrentIndex(highlighted);
    const auto verifyPinned = [&]() {
        QVERIFY(widget);
        QVERIFY(editor);
        QVERIFY(popup);
        QCOMPARE(QApplication::activePopupWidget(), popup.data());
        QVERIFY(popup->isVisible());
        QCOMPARE(owningWidget(), widget.data());
        QCOMPARE(view.indexForWidget(widget), binding);
        QCOMPARE(widget->property("boundIndex").value<QPersistentModelIndex>(),
                 QPersistentModelIndex(binding));
        QCOMPARE(editor->currentIndex(), 2);
        QCOMPARE(editor->currentText(), QStringLiteral("draft"));
        QCOMPARE(editor->view()->currentIndex(), highlighted);
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
    };
    const auto migratePanes = [&]() {
        if (!migrate)
            return;
        for (int layout = 0; layout < 4; ++layout) {
            view.setPanes({});
            view.setFrozenColumns(layout == 0 ? QVector<int>{1} : QVector<int>{0});
            view.setFrozenRightColumns(layout == 1 ? QVector<int>{1} : QVector<int>{2});
            view.setFrozenRows(layout == 0 ? 2 : 1);
            view.setFrozenBottomRows(layout == 2 ? 2 : 1);
            if (layout == 3) {
                const auto pane = [](QVector<int> columns, PaneScroll scroll, int group) {
                    TablePaneSpec result;
                    result.logicalColumns = columns;
                    result.scroll = scroll;
                    result.scrollGroup = group;
                    return result;
                };
                view.setPanes({pane({0}, PaneScroll::Frozen, 0),
                               pane({2}, PaneScroll::Scrollable, 0),
                               pane({1}, PaneScroll::Scrollable, 1)});
            }
            view.flushPendingRelayout();
            settle();
            verifyPinned();
        }
    };
    migratePanes();
    view.scrollTo(model.index(80, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    migratePanes();
    view.collapse(parent);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), qsizetype(-1));
    QVERIFY(widget->visibleRegion().isEmpty());
    migratePanes();
    view.expand(parent);
    view.scrollTo(node, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    QVERIFY(!editor->visibleRegion().isEmpty());
    editor->hidePopup();
    editor->clearFocus();
    view.setFocus();
    settle();
    QVERIFY(!QApplication::activePopupWidget());
    view.scrollTo(model.index(100, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(!owningWidget());
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
    QVERIFY(!view.isItemPinned(cell));
}

void TestTreeTableViewInteraction::popupMenusKeepActionsAcrossPaneMigration_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::popupMenusKeepActionsAcrossPaneMigration()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("edited")));
    model.appendRow(parentRow);
    for (int i = 0; i < 120; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    PopupRowAdapter rowAdapter;
    PopupCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    const QModelIndex binding = cells ? cell : node;
    view.expand(parent);
    showView(&view, QSize(440, 280));
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        view.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QTest::qWaitForWindowActive(&view));
    }
    const auto owningWidget = [&]() {
        return cells ? view.cellWidget(cell) : view.widgetForIndex(node);
    };
    QPointer<QWidget> widget(owningWidget());
    QVERIFY(widget);
    QPointer<QComboBox> editor(cells ? qobject_cast<QComboBox *>(widget.data())
                                   : widget->findChild<QComboBox *>());
    QVERIFY(editor);
    QPointer<QMenu> menu(new QMenu(editor));
    menu->addAction(QStringLiteral("Discard"));
    QAction *commit = menu->addAction(QStringLiteral("Commit draft"));
    QSignalSpy committed(commit, &QAction::triggered);
    QPersistentModelIndex submitted;
    connect(commit, &QAction::triggered, &view, [&]() {
        submitted = widget->property("boundIndex").value<QPersistentModelIndex>();
    });
    editor->setCurrentIndex(2);
    menu->popup(editor->mapToGlobal(QPoint(0, editor->height())));
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::activePopupWidget(), menu.data(), 2000);
    menu->setActiveAction(commit);
    view.setFocus();
    const auto verifyPinned = [&]() {
        QVERIFY(widget);
        QVERIFY(editor);
        QVERIFY(menu);
        QCOMPARE(QApplication::activePopupWidget(), menu.data());
        QVERIFY(menu->isVisible());
        QCOMPARE(menu->activeAction(), commit);
        QCOMPARE(owningWidget(), widget.data());
        QCOMPARE(view.indexForWidget(widget), binding);
        QCOMPARE(editor->currentText(), QStringLiteral("draft"));
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
        QCOMPARE(committed.count(), 0);
    };
    const auto pane = [](QVector<int> columns, PaneScroll scroll, int group) {
        TablePaneSpec spec;
        spec.logicalColumns = columns;
        spec.scroll = scroll;
        spec.scrollGroup = group;
        return spec;
    };
    for (int layout = 0; layout < 3; ++layout) {
        view.setPanes({});
        view.setFrozenColumns(layout == 0 ? QVector<int>{1} : QVector<int>{0});
        view.setFrozenRightColumns(layout == 1 ? QVector<int>{1} : QVector<int>{2});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        if (layout == 2)
            view.setPanes({pane({0}, PaneScroll::Frozen, 0),
                           pane({2}, PaneScroll::Scrollable, 0),
                           pane({1}, PaneScroll::Scrollable, 1)});
        view.flushPendingRelayout();
        settle();
        verifyPinned();
    }
    view.scrollTo(model.index(80, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    view.collapse(parent);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), qsizetype(-1));
    QVERIFY(widget->visibleRegion().isEmpty());
    view.expand(parent);
    view.scrollTo(node, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    QTest::keyClick(menu, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(committed.count(), 1, 2000);
    QCOMPARE(submitted, QPersistentModelIndex(binding));
    QTRY_VERIFY_WITH_TIMEOUT(!QApplication::activePopupWidget(), 2000);
    editor->clearFocus();
    view.setFocus();
    view.scrollTo(model.index(100, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(!owningWidget());
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestTreeTableViewInteraction::completionPopupKeepsEditorIdentity_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::completionPopupKeepsEditorIdentity()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("edited")));
    model.appendRow(parentRow);
    for (int i = 0; i < 120; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    EditorRowAdapter rowAdapter;
    EditorCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setColumnOverscan(0);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    const QModelIndex binding = cells ? cell : node;
    view.expand(parent);
    showView(&view, QSize(440, 280));
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        view.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QTest::qWaitForWindowActive(&view));
    }
    const auto owningWidget = [&]() {
        return cells ? view.cellWidget(cell) : view.widgetForIndex(node);
    };
    QPointer<QWidget> widget(owningWidget());
    QVERIFY(widget);
    QPointer<QLineEdit> editor(cells ? qobject_cast<QLineEdit *>(widget.data())
                                   : widget->findChild<QLineEdit *>());
    QVERIFY(editor);
    auto *completer = new QCompleter(
        QStringList{QStringLiteral("draft alpha"), QStringLiteral("draft beta")}, editor);
    editor->setCompleter(completer);
    editor->setText(QStringLiteral("draft"));
    editor->setFocus();
    completer->setCompletionPrefix(editor->text());
    completer->complete();
    QPointer<QAbstractItemView> popup(completer->popup());
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::activePopupWidget(), popup.data(), 2000);
    const QPersistentModelIndex highlighted(popup->model()->index(1, 0));
    popup->setCurrentIndex(highlighted);
    const QString previewDraft = editor->text();
    QVERIFY(!previewDraft.isEmpty());
    QSignalSpy activated(completer, QOverload<const QString &>::of(&QCompleter::activated));
    QPersistentModelIndex submitted;
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated), &view,
            [&](const QString &) {
        submitted = widget->property("boundIndex").value<QPersistentModelIndex>();
    });
    view.setFocus();
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(&view));
    const auto verifyPinned = [&]() {
        QVERIFY(widget);
        QVERIFY(editor);
        QVERIFY(popup);
        QCOMPARE(QApplication::activePopupWidget(), popup.data());
        QVERIFY(popup->isVisible());
        QCOMPARE(popup->currentIndex(), QModelIndex(highlighted));
        QCOMPARE(editor->text(), previewDraft);
        QCOMPARE(owningWidget(), widget.data());
        QCOMPARE(view.indexForWidget(widget), binding);
        QCOMPARE(widget->property("boundIndex").value<QPersistentModelIndex>(),
                 QPersistentModelIndex(binding));
        QCOMPARE(view.stats().pinnedWidgets, qsizetype(1));
        QCOMPARE(activated.count(), 0);
    };
    view.setRowSpacing(2);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    for (int layout = 0; layout < 2; ++layout) {
        view.setFrozenColumns(layout == 0 ? QVector<int>{1} : QVector<int>{0});
        view.setFrozenRightColumns(layout == 1 ? QVector<int>{1} : QVector<int>{2});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
        view.flushPendingRelayout();
        settle();
        verifyPinned();
    }
    view.scrollTo(model.index(80, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    view.collapse(parent);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    QVERIFY(widget->visibleRegion().isEmpty());
    view.expand(parent);
    view.scrollTo(node, VirtualItemView::EnsureVisible);
    view.flushPendingRelayout();
    settle();
    verifyPinned();
    QTest::keyClick(popup, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 2000);
    QCOMPARE(activated.at(0).at(0).toString(), QStringLiteral("draft beta"));
    QCOMPARE(editor->text(), QStringLiteral("draft beta"));
    QCOMPARE(submitted, QPersistentModelIndex(binding));
    QTRY_VERIFY_WITH_TIMEOUT(!QApplication::activePopupWidget(), 2000);
    editor->clearFocus();
    view.setFocus();
    view.scrollTo(model.index(100, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QVERIFY(!owningWidget());
    QCOMPARE(view.stats().pinnedWidgets, qsizetype(0));
}

void TestTreeTableViewInteraction::completionSubmissionRetiresView_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("synchronous");
    QTest::newRow("rows") << false << false;
    QTest::newRow("cells") << true << false;
    QTest::newRow("rows-synchronous-signal") << false << true;
    QTest::newRow("cells-synchronous-signal") << true << true;
}

void TestTreeTableViewInteraction::completionSubmissionRetiresView()
{
    QFETCH(bool, cells);
    QFETCH(bool, synchronous);
    QStandardItemModel model;
    auto parentItems = row(QStringLiteral("parent"));
    parentItems.first()->appendRow(row(QStringLiteral("edited")));
    model.appendRow(parentItems);
    EditorRowAdapter rowAdapter;
    EditorCellAdapter cellAdapter;
    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    if (cells) {
        view->setCellAdapter(&cellAdapter);
        view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view->setTableAdapter(&rowAdapter);
    }
    view->setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = node.siblingAtColumn(1);
    view->expand(parent);
    showView(view, QSize(440, 280));
    QPointer<QWidget> widget(cells ? view->cellWidget(cell) : view->widgetForIndex(node));
    QVERIFY(widget);
    QPointer<QLineEdit> editor(cells ? qobject_cast<QLineEdit *>(widget.data())
                                   : widget->findChild<QLineEdit *>());
    QVERIFY(editor);
    QPointer<QCompleter> completer = new QCompleter(
        QStringList{QStringLiteral("draft alpha"), QStringLiteral("draft beta")}, editor);
    editor->setCompleter(completer);
    editor->setText(QStringLiteral("draft"));
    editor->setFocus();
    completer->setCompletionPrefix(editor->text());
    completer->complete();
    QPointer<QAbstractItemView> popup(completer->popup());
    QTRY_COMPARE(QApplication::activePopupWidget(), popup.data());
    popup->setCurrentIndex(popup->model()->index(1, 0));
    view->setFrozenColumns({1});
    view->setFrozenRows(1);
    view->flushPendingRelayout();
    settle();
    QVERIFY(popup);
    QCOMPARE(QApplication::activePopupWidget(), popup.data());
    int submissions = 0;
    QPersistentModelIndex submitted;
    QString value;
    QObject receiver;
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated), &receiver,
            [&](const QString &text) {
        ++submissions;
        value = text;
        submitted = widget->property("boundIndex").value<QPersistentModelIndex>();
        if (synchronous)
            delete view.data();
        else
            view->deleteLater();
    });
    if (synchronous) {
        // Direct signal delivery isolates the application slot from Qt's key event stack.
        QVERIFY(QMetaObject::invokeMethod(completer, "activated", Qt::DirectConnection,
                                         Q_ARG(QString, QStringLiteral("draft beta"))));
        QVERIFY(!view);
        QVERIFY(!widget);
        QVERIFY(!editor);
        QVERIFY(!completer);
        QVERIFY(!popup);
    } else {
        QTest::keyClick(popup, Qt::Key_Return);
    }
    QTRY_VERIFY(!view);
    QCOMPARE(submissions, 1);
    QCOMPARE(value, QStringLiteral("draft beta"));
    QCOMPARE(submitted, QPersistentModelIndex(cells ? cell : node));
    QVERIFY(!widget);
    QVERIFY(!editor);
    QVERIFY(!completer);
    QVERIFY(!popup);
    QVERIFY(!QApplication::activePopupWidget());
    QCOMPARE(model.rowCount(parent), 1);
}

void TestTreeTableViewInteraction::completionMigrationCallbacksKeepNewestState_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<int>("action");
    for (bool cells : {false, true}) {
        for (int action = 0; action < 3; ++action) {
            const QByteArray name = QByteArray(cells ? "cells-" : "rows-")
                + QByteArray::number(action);
            QTest::newRow(name.constData()) << cells << action;
        }
    }
}

void TestTreeTableViewInteraction::completionMigrationCallbacksKeepNewestState()
{
    QFETCH(bool, cells);
    QFETCH(int, action);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QStringLiteral("edited")));
    model.appendRow(parentRow);
    QStandardItemModel replacement;
    replacement.appendRow(row(QStringLiteral("replacement")));
    EditorRowAdapter rowAdapter;
    EditorCellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex node(model.index(0, 0, parent));
    const QModelIndex cell = QModelIndex(node).siblingAtColumn(1);
    view.expand(parent);
    view.setFrozenColumns({0});
    showView(&view, QSize(440, 280));
    QPointer<QWidget> widget(cells ? view.cellWidget(cell) : view.widgetForIndex(node));
    QVERIFY(widget);
    QPointer<QLineEdit> editor(cells ? qobject_cast<QLineEdit *>(widget.data())
                                   : widget->findChild<QLineEdit *>());
    QVERIFY(editor);
    auto *completer = new QCompleter(
        QStringList{QStringLiteral("draft alpha"), QStringLiteral("draft beta")}, editor);
    editor->setCompleter(completer);
    editor->setText(QStringLiteral("draft"));
    editor->setFocus();
    completer->setCompletionPrefix(editor->text());
    completer->complete();
    QPointer<QAbstractItemView> popup(completer->popup());
    QTRY_COMPARE(QApplication::activePopupWidget(), popup.data());
    const QPersistentModelIndex highlighted(popup->model()->index(1, 0));
    popup->setCurrentIndex(highlighted);
    QSignalSpy submitted(completer, QOverload<const QString &>::of(&QCompleter::activated));
    view.setFocus();
    int callbacks = 0;
    connect(popup->selectionModel(), &QItemSelectionModel::currentChanged, &view,
            [&](const QModelIndex &current, const QModelIndex &) {
        if (current != highlighted || callbacks)
            return;
        ++callbacks;
        if (action == 0)
            popup->hide();
        else if (action == 1)
            view.setRootIndex(parent);
        else
            view.setModel(&replacement);
    });
    PopupMigrationObserver observer;
    QWidget *migrating = cells ? widget.data() : editor->parentWidget();
    migrating->installEventFilter(&observer);
    observer.onParentChange = [&]() {
        // Business widgets may reset their popup highlight when reparented.
        popup->hide();
        popup->setCurrentIndex(QModelIndex());
    };
    view.setFrozenColumns({1});
    view.flushPendingRelayout();
    settle();
    QCOMPARE(observer.changes, 1);
    QCOMPARE(callbacks, 1);
    QCOMPARE(submitted.count(), 0);
    QCOMPARE(view.model(), action == 2 ? static_cast<QAbstractItemModel *>(&replacement)
                                      : static_cast<QAbstractItemModel *>(&model));
    QCOMPARE(view.rootIndex(), action == 1 ? QModelIndex(parent) : QModelIndex());
    QCOMPARE(view.visibleRowCount(), qsizetype(action == 0 ? 2 : 1));
    if (action == 0) {
        QVERIFY(popup);
        QVERIFY(!popup->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(editor->text(), QStringLiteral("draft beta"));
    }
    for (qsizetype r = 0; r < view.visibleRowCount(); ++r) {
        const QModelIndex visibleNode = view.visibilityIndex()->indexAtVisibleRow(r);
        const QModelIndex visibleCell = visibleNode.siblingAtColumn(1);
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(
                     view.cellRect(visibleCell).center()), visibleCell);
        QWidget *currentWidget = cells ? view.cellWidget(visibleCell) : view.widgetForIndex(visibleNode);
        QVERIFY(currentWidget);
        QCOMPARE(currentWidget->property("boundIndex").value<QPersistentModelIndex>(),
                 QPersistentModelIndex(cells ? visibleCell : visibleNode));
    }
    if (popup)
        popup->hide();
}

void TestTreeTableViewInteraction::largeSelectionKeepsMaterializationBounded_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::largeSelectionKeepsMaterializationBounded()
{
    QFETCH(bool, cells);
    QStandardItemModel model;
    model.setColumnCount(4);
    for (int rowIndex = 0; rowIndex < 10000; ++rowIndex) {
        QList<QStandardItem *> items;
        for (int column = 0; column < 4; ++column)
            items.append(new QStandardItem(QStringLiteral("%1:%2").arg(rowIndex).arg(column)));
        model.appendRow(items);
    }

    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setOverscan(0, 0);
    view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    view.setSelectionBehavior(VirtualTableView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new RowAdapter, true);
    }
    view.setModel(&model);
    showView(&view, QSize(620, 320));
    view.flushPendingRelayout();
    settle();
    QVERIFY(view.stats().materializedItems > 0);
    QVERIFY(view.stats().materializedItems <= 96);

    const QModelIndex first = model.index(0, 0);
    const QModelIndex last = model.index(9999, 3);
    view.selectionModel()->select(QItemSelection(first, last),
                                  QItemSelectionModel::ClearAndSelect);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 40000);
    QVERIFY(view.selectionModel()->isSelected(last));
    QVERIFY(view.stats().materializedItems <= 96);

    view.scrollTo(model.index(5000, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 40000);
    QVERIFY(view.selectionModel()->isSelected(model.index(5000, 3)));
    QVERIFY(view.stats().materializedItems <= 96);
}

void TestTreeTableViewInteraction::scaleCostsStayBoundedAgainstExistingViews_data()
{
    QTest::addColumn<bool>("cells");
    QTest::newRow("rows") << false;
    QTest::newRow("cells") << true;
}

void TestTreeTableViewInteraction::scaleCostsStayBoundedAgainstExistingViews()
{
    QFETCH(bool, cells);
    quint64 previousScrollQueries[3] = {};
    for (int roots : {1000, 10000}) {
        QueryCountingTreeModel model;
        const auto wideRow = [](const QString &name) {
            QList<QStandardItem *> items;
            for (int column = 0; column < 12; ++column)
                items.append(new QStandardItem(name + QString::number(column)));
            return items;
        };
        for (int i = 0; i < roots; ++i)
            model.appendRow(wideRow(QStringLiteral("root-%1-").arg(i)));
        QStandardItem *branchItem = model.item(0);
        for (int i = 0; i < 300; ++i)
            branchItem->appendRow(wideRow(QStringLiteral("child-%1-").arg(i)));
        QStandardItem *deep = branchItem->child(0);
        for (int depth = 0; depth < 32; ++depth) {
            deep->appendRow(wideRow(QStringLiteral("depth-%1-").arg(depth)));
            deep = deep->child(0);
        }
        VirtualTreeTableView treeTable;
        VirtualTableView table;
        VirtualTreeView tree;
        tree.setAdapter(new RowAdapter, true);
        for (VirtualTableView *view : {static_cast<VirtualTableView *>(&treeTable), &table}) {
            view->setDefaultColumnWidth(100);
            view->setColumnOverscan(1);
            if (cells) {
                view->setCellAdapter(new CellAdapter, true);
                view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
            } else {
                view->setTableAdapter(new RowAdapter, true);
            }
        }
        const QModelIndex branch = model.index(0, 0);
        const QModelIndex deepRoot = model.index(0, 0, branch);
        VirtualItemView *views[] = {&treeTable, &table, &tree};
        const char *names[] = {"tree-table", "table", "tree"};
        for (int kind = 0; kind < 3; ++kind) {
            VirtualItemView *view = views[kind];
            view->setUniformItemHeight(28);
            view->setOverscan(1, 1);
            model.queries = 0;
            QElapsedTimer timer;
            timer.start();
            view->setModel(&model);
            if (kind == 0) {
                treeTable.expand(branch);
                treeTable.expandRecursively(deepRoot);
            } else if (kind == 2) {
                tree.expand(branch);
                tree.expandRecursively(deepRoot);
            }
            showView(view, QSize(620, 420));
            const qint64 initialUs = timer.nsecsElapsed() / 1000;
            const quint64 initialQueries = model.queries;
            const VirtualViewStats initial = view->stats();
            QVERIFY(initial.materializedItems > 0);
            const qsizetype limit = kind != 2 && cells ? 160 : 25;
            QVERIFY(initial.materializedItems <= limit);
            model.queries = 0;
            timer.restart();
            for (int pass = 0; pass < 8; ++pass) {
                const QModelIndex target = model.index(pass % 2 == 0 ? roots / 2 : roots * 3 / 4, 0);
                view->scrollTo(target, VirtualItemView::PositionAtTop);
                view->flushPendingRelayout();
                settle();
                QVERIFY(view->stats().materializedItems <= limit);
                QVERIFY(!view->visualRect(target).intersected(view->viewport()->rect()).isEmpty());
            }
            const qint64 scrollUs = timer.nsecsElapsed() / 1000;
            const quint64 scrollQueries = model.queries;
            const VirtualViewStats scrolled = view->stats();
            QVERIFY(scrolled.recycleCount > initial.recycleCount);
            QVERIFY(scrolled.createCount - initial.createCount <= quint64(limit));
            if (roots == 10000)
                QVERIFY2(scrollQueries <= previousScrollQueries[kind] * 2 + 1000,
                         "Tenfold root growth must not make fixed-window scrolling scan the model.");
            previousScrollQueries[kind] = scrollQueries;
            model.queries = 0;
            timer.restart();
            const QModelIndex anchor = model.index(roots / 2, 0);
            view->scrollTo(anchor, VirtualItemView::PositionAtTop);
            view->flushPendingRelayout();
            const int anchorY = view->visualRect(anchor).top();
            model.queries = 0;
            timer.restart();
            if (kind != 1) {
                for (int pass = 0; pass < 10; ++pass) {
                    if (kind == 0) {
                        treeTable.collapse(branch);
                        treeTable.expand(branch);
                    } else {
                        tree.collapse(branch);
                        tree.expand(branch);
                    }
                    view->flushPendingRelayout();
                    settle();
                    QCOMPARE(view->visualRect(anchor).top(), anchorY);
                    QVERIFY(view->stats().materializedItems <= limit);
                }
            }
            qInfo().nospace() << "scale roots=" << roots << " mode=" << (cells ? "cells" : "rows")
                << " view=" << names[kind] << " initial_us=" << initialUs
                << " initial_queries=" << initialQueries << " initial_widgets=" << initial.materializedItems
                << " scroll8_us=" << scrollUs << " scroll8_queries=" << scrollQueries
                << " scroll_created=" << scrolled.createCount - initial.createCount
                << " scroll_recycled=" << scrolled.recycleCount - initial.recycleCount
                << " fold_cycles=" << (kind == 1 ? 0 : 10)
                << " fold_us=" << timer.nsecsElapsed() / 1000 << " fold_queries=" << model.queries;
            view->hide();
        }
    }
}

void TestTreeTableViewInteraction::realtimeAnimationCostsStayWindowBound_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("advanced");
    for (bool cells : {false, true}) {
        for (bool advanced : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (advanced ? "-advanced" : "-plain");
            QTest::newRow(name.constData()) << cells << advanced;
        }
    }
}

void TestTreeTableViewInteraction::realtimeAnimationCostsStayWindowBound()
{
    QFETCH(bool, cells);
    QFETCH(bool, advanced);
    quint64 previousQueriesPerFrame[3] = {};
    for (int roots : {1000, 10000}) {
        QueryCountingTreeModel model;
        const auto wideRow = [](const QString &name) {
            QList<QStandardItem *> items;
            for (int column = 0; column < 12; ++column)
                items.append(new QStandardItem(name + QString::number(column)));
            return items;
        };
        for (int i = 0; i < roots; ++i)
            model.appendRow(wideRow(QStringLiteral("root-%1-").arg(i)));
        QStandardItem *branchItem = model.item(0);
        for (int i = 0; i < 300; ++i)
            branchItem->appendRow(wideRow(QStringLiteral("child-%1-").arg(i)));
        QStandardItem *deep = branchItem->child(0);
        QStandardItem *targetItem = nullptr;
        for (int depth = 0; depth < 32; ++depth) {
            deep->appendRow(wideRow(QStringLiteral("depth-%1-").arg(depth)));
            deep = deep->child(0);
            if (depth == 15)
                targetItem = deep;
        }
        VirtualTreeTableView treeTable;
        VirtualTableView table;
        VirtualTreeView tree;
        treeTable.setIndentation(6);
        tree.setIndentation(6);
        treeTable.setVisualStateBackgroundVisible(true);
        tree.setVisualStateBackgroundVisible(true);
        tree.setAdapter(new RowAdapter, true);
        const auto pane = [](QVector<int> columns, PaneScroll scroll, int group) {
            TablePaneSpec spec;
            spec.logicalColumns = columns;
            spec.scroll = scroll;
            spec.scrollGroup = group;
            return spec;
        };
        for (VirtualTableView *view : {static_cast<VirtualTableView *>(&treeTable), &table}) {
            view->setDefaultColumnWidth(100);
            view->setColumnOverscan(1);
            view->setVisualStateScope(cells ? VirtualTableView::VisualStateScope::Cell
                                           : VirtualTableView::VisualStateScope::Row);
            if (cells) {
                view->setCellAdapter(new CellAdapter, true);
                view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
            } else {
                view->setTableAdapter(new RowAdapter, true);
            }
            if (advanced)
                view->setPanes({pane({0}, PaneScroll::Frozen, 0),
                               pane({1, 2, 3, 4}, PaneScroll::Scrollable, 0),
                               pane({5}, PaneScroll::Frozen, 0),
                               pane({6, 7, 8, 9, 10}, PaneScroll::Scrollable, 1),
                               pane({11}, PaneScroll::Frozen, 0)});
        }
        VirtualItemView *views[] = {&treeTable, &table, &tree};
        const char *names[] = {"tree-table", "table", "tree"};
        const QModelIndex branch = model.index(0, 0);
        const QModelIndex deepRoot = model.index(0, 0, branch);
        for (int kind = 0; kind < 3; ++kind) {
            VirtualItemView *view = views[kind];
            view->setUniformItemHeight(28);
            view->setOverscan(1, 1);
            view->setVisualStateAnimationDuration(0);
            view->setSelectionBehavior(cells && kind != 2
                ? VirtualItemView::SelectionBehavior::SelectItems
                : VirtualItemView::SelectionBehavior::SelectRows);
            if (advanced) {
                view->setFrozenRows(1);
                view->setFrozenBottomRows(1);
            }
            view->setModel(&model);
            if (kind == 0) {
                treeTable.expand(branch);
                treeTable.expandRecursively(deepRoot);
            } else if (kind == 2) {
                tree.expand(branch);
                tree.expandRecursively(deepRoot);
            }
            showView(view, QSize(620, 420));
            if (QGuiApplication::platformName() == QStringLiteral("windows")) {
                view->activateWindow();
                QVERIFY(QTest::qWaitForWindowExposed(view));
                QVERIFY(QTest::qWaitForWindowActive(view));
            }
            const QModelIndex node = kind == 1 ? model.index(roots / 2, 0) : targetItem->index();
            const QModelIndex target = kind == 2 ? node : node.siblingAtColumn(1);
            const QModelIndex stateIndex = cells && kind != 2 ? target : node;
            view->scrollTo(node, VirtualItemView::PositionAtCenter);
            view->flushPendingRelayout();
            QCursor::setPos(view->mapToGlobal(QPoint(-20, -20)));
            QEvent leave(QEvent::Leave);
            QCoreApplication::sendEvent(view->viewport(), &leave);
            settle();
            const qreal dpr = view->devicePixelRatioF();
            QImage frame(QSize(qRound(view->width() * dpr), qRound(view->height() * dpr)),
                         QImage::Format_ARGB32_Premultiplied);
            frame.setDevicePixelRatio(dpr);
            const auto renderFrame = [&]() {
                frame.fill(Qt::transparent);
                QPainter painter(&frame);
                view->render(&painter);
            };
            renderFrame();
            const qsizetype widgetLimit = cells && kind != 2 ? 256 : 25;
            const VirtualViewStats before = view->stats();
            QVERIFY(before.materializedItems > 0);
            QVERIFY(before.materializedItems <= widgetLimit);
            view->setVisualStateAnimationDuration(400);
            QVERIFY(!view->visualState(stateIndex).hovered);
            QVERIFY(!view->visualState(stateIndex).selected);
            QVector<qint64> frameUs;
            QVector<qint64> intervalsUs;
            quint64 frameQueries = 0;
            int hoverIntermediateFrames = 0;
            int selectedIntermediateFrames = 0;
            qsizetype peakWidgets = before.materializedItems;
            QElapsedTimer wall;
            wall.start();
            qint64 previousTickUs = 0;
            QTimer sampler;
            sampler.setTimerType(Qt::PreciseTimer);
            sampler.setInterval(16);
            connect(&sampler, &QTimer::timeout, view, [&]() {
                const qint64 tickUs = wall.nsecsElapsed() / 1000;
                if (previousTickUs)
                    intervalsUs.append(tickUs - previousTickUs);
                previousTickUs = tickUs;
                const quint64 queriesBefore = model.queries;
                QElapsedTimer cpu;
                cpu.start();
                renderFrame();
                frameUs.append(cpu.nsecsElapsed() / 1000);
                frameQueries += model.queries - queriesBefore;
                const auto state = view->visualState(stateIndex);
                if (state.hoverProgress > 0.0 && state.hoverProgress < 1.0)
                    ++hoverIntermediateFrames;
                if (state.selectedProgress > 0.0 && state.selectedProgress < 1.0)
                    ++selectedIntermediateFrames;
                peakWidgets = qMax(peakWidgets, view->stats().materializedItems);
            });
            QRect targetRect = kind == 2 ? view->visualRect(node)
                : static_cast<VirtualTableView *>(view)->cellRect(target);
            targetRect = targetRect.intersected(view->viewport()->rect());
            QVERIFY(!targetRect.isEmpty());
            const QPoint hoverPoint(targetRect.right() - 3, targetRect.center().y());
            QCursor::setPos(view->viewport()->mapToGlobal(hoverPoint));
            sendHeaderMouse(view->viewport(), QEvent::MouseMove, hoverPoint, Qt::NoButton, Qt::NoButton);
            view->setCurrentIndex(target);
            const auto started = view->visualState(stateIndex);
            QVERIFY(started.hovered);
            QVERIFY(started.selected);
            QVERIFY(started.hoverProgress < 1.0);
            QVERIFY(started.selectedProgress < 1.0);
            sampler.start();
            // Let Qt's animation clock advance normally; never pause or setCurrentTime.
            QTest::qWait(500);
            sampler.stop();
            QVERIFY(frameUs.size() >= 3);
            QVERIFY(hoverIntermediateFrames > 0);
            QVERIFY(selectedIntermediateFrames > 0);
            QCOMPARE(view->visualState(stateIndex).hoverProgress, qreal(1.0));
            QCOMPARE(view->visualState(stateIndex).selectedProgress, qreal(1.0));
            for (QVariantAnimation *animation : view->findChildren<QVariantAnimation *>())
                QVERIFY(animation->state() != QAbstractAnimation::Running);
            QVERIFY(peakWidgets <= widgetLimit);
            QCOMPARE(view->stats().createCount, before.createCount);
            QCOMPARE(view->stats().recycleCount, before.recycleCount);
            const quint64 queriesPerFrame = frameQueries / quint64(frameUs.size());
            if (roots == 10000)
                QVERIFY2(queriesPerFrame <= previousQueriesPerFrame[kind] * 2 + 1000,
                         "Tenfold model growth must not turn animation painting into a model scan.");
            previousQueriesPerFrame[kind] = queriesPerFrame;
            std::sort(frameUs.begin(), frameUs.end());
            std::sort(intervalsUs.begin(), intervalsUs.end());
            qInfo().nospace() << "realtime roots=" << roots << " mode=" << (cells ? "cells" : "rows")
                << " layout=" << (advanced ? "advanced" : "plain") << " view=" << names[kind]
                << " frames=" << frameUs.size() << " hover_mid=" << hoverIntermediateFrames
                << " selected_mid=" << selectedIntermediateFrames
                << " frame_median_us=" << frameUs.at(frameUs.size() / 2)
                << " frame_p95_us=" << frameUs.at((frameUs.size() - 1) * 95 / 100)
                << " frame_max_us=" << frameUs.last()
                << " interval_p95_us=" << intervalsUs.at((intervalsUs.size() - 1) * 95 / 100)
                << " queries_per_frame=" << queriesPerFrame << " peak_widgets=" << peakWidgets;
            view->setVisualStateAnimationDuration(0);
            view->hide();
        }
    }
}

void TestTreeTableViewInteraction::realtimeHeaderPreviewCostsStayWindowBound_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("advanced");
    QTest::addColumn<bool>("flat");
    for (bool cells : {false, true}) {
        for (bool advanced : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (advanced ? "-advanced" : "-plain");
            QTest::newRow(name.constData()) << cells << advanced << false;
            QTest::newRow((name + "-flat").constData()) << cells << advanced << true;
        }
    }
}

void TestTreeTableViewInteraction::realtimeHeaderPreviewCostsStayWindowBound()
{
    QFETCH(bool, cells);
    QFETCH(bool, advanced);
    QFETCH(bool, flat);
    quint64 previousQueriesPerFrame[2] = {};
    for (int roots : {1000, 10000}) {
        QueryCountingTreeModel model;
        const auto wideRow = [](const QString &name) {
            QList<QStandardItem *> items;
            for (int column = 0; column < 12; ++column)
                items.append(new QStandardItem(name + QString::number(column)));
            return items;
        };
        for (int i = 0; i < roots; ++i)
            model.appendRow(wideRow(QStringLiteral("root-%1-").arg(i)));
        QStandardItem *branchItem = model.item(0);
        for (int i = 0; i < 300; ++i)
            branchItem->appendRow(wideRow(QStringLiteral("child-%1-").arg(i)));
        QStandardItem *deep = branchItem->child(0);
        for (int depth = 0; depth < 32; ++depth) {
            deep->appendRow(wideRow(QStringLiteral("depth-%1-").arg(depth)));
            deep = deep->child(0);
        }
        if (flat) {
            model.item(0)->removeRows(0, model.item(0)->rowCount());
            for (int i = 0; i < 332; ++i)
                model.appendRow(wideRow(QStringLiteral("flattened-%1-").arg(i)));
        }
        std::unique_ptr<VirtualTableView> ownedView(flat
            ? static_cast<VirtualTableView *>(new VirtualTableView)
            : static_cast<VirtualTableView *>(new VirtualTreeTableView));
        VirtualTableView &view = *ownedView;
        auto *treeView = qobject_cast<VirtualTreeTableView *>(&view);
        view.setUniformItemHeight(28);
        view.setDefaultColumnWidth(100);
        view.setOverscan(1, 1);
        view.setColumnOverscan(1);
        view.setRowSpacing(6);
        view.setColumnSpacing(6);
        view.setVerticalHeaderDragEnabled(true);
        view.setColumnDragEnabled(true);
        view.setHeaderAnimationEnabled(true);
        view.setHeaderAnimationDuration(400);
        if (treeView)
            treeView->setVisualStateBackgroundVisible(true);
        view.setVisualStateAnimationDuration(0);
        view.setVisualStateScope(VirtualTableView::VisualStateScope::Cell);
        view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
        if (cells) {
            view.setCellAdapter(new CellAdapter, true);
            view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
        } else {
            view.setTableAdapter(new RowAdapter, true);
        }
        view.setModel(&model);
        const QModelIndex branch = flat ? QModelIndex() : model.index(0, 0);
        const int nodeRow = flat ? 83 : 50;
        const QPersistentModelIndex node(model.index(nodeRow, 0, branch));
        const QPersistentModelIndex target(model.index(nodeRow, 1, branch));
        const QPersistentModelIndex neighbour(model.index(nodeRow + 1, 0, branch));
        if (treeView) {
            treeView->expand(branch);
            treeView->expandRecursively(model.index(0, 0, branch));
        }
        if (advanced) {
            view.setPanes({{{0}, PaneScroll::Frozen, 0},
                           {{1, 2, 3, 4}, PaneScroll::Scrollable, 0},
                           {{5}, PaneScroll::Frozen, 0},
                           {{6, 7, 8, 9, 10}, PaneScroll::Scrollable, 1},
                           {{11}, PaneScroll::Frozen, 0}});
            view.setFrozenRows(1);
            view.setFrozenBottomRows(1);
        }
        showView(&view, QSize(900, 420));
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            view.activateWindow();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QVERIFY(QTest::qWaitForWindowActive(&view));
        }
        view.scrollTo(node, VirtualItemView::PositionAtCenter);
        const int visibleNodeRow = treeView
            ? int(treeView->visibilityIndex()->visibleRowForIndex(node)) : node.row();
        view.setSpan(visibleNodeRow, 1, 2, 2);
        view.setCurrentIndex(target);
        view.flushPendingRelayout();
        QCursor::setPos(view.mapToGlobal(QPoint(-20, -20)));
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(view.viewport(), &leave);
        settle();
        const QRect committedSpan = view.spanRect(target);
        const QRect committedNode = view.visualRect(node);
        const int committedColumn = view.columnGeometry(1).viewportX;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            QVERIFY(header->sectionAnimationEnabled());
            QCOMPARE(header->sectionAnimationDuration(), 400);
        }
        view.setHeaderAnimationEnabled(false);
        view.setHeaderAnimationDuration(250);
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            QVERIFY(!header->sectionAnimationEnabled());
            QCOMPARE(header->sectionAnimationDuration(), 250);
        }
        view.setHeaderAnimationEnabled(true);
        view.setHeaderAnimationDuration(400);
        const qreal dpr = view.devicePixelRatioF();
        QImage frame(QSize(qRound(view.width() * dpr), qRound(view.height() * dpr)),
                     QImage::Format_ARGB32_Premultiplied);
        frame.setDevicePixelRatio(dpr);
        const auto render = [&]() {
            frame.fill(Qt::transparent);
            QPainter painter(&frame);
            view.render(&painter);
        };
        render();
        for (int kind = 0; kind < 2; ++kind) {
            VirtualHeaderView *header = nullptr;
            if (kind == 0) {
                header = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
            } else {
                for (VirtualHeaderView *candidate : view.findChildren<VirtualHeaderView *>()) {
                    QWidget *section = candidate->sectionWidget(1);
                    if (candidate->orientation() == Qt::Horizontal && section
                        && !section->visibleRegion().isEmpty()) {
                        header = candidate;
                        break;
                    }
                }
            }
            QVERIFY(header);
            const int moving = kind == 0 ? visibleNodeRow : 1;
            const int neighbourSection = kind == 0
                ? (treeView ? int(treeView->visibilityIndex()->visibleRowForIndex(neighbour))
                            : neighbour.row()) : 2;
            QWidget *neighbourWidget = header->sectionWidget(neighbourSection);
            QVERIFY(neighbourWidget);
            const QPoint neighbourPosition = neighbourWidget->mapTo(&view, QPoint())
                - view.viewport()->pos();
            const int neighbourBefore = kind == 0 ? neighbourPosition.y() : neighbourPosition.x();
            QPoint grab;
            QPoint drop;
            if (kind == 0) {
                grab = QPoint(4, header->mapFromGlobal(view.viewport()->mapToGlobal(
                    view.visualRect(node).center())).y());
                drop = QPoint(4, header->mapFromGlobal(view.viewport()->mapToGlobal(
                    view.visualRect(model.index(nodeRow + 2, 0, branch)).center())).y() + 10);
            } else {
                QWidget *section = header->sectionWidget(moving);
                QWidget *destination = header->sectionWidget(3);
                QVERIFY(section);
                QVERIFY(destination);
                grab = section->mapTo(header, section->rect().center());
                drop = destination->mapTo(header, destination->rect().center()) + QPoint(10, 0);
            }
            const VirtualViewStats before = view.stats();
            const qsizetype limit = cells ? 400 : 25;
            QVERIFY(before.materializedItems > 0 && before.materializedItems <= limit);
            QVector<qint64> renderUs;
            QVector<qint64> intervalsUs;
            int intermediateFrames = 0;
            qsizetype peakWidgets = before.materializedItems;
            QElapsedTimer wall;
            QTimer sampler;
            sampler.setTimerType(Qt::PreciseTimer);
            sampler.setInterval(16);
            qint64 previousTickUs = 0;
            connect(&sampler, &QTimer::timeout, &view, [&]() {
                const qint64 tickUs = wall.nsecsElapsed() / 1000;
                if (previousTickUs)
                    intervalsUs.append(tickUs - previousTickUs);
                previousTickUs = tickUs;
                bool intermediate = false;
                for (QVariantAnimation *animation : header->findChildren<QVariantAnimation *>()) {
                    if (animation->duration() == 400
                        && animation->state() == QAbstractAnimation::Running
                        && animation->currentTime() > 0 && animation->currentTime() < 400)
                        intermediate = true;
                }
                if (intermediate)
                    ++intermediateFrames;
                QElapsedTimer cpu;
                cpu.start();
                render();
                renderUs.append(cpu.nsecsElapsed() / 1000);
                peakWidgets = qMax(peakWidgets, view.stats().materializedItems);
            });
            const quint64 queriesBefore = model.queries;
            wall.start();
            sampler.start();
            sendHeaderMouse(header, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
            sendHeaderMouse(header, QEvent::MouseMove, grab + (kind == 0 ? QPoint(0, 2) : QPoint(2, 0)),
                            Qt::NoButton, Qt::LeftButton);
            sendHeaderMouse(header, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton);
            // Both the header clock and its body updates run naturally during this wait.
            QTest::qWait(650);
            sampler.stop();
            const quint64 queries = model.queries - queriesBefore;
            QVERIFY(renderUs.size() >= 3);
            QVERIFY(intermediateFrames > 0);
            int neighbourAfter = 0;
            QVERIFY(header->sectionVisualX(neighbourSection, &neighbourAfter));
            QVERIFY(neighbourAfter != neighbourBefore);
            for (QVariantAnimation *animation : header->findChildren<QVariantAnimation *>())
                QVERIFY(animation->state() != QAbstractAnimation::Running);
            QCOMPARE(view.spanRect(target), committedSpan);
            QCOMPARE(view.visualRect(node), committedNode);
            QCOMPARE(view.currentIndex(), QModelIndex(target));
            QCOMPARE(view.columnGeometry(1).viewportX, committedColumn);
            QVERIFY(peakWidgets <= limit);
            QCOMPARE(view.stats().createCount, before.createCount);
            QCOMPARE(view.stats().recycleCount, before.recycleCount);
            const quint64 queriesPerFrame = queries / quint64(renderUs.size());
            if (roots == 10000)
                QVERIFY2(queriesPerFrame <= previousQueriesPerFrame[kind] * 2 + 5000,
                         "Model growth must not turn header preview updates into a model scan.");
            previousQueriesPerFrame[kind] = queriesPerFrame;
            std::sort(renderUs.begin(), renderUs.end());
            std::sort(intervalsUs.begin(), intervalsUs.end());
            qInfo().nospace() << "header-preview roots=" << roots
                << " view=" << (flat ? "table" : "tree-table")
                << " mode=" << (cells ? "cells" : "rows")
                << " layout=" << (advanced ? "advanced" : "plain")
                << " axis=" << (kind == 0 ? "rows" : "columns")
                << " frames=" << renderUs.size() << " intermediate=" << intermediateFrames
                << " render_median_us=" << renderUs.at(renderUs.size() / 2)
                << " render_p95_us=" << renderUs.at((renderUs.size() - 1) * 95 / 100)
                << " interval_p95_us=" << intervalsUs.at((intervalsUs.size() - 1) * 95 / 100)
                << " queries_per_sample=" << queriesPerFrame << " peak_widgets=" << peakWidgets;
            QTest::keyClick(header, Qt::Key_Escape);
            settle();
            QCOMPARE(view.currentIndex(), QModelIndex(target));
            QCOMPARE(view.spanRect(target), committedSpan);
            QCOMPARE(view.columnGeometry(1).viewportX, committedColumn);
            QWidget *widget = cells ? view.cellWidget(target) : view.widgetForIndex(node);
            QVERIFY(widget);
            QCOMPARE(widget->mapTo(view.viewport(), QPoint()).y(), committedNode.top());
        }
    }
}

void TestTreeTableViewInteraction::animatedBackgroundsPreserveGridPixels_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("cellScope");
    QTest::addColumn<int>("spacing");
    QTest::addColumn<int>("extent");
    QTest::addColumn<int>("columnLayout");
    for (bool cells : {false, true}) {
        for (bool cellScope : {false, true}) {
            for (int spacing : {0, 6}) {
                for (int extent = 0; extent < 3; ++extent) {
                    for (int columnLayout = 0; columnLayout < 3; ++columnLayout) {
                        const QByteArray name = QByteArray(cells ? "cells" : "rows")
                            + (cellScope ? "-cell-scope-" : "-row-scope-") + QByteArray::number(spacing)
                            + "-extent-" + QByteArray::number(extent)
                            + "-layout-" + QByteArray::number(columnLayout);
                        QTest::newRow(name.constData()) << cells << cellScope << spacing
                                                       << extent << columnLayout;
                    }
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::animatedBackgroundsPreserveGridPixels()
{
    QFETCH(bool, cells);
    QFETCH(bool, cellScope);
    QFETCH(int, spacing);
    QFETCH(int, extent);
    QFETCH(int, columnLayout);
    QStandardItemModel model;
    auto parentRow = row(QStringLiteral("parent"));
    parentRow.first()->appendRow(row(QString()));
    parentRow.first()->appendRow(row(QString()));
    model.appendRow(parentRow);
    model.appendRow(row(QStringLiteral("tail")));
    RowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setRowSpacing(spacing);
    view.setColumnSpacing(spacing);
    view.setVisualStateScope(cellScope ? VirtualTableView::VisualStateScope::Cell
                                     : VirtualTableView::VisualStateScope::Row);
    view.setSelectionBehavior(cellScope ? VirtualItemView::SelectionBehavior::SelectItems
                                       : VirtualItemView::SelectionBehavior::SelectRows);
    view.setVisualStateBackgroundVisible(true);
    view.setVisualStateBackgroundExtent(
        static_cast<VirtualTreeTableView::VisualStateBackgroundExtent>(extent));
    view.setVisualStateAnimationDuration(0);
    const QColor hover(30, 190, 120);
    const QColor selected(230, 30, 70);
    const QColor horizontal(20, 120, 220);
    const QColor vertical(180, 100, 20);
    view.setHoverBackgroundColor(hover);
    view.setSelectedBackgroundColor(selected);
    view.setHorizontalGridLineColor(horizontal);
    view.setVerticalGridLineColor(vertical);
    view.setHorizontalGridLineWidth(1);
    view.setVerticalGridLineWidth(1);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex node = model.index(0, 0, parent);
    const QModelIndex cell = columnLayout == 2 ? node.siblingAtColumn(1) : node;
    const QModelIndex stateIndex = cellScope ? cell : node;
    view.expand(parent);
    view.setFrozenColumns(columnLayout == 0 ? QVector<int>{0} : QVector<int>{1});
    if (columnLayout == 1)
        view.moveColumn(0, 2);
    else if (columnLayout == 2)
        view.setColumnHidden(0, true);
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    showView(&view, QSize(440, 300));
    QCursor::setPos(view.viewport()->mapToGlobal(QPoint(400, 220)));
    sendHeaderMouse(view.viewport(), QEvent::MouseMove, QPoint(400, 220), Qt::NoButton, Qt::NoButton);
    settle();
    view.setVisualStateAnimationDuration(1000);
    const QRect rowRect = view.visualRect(node);
    const int gridColumn = spacing == 0 && columnLayout != 0 ? 2 : 1;
    const int columnEnd = view.columnGeometry(gridColumn).viewportX + 100;
    const auto stateColumn = view.columnGeometry(cell.column());
    const QPoint content(stateColumn.viewportX + stateColumn.width - 30, rowRect.center().y());
    const QPoint horizontalPoint(content.x(), rowRect.bottom() + (spacing ? 1 : 0));
    const QPoint verticalPoint(columnEnd - 1, content.y());
    const QPoint origin = view.viewport()->geometry().topLeft();
    const auto pixel = [&](const QPoint &point) {
        return view.grab().toImage().pixelColor(origin + point);
    };
    const QColor baseline = pixel(content);
    const int treeX = view.columnGeometry(0).viewportX;
    const QPoint ancestor(treeX + 5, rowRect.top() + 2);
    const QPoint icon(treeX + view.indentation() + 5, rowRect.top() + 2);
    const QColor ancestorBaseline = columnLayout == 2 ? QColor() : pixel(ancestor);
    const QColor iconBaseline = columnLayout == 2 ? QColor() : pixel(icon);
    const QPoint paneBoundary(view.columnGeometry(1).viewportX + 99, content.y());
    const QColor paneBaseline = pixel(paneBoundary);
    const auto verifyDecoration = [&](const QColor &color, bool midpoint) {
        if (spacing == 0 && columnLayout != 0)
            QCOMPARE(pixel(paneBoundary), paneBaseline);
        if (columnLayout == 2)
            return;
        const auto verify = [&](const QPoint &point, const QColor &original, bool covered) {
            if (!covered) {
                QCOMPARE(pixel(point), original);
            } else if (midpoint) {
                QVERIFY(pixel(point) != original);
                QVERIFY(pixel(point) != color);
            } else {
                QCOMPARE(pixel(point), color);
            }
        };
        verify(ancestor, ancestorBaseline, extent == 2);
        verify(icon, iconBaseline, extent != 0);
    };
    QVERIFY(!view.visualState(stateIndex).hovered);
    const auto advance = [&](int time) {
        const auto animations = view.findChildren<QVariantAnimation *>();
        bool found = false;
        for (QVariantAnimation *animation : animations) {
            if (animation->duration() != 1000 || animation->state() == QAbstractAnimation::Stopped)
                continue;
            found = true;
            if (animation->state() == QAbstractAnimation::Running)
                animation->pause();
            animation->setCurrentTime(time);
        }
        QVERIFY(found);
        settle();
    };
    QCursor::setPos(view.viewport()->mapToGlobal(content));
    sendHeaderMouse(view.viewport(), QEvent::MouseMove, content, Qt::NoButton, Qt::NoButton);
    settle();
    QVERIFY(view.visualState(stateIndex).hovered);
    advance(500);
    QVERIFY(view.visualState(stateIndex).hoverProgress > 0.4);
    QVERIFY(view.visualState(stateIndex).hoverProgress < 0.6);
    QVERIFY(pixel(content) != baseline);
    QVERIFY(pixel(content) != hover);
    verifyDecoration(hover, true);
    QCOMPARE(pixel(horizontalPoint), horizontal);
    QCOMPARE(pixel(verticalPoint), vertical);
    advance(1000);
    QCOMPARE(pixel(content), hover);
    verifyDecoration(hover, false);
    view.setCurrentIndex(cell);
    settle();
    advance(500);
    QVERIFY(view.visualState(stateIndex).selectedProgress > 0.4);
    QVERIFY(view.visualState(stateIndex).selectedProgress < 0.6);
    QVERIFY(pixel(content) != hover);
    QVERIFY(pixel(content) != selected);
    verifyDecoration(selected, true);
    QCOMPARE(pixel(horizontalPoint), horizontal);
    QCOMPARE(pixel(verticalPoint), vertical);
    advance(1000);
    QCOMPARE(pixel(content), selected);
    verifyDecoration(selected, false);
    if (spacing > 0) {
        const QPoint verticalGap(columnEnd - 1, rowRect.bottom() + 3);
        const QPoint horizontalGap(columnEnd + 2, horizontalPoint.y());
        view.setVerticalSpacingLineThroughRowSpacing(true);
        view.setHorizontalSpacingLineThroughColumnSpacing(true);
        settle();
        QCOMPARE(pixel(verticalGap), vertical);
        QCOMPARE(pixel(horizontalGap), horizontal);
        view.setVerticalSpacingLineThroughRowSpacing(false);
        view.setHorizontalSpacingLineThroughColumnSpacing(false);
        settle();
        QVERIFY(pixel(verticalGap) != vertical);
        QVERIFY(pixel(horizontalGap) != horizontal);
    }
    view.setHorizontalGridLinesVisible(false);
    view.setVerticalGridLinesVisible(false);
    settle();
    QVERIFY(pixel(horizontalPoint) != horizontal);
    QVERIFY(pixel(verticalPoint) != vertical);
    view.setHorizontalGridLineWidth(3);
    view.setVerticalGridLineWidth(3);
    view.setHorizontalGridLinesVisible(true);
    view.setVerticalGridLinesVisible(true);
    settle();
    for (int offset = 0; offset < 3; ++offset) {
        QCOMPARE(pixel(QPoint(columnEnd - 1 - offset, content.y())), vertical);
        const int y = spacing > 0 ? rowRect.bottom() + 1 + offset : rowRect.bottom() - offset;
        QCOMPARE(pixel(QPoint(content.x(), y)), horizontal);
    }
    const QColor changedHorizontal(100, 20, 170);
    const QColor changedVertical(20, 140, 90);
    view.setHorizontalGridLineColor(changedHorizontal);
    view.setVerticalGridLineColor(changedVertical);
    settle();
    QCOMPARE(pixel(horizontalPoint), changedHorizontal);
    QCOMPARE(pixel(verticalPoint), changedVertical);
}

void TestTreeTableViewInteraction::columnMovesKeepOrInvalidateNodeIdentity_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("global");
    QTest::addColumn<int>("kind");
    for (bool cells : {false, true}) {
        for (bool global : {false, true}) {
            for (int kind = 0; kind < 3; ++kind) {
                const QByteArray name = QByteArray(cells ? "cells" : "rows")
                    + (global ? "-global-" : "-children-") + QByteArray::number(kind);
                QTest::newRow(name.constData()) << cells << global << kind;
            }
        }
    }
}

void TestTreeTableViewInteraction::columnMovesKeepOrInvalidateNodeIdentity()
{
    QFETCH(bool, cells);
    QFETCH(bool, global);
    QFETCH(int, kind);
    ColumnMoveModel model;
    ParentTypedRowAdapter rowAdapter;
    CellAdapter cellAdapter;
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectItems);
    if (cells) {
        view.setCellAdapter(&cellAdapter);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(&rowAdapter);
    }
    view.setModel(&model);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex node(model.index(0, 0, parent));
    const QPersistentModelIndex cell(QModelIndex(node).siblingAtColumn(1));
    const QPersistentModelIndex peer(model.index(1, 0));
    const QPersistentModelIndex peerCell(QModelIndex(peer).siblingAtColumn(1));
    const QString originalText = cell.data().toString();
    view.expand(parent);
    view.expand(node);
    showView(&view, QSize(440, 330));
    view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(node), 63);
    view.setItemPinned(cell);
    view.setCurrentIndex(cell);
    if (!global) {
        view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(peer), 57);
        view.setItemPinned(peerCell);
        view.selectionModel()->select(peerCell, QItemSelectionModel::Select);
    }
    const QModelIndex changedParent = global ? QModelIndex() : QModelIndex(parent);
    QSignalSpy moved(&model, &QAbstractItemModel::columnsMoved);
    QVERIFY(model.moveColumns(changedParent, kind == 2 ? 0 : 1, 1,
                              changedParent, kind == 1 ? 0 : 3));
    QCOMPARE(moved.count(), 1);
    view.flushPendingRelayout();
    settle();
    QVERIFY(cell.isValid());
    QCOMPARE(cell.data().toString(), originalText);
    QCOMPARE(view.columnCount(), 3);
    QCOMPARE(view.horizontalHeaderGeometry()->sectionCount(), 3);
    if (kind == 0) {
        QCOMPARE(node.column(), 0);
        QCOMPARE(cell.column(), global ? 1 : 2);
        QCOMPARE(view.currentIndex(), QModelIndex(cell));
        QVERIFY(view.selectionModel()->isSelected(cell));
        QVERIFY(view.isExpanded(parent));
        QVERIFY(view.isExpanded(node));
        QVERIFY(view.isItemPinned(cell));
        QCOMPARE(view.rowHeight(view.visibilityIndex()->visibleRowForIndex(node)), 63);
        QCOMPARE(view.visibleRowCount(), qsizetype(5));
    } else {
        QCOMPARE(global ? parent.column() : node.column(), kind == 1 ? 1 : 2);
        QVERIFY(!view.currentIndex().isValid());
        QVERIFY(!view.isItemPinned(cell));
        QVERIFY(!view.isExpanded(node));
        QCOMPARE(view.visibleRowCount(), qsizetype(global ? 2 : 4));
        if (global) {
            QCOMPARE(view.visibilityIndex()->visibleRowForIndex(node), qsizetype(-1));
            QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());
            QVERIFY(!view.isExpanded(parent));
        } else {
            const qsizetype replacementRow = view.visibilityIndex()->visibleRowForIndex(node);
            QCOMPARE(replacementRow, qsizetype(1));
            const QModelIndex replacement = view.visibilityIndex()->indexAtVisibleRow(replacementRow);
            QCOMPARE(replacement, QModelIndex(node).siblingAtColumn(0));
            QVERIFY(replacement != node);
            QCOMPARE(view.selectionModel()->selectedIndexes(), QModelIndexList{peerCell});
            QVERIFY(view.isExpanded(parent));
        }
    }
    if (!global) {
        QVERIFY(view.isItemPinned(peerCell));
        QCOMPARE(view.rowHeight(view.visibilityIndex()->visibleRowForIndex(peer)), 57);
    }
    for (qsizetype visible = 0; visible < view.visibleRowCount(); ++visible) {
        const QModelIndex current = view.visibilityIndex()->indexAtVisibleRow(visible);
        QVERIFY(current.isValid());
        QCOMPARE(current.column(), 0);
        QCOMPARE(view.visibilityIndex()->visibleRowForIndex(current), visible);
        if (kind != 0 && (global || current != peer)) {
            QVERIFY(!view.hasExplicitRowHeight(visible));
            QCOMPARE(view.rowHeight(visible), 28);
        }
        for (int column = 0; column < 3; ++column) {
            const QModelIndex target = current.siblingAtColumn(column);
            const QRect rect = view.cellRect(target);
            QVERIFY(!rect.isEmpty());
            QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(rect.center()), target);
            if (cells) {
                auto *widget = qobject_cast<QLabel *>(view.cellWidget(target));
                QVERIFY(widget);
                QCOMPARE(widget->text(), target.data().toString());
            } else {
                auto *widget = static_cast<HostedRow *>(view.widgetForIndex(current));
                QVERIFY(widget);
                QVERIFY(widget->host(column)->isVisible());
            }
        }
    }
}

void TestTreeTableViewInteraction::customHeadersFollowTreeGeometry_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("advanced");
    for (bool cells : {false, true}) {
        for (bool advanced : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (advanced ? "-groups" : "-frozen");
            QTest::newRow(name.constData()) << cells << advanced;
        }
    }
}

void TestTreeTableViewInteraction::customHeaderSortingReentersWithNewestModel_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("advanced");
    for (bool cells : {false, true}) {
        for (bool advanced : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (advanced ? "-groups" : "-plain");
            QTest::newRow(name.constData()) << cells << advanced;
        }
    }
}

void TestTreeTableViewInteraction::customHeaderSortingReentersWithNewestModel()
{
    QFETCH(bool, cells);
    QFETCH(bool, advanced);
    RecordingSortModel original;
    RecordingSortModel replacement;
    for (const QString &name : {QStringLiteral("z"), QStringLiteral("a")}) {
        original.appendRow(row(name));
        replacement.appendRow(row(QStringLiteral("new-") + name));
    }
    for (int column = 0; column < 3; ++column) {
        original.setHeaderData(column, Qt::Horizontal, QStringLiteral("old %1").arg(column));
        replacement.setHeaderData(column, Qt::Horizontal, QStringLiteral("new %1").arg(column));
    }
    VirtualTreeTableView view;
    TreeHeaderAdapter horizontal(&view, Qt::Horizontal, 1);
    TreeHeaderAdapter vertical(&view, Qt::Vertical, 2);
    auto *columnHeader = new VirtualHeaderView(Qt::Horizontal);
    auto *rowHeader = new VirtualHeaderView(Qt::Vertical);
    columnHeader->setAdapter(&horizontal);
    rowHeader->setAdapter(&vertical);
    view.setHorizontalHeader(columnHeader);
    view.setVerticalHeader(rowHeader);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setSortingEnabled(true);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new RowAdapter, true);
    }
    view.setModel(&original);
    if (advanced) {
        TablePaneSpec frozen;
        frozen.logicalColumns = {0};
        frozen.scroll = PaneScroll::Frozen;
        TablePaneSpec scroll;
        scroll.logicalColumns = {1};
        scroll.scroll = PaneScroll::Scrollable;
        scroll.scrollGroup = 0;
        TablePaneSpec other;
        other.logicalColumns = {2};
        other.scroll = PaneScroll::Scrollable;
        other.scrollGroup = 1;
        view.setPanes({frozen, scroll, other});
    }
    int callbacks = 0;
    connect(&view, &VirtualTableView::sortIndicatorRequested, &view,
            [&](int logical, Qt::SortOrder order) {
        ++callbacks;
        QCOMPARE(logical, 1);
        QCOMPARE(order, Qt::AscendingOrder);
        view.setModel(&replacement);
    });
    struct DetachHeaders
    {
        VirtualTreeTableView *view;
        ~DetachHeaders()
        {
            view->setHorizontalHeader(nullptr);
            view->setVerticalHeader(nullptr);
        }
    } detach{&view};
    showView(&view, QSize(500, 300));
    view.flushPendingRelayout();
    settle();
    QVERIFY(horizontal.labelModel == &original);
    QWidget *section = columnHeader->sectionWidget(1);
    QVERIFY(section);
    const QPoint click = section->mapTo(columnHeader->headerWidget(), section->rect().center());
    sendHeaderMouse(columnHeader->headerWidget(), QEvent::MouseButtonPress, click,
                    Qt::LeftButton, Qt::NoButton);
    sendHeaderMouse(columnHeader->headerWidget(), QEvent::MouseButtonRelease, click,
                    Qt::NoButton, Qt::LeftButton);
    settle();
    view.flushPendingRelayout();
    QCOMPARE(callbacks, 1);
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(view.selectionModel()->model(), static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(original.sortCalls, 0);
    QCOMPARE(replacement.sortCalls, 0);
    QCOMPARE(view.horizontalHeaderGeometry()->sortIndicatorSection(), 1);
    QCOMPARE(view.horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::AscendingOrder);
    QCOMPARE(horizontal.labelModel, static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(vertical.labelModel, static_cast<QAbstractItemModel *>(&replacement));
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(0), replacement.index(0, 0));
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(1), replacement.index(1, 0));
}

void TestTreeTableViewInteraction::customHeadersFollowTreeGeometry()
{
    QFETCH(bool, cells);
    QFETCH(bool, advanced);
    QStandardItemModel model;
    const auto wideRow = [](const QString &text) {
        QList<QStandardItem *> items;
        for (int column = 0; column < 8; ++column)
            items.append(new QStandardItem(text + QString::number(column)));
        return items;
    };
    auto parentItems = wideRow(QStringLiteral("parent"));
    auto childItems = wideRow(QStringLiteral("child"));
    childItems.first()->appendRow(wideRow(QStringLiteral("leaf")));
    parentItems.first()->appendRow(childItems);
    parentItems.first()->appendRow(wideRow(QStringLiteral("sibling")));
    model.appendRow(parentItems);
    auto narrowItems = wideRow(QStringLiteral("narrow"));
    narrowItems.first()->appendRow({new QStandardItem(QStringLiteral("small")),
                                   new QStandardItem(QStringLiteral("small type"))});
    model.appendRow(narrowItems);
    model.appendRow(wideRow(QStringLiteral("tail")));
    for (int column = 0; column < 8; ++column)
        model.setHeaderData(column, Qt::Horizontal, QStringLiteral("header %1").arg(column));
    VirtualTreeTableView view;
    TreeHeaderAdapter horizontal(&view, Qt::Horizontal, 1);
    TreeHeaderAdapter vertical(&view, Qt::Vertical, 2);
    TreeHeaderAdapter replacementHorizontal(&view, Qt::Horizontal, 3);
    TreeHeaderAdapter replacementVertical(&view, Qt::Vertical, 4);
    // Headers borrow these adapters and must release their sections before the adapters die.
    struct DetachHeaders
    {
        VirtualTreeTableView *view;
        ~DetachHeaders()
        {
            view->setHorizontalHeader(nullptr);
            view->setVerticalHeader(nullptr);
        }
    } detach{&view};
    auto *columnHeader = new VirtualHeaderView(Qt::Horizontal);
    auto *rowHeader = new VirtualHeaderView(Qt::Vertical);
    columnHeader->setAdapter(&horizontal);
    rowHeader->setAdapter(&vertical);
    view.setHorizontalHeader(columnHeader);
    view.setVerticalHeader(rowHeader);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setRowSpacing(6);
    view.setDepthRowSpacing(1, 9);
    view.setDepthRowSpacing(2, 3);
    view.setColumnSpacing(6);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(8), true);
    }
    view.setModel(&model);
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    if (advanced) {
        const auto pane = [](QVector<int> columns, PaneScroll scroll, int group) {
            TablePaneSpec result;
            result.logicalColumns = columns;
            result.scroll = scroll;
            result.scrollGroup = group;
            return result;
        };
        view.setPanes({pane({0}, PaneScroll::Frozen, 0),
                       pane({1, 2}, PaneScroll::Scrollable, 0),
                       pane({3}, PaneScroll::Frozen, 0),
                       pane({4, 5, 6, 7}, PaneScroll::Scrollable, 1)});
    } else {
        view.setFrozenColumns({0});
        view.setFrozenRightColumns({7});
    }
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    const QModelIndex narrow = model.index(1, 0);
    showView(&view, QSize(700, 400));
    TreeHeaderAdapter *activeHorizontal = &horizontal;
    TreeHeaderAdapter *activeVertical = &vertical;
    const auto verify = [&]() {
        view.flushPendingRelayout();
        settle();
        QSet<int> columns;
        QSet<int> rows;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            TreeHeaderAdapter *adapter = header->orientation() == Qt::Horizontal
                ? activeHorizontal : activeVertical;
            QCOMPARE(header->adapter(), static_cast<HeaderWidgetAdapter *>(adapter));
            QCOMPARE(adapter->labelModel.data(), static_cast<QAbstractItemModel *>(&model));
            for (int logical : header->materializedSections()) {
                QWidget *section = header->sectionWidget(logical);
                QVERIFY(section);
                if (section->visibleRegion().isEmpty())
                    continue;
                QCOMPARE(section->property("headerOwner").toInt(), adapter->owner);
                QCOMPARE(section->property("logicalSection").toInt(), logical);
                QCOMPARE(static_cast<QLabel *>(section)->text(), adapter->text(logical));
                const QPoint position = section->mapTo(&view, QPoint()) - view.viewport()->pos();
                if (header->orientation() == Qt::Horizontal) {
                    QVERIFY(!view.isColumnHidden(logical));
                    QCOMPARE(position.x(), view.columnGeometry(logical).viewportX);
                    QCOMPARE(section->width(), view.columnWidth(logical));
                    columns.insert(logical);
                } else {
                    const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(logical);
                    QVERIFY(node.isValid());
                    QCOMPARE(position.y(), view.visualRect(node).y());
                    QCOMPARE(section->height(), view.visualRect(node).height());
                    rows.insert(logical);
                }
            }
        }
        QVERIFY(columns.contains(0));
        QCOMPARE(rows.size(), int(view.visibleRowCount()));
        QVERIFY(activeHorizontal->binds > 0);
        QVERIFY(activeVertical->binds > 0);
    };
    verify();
    view.expand(parent);
    view.expand(child);
    verify();
    view.setRowHeight(view.visibilityIndex()->visibleRowForIndex(child), 47);
    view.setColumnWidth(1, 83);
    view.setColumnHidden(2, true);
    view.horizontalHeaderGeometry()->moveSection(0, 2);
    verify();
    if (advanced) {
        QVERIFY(view.maximumHorizontalOffset(1) > 0);
        view.setHorizontalOffset(1, qMin<qint64>(40, view.maximumHorizontalOffset(1)));
        verify();
    }
    view.collapse(parent);
    verify();
    view.expand(parent);
    verify();
    activeHorizontal = &replacementHorizontal;
    activeVertical = &replacementVertical;
    columnHeader->setAdapter(activeHorizontal);
    rowHeader->setAdapter(activeVertical);
    verify();
    QVERIFY(horizontal.unbinds > 0);
    QVERIFY(vertical.unbinds > 0);
    view.setRootIndex(narrow);
    QCOMPARE(view.columnCount(), 2);
    verify();
    view.setRootIndex(QModelIndex());
    QCOMPARE(view.columnCount(), 8);
    verify();
}

void TestTreeTableViewInteraction::customHeaderAdapterReplacementDuringBind()
{
    QStandardItemModel model;
    model.appendRow(row(QStringLiteral("first")));
    model.appendRow(row(QStringLiteral("second")));

    VirtualTreeTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    ReentrantHeaderAdapter first(&view, Qt::Horizontal, 1);
    TreeHeaderAdapter replacement(&view, Qt::Horizontal, 2);
    first.onBind = [&]() { header->setAdapter(&replacement); };
    header->setAdapter(&first);
    view.setHorizontalHeader(header);
    view.setModel(&model);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    struct DetachHeader
    {
        VirtualTreeTableView *view;
        ~DetachHeader() { view->setHorizontalHeader(nullptr); }
    } detach{&view};

    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(header->adapter(), static_cast<HeaderWidgetAdapter *>(&replacement));
    QVERIFY(first.unbinds > 0);
    QVERIFY(replacement.binds > 0);
    for (int logical : header->materializedSections()) {
        QWidget *section = header->sectionWidget(logical);
        QVERIFY(section);
        QCOMPARE(section->property("headerOwner").toInt(), 2);
    }
}

void TestTreeTableViewInteraction::customHeaderAdapterDetachDuringBind()
{
    QStandardItemModel model;
    model.appendRow(row(QStringLiteral("first")));

    VirtualTreeTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    ReentrantHeaderAdapter adapter(&view, Qt::Horizontal, 1);
    adapter.onBind = [&]() { header->setAdapter(nullptr); };
    header->setAdapter(&adapter);
    view.setHorizontalHeader(header);
    view.setModel(&model);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    struct DetachHeader
    {
        VirtualTreeTableView *view;
        ~DetachHeader() { view->setHorizontalHeader(nullptr); }
    } detach{&view};

    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();
    QVERIFY(!header->adapter());
    QCOMPARE(header->materializedSectionCount(), qsizetype(0));
    QVERIFY(adapter.unbinds > 0);
}

void TestTreeTableViewInteraction::customHeaderDeletingSectionWidget()
{
    QStandardItemModel model;
    model.appendRow(row(QStringLiteral("first")));

    VirtualTreeTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    TreeHeaderAdapter adapter(&view, Qt::Horizontal, 1);
    QPointer<QWidget> deletedSection;
    adapter.onBindWidget = [&deletedSection](QWidget *widget) {
        deletedSection = widget;
        delete widget;
    };
    header->setAdapter(&adapter);
    view.setHorizontalHeader(header);
    view.setModel(&model);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);

    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();
    QVERIFY(deletedSection.isNull());
    QCOMPARE(header->materializedSectionCount(), qsizetype(3));
    QVERIFY(adapter.binds > 0);

    view.setColumnWidth(0, 101);
    view.flushPendingRelayout();
    settle();
    QVERIFY(header->materializedSectionCount() > 0);
    for (int logical : header->materializedSections())
        QVERIFY(header->sectionWidget(logical));
}

void TestTreeTableViewInteraction::customHeaderDeletingModelDuringBind()
{
    auto *model = new QStandardItemModel;
    model->appendRow(row(QStringLiteral("first")));
    QPointer<QStandardItemModel> modelGuard(model);

    VirtualTreeTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    TreeHeaderAdapter adapter(&view, Qt::Horizontal, 1);
    adapter.onBindWidget = [&model](QWidget *) { delete model; };
    header->setAdapter(&adapter);
    view.setHorizontalHeader(header);
    view.setModel(model);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);

    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();
    QVERIFY(modelGuard.isNull());
    QCOMPARE(view.model(), nullptr);
    QCOMPARE(adapter.labelModel.data(), nullptr);
    QVERIFY(header->materializedSectionCount() > 0);
    for (int logical : header->materializedSections())
        QCOMPARE(static_cast<QLabel *>(header->sectionWidget(logical))->text(), QString());

    auto *replacement = new QStandardItemModel;
    replacement->setHorizontalHeaderLabels({QStringLiteral("replacement-header"),
                                             QStringLiteral("type"), QStringLiteral("state")});
    replacement->appendRow(row(QStringLiteral("replacement")));
    view.setModel(replacement);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(replacement));
    QVERIFY(header->materializedSectionCount() > 0);
    QCOMPARE(static_cast<QLabel *>(header->sectionWidget(0))->text(),
             QStringLiteral("replacement-header"));
    delete replacement;
}

void TestTreeTableViewInteraction::customHeaderDeletingViewDuringBind_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::addColumn<bool>("firstBinding");
    QTest::newRow("horizontal") << false << false;
    QTest::newRow("vertical") << true << false;
    QTest::newRow("vertical-first-binding") << true << true;
}

void TestTreeTableViewInteraction::customHeaderDeletingViewDuringBind()
{
    QFETCH(bool, vertical);
    QFETCH(bool, firstBinding);
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("name"), QStringLiteral("type"),
                                     QStringLiteral("state")});
    model.appendRow(row(QStringLiteral("first")));

    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    const Qt::Orientation orientation = vertical ? Qt::Vertical : Qt::Horizontal;
    auto *header = new VirtualHeaderView(orientation);
    auto *adapter = new TreeHeaderAdapter(view.data(), orientation, 1);
    if (!vertical)
        adapter->onBindWidget = [&view](QWidget *) { delete view.data(); };
    if (!firstBinding)
        header->setAdapter(adapter, true);
    if (vertical)
        view->setVerticalHeader(header);
    else
        view->setHorizontalHeader(header);
    view->setVerticalHeaderVisible(true);
    view->setHeaderAnimationEnabled(false);
    view->setTableAdapter(new RowAdapter, true);
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    view->setModel(&model);
    if (vertical) {
        showView(view.data(), QSize(520, 260));
        if (firstBinding)
            QCOMPARE(header->materializedSectionCount(), qsizetype(0));
        else
            QVERIFY(header->materializedSectionCount() > 0);
        adapter->onBindWidget = [&view](QWidget *) { delete view.data(); };
        if (firstBinding)
            header->setAdapter(adapter, true);
        else
            header->refreshSectionLabels();
    }
    settle();
    QVERIFY(view.isNull());
}

void TestTreeTableViewInteraction::customHeaderDeletingViewDuringUnbind_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("horizontal") << false;
    QTest::newRow("vertical") << true;
}

void TestTreeTableViewInteraction::customHeaderDeletingViewDuringUnbind()
{
    QFETCH(bool, vertical);
    QStandardItemModel model;
    for (int i = 0; i < 100; ++i)
        model.appendRow(row(QStringLiteral("node %1").arg(i)));

    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    const Qt::Orientation orientation = vertical ? Qt::Vertical : Qt::Horizontal;
    QPointer<VirtualHeaderView> header = new VirtualHeaderView(orientation);
    auto *adapter = new TreeHeaderAdapter(view.data(), orientation, 1);
    header->setAdapter(adapter, true);
    if (vertical)
        view->setVerticalHeader(header.data());
    else
        view->setHorizontalHeader(header.data());
    view->setVerticalHeaderVisible(true);
    view->setHeaderAnimationEnabled(false);
    view->setTableAdapter(new RowAdapter, true);
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    view->setModel(&model);
    showView(view.data(), QSize(520, 260));
    QVERIFY(header->materializedSectionCount() > 0);
    bool invoked = false;
    adapter->onUnbindWidget = [&](QWidget *) {
        invoked = true;
        delete view.data();
    };
    header->setPaneOffset(100000);
    settle();
    QVERIFY(invoked);
    QVERIFY(view.isNull());
    QVERIFY(header.isNull());
}

void TestTreeTableViewInteraction::customHeaderDeletingViewDuringAcquisition_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::addColumn<bool>("create");
    QTest::addColumn<bool>("recycling");
    QTest::newRow("horizontal-type") << false << false << false;
    QTest::newRow("vertical-type") << true << false << false;
    QTest::newRow("horizontal-create") << false << true << false;
    QTest::newRow("vertical-create") << true << true << false;
    QTest::newRow("horizontal-recycle-type") << false << false << true;
    QTest::newRow("vertical-recycle-type") << true << false << true;
}

void TestTreeTableViewInteraction::customHeaderDeletingViewDuringAcquisition()
{
    QFETCH(bool, vertical);
    QFETCH(bool, create);
    QFETCH(bool, recycling);
    QStandardItemModel model;
    for (int i = 0; i < 100; ++i)
        model.appendRow(row(QStringLiteral("node %1").arg(i)));
    QPointer<VirtualTreeTableView> view = new VirtualTreeTableView;
    const Qt::Orientation orientation = vertical ? Qt::Vertical : Qt::Horizontal;
    QPointer<VirtualHeaderView> header = new VirtualHeaderView(orientation);
    auto *adapter = new TreeHeaderAdapter(view.data(), orientation, 1);
    header->setAdapter(adapter, true);
    if (vertical)
        view->setVerticalHeader(header.data());
    else
        view->setHorizontalHeader(header.data());
    view->setVerticalHeaderVisible(true);
    view->setTableAdapter(new RowAdapter, true);
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    view->setModel(&model);
    showView(view.data(), QSize(520, 260));
    QVERIFY(header->materializedSectionCount() > 0);
    if (!recycling) {
        header->setPaneFilter({}, true);
        QCOMPARE(header->materializedSectionCount(), qsizetype(0));
    }
    adapter->type = 42;
    bool invoked = false;
    auto callback = [&]() {
        invoked = true;
        delete view.data();
    };
    if (create)
        adapter->onCreate = callback;
    else
        adapter->onType = callback;
    if (recycling)
        header->setPaneFilter({}, true);
    else
        header->clearPaneFilter();
    settle();
    QVERIFY(invoked);
    QVERIFY(view.isNull());
    QVERIFY(header.isNull());
}

void TestTreeTableViewInteraction::customHeaderNestedAdapterReplacementDuringBind()
{
    QStandardItemModel model;
    model.appendRow(row(QStringLiteral("first")));

    VirtualTreeTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    ReentrantHeaderAdapter first(&view, Qt::Horizontal, 1);
    ReentrantHeaderAdapter second(&view, Qt::Horizontal, 2);
    TreeHeaderAdapter third(&view, Qt::Horizontal, 3);
    first.onBind = [&]() { header->setAdapter(&second); };
    second.onBind = [&]() { header->setAdapter(&third); };
    header->setAdapter(&first);
    view.setHorizontalHeader(header);
    view.setModel(&model);
    view.setHeaderAnimationEnabled(false);
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    struct DetachHeader
    {
        VirtualTreeTableView *view;
        ~DetachHeader() { view->setHorizontalHeader(nullptr); }
    } detach{&view};

    showView(&view, QSize(520, 260));
    view.flushPendingRelayout();
    settle();
    QCOMPARE(header->adapter(), static_cast<HeaderWidgetAdapter *>(&third));
    QVERIFY(first.unbinds > 0);
    QVERIFY(second.unbinds > 0);
    QVERIFY(third.binds > 0);
    for (int logical : header->materializedSections()) {
        QWidget *section = header->sectionWidget(logical);
        QVERIFY(section);
        QCOMPARE(section->property("headerOwner").toInt(), 3);
    }
}

void TestTreeTableViewInteraction::flatFrozenRowHeadersRespectPaneRanges()
{
    QStandardItemModel model(3, 3);
    VirtualTableView view;
    view.setTableAdapter(new RowAdapter, true);
    view.setUniformItemHeight(28);
    view.setRowSpacing(6);
    view.setModel(&model);
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    showView(&view, QSize(440, 330));
    const auto verify = [&]() {
        view.flushPendingRelayout();
        settle();
        QSet<int> rows;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Vertical)
                continue;
            for (int logical : header->materializedSections()) {
                QWidget *section = header->sectionWidget(logical);
                if (!section || section->visibleRegion().isEmpty())
                    continue;
                QVERIFY(!rows.contains(logical));
                rows.insert(logical);
                const QRect body = view.visualRect(model.index(logical, 0));
                QCOMPARE(section->mapTo(&view, QPoint()).y() - view.viewport()->y(), body.y());
                QCOMPARE(section->height(), body.height());
            }
        }
        QCOMPARE(rows.size(), 3);
    };
    verify();
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip);
    strip->setAdapter(new LabelHeaderAdapter, true);
    verify();
    view.setFrozenRows(0);
    view.setFrozenBottomRows(0);
    verify();
}

void TestTreeTableViewInteraction::animatedColumnMovesKeepBranchesAndWidgetsAligned_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("frozen");
    QTest::addColumn<bool>("follows");
    QTest::addColumn<bool>("gesture");
    for (bool cells : {false, true}) {
        for (bool frozen : {false, true}) {
            for (bool follows : {false, true}) {
                for (bool gesture : {false, true}) {
                    const QByteArray name = QByteArray(cells ? "cells" : "rows")
                        + (frozen ? "-frozen" : "-plain") + (follows ? "-follow" : "-committed")
                        + (gesture ? "-drag" : "-api");
                    QTest::newRow(name.constData()) << cells << frozen << follows << gesture;
                }
            }
        }
    }
}

void TestTreeTableViewInteraction::animatedColumnMovesKeepBranchesAndWidgetsAligned()
{
    QFETCH(bool, cells);
    QFETCH(bool, frozen);
    QFETCH(bool, follows);
    QFETCH(bool, gesture);
    QStandardItemModel model;
    const auto fiveColumns = [](const QString &name) {
        auto items = row(name);
        items.append(new QStandardItem(name + QStringLiteral(" fourth")));
        items.append(new QStandardItem(name + QStringLiteral(" fifth")));
        return items;
    };
    auto parentItems = fiveColumns(QStringLiteral("parent"));
    parentItems.first()->appendRow(fiveColumns(QStringLiteral("child")));
    model.appendRow(parentItems);
    model.appendRow(fiveColumns(QStringLiteral("tail")));
    VirtualTreeTableView view;
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(100);
    view.setRowSpacing(6);
    view.setColumnSpacing(6);
    view.setHeaderAnimationDuration(1000);
    view.setColumnDragEnabled(true);
    view.setColumnFollowsHeaderVisual(follows);
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(5), true);
    }
    view.setModel(&model);
    const QModelIndex parent = model.index(0, 0);
    const QModelIndex child = model.index(0, 0, parent);
    view.expand(parent);
    if (frozen) {
        view.setFrozenColumns({3});
        view.setFrozenRightColumns({4});
        view.setFrozenRows(1);
        view.setFrozenBottomRows(1);
    }
    showView(&view, QSize(650, 300));
    const int originalX = view.columnGeometry(0).viewportX;
    const auto advance = [&](int time) {
        bool found = false;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Horizontal)
                continue;
            for (QVariantAnimation *animation : header->findChildren<QVariantAnimation *>()) {
                if (animation->duration() != 1000 || animation->state() == QAbstractAnimation::Stopped)
                    continue;
                found = true;
                if (animation->state() == QAbstractAnimation::Running)
                    animation->pause();
                animation->setCurrentTime(time);
            }
        }
        QVERIFY(found);
        settle();
    };
    const auto headerX = [&](int logical) {
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Horizontal)
                continue;
            QWidget *section = header->sectionWidget(logical);
            if (section)
                return section->mapTo(&view, QPoint()).x() - view.viewport()->x();
        }
        return -1;
    };
    const auto verify = [&]() {
        const QImage image = view.grab().toImage();
        for (const QModelIndex &node : {parent, child}) {
            const int inset = (view.itemDepth(node) + 1) * view.indentation();
            for (int column = 0; column < 5; ++column) {
                const QModelIndex cell = node.siblingAtColumn(column);
                const int committed = view.columnGeometry(column).viewportX;
                const int visual = headerX(column);
                QVERIFY(visual >= 0);
                QWidget *widget = cells ? view.cellWidget(cell)
                    : static_cast<HostedRow *>(view.widgetForIndex(node))->host(column);
                QVERIFY(widget);
                QCOMPARE(widget->mapTo(&view, QPoint()).x() - view.viewport()->x(),
                         (follows ? visual : committed) + (column == 0 ? inset : 0));
                QCOMPARE(view.cellRect(cell).x(), committed);
                QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(view.cellRect(cell).center()), cell);
            }
            const int branchX = follows ? headerX(0) : view.columnGeometry(0).viewportX;
            const QPoint marker(branchX + view.itemDepth(node) * view.indentation() + 9,
                                view.visualRect(node).center().y());
            QCOMPARE(image.pixelColor(view.viewport()->pos() + marker),
                     node == parent ? QColor(10, 180, 210) : QColor(230, 180, 10));
        }
    };
    if (gesture) {
        auto *header = dynamic_cast<VirtualHeaderView *>(view.horizontalHeader());
        QVERIFY(header);
        QWidget *section = header->sectionWidget(0);
        QWidget *target = header->sectionWidget(2);
        QVERIFY(section);
        QVERIFY(target);
        const QPoint start = section->geometry().center();
        const QPoint destination(target->x() + 65, start.y());
        QSignalSpy moved(view.horizontalHeaderGeometry(), &HeaderGeometry::sectionMoved);
        const auto preview = [&]() {
            sendHeaderMouse(header, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendHeaderMouse(header, QEvent::MouseMove, start + QPoint(2, 0), Qt::NoButton, Qt::LeftButton);
            sendHeaderMouse(header, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
            advance(200);
            QCOMPARE(view.columnGeometry(0).viewportX, originalX);
            QCOMPARE(view.horizontalHeaderGeometry()->visualIndex(0), 0);
            QCOMPARE(moved.count(), 0);
            QVERIFY(headerX(0) > originalX + 212);
            verify();
        };
        preview();
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(header, &escape);
        settle();
        QCOMPARE(headerX(0), originalX);
        QCOMPARE(moved.count(), 0);
        verify();
        preview();
        sendHeaderMouse(header, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
        settle();
        QCOMPARE(moved.count(), 1);
    } else {
        view.moveColumn(0, 2, VirtualTableView::MoveAnimation::Animate);
    }
    const int committedX = view.columnGeometry(0).viewportX;
    QVERIFY(committedX > originalX);
    advance(200);
    QVERIFY(headerX(0) > originalX);
    if (gesture)
        QVERIFY(headerX(0) > committedX);
    else
        QVERIFY(headerX(0) < committedX);
    verify();
    advance(1000);
    QCOMPARE(headerX(0), committedX);
    verify();
    view.moveColumn(0, 1, VirtualTableView::MoveAnimation::Animate);
    advance(200);
    verify();
    const int branchX = follows ? headerX(0) : view.columnGeometry(0).viewportX;
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(branchX + 9, view.visualRect(parent).center().y()));
    QVERIFY(!view.isExpanded(parent));
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
}

void TestTreeTableViewInteraction::animatedColumnMovesKeepAdvancedPanesAndSpansAligned_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("customHeaders");
    QTest::newRow("rows-default") << false << false;
    QTest::newRow("cells-default") << true << false;
    QTest::newRow("rows-custom") << false << true;
    QTest::newRow("cells-custom") << true << true;
}

void TestTreeTableViewInteraction::animatedColumnMovesKeepAdvancedPanesAndSpansAligned()
{
    QFETCH(bool, cells);
    QFETCH(bool, customHeaders);
    QStandardItemModel model;
    const auto sixColumns = [](const QString &name) {
        auto items = row(name);
        items.append(new QStandardItem(name + QStringLiteral(" fourth")));
        items.append(new QStandardItem(name + QStringLiteral(" fifth")));
        items.append(new QStandardItem(name + QStringLiteral(" sixth")));
        return items;
    };
    auto parentItems = sixColumns(QStringLiteral("parent"));
    parentItems.first()->appendRow(sixColumns(QStringLiteral("child one")));
    parentItems.first()->appendRow(sixColumns(QStringLiteral("child two")));
    model.appendRow(parentItems);
    auto tailItems = sixColumns(QStringLiteral("tail"));
    tailItems.first()->appendRow(sixColumns(QStringLiteral("tail child")));
    model.appendRow(tailItems);

    VirtualTreeTableView view;
    if (customHeaders) {
        auto *horizontal = new VirtualHeaderView(Qt::Horizontal);
        horizontal->setAdapter(new TreeHeaderAdapter(&view, Qt::Horizontal, 1), true);
        auto *vertical = new VirtualHeaderView(Qt::Vertical);
        vertical->setAdapter(new TreeHeaderAdapter(&view, Qt::Vertical, 2), true);
        view.setHorizontalHeader(horizontal);
        view.setVerticalHeader(vertical);
    }
    view.setUniformItemHeight(28);
    view.setDefaultColumnWidth(120);
    view.setColumnSpacing(6);
    view.setRowSpacing(6);
    view.setColumnOverscan(0);
    view.setHeaderAnimationDuration(1000);
    view.setColumnFollowsHeaderVisual(true);
    view.setBranchIndicatorRenderer(new SolidBranchRenderer, true);
    PaneSeparatorStyle separatorStyle;
    separatorStyle.width = 3;
    separatorStyle.color = QColor(180, 35, 125);
    view.setPaneSeparatorStyle(separatorStyle);
    view.setFrozenRows(1);
    view.setFrozenBottomRows(1);
    if (cells) {
        view.setCellAdapter(new CellAdapter, true);
        view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view.setTableAdapter(new ParentTypedRowAdapter(6), true);
    }
    view.setModel(&model);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex first(model.index(0, 0, parent));
    const QPersistentModelIndex second(model.index(1, 0, parent));
    const QPersistentModelIndex tail(model.index(1, 0));
    view.expand(parent);
    view.setPanes({{{0}, PaneScroll::Frozen, 0},
                   {{1, 2, 3}, PaneScroll::Scrollable, 0},
                   {{4, 5}, PaneScroll::Scrollable, 1}});
    auto *spans = new TableSpanMap;
    spans->setSpan(QModelIndex(first).siblingAtColumn(1), 2, 3);
    const QModelIndex spanAnchor = QModelIndex(first).siblingAtColumn(1);
    spans->setSpan(QModelIndex(parent).siblingAtColumn(4), 1, 2);
    const QModelIndex secondPaneSpanAnchor = QModelIndex(parent).siblingAtColumn(4);
    QCOMPARE(spans->spanAt(spanAnchor), (TableSpan{2, 3}));
    QCOMPARE(spans->maximumSpan(), (TableSpan{2, 3}));
    QCOMPARE(spans->anchorOf(QModelIndex(second).siblingAtColumn(3)), spanAnchor);
    QCOMPARE(spans->anchorOf(QModelIndex(parent).siblingAtColumn(5)), secondPaneSpanAnchor);
    view.setSpanProvider(spans, true);
    showView(&view, QSize(1000, 300));
    QCOMPARE(view.spanAt(spanAnchor), (TableSpan{2, 3}));

    const auto advance = [&](int time) {
        bool found = false;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Horizontal)
                continue;
            for (QVariantAnimation *animation : header->findChildren<QVariantAnimation *>()) {
                if (animation->duration() != 1000 || animation->state() == QAbstractAnimation::Stopped)
                    continue;
                found = true;
                if (animation->state() == QAbstractAnimation::Running)
                    animation->pause();
                animation->setCurrentTime(time);
            }
        }
        QVERIFY(found);
        settle();
    };
    const auto headerX = [&](int logical) {
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Horizontal)
                continue;
            QWidget *section = header->sectionWidget(logical);
            if (section && !section->visibleRegion().isEmpty())
                return section->mapTo(&view, QPoint()).x() - view.viewport()->x();
        }
        return -1;
    };
    const auto verify = [&]() {
        view.flushPendingRelayout();
        settle();
        const int committed = view.columnGeometry(1).viewportX;
        const int visual = headerX(1);
        QVERIFY(visual >= 0);
        for (const QModelIndex &node : {QModelIndex(parent), QModelIndex(first)}) {
            const QModelIndex cell = node.siblingAtColumn(1);
            QWidget *widget = cells ? view.cellWidget(cell)
                : static_cast<HostedRow *>(view.widgetForIndex(node))->host(1);
            QVERIFY(widget);
            QCOMPARE(widget->mapTo(&view, QPoint()).x() - view.viewport()->x(), visual);
            QCOMPARE(view.cellRect(cell).x(), committed);
            const int paneIndex = view.paneIndexOfColumn(1);
            QVERIFY(paneIndex >= 0);
            const QRect visible = view.visualRect(cell)
                .intersected(view.panes().at(paneIndex).viewportRect)
                .intersected(view.viewport()->rect());
            QVERIFY(!visible.isEmpty());
            const QPoint cellHit(view.cellRect(cell).left() + 5, visible.center().y());
            QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(cellHit), cell);
        }
        const QModelIndex anchor = spanAnchor;
        const QModelIndex covered = QModelIndex(second).siblingAtColumn(3);
        QCOMPARE(view.anchorIndex(covered), anchor);
        const QRect merged = view.spanRect(anchor);
        QVERIFY(!merged.isEmpty());
        const QPoint mergedHit(merged.left() + 5, view.visualRect(anchor).center().y());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(mergedHit), anchor);
        QVERIFY(merged.width() >= view.columnWidth(1) + view.columnWidth(2)
                + view.columnWidth(3));
        if (cells) {
            QVERIFY(view.cellWidget(anchor));
            QVERIFY(!view.cellWidget(covered));
        } else {
            auto *rowWidget = static_cast<HostedRow *>(view.widgetForIndex(first));
            QVERIFY(rowWidget);
            QVERIFY(rowWidget->host(1)->isVisible());
            QVERIFY(!rowWidget->host(2)->isVisible());
        }

        const QModelIndex secondPaneCovered = QModelIndex(parent).siblingAtColumn(5);
        QCOMPARE(view.anchorIndex(secondPaneCovered), secondPaneSpanAnchor);
        const QRect secondPaneMerged = view.spanRect(secondPaneSpanAnchor);
        QVERIFY(!secondPaneMerged.isEmpty());
        const int secondPaneIndex = view.paneIndexOfColumn(4);
        QVERIFY(secondPaneIndex >= 0);
        const QRect secondPaneVisible = view.visualRect(secondPaneSpanAnchor)
            .intersected(view.panes().at(secondPaneIndex).viewportRect)
            .intersected(view.viewport()->rect());
        QVERIFY(!secondPaneVisible.isEmpty());
        const QPoint secondPaneHit(secondPaneVisible.left() + 5, secondPaneVisible.center().y());
        QCOMPARE(static_cast<const VirtualItemView &>(view).indexAt(secondPaneHit),
                 secondPaneSpanAnchor);
        QVERIFY(secondPaneMerged.width() >= view.columnWidth(4) + view.columnWidth(5));
        if (cells) {
            QVERIFY(view.cellWidget(secondPaneSpanAnchor));
            QVERIFY(!view.cellWidget(secondPaneCovered));
        } else {
            auto *parentRow = static_cast<HostedRow *>(view.widgetForIndex(parent));
            QVERIFY(parentRow);
            QVERIFY(parentRow->host(4)->isVisible());
            QVERIFY(!parentRow->host(5)->isVisible());
        }
    };

    const auto verifyPixels = [&]() {
        view.flushPendingRelayout();
        settle();
        const QImage image = view.grab().toImage();
        for (const QModelIndex &node : {QModelIndex(parent), QModelIndex(first), QModelIndex(tail)}) {
            const QPoint marker(view.columnGeometry(0).viewportX
                                    + view.itemDepth(node) * view.indentation() + 9,
                                view.visualRect(node).center().y());
            QCOMPARE(image.pixelColor(view.viewport()->pos() + marker),
                     view.hasChildren(node) ? QColor(10, 180, 210) : QColor(230, 180, 10));
        }
        const QVector<QRect> separators = view.paneSeparatorRects();
        QCOMPARE(separators.size(), 2);
        for (const QRect &rect : separators) {
            QCOMPARE(rect.width(), separatorStyle.width);
            for (const QModelIndex &node : {QModelIndex(parent), QModelIndex(first), QModelIndex(tail)}) {
                const int y = view.viewport()->y() + view.visualRect(node).center().y();
                for (int x = rect.left(); x <= rect.right(); ++x)
                    QCOMPARE(image.pixelColor(x, y), separatorStyle.color);
            }
            QCOMPARE(image.pixelColor(rect.center().x(), view.viewport()->y() - 10),
                     separatorStyle.color);
        }
        QSet<int> seenRows;
        for (VirtualHeaderView *header : view.findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Vertical)
                continue;
            for (int logical : header->materializedSections()) {
                QWidget *section = header->sectionWidget(logical);
                if (!section || section->visibleRegion().isEmpty())
                    continue;
                const QModelIndex node = view.visibilityIndex()->indexAtVisibleRow(logical);
                QVERIFY(node.isValid());
                QVERIFY(!seenRows.contains(logical));
                seenRows.insert(logical);
                QCOMPARE(section->mapTo(&view, QPoint()).y() - view.viewport()->y(),
                         view.visualRect(node).y());
                QCOMPARE(section->height(), view.visualRect(node).height());
            }
        }
        QCOMPARE(seenRows.size(), int(view.visibleRowCount()));
    };

    verify();
    verifyPixels();
    const QVector<int> groups = view.scrollGroups();
    QVERIFY(groups.contains(0));
    QVERIFY(groups.contains(1));
    view.moveColumn(2, 3, VirtualTableView::MoveAnimation::Animate);
    const int committedX = view.columnGeometry(2).viewportX;
    advance(200);
    QVERIFY(headerX(2) > 0);
    QCOMPARE(view.columnGeometry(2).viewportX, committedX);
    verify();
    verifyPixels();
    advance(1000);
    QCOMPARE(headerX(2), committedX);
    verify();
    verifyPixels();

    view.resize(650, 300);
    view.flushPendingRelayout();
    settle();
    QVERIFY(view.maximumHorizontalOffset(0) > 0);
    QVERIFY(view.maximumHorizontalOffset(1) > 0);
    const int frozenX = view.columnGeometry(0).viewportX;
    const int primaryX = view.columnGeometry(1).viewportX;
    const int secondaryX = view.columnGeometry(4).viewportX;
    const qint64 secondaryOffset = qMin<qint64>(20, view.maximumHorizontalOffset(1));
    view.setHorizontalOffset(1, secondaryOffset);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.horizontalOffset(1), secondaryOffset);
    QCOMPARE(view.horizontalOffset(0), qint64(0));
    QCOMPARE(view.columnGeometry(0).viewportX, frozenX);
    QCOMPARE(view.columnGeometry(1).viewportX, primaryX);
    QCOMPARE(view.columnGeometry(4).viewportX, secondaryX - int(secondaryOffset));
    const qint64 primaryOffset = qMin<qint64>(20, view.maximumHorizontalOffset(0));
    view.setHorizontalOffset(primaryOffset);
    view.flushPendingRelayout();
    settle();
    QCOMPARE(view.horizontalOffset(0), primaryOffset);
    QCOMPARE(view.horizontalOffset(1), secondaryOffset);
    QCOMPARE(view.columnGeometry(0).viewportX, frozenX);
    QCOMPARE(view.columnGeometry(1).viewportX, primaryX - int(primaryOffset));
    QCOMPARE(view.columnGeometry(4).viewportX, secondaryX - int(secondaryOffset));
    verifyPixels();
    const QPoint tailBranch(frozenX + 9, view.visualRect(tail).center().y());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, tailBranch);
    QVERIFY(view.isExpanded(tail));
    QCOMPARE(view.visibleRowCount(), qsizetype(5));
    view.flushPendingRelayout();
    settle();
    const QModelIndex last = model.index(0, 0, tail);
    QCOMPARE(view.visibilityIndex()->indexAtVisibleRow(4), last);
    QCOMPARE(view.visualRect(last).bottom(), view.viewport()->rect().bottom());
    const QPoint lastMarker(frozenX + view.indentation() + 9, view.visualRect(last).center().y());
    QCOMPARE(view.grab().toImage().pixelColor(view.viewport()->pos() + lastMarker),
             QColor(230, 180, 10));
}

void TestTreeTableViewInteraction::paneHeaderDragsKeepGlobalOrderAndWidgetsAligned_data()
{
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("customHeaders");
    for (bool cells : {false, true}) {
        for (bool customHeaders : {false, true}) {
            const QByteArray name = QByteArray(cells ? "cells" : "rows")
                + (customHeaders ? "-custom" : "-default");
            QTest::newRow(name.constData()) << cells << customHeaders;
        }
    }
}

void TestTreeTableViewInteraction::paneHeaderDragsKeepGlobalOrderAndWidgetsAligned()
{
    QFETCH(bool, cells);
    QFETCH(bool, customHeaders);
    QStandardItemModel model;
    const auto sixColumns = [](const QString &name) {
        auto items = row(name);
        items.append(new QStandardItem(name + QStringLiteral(" fourth")));
        items.append(new QStandardItem(name + QStringLiteral(" fifth")));
        items.append(new QStandardItem(name + QStringLiteral(" sixth")));
        return items;
    };
    model.setHorizontalHeaderLabels({QStringLiteral("zero"), QStringLiteral("one"),
                                     QStringLiteral("two"), QStringLiteral("three"),
                                     QStringLiteral("four"), QStringLiteral("five")});
    model.appendRow(sixColumns(QStringLiteral("row")));
    auto *view = new VirtualTreeTableView;
    view->setUniformItemHeight(28);
    view->setDefaultColumnWidth(100);
    view->setColumnSpacing(6);
    view->setColumnDragEnabled(true);
    view->setHeaderAnimationEnabled(false);
    if (customHeaders) {
        auto *horizontal = new VirtualHeaderView(Qt::Horizontal);
        horizontal->setAdapter(new TreeHeaderAdapter(view, Qt::Horizontal, 1), true);
        auto *vertical = new VirtualHeaderView(Qt::Vertical);
        vertical->setAdapter(new TreeHeaderAdapter(view, Qt::Vertical, 2), true);
        view->setHorizontalHeader(horizontal);
        view->setVerticalHeader(vertical);
    }
    if (cells) {
        view->setCellAdapter(new CellAdapter, true);
        view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    } else {
        view->setTableAdapter(new ParentTypedRowAdapter(6), true);
    }
    view->setModel(&model);
    view->setPanes({{{0}, PaneScroll::Frozen, 0},
                    {{1, 2}, PaneScroll::Scrollable, 0},
                    {{3}, PaneScroll::Frozen, 0},
                    {{4, 5}, PaneScroll::Scrollable, 1}});
    showView(view, QSize(760, 280));
    view->flushPendingRelayout();
    settle();

    const auto findHeader = [&](int logical) {
        for (VirtualHeaderView *header : view->findChildren<VirtualHeaderView *>()) {
            if (header->orientation() != Qt::Horizontal || !header->sectionWidget(logical))
                continue;
            QWidget *section = header->sectionWidget(logical);
            if (section->isVisible() && !section->visibleRegion().isEmpty())
                return header;
        }
        return static_cast<VirtualHeaderView *>(nullptr);
    };
    const auto verifyColumn = [&](int logical) {
        VirtualHeaderView *header = findHeader(logical);
        QVERIFY(header);
        QWidget *section = header->sectionWidget(logical);
        QVERIFY(section);
        QCOMPARE(section->mapTo(view, QPoint()).x() - view->viewport()->x(),
                 view->columnGeometry(logical).viewportX);
        QCOMPARE(section->width(), view->columnWidth(logical));
        const QModelIndex cell = model.index(0, logical);
        if (cells) {
            QWidget *widget = view->cellWidget(cell);
            QVERIFY(widget);
            QCOMPARE(widget->mapTo(view, QPoint()).x() - view->viewport()->x(),
                     view->columnGeometry(logical).viewportX);
        } else {
            auto *rowWidget = static_cast<HostedRow *>(view->widgetForIndex(model.index(0, 0)));
            QVERIFY(rowWidget);
            QCOMPARE(rowWidget->host(logical)->mapTo(view, QPoint()).x() - view->viewport()->x(),
                     view->columnGeometry(logical).viewportX);
        }
    };
    const auto dragWithinPane = [&](int sourceLogical, int targetLogical) {
        VirtualHeaderView *header = findHeader(sourceLogical);
        QVERIFY(header);
        QCOMPARE(findHeader(targetLogical), header);
        QWidget *source = header->sectionWidget(sourceLogical);
        QWidget *target = header->sectionWidget(targetLogical);
        QVERIFY(source);
        QVERIFY(target);
        const QPoint start = source->geometry().center();
        const QPoint destination = target->geometry().center() + QPoint(10, 0);
        sendHeaderMouse(header, QEvent::MouseButtonPress, start,
                        Qt::LeftButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, start + QPoint(3, 0),
                        Qt::NoButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, destination,
                        Qt::NoButton, Qt::LeftButton);
        settle();
        QCOMPARE(view->horizontalHeaderGeometry()->visualIndex(sourceLogical), sourceLogical);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(header, &escape);
        settle();
        QCOMPARE(view->horizontalHeaderGeometry()->visualIndex(sourceLogical), sourceLogical);
        sendHeaderMouse(header, QEvent::MouseButtonPress, start,
                        Qt::LeftButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, start + QPoint(3, 0),
                        Qt::NoButton, Qt::LeftButton);
        sendHeaderMouse(header, QEvent::MouseMove, destination,
                        Qt::NoButton, Qt::LeftButton);
        QSignalSpy moved(view->horizontalHeaderGeometry(), &HeaderGeometry::sectionMoved);
        sendHeaderMouse(header, QEvent::MouseButtonRelease, destination,
                        Qt::LeftButton, Qt::NoButton);
        settle();
        view->flushPendingRelayout();
        settle();
    };

    verifyColumn(1);
    verifyColumn(4);
    dragWithinPane(1, 2);
    verifyColumn(1);
    verifyColumn(2);
    dragWithinPane(4, 5);
    verifyColumn(4);
    verifyColumn(5);
    QCOMPARE(view->horizontalHeaderGeometry()->visualIndex(1), 2);
    QCOMPARE(view->horizontalHeaderGeometry()->visualIndex(4), 5);
    delete view;
}

QTEST_MAIN(TestTreeTableViewInteraction)

#include "tst_treetableviewinteraction.moc"
