#include <virtualitemviews/headerview.h>
#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/headerwidgetadapter.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include <algorithm>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 30;
constexpr int kColumnWidth = 100;
constexpr int kViewWidth = 460;
constexpr int kViewHeight = 320;

QString cellText(int row, int column)
{
    return QStringLiteral("r%1c%2").arg(row).arg(column);
}

/// Row widget with one ColumnHost per column: the framework owns the geometry.
class TableRowWidget : public QWidget
{
public:
    TableRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        m_hosts.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(2, 0, 80, 18);
            m_hosts.append(host);
            m_labels.append(label);
        }
    }

    ColumnHost *host(int logicalColumn) const { return m_hosts.value(logicalColumn); }
    QLabel *label(int logicalColumn) const { return m_labels.value(logicalColumn); }

    QSize sizeHint() const override { return QSize(400, kRowHeight); }

private:
    QVector<ColumnHost *> m_hosts;
    QVector<QLabel *> m_labels;
};

/// Row widget of the "business schema" test: it grows and shrinks its ColumnHosts with the
/// model, but only ever inside bindWidget() - which is the contract the review asks about.
///
/// The hosts are looked up through the child tree (not cached pointers): the framework
/// reparents them into its pane clip containers, and a clip container that goes away takes
/// its hosts with it.
class SchemaRowWidget : public QWidget
{
public:
    explicit SchemaRowWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    /// Hosts sorted by their logical column.
    QList<ColumnHost *> hostsInColumnOrder() const
    {
        QList<ColumnHost *> hosts = findChildren<ColumnHost *>();
        std::sort(hosts.begin(), hosts.end(), [](ColumnHost *lhs, ColumnHost *rhs) {
            return lhs->logicalColumn() < rhs->logicalColumn();
        });
        return hosts;
    }

    void rebuildForColumns(int columns)
    {
        QList<ColumnHost *> hosts = hostsInColumnOrder();
        while (hosts.size() > columns) {
            delete hosts.takeLast();
        }
        for (int column = 0; column < columns; ++column) {
            const bool present = std::any_of(hosts.cbegin(), hosts.cend(), [column](ColumnHost *host) {
                return host->logicalColumn() == column;
            });
            if (present)
                continue;
            auto *host = new ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(2, 0, 80, 18);
            hosts.append(host);
        }
    }

    ColumnHost *host(int column) const
    {
        for (ColumnHost *host : hostsInColumnOrder()) {
            if (host->logicalColumn() == column)
                return host;
        }
        return nullptr;
    }

    QLabel *label(int column) const
    {
        ColumnHost *host = this->host(column);
        return host ? host->findChild<QLabel *>() : nullptr;
    }

    int hostCount() const { return hostsInColumnOrder().size(); }

    void clearTexts()
    {
        for (ColumnHost *host : hostsInColumnOrder()) {
            if (QLabel *label = host->findChild<QLabel *>())
                label->setText(QString());
        }
    }

    QSize sizeHint() const override { return QSize(400, kRowHeight); }
};

class SchemaRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        ++created;
        return new SchemaRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++bound;
        auto *row = static_cast<SchemaRowWidget *>(widget);
        const int columns = index.model() ? index.model()->columnCount() : 0;
        row->rebuildForColumns(columns);
        for (int column = 0; column < columns; ++column)
            row->label(column)->setText(index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        ++unbound;
        static_cast<SchemaRowWidget *>(widget)->clearTexts();
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(400, kRowHeight); }

    int bound = 0;
    int unbound = 0;
    int created = 0;
};

/// Adapter of the row-identity test: records every index it is handed, so a non-canonical
/// (column != 0) or invalid row index shows up as a counter instead of a silent misread.
class RecordingRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("recordingRow"));
        return label;
    }

    void bindWidget(QWidget *, const QModelIndex &index) override
    {
        ++bound;
        countIndex(index);
    }

    void unbindWidget(QWidget *, const QModelIndex &) override { ++unbound; }

    void layoutRowWidget(QWidget *, const QModelIndex &rowIndex,
                         const TableRowLayoutContext &) override
    {
        ++layoutCalls;
        if (!rowIndex.isValid())
            ++layoutInvalid;
        else if (rowIndex.column() != 0)
            ++layoutNonCanonical;
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(400, kRowHeight); }

    void countIndex(const QModelIndex &index)
    {
        if (!index.isValid())
            ++invalidIndexes;
        else if (index.column() != 0)
            ++nonCanonicalIndexes;
    }

    int bound = 0;
    int unbound = 0;
    int invalidIndexes = 0;
    int nonCanonicalIndexes = 0;
    int layoutCalls = 0;
    int layoutInvalid = 0;
    int layoutNonCanonical = 0;
};

class TableTestAdapter : public TableWidgetAdapter
{
public:
    explicit TableTestAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new TableRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++bound;
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column) {
            row->label(column)->setText(
                index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        ++unbound;
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column)
            row->label(column)->setText(QString());
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(400, kRowHeight);
    }

    int created = 0;
    int bound = 0;
    int unbound = 0;

private:
    int m_columnCount = 0;
};

QStandardItemModel *buildModel(int rows, int columns, QObject *parent)
{
    auto *model = new QStandardItemModel(rows, columns, parent);
    QStringList labels;
    for (int column = 0; column < columns; ++column)
        labels << QStringLiteral("Column %1").arg(column);
    model->setHorizontalHeaderLabels(labels);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column)
            model->setItem(row, column, new QStandardItem(cellText(row, column)));
    }
    return model;
}

/// Row/column model without per-cell storage: used for the million-row case.
class BigTableModel : public QAbstractTableModel
{
public:
    explicit BigTableModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(rows)
        , m_columns(columns)
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
        return cellText(index.row(), index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("Column %1").arg(section)
                                             : QString::number(section + 1);
    }

    /// The row-header tests need a model that gets small again.
    void setDataRowCount(int rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }

    /// Column structure changes that emit *only* the column signals: a plain
    /// QAbstractTableModel does not additionally reset the view (QStandardItemModel
    /// emits coarser signals), which is what the column-structure tests need to isolate.
    void insertDataColumn(int at)
    {
        beginInsertColumns(QModelIndex(), at, at);
        ++m_columns;
        endInsertColumns();
    }

    void removeDataColumn(int at)
    {
        beginRemoveColumns(QModelIndex(), at, at);
        --m_columns;
        endRemoveColumns();
    }

    void moveDataColumn(int from, int to)
    {
        const int destination = to > from ? to + 1 : to;
        if (!beginMoveColumns(QModelIndex(), from, from, QModelIndex(), destination))
            return;
        endMoveColumns();
    }

private:
    int m_rows = 0;
    int m_columns = 0;
};

QWidget *rowWidgetFor(VirtualTableView &view, int row)
{
    for (const MaterializedItem &item : view.materializedItems()) {
        if (item.index.row() == row)
            return item.widget;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Row kind that follows the column structure (P1 of the fourth review).
// ---------------------------------------------------------------------------

/// Row widget of kind A / kind B: two different classes for two WidgetTypes, exactly like a
/// business that draws "order rows" and "group rows" with different widgets.
class KindARow : public QWidget
{
public:
    explicit KindARow(QWidget *parent = nullptr) : QWidget(parent) {}
};

class KindBRow : public QWidget
{
public:
    explicit KindBRow(QWidget *parent = nullptr) : QWidget(parent) {}
};

/// Model whose column-0 cell content (and therefore the row's WidgetType) is decided by the
/// *current* column structure: inserting a column before column 0 renames the canonical
/// (row, 0) cell, so the same row becomes another kind.
class KindColumnModel : public QAbstractTableModel
{
public:
    explicit KindColumnModel(int rows, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(rows)
        , m_kinds({QStringLiteral("A"), QStringLiteral("A"), QStringLiteral("A")})
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_kinds.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("%1%2").arg(m_kinds.value(index.column())).arg(index.row());
    }

    void insertKindColumn(int at, const QString &kind)
    {
        beginInsertColumns(QModelIndex(), at, at);
        m_kinds.insert(at, kind);
        endInsertColumns();
    }

    void removeKindColumn(int at)
    {
        beginRemoveColumns(QModelIndex(), at, at);
        m_kinds.removeAt(at);
        endRemoveColumns();
    }

    void moveKindColumn(int from, int to)
    {
        const int destination = to > from ? to + 1 : to;
        if (!beginMoveColumns(QModelIndex(), from, from, QModelIndex(), destination))
            return;
        m_kinds.move(from, to);
        endMoveColumns();
    }

private:
    int m_rows = 0;
    QVector<QString> m_kinds;
};

/// Adapter that records the exact unbind/bind pairs, checks the widget class against the type
/// the framework asked for, and refuses non-canonical (column != 0) row indexes.
class KindRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        auto *widget = type == 0 ? static_cast<QWidget *>(new KindARow(parent))
                                 : static_cast<QWidget *>(new KindBRow(parent));
        m_typeOf.insert(widget, type);
        ++created;
        return widget;
    }

    WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.isValid() && index.data(Qt::DisplayRole).toString().startsWith(QLatin1Char('A'))
            ? WidgetType(0)
            : WidgetType(1);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++bound;
        lastBoundWidget = widget;
        lastBoundIndex = index;
        if (!index.isValid() || index.column() != 0)
            ++nonCanonicalBinds;
        if (m_typeOf.value(widget, -1) != widgetType(index))
            ++typeMismatches;
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++unbound;
        lastUnboundWidget = widget;
        lastUnboundIndex = index;
        if (!index.isValid() || index.column() != 0)
            ++nonCanonicalUnbinds;
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(400, kRowHeight); }

    int created = 0;
    int bound = 0;
    int unbound = 0;
    int typeMismatches = 0;
    int nonCanonicalBinds = 0;
    int nonCanonicalUnbinds = 0;
    QPointer<QWidget> lastBoundWidget;
    QPointer<QWidget> lastUnboundWidget;
    QModelIndex lastBoundIndex;
    QModelIndex lastUnboundIndex;

private:
    QHash<const QWidget *, WidgetType> m_typeOf;
};

/// Header adapter that renders the header data of the model it was told about - the pattern
/// the README teaches, with the `setLabelModel()` hook and a QPointer.
class ModelLabelHeaderAdapter : public HeaderWidgetAdapter
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("headerSectionLabel"));
        return label;
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        const QString text = m_model
            ? m_model->headerData(logicalIndex, m_orientation, Qt::DisplayRole).toString()
            : QString();
        static_cast<QLabel *>(widget)->setText(text);
    }

    void unbindSection(QWidget *widget, int) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    void setLabelModel(QAbstractItemModel *model) override { m_model = model; }

    void setOrientation(Qt::Orientation orientation) { m_orientation = orientation; }
    QAbstractItemModel *labelModel() const { return m_model.data(); }

private:
    QPointer<QAbstractItemModel> m_model;
    Qt::Orientation m_orientation = Qt::Horizontal;
};

/// Counts the "the row-number strip is hidden" diagnostic: it belongs to the
/// transition, not to every relayout (P2-9).
int g_rowHeaderWarnings = 0;

void countRowHeaderWarnings(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg && message.contains(QStringLiteral("vertical header hidden")))
        ++g_rowHeaderWarnings;
}
} // namespace

namespace {

/// Colour a scrollable column would bleed with if it were not clipped.
QColor paneProbeColor(int column)
{
    static const QColor colors[] = {
        QColor(200, 0, 0), QColor(0, 0, 200), QColor(200, 140, 0),
        QColor(160, 0, 160), QColor(0, 150, 150),
    };
    return colors[column % 5];
}

/// Child of a column host: paints one thin stripe so a leak is easy to spot.
class PaneProbe : public QWidget
{
public:
    PaneProbe(int column, QWidget *parent)
        : QWidget(parent)
        , m_column(column)
    {
        setGeometry(0, 0, kColumnWidth, kRowHeight);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(0, 4 + m_column * 3, width(), 2), paneProbeColor(m_column));
    }

private:
    int m_column = 0;
};

/// Row widget with a custom opaque background: the framework must not paint over
/// it when the row is inside a frozen pane.
class PaintedRowWidget : public QWidget
{
public:
    PaintedRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new ColumnHost(column, this);
            new PaneProbe(column, host);
        }
    }

    static QColor background()
    {
        return QColor(0, 128, 0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), background());
    }
};

class PaintedRowAdapter : public TableWidgetAdapter
{
public:
    explicit PaintedRowAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new PaintedRowWidget(parent, m_columnCount);
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}

    QSize estimatedSize(const QModelIndex &) const override
    {
        return QSize(kColumnWidth * 2, kRowHeight);
    }

private:
    int m_columnCount = 0;
};

} // namespace

/// Row-reorderable model for the header-drag tests: QStandardItemModel itself refuses
/// `moveRows()`, so this one moves a row for real (the example's WideModel does the same
/// with its display order, an application model with its data).
/// Row-reorderable model for the header-drag tests: `QStandardItemModel` refuses
/// `moveRows()`, and a model that moves rows *inside* beginMoveRows()/endMoveRows() trips
/// Qt's own assertions (nested row signals), so this one keeps a display order like the
/// example's WideModel - an application model would move its data instead.
class ReorderableModel : public QAbstractTableModel
{
public:
    ReorderableModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_columns(qMax(1, columns))
    {
        for (int row = 0; row < qMax(0, rows); ++row)
            m_order.append(row);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_order.size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columns;
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("r%1c%2").arg(m_order.at(index.row())).arg(index.column());
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || section < 0 || section >= m_order.size())
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("c%1").arg(section)
                                             : QStringLiteral("R%1").arg(m_order.at(section));
    }

    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if (sourceParent.isValid() || destinationParent.isValid() || count <= 0
            || sourceRow < 0 || sourceRow + count > m_order.size())
            return false;
        if (destinationChild < 0 || destinationChild > m_order.size())
            return false;
        if (destinationChild >= sourceRow && destinationChild <= sourceRow + count)
            return false;   // Qt's "a move onto itself is no move" rule
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent,
                           destinationChild))
            return false;
        QVector<int> moved;
        for (int index = 0; index < count; ++index)
            moved.append(m_order.at(sourceRow + index));
        m_order.remove(sourceRow, count);
        const int target = destinationChild > sourceRow ? destinationChild - count
                                                       : destinationChild;
        for (int index = 0; index < count; ++index)
            m_order.insert(target + index, moved.at(index));
        endMoveRows();
        return true;
    }

private:
    int m_columns = 0;
    QVector<int> m_order;
};
class TestVirtualTableView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void materializesOnlyVisibleRows();
    void rowWidgetModeCreatesNoCellWidgets();
    void headerAndRowsAgreeOnColumnBoundaries();
    void defaultHeaderStaysOnTheCommittedColumns();
    void columnStructureChangesRebindTheRowWidgets();
    void columnZeroChangesKeepTheRowIdentityCanonical();
    void columnZeroChangesReleaseTheRowWidgetsFirst();
    void setAdapterConfiguresTheTableNotJustTheBase();
    void rowStripDragReportsAMoveTheModelMayRefuse();
    void rowStripDragTakesTheRowsWithIt();
    void draggingAHeaderSectionSwapsTheOrderInBothAxes();
    void tenMillionUniformRowsResizeOneStaysCompact();
    void switchingTheModelRebindsEveryPaneHeader();
    void rowInsertKeepsTheColumnWidths();
    void rowInsertKeepsTheExplicitRowHeights();
    void columnResizeTouchesMaterializedRowsOnly();
    void geometryIsTheSingleAuthority();
    void columnMoveFollowsTheGeometry();
    void hiddenColumnHidesHost();
    void headerStateRoundTrip();
    void hugeModelColumnResizeDoesNotWalkRows();
    void columnCountFollowsTheModel();
    void sortingRequestsAndAppliesModelSort();
    void verticalHeaderMirrorsRowHeights();
    void verticalHeaderFollowsVerticalScroll();
    void verticalHeaderDragChangesRowHeight();
    void rowSizePolicyControlsMeasurement();
    void rowHeaderMirrorsTheCommittedSizeUnderMeasuredWins();
    void theRowHeaderComesBackWhenTheModelShrinks();
    void keyboardMovesTheCurrentColumn();
    void visibleColumnRangeFollowsOverscanAndOffset();
    void frozenColumnsStayWhileTheScrollablePaneScrolls();
    void frozenPanesDoNotAddScrollSpace();
    void frozenColumnsFollowTheSingleGeometry();
    void frozenRightPaneIsPinnedToTheRightEdge();
    void frozenPaneIsUnaffectedByScrolling();
    void frozenPaneKeepsTheRowBackground();
    void frozenPanesDrawBodySeparatorLines();
    void paneSeparatorStyleIsCustomizable();
    void headerStateRestoresFrozenColumns();
    void aBrokenStateLeavesTheViewUntouched();
    void columnsShareTheLeftoverWidthByStretchFactor();

private:
    void sendMouseTo(QWidget *widget, QEvent::Type type, const QPoint &pos,
                     Qt::MouseButton button, Qt::MouseButtons buttons);
    int sectionViewportX(const VirtualHeaderView *renderer, int column) const;

    QStandardItemModel *m_model = nullptr;
    TableTestAdapter *m_adapter = nullptr;
    VirtualTableView *m_view = nullptr;
};

void TestVirtualTableView::init()
{
    m_model = buildModel(50, 6, this);
    m_adapter = new TableTestAdapter(6);
    m_view = new VirtualTableView();
    m_view->setTableAdapter(m_adapter);
    m_view->setUniformItemHeight(kRowHeight);
    m_view->setDefaultColumnWidth(kColumnWidth);
    m_view->setModel(m_model);
    showView(m_view, QSize(kViewWidth, kViewHeight));
}

void TestVirtualTableView::cleanup()
{
    delete m_view;
    m_view = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_model;
    m_model = nullptr;
}

/// Sends a mouse event with an explicit button state: the resize/drag gestures read the
/// pressed buttons, which QTest::mouseMove does not always carry.
void TestVirtualTableView::sendMouseTo(QWidget *widget, QEvent::Type type, const QPoint &pos,
                                       Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

/// Viewport x of a materialized section of \a renderer (the section's x is relative
/// to its renderer, which sits on its pane rect - a clone of a frozen pane is not at
/// the viewport origin).
int TestVirtualTableView::sectionViewportX(const VirtualHeaderView *renderer, int column) const
{
    if (renderer->orientation() != Qt::Horizontal)
        return std::numeric_limits<int>::min();
    QWidget *section = renderer->sectionWidget(column);
    // A section whose column left the widget is hidden and keeps the geometry it had:
    // only the sections that are really shown carry the current position (§19).
    if (!section || !section->isVisible())
        return std::numeric_limits<int>::min();
    return section->x() + renderer->x() - m_view->viewport()->x();
}

void TestVirtualTableView::materializesOnlyVisibleRows()
{
    QCOMPARE(m_view->columnCount(), 6);
    // The horizontal header and the horizontal scrollbar reduce the viewport.
    QVERIFY(m_view->viewport()->height() < kViewHeight);
    QVERIFY(m_view->viewport()->height() > kViewHeight - 3 * m_view->headerHeight());

    // The window is visible rows + overscan; at the top of the model the "before"
    // overscan cannot be used.
    const int viewportHeight = m_view->viewport()->height();
    const qsizetype visibleRows = viewportHeight / kRowHeight + (viewportHeight % kRowHeight ? 1 : 0);
    QVERIFY(m_view->materializedItemCount() >= visibleRows);
    QVERIFY(m_view->materializedItemCount() <= visibleRows + 4);
    QCOMPARE(m_view->stats().createCount,
             quint64(m_view->materializedItemCount() + m_view->pooledWidgetCount()));
    QVERIFY(m_view->visibleRows().isValid());
    QCOMPARE(m_view->visibleRows().first, qsizetype(0));
    QCOMPARE(m_view->rowHeight(0), kRowHeight);
}

void TestVirtualTableView::rowWidgetModeCreatesNoCellWidgets()
{
    // Row Widget Mode: one widget per materialized row, no cell level widgets.
    QWidget *rowWidget = rowWidgetFor(*m_view, 0);
    QVERIFY(rowWidget != nullptr);
    QCOMPARE(rowWidget->findChildren<QWidget *>(QStringLiteral("cellLabel")).size(), 6);
    QCOMPARE(rowWidget->findChildren<ColumnHost *>().size(), 6);
    // The body owns row widgets only (materialized + pooled), never cells.
    QCOMPARE(m_view->viewport()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).size(),
             int(m_view->materializedItemCount() + m_view->pooledWidgetCount()));
    QCOMPARE(m_view->stats().createCount, quint64(m_adapter->created));
}

void TestVirtualTableView::headerAndRowsAgreeOnColumnBoundaries()
{
    // The default horizontal header is a widget header (LabelHeaderView), so the
    // agreement is read from the section widgets - the same x the body's ColumnHosts
    // use (§45.1).
    auto *header = dynamic_cast<VirtualHeaderView *>(m_view->horizontalHeader());
    QVERIFY(header != nullptr);
    QVERIFY(header->headerWidget()->isVisible());

    // Column resize: header and every materialized row must agree pixel exactly.
    m_view->setColumnWidth(1, 170);
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 2));
    QVERIFY(rowWidget != nullptr);

    for (int column : m_view->visibleColumnLogicalIndexes()) {
        const ColumnGeometry geometry = m_view->columnGeometry(column);
        QWidget *section = header->sectionWidget(column);
        QVERIFY(section != nullptr);
        // The visible ones agree pixel exactly; a section the header hid (its column
        // scrolled out) is not part of this pass.
        if (!section->isVisible())
            continue;
        QCOMPARE(sectionViewportX(header, column), geometry.viewportX);
        QCOMPARE(section->width(), geometry.width);

        ColumnHost *host = rowWidget->host(column);
        QVERIFY(host != nullptr);
        QCOMPARE(host->mapTo(m_view->viewport(), QPoint(0, 0)).x(), geometry.viewportX);
        QCOMPARE(host->width(), geometry.width);
    }
}

void TestVirtualTableView::defaultHeaderStaysOnTheCommittedColumns()
{
    // The header the view installs itself: a label-only widget header. It has to sit
    // exactly on the committed column geometry for the header *and* for the pane
    // clones, after a resize, a scroll, a reorder and a freeze.
    QVERIFY(dynamic_cast<LabelHeaderView *>(m_view->horizontalHeader()) != nullptr);
    // A hidden header still works - the sections just have no visible geometry.
    QVERIFY(m_view->isHorizontalHeaderVisible());

    // Every section of every renderer (the view's own header and the clones of the
    // frozen / extra panes) sits exactly on its column's committed geometry.
    const auto checkEverySection = [this]() {
        // The *column* renderers: the view also owns the row-number strip now, whose
        // sections are packed along y and are checked by the vertical-header tests.
        QList<VirtualHeaderView *> renderers;
        for (VirtualHeaderView *renderer : m_view->findChildren<VirtualHeaderView *>()) {
            if (renderer->orientation() == Qt::Horizontal)
                renderers.append(renderer);
        }
        QVERIFY(!renderers.isEmpty());
        for (VirtualHeaderView *renderer : renderers) {
            for (int column : renderer->materializedSections()) {
                QWidget *section = renderer->sectionWidget(column);
                QVERIFY(section != nullptr);
                if (!section->isVisible())
                    continue;   // its column left this renderer: hidden, geometry parked
                const ColumnGeometry geometry = m_view->columnGeometry(column);
                QCOMPARE(sectionViewportX(renderer, column), geometry.viewportX);
                QCOMPARE(section->width(), geometry.width);
            }
        }
    };

    checkEverySection();
    QCOMPARE(m_view->horizontalHeader()->headerWidget()->width(),
             m_view->viewport()->width());

    m_view->setColumnWidth(1, 170);
    QApplication::processEvents();
    checkEverySection();
    QCOMPARE(m_view->columnWidth(1), 170);

    m_view->setHorizontalOffset(120);
    QApplication::processEvents();
    checkEverySection();

    m_view->moveColumn(0, 3);
    QApplication::processEvents();
    checkEverySection();

    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->setHorizontalOffset(0);
    QApplication::processEvents();
    checkEverySection();

    // ... and hiding the header stays a plain visibility switch.
    m_view->setHorizontalHeaderVisible(false);
    QApplication::processEvents();
    QVERIFY(!m_view->isHorizontalHeaderVisible());
    QVERIFY(m_view->horizontalHeader()->headerWidget()->isHidden());
    m_view->setHorizontalHeaderVisible(true);
    QApplication::processEvents();
    QVERIFY(!m_view->horizontalHeader()->headerWidget()->isHidden());
    checkEverySection();
}


void TestVirtualTableView::columnStructureChangesRebindTheRowWidgets()
{
    // P1-2 / P2-2 of the second review: in Row Widget Mode the row keeps its identity across
    // a column insert/remove/move, so the widget is not recycled - but the business row
    // widget builds its column schema in bindWidget(), and nothing told it. The result was
    // "geometry right, business row still the old schema".
    // A model that emits only the column signals: QStandardItemModel additionally resets
    // the view's materialized set, which would mask the missing re-bind.
    BigTableModel model(50, 3, this);
    SchemaRowAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QWidget *widget = rowWidgetFor(view, 1);
    QVERIFY(widget != nullptr);
    auto *row = static_cast<SchemaRowWidget *>(widget);
    QCOMPARE(row->hostCount(), 3);
    QCOMPARE(row->label(1)->text(), cellText(1, 1));

    const auto rowMatchesTheModel = [&](int modelRow) {
        auto *schemaRow = static_cast<SchemaRowWidget *>(rowWidgetFor(view, modelRow));
        QVERIFY(schemaRow != nullptr);
        QCOMPARE(schemaRow->hostCount(), model.columnCount());
        for (int column = 0; column < model.columnCount(); ++column) {
            QCOMPARE(schemaRow->label(column)->text(), cellText(modelRow, column));
        }
    };

    const int bindsBefore = adapter.bound;
    model.insertDataColumn(1);
    view.flushPendingRelayout();
    QVERIFY(adapter.bound > bindsBefore);        // the visible rows were re-bound
    QCOMPARE(rowWidgetFor(view, 1), widget);     // ... without recycling (identity kept)
    rowMatchesTheModel(1);

    model.removeDataColumn(3);
    view.flushPendingRelayout();
    rowMatchesTheModel(1);

    model.moveDataColumn(0, 2);
    view.flushPendingRelayout();
    rowMatchesTheModel(1);

    // The hosts follow the geometry of the *new* column layout.
    for (int column : view.visibleColumnLogicalIndexes()) {
        ColumnHost *host = row->host(column);
        QVERIFY(host != nullptr);
        QCOMPARE(host->width(), view.columnGeometry(column).width);
    }
}


void TestVirtualTableView::columnZeroChangesKeepTheRowIdentityCanonical()
{
    // P1 of the third review: a table tracks a materialized row by its (row, 0) cell, and a
    // column insert/remove/move that touches column 0 renames or invalidates exactly that
    // cell. The adapter then received a non-canonical (column != 0) - or, after removing
    // column 0, an invalid - row index, and layoutRowWidget() the same.
    BigTableModel model(20, 3, this);
    RecordingRowAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const auto checkCanonical = [&](const char *phase) {
        QVERIFY2(adapter.invalidIndexes == 0 && adapter.nonCanonicalIndexes == 0,
                 qPrintable(QStringLiteral("%1: bindWidget saw %2 invalid / %3 non-canonical "
                                           "indexes")
                                .arg(QString::fromLatin1(phase))
                                .arg(adapter.invalidIndexes)
                                .arg(adapter.nonCanonicalIndexes)));
        QVERIFY2(adapter.layoutInvalid == 0 && adapter.layoutNonCanonical == 0,
                 qPrintable(QStringLiteral("%1: layoutRowWidget saw %2 invalid / %3 "
                                           "non-canonical indexes")
                                .arg(QString::fromLatin1(phase))
                                .arg(adapter.layoutInvalid)
                                .arg(adapter.layoutNonCanonical)));
        QVERIFY(adapter.layoutCalls > 0);
    };
    const auto resetCounters = [&]() {
        adapter.invalidIndexes = 0;
        adapter.nonCanonicalIndexes = 0;
        adapter.layoutInvalid = 0;
        adapter.layoutNonCanonical = 0;
        adapter.layoutCalls = 0;
    };
    const auto identityOf = [&](int row) {
        QWidget *widget = rowWidgetFor(view, row);
        return widget ? view.indexForWidget(widget) : QModelIndex();
    };

    QCOMPARE(identityOf(1), model.index(1, 0));

    // (a) Insert a column *before* column 0: the old identity cell becomes column 1.
    resetCounters();
    model.insertDataColumn(0);
    view.flushPendingRelayout();
    checkCanonical("insert at 0");
    QCOMPARE(identityOf(1), model.index(1, 0));

    // (b) Remove column 0: the old identity cell is gone entirely (it would be invalid).
    resetCounters();
    model.removeDataColumn(0);
    view.flushPendingRelayout();
    checkCanonical("remove at 0");
    QCOMPARE(identityOf(1), model.index(1, 0));

    // (c) Move column 0 to the end: the identity cell moves to another column.
    resetCounters();
    model.moveDataColumn(0, 2);
    view.flushPendingRelayout();
    checkCanonical("move from 0");
    QCOMPARE(identityOf(1), model.index(1, 0));
}

void TestVirtualTableView::columnZeroChangesReleaseTheRowWidgetsFirst()
{
    // P1 of the fourth review: the third-round fix re-keyed the *live* MaterializedItem, which
    // silently skipped three parts of the adapter contract - unbindWidget() never saw the old
    // index (so business code kept its subscriptions to a cell that no longer exists),
    // WidgetType was not recomputed, and the index -> widget lookup still pointed at the old
    // identity. The kernel now releases every row *before* the change (old index still valid,
    // honest unbind) and materializes the canonical (row, 0) cell afterwards.
    KindColumnModel model(6, this);
    KindRowAdapter adapter;
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();
    QVERIFY(adapter.bound > 0);
    QVERIFY(rowWidgetFor(view, 1) != nullptr);
    QVERIFY(dynamic_cast<KindARow *>(rowWidgetFor(view, 1)) != nullptr);

    const auto checkContract = [&](const char *phase) {
        QVERIFY2(adapter.typeMismatches == 0,
                 qPrintable(QStringLiteral("%1: bindWidget() got %2 widgets of the wrong class")
                                .arg(QString::fromLatin1(phase))
                                .arg(adapter.typeMismatches)));
        QCOMPARE(adapter.nonCanonicalBinds, 0);
        QCOMPARE(adapter.nonCanonicalUnbinds, 0);
    };
    const auto resetCounters = [&]() {
        adapter.bound = 0;
        adapter.unbound = 0;
        adapter.nonCanonicalBinds = 0;
        adapter.nonCanonicalUnbinds = 0;
        adapter.typeMismatches = 0;
        adapter.lastBoundWidget = nullptr;
        adapter.lastUnboundWidget = nullptr;
        adapter.lastBoundIndex = QModelIndex();
        adapter.lastUnboundIndex = QModelIndex();
    };
    const auto rowIsSymmetric = [&](const char *phase) {
        QWidget *row = rowWidgetFor(view, 1);
        QVERIFY2(row != nullptr, phase);
        QCOMPARE(view.indexForWidget(row), model.index(1, 0));
        QCOMPARE(view.widgetForIndex(model.index(1, 0)), row);
    };

    // (a) Insert a column before column 0: the canonical cell (and with it the row's
    // WidgetType) changes from kind A to kind B.
    resetCounters();
    model.insertKindColumn(0, QStringLiteral("B"));
    view.flushPendingRelayout();
    QVERIFY(adapter.unbound > 0);                    // released ...
    QVERIFY(adapter.lastUnboundIndex.isValid());
    QCOMPARE(adapter.lastUnboundIndex.column(), 0);  // ... through its canonical identity
    QVERIFY(adapter.bound > 0);                      // and materialized again
    checkContract("insert at 0");
    QVERIFY(dynamic_cast<KindBRow *>(rowWidgetFor(view, 1)) != nullptr);
    rowIsSymmetric("insert at 0");

    // (b) Remove column 0 again: back to kind A.
    resetCounters();
    model.removeKindColumn(0);
    view.flushPendingRelayout();
    QVERIFY(adapter.unbound > 0);
    QCOMPARE(adapter.lastUnboundIndex.column(), 0);
    checkContract("remove at 0");
    QVERIFY(dynamic_cast<KindARow *>(rowWidgetFor(view, 1)) != nullptr);
    rowIsSymmetric("remove at 0");

    // (c) Move column 0 away: the kind moves to whatever column is first afterwards.
    resetCounters();
    model.moveKindColumn(0, 2);
    view.flushPendingRelayout();
    QVERIFY(adapter.unbound > 0);
    QCOMPARE(adapter.lastUnboundIndex.column(), 0);
    checkContract("move from 0");
    rowIsSymmetric("move from 0");

    // (d) A column change that does not touch column 0 keeps the row identity: the widgets
    // are only re-bound (the table's schema rebind), never recycled through the adapter.
    resetCounters();
    model.insertKindColumn(2, QStringLiteral("A"));
    view.flushPendingRelayout();
    QCOMPARE(adapter.unbound, 0);
    QVERIFY(adapter.bound > 0);
    checkContract("insert at 2");
    rowIsSymmetric("insert at 2");
}

/// Drops a row from another model onto \a view at \a viewportPos: the four events a real drag
/// sends, with a payload of the kind a QStandardItemModel source hands over (the drag_drop
/// example drags a tree node into the table exactly like this).
bool dropARowInto(VirtualTableView &view, const QPoint &viewportPos)
{
    QStandardItemModel source(1, 1);
    source.setItem(0, 0, new QStandardItem(QStringLiteral("dropped")));
    QScopedPointer<QMimeData> payload(source.mimeData({source.index(0, 0)}));
    if (!payload)
        return false;
    view.setDragEnabled(true);
    QDragEnterEvent enter(viewportPos, Qt::CopyAction, payload.data(), Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &enter);
    QDragMoveEvent move(viewportPos, Qt::CopyAction, payload.data(), Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    if (!move.isAccepted())
        return false;
    QDropEvent drop(QPointF(viewportPos), Qt::CopyAction, payload.data(), Qt::LeftButton,
                    Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &drop);
    return drop.isAccepted();
}

void TestVirtualTableView::rowInsertKeepsTheColumnWidths()
{
    // Follow-up of the fourth review (found with examples/drag_drop: "dropping a row into the
    // table collapses every column"): the native header mirrors QHeaderView::sectionResized()
    // back into the geometry, but QHeaderView also emits it for its *own* layout work - a model
    // change re-lays out the sections and reports the size they had before that pass, which is 0.
    // Writing those 0s into the geometry clamped every column to the minimum width (24). It needs
    // a *pane* header to show up: the pane clones re-lay out on every model change.
    QStandardItemModel model(20, 4, this);
    for (int row = 0; row < 20; ++row) {
        for (int column = 0; column < 4; ++column)
            model.setItem(row, column, new QStandardItem(cellText(row, column)));
    }
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenColumns({0, 1});        // the pane clone + its own filter
    view.setColumnWidth(2, 200);
    view.setColumnWidth(3, 90);
    view.flushPendingRelayout();
    QCOMPARE(view.columnWidth(0), kColumnWidth);
    QCOMPARE(view.columnWidth(2), 200);

    // A drop inserts a row into the model; the geometry must not move.
    QVERIFY(dropARowInto(view, QPoint(kColumnWidth * 2 + 10, kRowHeight + 5)));
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    QCOMPARE(model.rowCount(), 21);
    QCOMPARE(view.columnWidth(0), kColumnWidth);   // frozen pane, own width
    QCOMPARE(view.columnWidth(1), kColumnWidth);
    QCOMPARE(view.columnWidth(2), 200);            // primary pane, explicit width
    QCOMPARE(view.columnWidth(3), 90);
    QCOMPARE(view.rowHeight(0), kRowHeight);
    QCOMPARE(view.uniformItemHeight(), kRowHeight);

    // Removing a row and inserting a column take the same mirror path.
    QVERIFY(model.removeRow(0));
    view.flushPendingRelayout();
    QCOMPARE(view.columnWidth(2), 200);
    model.insertColumn(2);
    view.flushPendingRelayout();
    QCOMPARE(view.columnWidth(0), kColumnWidth);
    QCOMPARE(view.columnWidth(3), 200);
    QCOMPARE(view.columnWidth(4), 90);

    // The same rule protects a hidden column's width: QHeaderView reports 0 for a section it
    // does not show, and hiding a column must not rewrite its stored width (docs mention that
    // the width survives a hide/show round trip).
    view.setColumnHidden(3, true);
    view.flushPendingRelayout();
    view.setColumnHidden(3, false);
    view.flushPendingRelayout();
    QCOMPARE(view.columnWidth(3), 200);
}

void TestVirtualTableView::rowInsertKeepsTheExplicitRowHeights()
{
    // The same hole on the vertical axis: the frozen-row strips are pane headers for m_rowHeaders,
    // so a model change must not clamp the explicit row heights to the minimum either.
    QStandardItemModel model(20, 2, this);
    TableTestAdapter adapter(2);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenRows(2);                 // the vertical pane strips
    view.setRowHeight(3, 60);              // the user dragged that boundary
    view.setRowHeight(4, 44);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(3), 60);

    // Drop above the two resized rows: their heights have to move with them.
    // The insert a *drop* performs goes through QStandardItemModel::dropMimeData(), which reports
    // it as a layout change - the kernel rebuilds every derived row size then, so the heights the
    // user set are only preserved because they are re-applied to the rows they belong to.
    QSignalSpy layoutSpy(&model, &QAbstractItemModel::layoutChanged);
    QVERIFY(dropARowInto(view, QPoint(kColumnWidth / 2, 2)));
    QVERIFY(layoutSpy.count() > 0);
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    QCOMPARE(view.rowHeight(4), 60);       // the heights followed their rows
    QCOMPARE(view.rowHeight(5), 44);
    QCOMPARE(view.rowHeight(2), kRowHeight);
}

void TestVirtualTableView::draggingAHeaderSectionSwapsTheOrderInBothAxes()
{
    // End to end through the table (not just the renderer): grabbing a header section and
    // dropping it further along must really reorder - columns through HeaderGeometry, rows
    // through the model's moveRows().
    ReorderableModel model(40, 6, this);
    TableTestAdapter adapter(6);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // -- rows: drag the row-number strip -----------------------------------------------
    QSignalSpy moveSpy(&view, &VirtualTableView::rowMoveRequested);
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip != nullptr);
    QWidget *stripWidget = strip->headerWidget();
    const int rowGrabY = view.visualRect(model.index(1, 0)).center().y();
    sendMouseTo(stripWidget, QEvent::MouseButtonPress, QPoint(4, rowGrabY), Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseMove, QPoint(4, rowGrabY + 2), Qt::NoButton,
                Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseMove, QPoint(4, rowGrabY + 3 * kRowHeight), Qt::NoButton,
                Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseButtonRelease, QPoint(4, rowGrabY + 3 * kRowHeight),
                Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QCOMPARE(moveSpy.count(), 1);   // the strip asked for the move ...
    QCOMPARE(model.index(3, 0).data().toString(), QStringLiteral("r1c0"));
    // the strip shows the moved row label, not the number of the new slot
    auto *movedSection = dynamic_cast<LabelHeaderSection *>(strip->sectionWidget(3));
    QVERIFY(movedSection != nullptr);
    QCOMPARE(movedSection->text(), QStringLiteral("R1"));

    // -- columns: drag the (default widget) header -------------------------------------
    auto *header = dynamic_cast<VirtualHeaderView *>(view.horizontalHeader());
    QVERIFY(header != nullptr);
    QWidget *headerWidget = header->headerWidget();
    const int columnGrabX = view.columnGeometry(1).viewportX + view.columnWidth(1) / 2;
    // Past the *centre* of column 3: on the exact centre the insertion slot is the one
    // before it (the renderer counts sections whose centre lies strictly left).
    const int columnDropX = view.columnGeometry(3).viewportX + view.columnWidth(3) / 2 + 20;
    const int headerY = qMax(2, headerWidget->height() / 2);
    sendMouseTo(headerWidget, QEvent::MouseButtonPress, QPoint(columnGrabX, headerY),
                Qt::LeftButton, Qt::LeftButton);
    sendMouseTo(headerWidget, QEvent::MouseMove, QPoint(columnGrabX + 2, headerY), Qt::NoButton,
                Qt::LeftButton);
    sendMouseTo(headerWidget, QEvent::MouseMove, QPoint(columnDropX, headerY), Qt::NoButton,
                Qt::LeftButton);
    sendMouseTo(headerWidget, QEvent::MouseButtonRelease, QPoint(columnDropX, headerY),
                Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QCOMPARE(view.horizontalHeaderGeometry()->logicalIndex(3), 1);
}
void TestVirtualTableView::rowStripDragTakesTheRowsWithIt()
{
    // §8 of the vertical-header decision (row analogue of setColumnFollowsHeaderVisual):
    // while the strip is dragged, the rows are drawn where their numbers are - the dragged
    // row follows the pointer and the rows making room slide along - and the commit puts
    // them back on the committed layout. The committed geometry itself never moves.
    BigTableModel model(40, 3, this);
    TableTestAdapter adapter(3);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(view.rowFollowsHeaderVisual());

    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip != nullptr);
    QWidget *stripWidget = strip->headerWidget();
    const int stripOffset = strip->y() - view.viewport()->y();
    const auto rowWidget = [&view](int row) -> QWidget * {
        for (const MaterializedItem &item : view.materializedItems()) {
            if (item.index.row() == row)
                return item.widget;
        }
        return nullptr;
    };
    const auto sectionY = [&](int row) {
        QWidget *section = strip->sectionWidget(row);
        return section ? section->y() + stripOffset : std::numeric_limits<int>::min();
    };

    QWidget *dragged = rowWidget(1);
    QWidget *neighbour = rowWidget(2);
    QVERIFY(dragged != nullptr);
    QVERIFY(neighbour != nullptr);
    const int committedY = view.visualRect(model.index(1, 0)).y();
    const int neighbourY = view.visualRect(model.index(2, 0)).y();
    QCOMPARE(dragged->y(), committedY);
    QCOMPARE(neighbour->y(), neighbourY);

    // Grab row 1 in the middle (away from the row boundary) and pull it three rows down.
    const int grabY = view.visualRect(model.index(1, 0)).center().y();
    sendMouseTo(stripWidget, QEvent::MouseButtonPress, QPoint(4, grabY), Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseMove, QPoint(4, grabY + 2), Qt::NoButton,
                Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseMove, QPoint(4, grabY + 3 * kRowHeight), Qt::NoButton,
                Qt::LeftButton);
    QApplication::processEvents();

    // The dragged row is *drawn* where its number is (not where the commit will put it) ...
    QVERIFY(dragged->y() != committedY);
    QCOMPARE(dragged->y(), sectionY(1));
    // ... the row making room follows its own number ...
    QCOMPARE(neighbour->y(), sectionY(2));
    // ... and its make-room tween finishes on its own (the pointer stands still).
    QTest::qWait(320);
    QCOMPARE(neighbour->y(), sectionY(2));
    QVERIFY(neighbour->y() < neighbourY);
    // ... and the committed layout (visualRect, hit testing, the model) has not moved.
    QCOMPARE(view.visualRect(model.index(1, 0)).y(), committedY);
    QCOMPARE(view.visualRect(model.index(2, 0)).y(), neighbourY);

    // Release: this model has no moveRows(), so the order stays - and the rows come back to
    // the committed layout instead of being left at the preview offset.
    sendMouseTo(stripWidget, QEvent::MouseButtonRelease, QPoint(4, grabY + 3 * kRowHeight),
                Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QCOMPARE(dragged->y(), committedY);
    QCOMPARE(neighbour->y(), neighbourY);

    // Turning the setting off puts a following row back at once.
    view.setRowFollowsHeaderVisual(false);
    QVERIFY(!view.rowFollowsHeaderVisual());
    QCOMPARE(dragged->y(), committedY);
    view.setRowFollowsHeaderVisual(true);
}
void TestVirtualTableView::rowStripDragReportsAMoveTheModelMayRefuse()
{
    // §9 of the vertical-header decision: the strip only *asks* for the row move. A model
    // without moveRows() (the QAbstractItemModel default returns false) refuses it - and
    // then the row order and every section position must be exactly where they were, i.e.
    // no stale preview is left behind.
    BigTableModel model(40, 3, this);   // no moveRows() override: the move is refused
    TableTestAdapter adapter(3);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QSignalSpy moveSpy(&view, &VirtualTableView::rowMoveRequested);
    QVERIFY(moveSpy.isValid());

    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip != nullptr);
    QWidget *stripWidget = strip->headerWidget();
    const int grabY = view.visualRect(model.index(1, 0)).center().y();
    sendMouseTo(stripWidget, QEvent::MouseButtonPress, QPoint(4, grabY), Qt::LeftButton,
                Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseMove, QPoint(4, grabY + 2), Qt::NoButton, Qt::LeftButton);
    sendMouseTo(stripWidget, QEvent::MouseMove, QPoint(4, grabY + 3 * kRowHeight), Qt::NoButton,
                Qt::LeftButton);
    QApplication::processEvents();
    sendMouseTo(stripWidget, QEvent::MouseButtonRelease, QPoint(4, grabY + 3 * kRowHeight),
                Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    // The request was reported for row 1 ...
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.first().at(0).toInt(), 1);
    QVERIFY(moveSpy.first().at(1).toInt() > 1);
    // ... the model refused, so nothing moved ...
    QCOMPARE(view.verticalHeaderGeometry()->visualIndex(0), 0);
    QCOMPARE(view.verticalHeaderGeometry()->visualIndex(1), 1);
    QCOMPARE(model.index(1, 0).row(), 1);
    QVERIFY(!model.moveRows(QModelIndex(), 1, 1, QModelIndex(), 4));   // really refuses
    // ... and the strip is back on the committed rows (no preview left over).
    for (qsizetype row : {qsizetype(0), qsizetype(1), qsizetype(2)}) {
        QWidget *section = strip->sectionWidget(int(row));
        QVERIFY(section != nullptr);
        QCOMPARE(section->y() + strip->y() - view.viewport()->y(),
                 view.visualRect(model.index(int(row), 0)).top());
    }
}
void TestVirtualTableView::tenMillionUniformRowsResizeOneStaysCompact()
{
    // §12 of the vertical-header decision: one row resize in a ten-million-row uniform
    // table must not materialise ten million per-row states - and the row-number strip
    // still mirrors the committed height.
    constexpr int kRows = 10'000'000;
    BigTableModel model(kRows, 4, this);
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    QCOMPARE(view.verticalHeaderGeometry()->sectionCount(), kRows);
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionStateCount(), 0);
    QVERIFY(view.verticalHeaderGeometry()->isUniform());
    // The strip is a widget one (LabelHeaderView(Qt::Vertical)): only the rows its window
    // shows own a widget.
    auto *strip = dynamic_cast<VirtualHeaderView *>(view.verticalHeader());
    QVERIFY(strip != nullptr);
    QVERIFY(strip->materializedSectionCount() < 100);

    view.setRowHeight(1234, kRowHeight * 2);
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    // The committed layout follows the explicit height ...
    QCOMPARE(view.rowHeight(1234), kRowHeight * 2);
    QCOMPARE(view.visualRect(model.index(1234, 0)).height(), kRowHeight * 2);
    // ... the strip mirrors it as one sparse override (the row is off screen, so no widget)
    // ...
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionStateCount(), 1);
    QVERIFY(view.verticalHeaderGeometry()->isUniform());
    QCOMPARE(view.verticalHeaderGeometry()->sectionSize(1234), kRowHeight * 2);
    QCOMPARE(view.verticalHeaderGeometry()->sectionPosition(1235),
             qint64(1234) * kRowHeight + kRowHeight * 2);
    QCOMPARE(view.verticalHeaderGeometry()->totalExtent(),
             qint64(kRows) * kRowHeight + kRowHeight);
    // ... and nothing walked or created ten million rows.
    QVERIFY(view.materializedItemCount() < 40);
    QVERIFY(strip->materializedSectionCount() < 100);

    // Clearing it returns to the fully compact state.
    view.clearRowHeight(1234);
    view.flushPendingRelayout();
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionStateCount(), 0);
    QCOMPARE(view.verticalHeaderGeometry()->sectionSize(1234), kRowHeight);
    QCOMPARE(view.verticalHeaderGeometry()->totalExtent(), qint64(kRows) * kRowHeight);
}
void TestVirtualTableView::switchingTheModelRebindsEveryPaneHeader()
{
    // P1 of the fourth review: setModel() updated the primary horizontal/vertical headers, but
    // an already materialized *derived* renderer kept the old label model. With two models of
    // the same column count nothing else rebuilds those clones, so the frozen pane and the
    // frozen row strips kept drawing A's titles - and, for widget headers, kept listening to
    // A's signals while binding through B's adapter.
    QStandardItemModel modelA(20, 3, this);
    QStandardItemModel modelB(20, 3, this);
    for (int column = 0; column < 3; ++column) {
        modelA.setHeaderData(column, Qt::Horizontal, QStringLiteral("A%1").arg(column));
        modelB.setHeaderData(column, Qt::Horizontal, QStringLiteral("B%1").arg(column));
    }
    for (int row = 0; row < 20; ++row) {
        modelA.setHeaderData(row, Qt::Vertical, QStringLiteral("A row %1").arg(row));
        modelB.setHeaderData(row, Qt::Vertical, QStringLiteral("B row %1").arg(row));
    }

    TableTestAdapter rowAdapter(3);
    ModelLabelHeaderAdapter headerAdapter;
    VirtualTableView view;
    view.setTableAdapter(&rowAdapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&headerAdapter);
    view.setHorizontalHeader(header);
    view.setModel(&modelA);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.setFrozenColumns({0});   // a horizontal pane renderer
    view.setFrozenRows(2);        // frozen row strips
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    const auto paneHeaderClone = [&]() -> VirtualHeaderView * {
        VirtualHeaderView *clone = nullptr;
        for (VirtualHeaderView *candidate : view.findChildren<VirtualHeaderView *>()) {
            // The *column* clone: the frozen row bands are the same renderer class on the
            // other axis and are checked separately below.
            if (candidate != header && candidate->orientation() == Qt::Horizontal)
                clone = candidate;
        }
        return clone;
    };
    const auto sectionText = [&](VirtualHeaderView *pane, int logicalColumn) -> QString {
        QWidget *section = pane ? pane->sectionWidget(logicalColumn) : nullptr;
        auto *label = dynamic_cast<QLabel *>(section);
        return label ? label->text() : QString();
    };

    VirtualHeaderView *paneHeader = paneHeaderClone();
    QVERIFY(paneHeader != nullptr);
    QCOMPARE(paneHeader->labelModel(), &modelA);
    QCOMPARE(sectionText(paneHeader, 0), QStringLiteral("A0"));

    QList<VirtualHeaderView *> strips;
    for (VirtualHeaderView *candidate : view.findChildren<VirtualHeaderView *>()) {
        if (candidate->orientation() == Qt::Vertical && candidate->labelModel() == &modelA)
            strips.append(candidate);
    }
    QVERIFY(!strips.isEmpty());            // the frozen row bands have their own strips

    view.setModel(&modelB);
    view.flushPendingRelayout();
    QCoreApplication::processEvents();

    paneHeader = paneHeaderClone();
    QVERIFY(paneHeader != nullptr);
    QCOMPARE(header->labelModel(), &modelB);
    QCOMPARE(paneHeader->labelModel(), &modelB);
    QCOMPARE(sectionText(paneHeader, 0), QStringLiteral("B0"));
    QCOMPARE(sectionText(header, 1), QStringLiteral("B1"));   // the primary pane too
    for (VirtualHeaderView *strip : strips)
        QCOMPARE(strip->labelModel(), &modelB);
}

void TestVirtualTableView::setAdapterConfiguresTheTableNotJustTheBase()
{
    // P1 of the third review: VirtualTableView inherited setAdapter(WidgetAdapter *),
    // which only stored the *base* adapter - while the recycler factory and every row
    // materialization read m_tableAdapter. That call therefore compiled, reported
    // adapter() != nullptr and materialized nothing at all. The table now hides the
    // inherited overload with its own typed setAdapter(TableWidgetAdapter *), so the
    // call the docs of List/Tree/Table have in common does the right thing here too.
    BigTableModel model(20, 3, this);
    RecordingRowAdapter adapter;
    VirtualTableView view;
    view.setAdapter(&adapter);              // TableWidgetAdapter * -> the typed overload
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    QCOMPARE(view.tableAdapter(), &adapter);   // the table adapter really is set...
    QCOMPARE(view.adapter(), &adapter);        // ...and the kernel sees the same object
    QVERIFY(adapter.bound > 0);                // ...so rows are materialized
    QVERIFY(view.materializedItemCount() > 0);
}

void TestVirtualTableView::columnResizeTouchesMaterializedRowsOnly()
{
    const int bindsBefore = m_adapter->bound;
    const int createdBefore = m_adapter->created;
    const qsizetype materializedBefore = m_view->materializedItemCount();

    m_view->setColumnWidth(2, 240);

    QCOMPARE(m_adapter->bound, bindsBefore);                       // no rebind
    QCOMPARE(m_view->materializedItemCount(), materializedBefore); // no rematerialize
    QCOMPARE(m_adapter->created, createdBefore);                   // no new widgets

    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 1));
    QVERIFY(rowWidget != nullptr);
    QCOMPARE(rowWidget->host(2)->width(), 240);
    QCOMPARE(m_view->columnGeometry(2).width, 240);
}

void TestVirtualTableView::geometryIsTheSingleAuthority()
{
    // Mutating only the HeaderGeometry must be visible in the body: the view
    // keeps no second authoritative column state (§45.10).
    HeaderGeometry *geometry = m_view->horizontalHeaderGeometry();
    geometry->resizeSection(1, 260);
    QCOMPARE(m_view->columnGeometry(1).width, 260);
    QCOMPARE(m_view->columnWidth(1), 260);

    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);
    QCOMPARE(rowWidget->host(1)->width(), 260);

    geometry->setSectionHidden(1, true);
    QVERIFY(m_view->isColumnHidden(1));
    QVERIFY(!rowWidget->host(1)->isVisible());
    geometry->setSectionHidden(1, false);
    QVERIFY(rowWidget->host(1)->isVisible());
}

void TestVirtualTableView::columnMoveFollowsTheGeometry()
{
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);
    const int xOfColumnThreeBefore = rowWidget->host(3)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    QVERIFY(xOfColumnThreeBefore > 0);

    m_view->moveColumn(0, 3); // move logical 0 to the visual position of logical 3

    const int xOfColumnThreeAfter = rowWidget->host(3)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    const int xOfColumnZero = rowWidget->host(0)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    QVERIFY(xOfColumnThreeAfter != xOfColumnThreeBefore);
    QVERIFY(xOfColumnZero > xOfColumnThreeAfter);
    QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), 1);
}


void TestVirtualTableView::hiddenColumnHidesHost()
{
    auto *rowWidget = static_cast<TableRowWidget *>(rowWidgetFor(*m_view, 0));
    QVERIFY(rowWidget != nullptr);
    const int xOfColumnTwo = rowWidget->host(2)->mapTo(m_view->viewport(), QPoint(0, 0)).x();

    m_view->setColumnHidden(1, true);
    QVERIFY(!rowWidget->host(1)->isVisible());
    QCOMPARE(m_view->columnWidth(1), 0);
    QVERIFY(rowWidget->host(2)->mapTo(m_view->viewport(), QPoint(0, 0)).x() < xOfColumnTwo);

    m_view->setColumnHidden(1, false);
    QVERIFY(rowWidget->host(1)->isVisible());
    QCOMPARE(rowWidget->host(2)->mapTo(m_view->viewport(), QPoint(0, 0)).x(), xOfColumnTwo);
}


void TestVirtualTableView::headerStateRoundTrip()
{
    m_view->setColumnWidth(1, 150);
    m_view->setColumnHidden(4, true);
    m_view->moveColumn(0, 2);
    const QByteArray state = m_view->saveHeaderState();
    QVERIFY(!state.isEmpty());
    const qint64 extent = m_view->horizontalContentExtent();

    m_view->setColumnWidth(1, 400);
    m_view->setColumnHidden(4, false);
    m_view->moveColumn(2, 0);
    QVERIFY(m_view->horizontalContentExtent() != extent);

    QVERIFY(m_view->restoreHeaderState(state));
    QCOMPARE(m_view->columnWidth(1), 150);
    QVERIFY(m_view->isColumnHidden(4));
    QCOMPARE(m_view->horizontalContentExtent(), extent);
    QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), 1);
}

void TestVirtualTableView::hugeModelColumnResizeDoesNotWalkRows()
{
    BigTableModel model(1000000, 5, this);
    TableTestAdapter adapter(5);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    const int bindsBefore = adapter.bound;
    const int createdBefore = adapter.created;
    view.setColumnWidth(3, 321);

    QCOMPARE(adapter.bound, bindsBefore);
    QCOMPARE(adapter.created, createdBefore);
    QVERIFY(view.materializedItemCount() < 40);
    QCOMPARE(view.columnGeometry(3).width, 321);
    // Uniform row heights: the row geometry carries the *count* (so a row-number strip can
    // materialize its window) but stores nothing per row - that decoupling is what keeps a
    // ten-million-row table cheap (HeaderGeometry's uniform representation).
    QCOMPARE(view.verticalHeaderGeometry()->sectionCount(), model.rowCount());
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionStateCount(), 0);
    QVERIFY(view.verticalHeaderGeometry()->isUniform());
}

void TestVirtualTableView::columnCountFollowsTheModel()
{
    QCOMPARE(m_view->horizontalHeaderGeometry()->sectionCount(), 6);
    m_model->insertColumn(6);
    QCOMPARE(m_view->columnCount(), 7);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sectionCount(), 7);

    m_model->removeColumn(0);
    QCOMPARE(m_view->columnCount(), 6);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sectionCount(), 6);
}

void TestVirtualTableView::sortingRequestsAndAppliesModelSort()
{
    QSignalSpy sortSpy(m_view, &VirtualTableView::sortIndicatorRequested);
    m_view->setSortingEnabled(true);

    // A header click writes the sort indicator into the geometry; the view
    // reacts by sorting the model (§33).
    m_view->horizontalHeaderGeometry()->setSortIndicator(1, Qt::AscendingOrder);
    QCOMPARE(sortSpy.count(), 1);
    QCOMPARE(sortSpy.at(0).at(0).toInt(), 1);

    QStringList values;
    for (int row = 0; row < m_model->rowCount(); ++row)
        values << m_model->data(m_model->index(row, 1)).toString();
    std::sort(values.begin(), values.end());
    QCOMPARE(m_model->data(m_model->index(0, 1)).toString(), values.first());

    m_view->sortByColumn(1, Qt::DescendingOrder);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), 1);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorOrder(), Qt::DescendingOrder);
    QCOMPARE(m_model->data(m_model->index(0, 1)).toString(), values.last());

    m_view->setSortingEnabled(false);
    QCOMPARE(m_view->horizontalHeaderGeometry()->sortIndicatorSection(), -1);
}

void TestVirtualTableView::verticalHeaderMirrorsRowHeights()
{
    auto *model = buildModel(20, 4, this);
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    HeaderGeometry *rowHeaders = view.verticalHeaderGeometry();
    QCOMPARE(rowHeaders->sectionCount(), 20);
    for (qsizetype row = 0; row < 20; ++row)
        QCOMPARE(rowHeaders->storedSectionSize(int(row)), view.rowHeight(row));

    // A user resize of the row-number strip becomes an explicit row height.
    rowHeaders->resizeSection(3, 64);
    QCOMPARE(view.rowHeight(3), 64);
    QVERIFY(view.hasExplicitRowHeight(3));
    QCOMPARE(rowHeaders->storedSectionSize(3), 64);
}

void TestVirtualTableView::verticalHeaderFollowsVerticalScroll()
{
    // The row-number strip is a widget strip (LabelHeaderView(Qt::Vertical)): the same
    // renderer as the column header, other axis.
    auto *header = dynamic_cast<VirtualHeaderView *>(m_view->verticalHeader());
    QVERIFY(header != nullptr);
    QVERIFY(header->headerWidget()->isVisible());

    for (int step = 1; step <= 4; ++step) {
        m_view->verticalScrollBar()->setValue(step * kRowHeight * 3);
        m_view->flushPendingRelayout();

        // The row-number strip consumes the same vertical offset as the body.
        QCOMPARE(m_view->verticalHeaderGeometry()->viewportOffset(), m_view->verticalOffset());

        // Every visible row number sits exactly at its row's y position.
        const VisibleRange rows = m_view->visibleRows();
        QVERIFY(rows.isValid());
        for (qsizetype row = rows.first; row <= rows.last; ++row) {
            const QRect rect = m_view->visualRect(m_model->index(int(row), 0));
            QWidget *section = header->sectionWidget(int(row));
            QVERIFY(section != nullptr);
            QCOMPARE(section->y() + header->y() - m_view->viewport()->y(), rect.top());
            QCOMPARE(section->height(), rect.height());
        }
    }
}

void TestVirtualTableView::verticalHeaderDragChangesRowHeight()
{
    auto *header = dynamic_cast<VirtualHeaderView *>(m_view->verticalHeader());
    QVERIFY(header != nullptr);
    QCOMPARE(m_view->rowHeight(2), kRowHeight);
    QCOMPARE(m_view->itemHeightMode(), VirtualItemView::ItemHeightMode::Uniform);

    // A user drag on the row boundary of the strip must become an explicit row height (and
    // switch the table to variable heights) instead of being swallowed.
    const int boundaryY = m_view->visualRect(m_model->index(2, 0)).bottom();
    sendMouseTo(header->headerWidget(), QEvent::MouseButtonPress, QPoint(4, boundaryY),
                Qt::LeftButton, Qt::LeftButton);
    sendMouseTo(header->headerWidget(), QEvent::MouseMove, QPoint(4, boundaryY + 34),
                Qt::NoButton, Qt::LeftButton);
    sendMouseTo(header->headerWidget(), QEvent::MouseButtonRelease, QPoint(4, boundaryY + 34),
                Qt::LeftButton, Qt::NoButton);
    m_view->flushPendingRelayout();

    QCOMPARE(m_view->itemHeightMode(), VirtualItemView::ItemHeightMode::Variable);
    QCOMPARE(m_view->rowHeight(2), 64);
    QVERIFY(m_view->hasExplicitRowHeight(2));
    QCOMPARE(m_view->visualRect(m_model->index(2, 0)).height(), 64);
    // Neighbouring rows keep the height they had.
    QCOMPARE(m_view->rowHeight(1), kRowHeight);
    QCOMPARE(m_view->verticalHeaderGeometry()->storedSectionSize(2), 64);
    QCOMPARE(header->sectionWidget(2)->height(), 64);
}

void TestVirtualTableView::rowSizePolicyControlsMeasurement()
{
    auto *model = buildModel(20, 4, this);
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    // ExplicitWins (default): measurement must not override the user's height.
    view.setRowHeight(2, 70);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(2), 70);
    QCOMPARE(view.rowSizePolicy(), VirtualTableView::RowSizePolicy::ExplicitWins);

    // MeasuredWins: the widget size hint (30) wins again.
    view.setRowSizePolicy(VirtualTableView::RowSizePolicy::MeasuredWins);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(2), kRowHeight);

    view.clearRowHeight(2);
    QVERIFY(!view.hasExplicitRowHeight(2));
}

void TestVirtualTableView::rowHeaderMirrorsTheCommittedSizeUnderMeasuredWins()
{
    auto *model = buildModel(20, 4, this);
    TableTestAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    view.flushPendingRelayout();

    // The user resizes row 2: body and row-number strip agree.
    view.setRowHeight(2, 70);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(2), 70);
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionSize(2), 70);

    // Under MeasuredWins the measured height (30) wins over the explicit one: the
    // strip has to follow the body, not keep the stale explicit value.
    view.setRowSizePolicy(VirtualTableView::RowSizePolicy::MeasuredWins);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(2), kRowHeight);
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionSize(2), kRowHeight);

    // Back to ExplicitWins for the clearing part: there the explicit height is the
    // one the body uses, so it is the one that has to be given back.
    view.setRowSizePolicy(VirtualTableView::RowSizePolicy::ExplicitWins);
    view.setRowHeight(3, 90);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(3), 90);
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionSize(3), 90);
    view.clearRowHeight(3);
    view.flushPendingRelayout();
    QVERIFY(!view.hasExplicitRowHeight(3));
    QCOMPARE(view.rowHeight(3), kRowHeight);      // the measured size, right away
    QCOMPARE(view.verticalHeaderGeometry()->storedSectionSize(3), kRowHeight);

    // The same for a row that is not materialized: the estimate comes back.
    const qsizetype offscreen = 18;               // below the materialized window
    view.setRowHeight(offscreen, 55);
    view.flushPendingRelayout();
    QVERIFY(view.widgetForIndex(model->index(int(offscreen), 0)) == nullptr);
    QCOMPARE(view.rowHeight(offscreen), 55);
    view.clearRowHeight(offscreen);
    view.flushPendingRelayout();
    QCOMPARE(view.rowHeight(offscreen), kRowHeight);
}

void TestVirtualTableView::theRowHeaderComesBackWhenTheModelShrinks()
{
    // Above the mirror limit a native strip cannot follow per-row heights, so the view
    // hides it. That is a property of the *model*, not a sticky switch (P2-9): the
    // request is untouched and the strip returns as soon as the reason is gone.
    BigTableModel model(1000001, 3, this);
    TableTestAdapter adapter(3);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setItemHeightMode(VirtualItemView::ItemHeightMode::Variable);
    view.setEstimatedItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);

    g_rowHeaderWarnings = 0;
    QtMessageHandler defaultHandler = qInstallMessageHandler(countRowHeaderWarnings);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    // More relayouts of the same unsupported model must not repeat the message: it
    // follows the transition, not the number of passes.
    view.setEstimatedItemHeight(kRowHeight + 4);
    view.flushPendingRelayout();
    qInstallMessageHandler(defaultHandler);

    QVERIFY(!view.isVerticalHeaderShown());
    QVERIFY(view.isVerticalHeaderVisible()); // the application's request is untouched
    QVERIFY(view.verticalHeader()->headerWidget()->isHidden());
    QCOMPARE(g_rowHeaderWarnings, 1);

    // The model gets small again: the strip comes back by itself.
    g_rowHeaderWarnings = 0;
    defaultHandler = qInstallMessageHandler(countRowHeaderWarnings);
    model.setDataRowCount(5);
    view.flushPendingRelayout();
    qInstallMessageHandler(defaultHandler);
    QVERIFY(view.isVerticalHeaderShown());
    QVERIFY(!view.verticalHeader()->headerWidget()->isHidden());
    QCOMPARE(view.verticalHeader()->headerWidget()->width(), view.verticalHeaderWidth());
    QCOMPARE(g_rowHeaderWarnings, 0);

    // The explicit request keeps working on its own terms ...
    view.setVerticalHeaderVisible(false);
    QVERIFY(!view.isVerticalHeaderShown());
    QVERIFY(view.verticalHeader()->headerWidget()->isHidden());
    view.setVerticalHeaderVisible(true);
    QVERIFY(view.isVerticalHeaderShown());
    QVERIFY(!view.verticalHeader()->headerWidget()->isHidden());

    // ... and a big model is fine when the heights are uniform: the limit only exists
    // for per-row heights.
    model.setDataRowCount(1000001);
    view.setUniformItemHeight(kRowHeight);
    view.flushPendingRelayout();
    QVERIFY(view.isVerticalHeaderShown());
    QVERIFY(!view.verticalHeader()->headerWidget()->isHidden());
}

void TestVirtualTableView::keyboardMovesTheCurrentColumn()
{
    m_view->setCurrentIndex(m_model->index(1, 1));
    QTest::keyClick(m_view, Qt::Key_Right);
    QCOMPARE(m_view->currentIndex().column(), 2);
    QCOMPARE(m_view->currentIndex().row(), 1);
    QTest::keyClick(m_view, Qt::Key_Left);
    QCOMPARE(m_view->currentIndex().column(), 1);
    QTest::keyClick(m_view, Qt::Key_Down);
    QCOMPARE(m_view->currentIndex().row(), 2);
    QCOMPARE(m_view->currentIndex().column(), 1);

    // Hidden columns are skipped by horizontal navigation.
    m_view->setColumnHidden(2, true);
    QTest::keyClick(m_view, Qt::Key_Right);
    QCOMPARE(m_view->currentIndex().column(), 3);
}

void TestVirtualTableView::visibleColumnRangeFollowsOverscanAndOffset()
{
    // 6 columns of 100 px in a ~460 px viewport: 5 visible, plus overscan.
    const QVector<int> tight = m_view->visibleColumnLogicalIndexes();
    QCOMPARE(tight.size(), 5);
    QCOMPARE(tight.first(), 0);

    m_view->setColumnOverscan(2);
    const QVector<int> wide = m_view->visibleColumnLogicalIndexes();
    QVERIFY(wide.size() >= tight.size());
    QCOMPARE(wide.first(), 0);
    QCOMPARE(wide.last(), 5);

    m_view->setColumnOverscan(0);
    m_view->setHorizontalOffset(200);
    const QVector<int> scrolled = m_view->visibleColumnLogicalIndexes();
    QVERIFY(!scrolled.isEmpty());
    QVERIFY(scrolled.first() >= 1);
    const VisibleRange visual = m_view->visibleColumns();
    QVERIFY(visual.isValid());
}

void TestVirtualTableView::frozenColumnsStayWhileTheScrollablePaneScrolls()
{
    // 6 columns of 100 px: freeze the first two (§31).
    m_view->setFrozenColumns(QVector<int>({0, 1}));
    QCOMPARE(m_view->frozenColumns(), QVector<int>({0, 1}));
    QVERIFY(m_view->isColumnFrozen(0));
    QVERIFY(m_view->isColumnFrozen(1));
    QVERIFY(!m_view->isColumnFrozen(2));

    const QVector<TablePane> panes = m_view->panes();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes.at(0).type, TablePane::Type::FrozenLeft);
    QCOMPARE(panes.at(0).viewportRect.x(), 0);
    QCOMPARE(panes.at(0).viewportRect.width(), 2 * kColumnWidth);
    QCOMPARE(panes.at(0).logicalColumns, QVector<int>({0, 1}));
    QCOMPARE(panes.at(1).type, TablePane::Type::Scrollable);
    QCOMPARE(panes.at(1).viewportRect.x(), 2 * kColumnWidth);

    // The scrollable pane starts behind the frozen columns.
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    QCOMPARE(m_view->columnGeometry(1).viewportX, kColumnWidth);
    QCOMPARE(m_view->columnGeometry(2).viewportX, 2 * kColumnWidth);

    m_view->setHorizontalOffset(150);
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    QCOMPARE(m_view->columnGeometry(1).viewportX, kColumnWidth);
    QCOMPARE(m_view->columnGeometry(2).viewportX, 2 * kColumnWidth - 150);

    // The row widget's column hosts follow the same committed geometry, so the
    // frozen host keeps its pane position.
    auto *row = static_cast<TableRowWidget *>(m_view->widgetForIndex(m_model->index(0, 0)));
    QVERIFY(row != nullptr);
    QVERIFY(row->host(0) != nullptr);
    // The scrollable hosts live in the framework's clip container, so their own
    // x is relative to it; the viewport position is what has to match the body.
    const auto hostViewportX = [this, row](int column) {
        return row->host(column)->mapTo(m_view->viewport(), QPoint(0, 0)).x();
    };
    QCOMPARE(hostViewportX(0), 0);
    QCOMPARE(hostViewportX(1), kColumnWidth);
    QCOMPARE(hostViewportX(2), 2 * kColumnWidth - 150);
    // Frozen columns stay visible; a scrollable column that is completely under
    // the frozen pane is hidden, a fully scrollable one is not.
    QVERIFY(row->host(0)->isVisible());
    QVERIFY(row->host(1)->isVisible());
    QVERIFY(!row->host(2)->isVisible());
    QVERIFY(row->host(4)->isVisible());
    QVERIFY(row->host(0)->parentWidget() == row);
    QVERIFY(row->host(2)->parentWidget() != row); // inside the clip container

    // The framework clips the scrollable columns with a container instead of
    // repainting them, so a row widget keeps its own background and nothing else
    // has to change: no masks, no background fills.
    QVERIFY(row->host(0)->mask().isEmpty());
    QVERIFY(row->host(2)->mask().isEmpty());
    QWidget *clipHost = row->host(2)->parentWidget();
    QVERIFY(clipHost != nullptr && clipHost != row);
    QCOMPARE(clipHost->geometry().x(), 2 * kColumnWidth);
    QCOMPARE(clipHost->geometry().width(), m_view->viewport()->width() - 2 * kColumnWidth);
    // Hidden/fully scrolled-out scrollable columns are hidden, not repainted.
    QCOMPARE(row->host(3)->mapTo(m_view->viewport(), QPoint(0, 0)).x(), 3 * kColumnWidth - 150);
}

void TestVirtualTableView::frozenPanesDoNotAddScrollSpace()
{
    const int viewportWidth = m_view->viewport()->width();
    const qint64 plain = m_view->maximumHorizontalOffset();
    QCOMPARE(plain, qMax<qint64>(0, 6 * kColumnWidth - viewportWidth));

    // Freezing redistributes the viewport: the scrollable content shrinks by
    // exactly the frozen width, so the range stays the same (no phantom scroll
    // space), only the scrollable pane gets narrower.
    m_view->setFrozenColumns(QVector<int>({0}));
    QCOMPARE(m_view->panes().at(1).viewportRect.width(), viewportWidth - kColumnWidth);
    QCOMPARE(m_view->maximumHorizontalOffset(), plain);

    m_view->setHorizontalOffset(plain);
    QCOMPARE(m_view->horizontalOffset(), plain);
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    // At the end of the range the last scrollable column ends exactly at the
    // right edge of the scrollable pane.
    const ColumnGeometry last = m_view->columnGeometry(5);
    QCOMPARE(last.viewportX + last.width, viewportWidth);

    // A frozen right pane stops the scrollable pane before it.
    m_view->setFrozenRightColumns(QVector<int>({5}));
    QCOMPARE(m_view->panes().at(1).viewportRect.width(), viewportWidth - 2 * kColumnWidth);
    QCOMPARE(m_view->maximumHorizontalOffset(), plain);
    m_view->setHorizontalOffset(plain);
    QCOMPARE(m_view->columnGeometry(5).viewportX, viewportWidth - kColumnWidth);
    const ColumnGeometry previous = m_view->columnGeometry(4);
    QCOMPARE(previous.viewportX + previous.width, viewportWidth - kColumnWidth);

    m_view->clearFrozenColumns();
    QVERIFY(m_view->frozenColumns().isEmpty());
    QVERIFY(m_view->frozenRightColumns().isEmpty());
    QCOMPARE(m_view->panes().size(), 1);
    QCOMPARE(m_view->panes().at(0).type, TablePane::Type::Scrollable);
}

void TestVirtualTableView::frozenColumnsFollowTheSingleGeometry()
{
    m_view->setFrozenColumns(QVector<int>({0}));
    const int scrollableBefore = m_view->columnGeometry(1).viewportX;
    QCOMPARE(scrollableBefore, kColumnWidth);

    // Resizing a frozen column grows the pane: there is no width copy, the
    // scrollable pane simply starts further right.
    m_view->setColumnWidth(0, kColumnWidth + 40);
    QCOMPARE(m_view->panes().at(0).viewportRect.width(), kColumnWidth + 40);
    QCOMPARE(m_view->columnGeometry(1).viewportX, scrollableBefore + 40);
    QCOMPARE(m_view->columnGeometry(0).width, kColumnWidth + 40);

    // A hidden frozen column leaves the pane, the others close the gap.
    m_view->setColumnHidden(0, true);
    QCOMPARE(m_view->panes().size(), 1);
    QCOMPARE(m_view->panes().at(0).type, TablePane::Type::Scrollable);
    QCOMPARE(m_view->columnGeometry(1).viewportX, 0);
    QVERIFY(!m_view->isColumnFrozen(0));

    m_view->setColumnHidden(0, false);
    QCOMPARE(m_view->panes().size(), 2);
    QCOMPARE(m_view->panes().at(0).viewportRect.width(), kColumnWidth + 40);
}

void TestVirtualTableView::frozenRightPaneIsPinnedToTheRightEdge()
{
    m_view->setFrozenRightColumns(QVector<int>({5}));
    const int viewportWidth = m_view->viewport()->width();

    const QVector<TablePane> panes = m_view->panes();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes.at(0).type, TablePane::Type::Scrollable);
    QCOMPARE(panes.at(1).type, TablePane::Type::FrozenRight);
    QCOMPARE(panes.at(1).logicalColumns, QVector<int>({5}));
    QCOMPARE(panes.at(1).viewportRect.x() + panes.at(1).viewportRect.width(), viewportWidth);
    QCOMPARE(m_view->columnGeometry(5).viewportX, viewportWidth - kColumnWidth);

    // Even at the end of the scroll range the frozen right column stays visible.
    m_view->setHorizontalOffset(m_view->maximumHorizontalOffset());
    QCOMPARE(m_view->columnGeometry(5).viewportX, viewportWidth - kColumnWidth);
    QVERIFY(m_view->columnGeometry(4).viewportX < viewportWidth - kColumnWidth);
}


void TestVirtualTableView::frozenPaneIsUnaffectedByScrolling()
{
    // The rendered frozen pane must not depend on the horizontal offset: if the
    // scrollable columns bleed through (or the frozen columns move), the two
    // renders differ.
    // A translucent table background must not defeat the cover (the Windows 11
    // style of Qt 6.8 hands out a translucent QPalette::Base).
    QPalette translucent = m_view->palette();
    translucent.setColor(QPalette::Base, QColor(255, 255, 255, 180));
    m_view->setPalette(translucent);

    m_view->setFrozenColumns(QVector<int>({0, 1}));
    const auto renderPane = [this](qsizetype offset) {
        m_view->setHorizontalOffset(offset);
        m_view->flushPendingRelayout();
        QApplication::processEvents();
        const QImage full = m_view->viewport()->grab().toImage();
        return full.copy(QRect(0, 0, 2 * kColumnWidth, full.height()));
    };

    const QImage first = renderPane(150);
    const QImage second = renderPane(157);
    QCOMPARE(first.size(), second.size());

    int differentPixels = 0;
    QRect diffRect;
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            if (first.pixel(x, y) == second.pixel(x, y))
                continue;
            ++differentPixels;
            diffRect = diffRect.isNull() ? QRect(x, y, 1, 1) : diffRect.united(QRect(x, y, 1, 1));
        }
    }
    QVERIFY2(differentPixels == 0,
             qPrintable(QStringLiteral("frozen pane changed while scrolling: %1 pixels, first diff at (%2,%3)")
                            .arg(differentPixels)
                            .arg(diffRect.x())
                            .arg(diffRect.y())));
}

void TestVirtualTableView::frozenPaneKeepsTheRowBackground()
{
    // The row widget paints its own background: the frozen pane must show it
    // unchanged (no base coloured fill on top) while the scrollable columns stay
    // clipped away.
    auto *model = buildModel(20, 4, this);
    PaintedRowAdapter adapter(4);
    VirtualTableView view;
    view.setTableAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    view.setFrozenColumns(QVector<int>({0}));
    view.setHorizontalOffset(150); // column 2 sits under the frozen pane
    view.flushPendingRelayout();
    QApplication::processEvents();

    const QImage image = view.viewport()->grab().toImage();
    int rowBackground = 0;
    int leaked = 0;
    int frozenContent = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < kColumnWidth; ++x) {
            const QColor pixel(image.pixel(x, y));
            if (pixel == PaintedRowWidget::background())
                ++rowBackground;
            if (pixel == paneProbeColor(0))
                ++frozenContent;
            for (int column = 1; column < 4; ++column) {
                if (pixel == paneProbeColor(column))
                    ++leaked;
            }
        }
    }
    QVERIFY2(rowBackground > 0, "the row background was painted over in the frozen pane");
    // The frozen column itself is painted: it is not clipped away.
    QVERIFY2(frozenContent > 0, "the frozen column disappeared");
    QCOMPARE(leaked, 0);
}
void TestVirtualTableView::frozenPanesDrawBodySeparatorLines()
{
    const auto lines = [this]() {
        return m_view->findChildren<QWidget *>(QStringLiteral("vivPaneSeparatorLine"));
    };

    // Nothing frozen: no line at all, the body is untouched.
    QCOMPARE(lines().size(), 0);

    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->flushPendingRelayout();
    QApplication::processEvents();

    QCOMPARE(lines().size(), 1);
    const int viewportX = m_view->viewport()->geometry().x();
    const int viewportY = m_view->viewport()->geometry().y();
    // The band lies inside the frozen pane and covers header + body (the line is
    // a child of the view, so the header widgets cannot hide it).
    QCOMPARE(lines().first()->geometry().x(), viewportX + 2 * kColumnWidth - 1);
    QCOMPARE(lines().first()->width(), 1);
    QCOMPARE(lines().first()->height(), m_view->headerHeight() + m_view->viewport()->height());
    QVERIFY(lines().first()->isVisible());

    // The body line uses the colour the style paints section separators with, so
    // it matches the lines between the other columns.
    const QImage image = m_view->grab().toImage();
    const QColor separator = VirtualTableView::sectionSeparatorColor(m_view);
    QCOMPARE(image.pixelColor(viewportX + 2 * kColumnWidth - 1, viewportY + 20), separator);

    // A frozen right pane adds the line of the other boundary.
    m_view->setFrozenRightColumns(QVector<int>({5}));
    m_view->flushPendingRelayout();
    QCOMPARE(lines().size(), 2);
    QCOMPARE(lines().at(1)->geometry().x(),
             viewportX + m_view->viewport()->width() - kColumnWidth);

    m_view->clearFrozenColumns();
    m_view->flushPendingRelayout();
    QCOMPARE(lines().size(), 0);
}

void TestVirtualTableView::paneSeparatorStyleIsCustomizable()
{
    m_view->setFrozenColumns(QVector<int>({0}));
    m_view->flushPendingRelayout();
    QApplication::processEvents();
    const int boundary = kColumnWidth;
    const int viewportY = m_view->viewport()->geometry().y();

    // Default: 1 px, style coloured.
    QCOMPARE(m_view->paneSeparatorStyle().width, 1);
    QVERIFY(!m_view->paneSeparatorStyle().color.isValid());

    // A custom colour and width apply to the header line and the body line.
    PaneSeparatorStyle custom;
    custom.width = 3;
    custom.color = QColor(200, 0, 0);
    m_view->setPaneSeparatorStyle(custom);
    m_view->flushPendingRelayout();
    QApplication::processEvents();
    QCOMPARE(m_view->paneSeparatorStyle().color, QColor(200, 0, 0));

    const QImage image = m_view->grab().toImage();
    const int viewportX = m_view->viewport()->geometry().x();
    // The band sits inside the frozen pane, continuous from header to body.
    for (int dx = 1; dx <= 3; ++dx) {
        QCOMPARE(image.pixelColor(viewportX + boundary - dx, viewportY + 20), QColor(200, 0, 0));
        QCOMPARE(image.pixelColor(viewportX + boundary - dx, 8), QColor(200, 0, 0));
    }
    QCOMPARE(image.pixelColor(viewportX + boundary, viewportY + 20), QColor(Qt::white));

    // Width 0 hides the boundary line (header and body).
    PaneSeparatorStyle hidden;
    hidden.width = 0;
    m_view->setPaneSeparatorStyle(hidden);
    m_view->flushPendingRelayout();
    const QImage without = m_view->grab().toImage();
    QCOMPARE(without.pixelColor(viewportX + boundary - 1, viewportY + 20), QColor(Qt::white));
    QVERIFY(without.pixelColor(viewportX + boundary - 1, 8) != QColor(200, 0, 0));
}

void TestVirtualTableView::headerStateRestoresFrozenColumns()
{
    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->setFrozenRightColumns(QVector<int>({4}));
    m_view->setColumnWidth(1, kColumnWidth + 20);
    const QByteArray state = m_view->saveHeaderState();

    // Change everything the state covers.
    m_view->clearFrozenColumns();
    m_view->moveColumn(2, 0);
    m_view->setColumnWidth(1, kColumnWidth);
    QVERIFY(m_view->frozenColumns().isEmpty());

    QVERIFY(m_view->restoreHeaderState(state));
    QCOMPARE(m_view->frozenColumns(), QVector<int>({0, 1}));
    QCOMPARE(m_view->frozenRightColumns(), QVector<int>({4}));
    QCOMPARE(m_view->panes().size(), 3);
    QCOMPARE(m_view->columnWidth(1), kColumnWidth + 20);
    QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), 0);
    QCOMPARE(m_view->panes().at(0).viewportRect.width(), 2 * kColumnWidth + 20);

    // A bare HeaderGeometry state (the format before the pane sets existed)
    // still restores the columns and leaves the frozen sets alone.
    const QByteArray legacy = m_view->horizontalHeaderGeometry()->saveState();
    m_view->setFrozenColumns(QVector<int>({2}));
    m_view->moveColumn(0, 3);
    QVERIFY(m_view->restoreHeaderState(legacy));
    QCOMPARE(m_view->frozenColumns(), QVector<int>({2}));
    QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), 0);
}

void TestVirtualTableView::aBrokenStateLeavesTheViewUntouched()
{
    // The state is parsed and validated as a whole before anything is applied (P2-3):
    // a corrupt tail used to return false *after* the column geometry had already been
    // replaced, so a failed restore left the view half restored.
    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->setFrozenRightColumns(QVector<int>({4}));
    m_view->moveColumn(2, 0);
    m_view->setColumnWidth(1, kColumnWidth + 20);
    m_view->setFrozenRows(2);
    const QByteArray good = m_view->saveHeaderState();
    QVERIFY(m_view->restoreHeaderState(good));

    // Move to a state that differs from the saved one in every part the state covers,
    // so "nothing changed" is observable.
    m_view->clearFrozenColumns();
    m_view->moveColumn(0, 4);
    m_view->setColumnWidth(1, kColumnWidth);
    m_view->setFrozenRows(0);
    m_view->flushPendingRelayout();
    const int widthBefore = m_view->columnWidth(1);
    const int visual0Before = m_view->horizontalHeaderGeometry()->logicalIndex(0);
    const int visual2Before = m_view->horizontalHeaderGeometry()->logicalIndex(2);
    const int frozenBefore = m_view->frozenColumns().size();
    const int frozenRowsBefore = m_view->frozenRows();
    const auto checkUnchanged = [&]() {
        QCOMPARE(m_view->columnWidth(1), widthBefore);
        QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(0), visual0Before);
        QCOMPARE(m_view->horizontalHeaderGeometry()->logicalIndex(2), visual2Before);
        QCOMPARE(m_view->frozenColumns().size(), frozenBefore);
        QCOMPARE(m_view->frozenRows(), frozenRowsBefore);
    };

    // (a) A truncated tail: the frozen row counts are missing.
    const QByteArray truncated = good.left(good.size() - 4);
    QVERIFY(!m_view->restoreHeaderState(truncated));
    checkUnchanged();

    // (b) A negative count where the frozen-left set begins.
    QDataStream header(good);
    header.setVersion(QDataStream::Qt_5_15);
    quint32 magic = 0;
    quint32 version = 0;
    quint32 columnStateSize = 0;
    header >> magic >> version >> columnStateSize;
    const int setOffset = 4 + 4 + 4 + int(columnStateSize);
    QByteArray negativeCount = good;
    for (int byte = 0; byte < 4; ++byte)
        negativeCount[setOffset + byte] = char(0xFF); // qint32(-1)
    QVERIFY(!m_view->restoreHeaderState(negativeCount));
    checkUnchanged();

    // (c) A column state size that does not fit into int: reading it would allocate a
    // negative-length buffer, so it is rejected before the cast.
    QByteArray hugeSize = good;
    for (int byte = 8; byte < 12; ++byte)
        hugeSize[byte] = char(0xFF); // quint32(0xFFFFFFFF)
    QVERIFY(!m_view->restoreHeaderState(hugeSize));
    checkUnchanged();

    // The intact state still restores everything (the guards do not reject the
    // format itself).
    QVERIFY(m_view->restoreHeaderState(good));
    QCOMPARE(m_view->frozenColumns(), QVector<int>({0, 1}));
    QCOMPARE(m_view->frozenRows(), 2);
    QCOMPARE(m_view->columnWidth(1), kColumnWidth + 20);
}

void TestVirtualTableView::columnsShareTheLeftoverWidthByStretchFactor()
{
    // Columns 2/3/4 split the leftover 1 : 2 : 1, the ones with a factor of 0 keep their
    // own width - header and body read the same HeaderGeometry, so they cannot disagree.
    m_view->setColumnWidth(0, 80);
    m_view->setColumnWidth(1, 60);
    m_view->setColumnStretchFactor(2, 1.0);
    m_view->setColumnStretchFactor(3, 2.0);
    m_view->setColumnStretchFactor(4, 1.0);
    QCOMPARE(m_view->columnStretchFactor(3), 2.0);
    QCOMPARE(m_view->columnStretchFactor(5), 0.0);
    QApplication::processEvents();

    const int viewportWidth = m_view->viewport()->width();
    const int fixed = 80 + 60 + m_view->columnWidth(5);
    const int leftover = viewportWidth - fixed;
    QVERIFY(leftover > 0);
    const int quarter = leftover / 4;
    QCOMPARE(m_view->columnWidth(0), 80);
    QCOMPARE(m_view->columnWidth(1), 60);
    QCOMPARE(m_view->columnWidth(2), quarter);
    QCOMPARE(m_view->columnWidth(3), leftover / 2);
    // The last participant absorbs the rounding, so the three shares add up exactly.
    QCOMPARE(m_view->columnWidth(4), leftover - quarter - leftover / 2);
    QCOMPARE(m_view->horizontalContentExtent(), qint64(viewportWidth));
    QCOMPARE(m_view->maximumHorizontalOffset(), qint64(0));

    // The header section and the body column agree pixel for pixel.
    auto *header = dynamic_cast<VirtualHeaderView *>(m_view->horizontalHeader());
    QVERIFY(header != nullptr);
    const int thirdX = sectionViewportX(header, 3);
    const int secondX = sectionViewportX(header, 2);
    QVERIFY(thirdX != std::numeric_limits<int>::min());
    QVERIFY(secondX != std::numeric_limits<int>::min());
    QCOMPARE(thirdX - secondX, m_view->columnWidth(2));
    QCOMPARE(m_view->columnGeometry(3).viewportX, thirdX);
    // The last visible column ends exactly at the viewport edge: the stretched columns
    // fill what the fixed ones leave, no gap and no overhang.
    const ColumnGeometry last = m_view->columnGeometry(5);
    QCOMPARE(last.viewportX + last.width, viewportWidth);

    // A wider view moves the target: the same ratios, a bigger leftover.
    showView(m_view, QSize(kViewWidth + 160, kViewHeight));
    QApplication::processEvents();
    const int widerLeftover = m_view->viewport()->width() - fixed;
    QCOMPARE(m_view->columnWidth(3), widerLeftover / 2);
    QCOMPARE(m_view->horizontalContentExtent(), qint64(m_view->viewport()->width()));
    QCOMPARE(m_view->maximumHorizontalOffset(), qint64(0));

    // Dragging a stretching column's edge fixes it (QHeaderView's Stretch -> Interactive):
    // from then on it keeps the dragged width and the others share what is left.
    m_view->setColumnWidth(3, 120);
    QCOMPARE(m_view->columnStretchFactor(3), 0.0);
    const int afterDrag = m_view->viewport()->width() - (80 + 60 + 120 + m_view->columnWidth(5));
    QCOMPARE(m_view->columnWidth(2) + m_view->columnWidth(4), afterDrag);
    QCOMPARE(m_view->horizontalContentExtent(), qint64(m_view->viewport()->width()));

    // Frozen columns are part of the same geometry: they keep their width and the
    // scrollable pane gets exactly what the stretched columns add up to.
    m_view->setFrozenColumns(QVector<int>({0}));
    QApplication::processEvents();
    QCOMPARE(m_view->columnWidth(0), 80);
    QCOMPARE(m_view->columnGeometry(0).viewportX, 0);
    const QVector<TablePane> panes = m_view->panes();
    int paneWidths = 0;
    for (const TablePane &pane : panes)
        paneWidths += pane.viewportRect.width();
    QCOMPARE(paneWidths, m_view->viewport()->width());
    QCOMPARE(panes.first().viewportRect.width(), 80);
    QCOMPARE(m_view->horizontalContentExtent(), qint64(m_view->viewport()->width()));
}

QTEST_MAIN(TestVirtualTableView)

#include "tst_virtualtableview.moc"
