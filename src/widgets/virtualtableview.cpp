#include <virtualitemviews/virtualtableview.h>

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QWheelEvent>
#include <QDebug>

#include <limits>

namespace viv {

namespace {
/// Above this row count the native vertical header cannot mirror per-row heights
/// cheaply (QHeaderView keeps an O(rows) position cache plus a Section per row
/// here), so per-row mirroring is disabled; the widget header (v0.5) removes the
/// limit. One million rows cost about 8 MB of mirror state.
constexpr qsizetype kRowHeaderMirrorLimit = 1000000;
} // namespace

VirtualTableView::VirtualTableView(QWidget *parent)
    : VirtualItemView(parent)
{
    // Tables select whole rows by default (§45: row selection).
    setSelectionBehavior(SelectionBehavior::SelectRows);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_columns = new HeaderGeometry(Qt::Horizontal, this);
    m_rowHeaders = new HeaderGeometry(Qt::Vertical, this);
    m_rowLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_rowLayout, true);

    // The recycler creates both row widgets and cell widgets, so the table owns
    // the factory and dispatches on the current materialization mode.
    recycler()->setFactory([this](WidgetType type, QWidget *parent) -> QWidget * {
        if (m_materializationMode == MaterializationMode::CellWidgets && m_cellAdapter)
            return m_cellAdapter->createCellWidget(type, parent);
        return m_tableAdapter ? m_tableAdapter->createWidget(type, parent) : nullptr;
    });

    ensureHeaders();

    connect(m_columns, &HeaderGeometry::geometryChanged, this,
            &VirtualTableView::onHeaderGeometryChanged);
    connect(m_rowHeaders, &HeaderGeometry::sectionResized, this,
            &VirtualTableView::onVerticalSectionResized);
    connect(m_columns, &HeaderGeometry::sortIndicatorChanged, this,
            &VirtualTableView::onSortIndicatorChanged);
}

VirtualTableView::~VirtualTableView()
{
    // Cells are children of the viewport, but they must be unbound while the
    // adapter is still alive.
    recycleAllCells();
    if (m_ownHorizontalHeader)
        delete m_horizontalHeader;
    if (m_ownVerticalHeader)
        delete m_verticalHeader;
    if (m_ownTableAdapter)
        delete m_tableAdapter;
    if (m_ownCellAdapter)
        delete m_cellAdapter;
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

void VirtualTableView::ensureHeaders()
{
    if (!m_horizontalHeader) {
        m_horizontalHeader = new NativeHeaderView(Qt::Horizontal, this);
        m_ownHorizontalHeader = true;
        m_horizontalHeader->setGeometryModel(m_columns);
        m_horizontalHeader->setLabelModel(model());
        m_horizontalHeader->setSortInteractionEnabled(m_sortingEnabled);
    }
    if (!m_verticalHeader) {
        m_verticalHeader = new NativeHeaderView(Qt::Vertical, this);
        m_ownVerticalHeader = true;
        m_verticalHeader->setGeometryModel(m_rowHeaders);
        m_verticalHeader->setLabelModel(model());
    }
    if (auto *native = qobject_cast<QHeaderView *>(m_verticalHeader->headerWidget())) {
        // A user drag on the row-number strip writes an explicit row height.
        connect(native, &QHeaderView::sectionResized, this,
                &VirtualTableView::onVerticalHeaderUserResized, Qt::UniqueConnection);
    }
}

void VirtualTableView::setHorizontalHeader(HeaderViewInterface *header)
{
    if (!header) {
        auto *native = new NativeHeaderView(Qt::Horizontal, this);
        native->setGeometryModel(m_columns);
        native->setLabelModel(model());
        header = native;
    }
    if (m_horizontalHeader == header)
        return;
    if (m_ownHorizontalHeader && m_horizontalHeader)
        delete m_horizontalHeader->headerWidget();
    m_horizontalHeader = header;
    m_ownHorizontalHeader = true;
    m_horizontalHeader->setGeometryModel(m_columns);
    m_horizontalHeader->setLabelModel(model());
    layoutHeaderWidgets();
}

void VirtualTableView::setVerticalHeader(HeaderViewInterface *header)
{
    if (!header) {
        auto *native = new NativeHeaderView(Qt::Vertical, this);
        native->setGeometryModel(m_rowHeaders);
        native->setLabelModel(model());
        header = native;
    }
    if (m_verticalHeader == header)
        return;
    if (m_ownVerticalHeader && m_verticalHeader)
        delete m_verticalHeader->headerWidget();
    m_verticalHeader = header;
    m_ownVerticalHeader = true;
    m_verticalHeader->setGeometryModel(m_rowHeaders);
    m_verticalHeader->setLabelModel(model());
    layoutHeaderWidgets();
}

void VirtualTableView::setHorizontalHeaderVisible(bool visible)
{
    if (m_horizontalHeaderVisible == visible)
        return;
    m_horizontalHeaderVisible = visible;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::setVerticalHeaderVisible(bool visible)
{
    if (m_verticalHeaderVisible == visible)
        return;
    m_verticalHeaderVisible = visible;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::setHeaderHeight(int height)
{
    const int clamped = qMax(0, height);
    if (m_headerHeight == clamped)
        return;
    m_headerHeight = clamped;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::setVerticalHeaderWidth(int width)
{
    const int clamped = qMax(0, width);
    if (m_verticalHeaderWidth == clamped)
        return;
    m_verticalHeaderWidth = clamped;
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::layoutHeaderWidgets()
{
    const int headerHeight = (m_horizontalHeaderVisible && m_horizontalHeader) ? m_headerHeight : 0;
    const int rowHeaderWidth = (m_verticalHeaderVisible && m_verticalHeader) ? m_verticalHeaderWidth : 0;

    setViewportMargins(rowHeaderWidth, headerHeight, 0, 0);

    const QRect viewportRect = viewport()->geometry();
    if (m_horizontalHeader) {
        QWidget *widget = m_horizontalHeader->headerWidget();
        widget->setGeometry(viewportRect.x(), viewportRect.y() - headerHeight, viewportRect.width(),
                            headerHeight);
        widget->setVisible(headerHeight > 0);
    }
    if (m_verticalHeader) {
        QWidget *widget = m_verticalHeader->headerWidget();
        widget->setGeometry(viewportRect.x() - rowHeaderWidth, viewportRect.y(), rowHeaderWidth,
                            viewportRect.height());
        widget->setVisible(rowHeaderWidth > 0);
    }
    m_headersLaidOut = true;
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void VirtualTableView::setModel(QAbstractItemModel *model)
{
    QAbstractItemModel *previous = VirtualItemView::model();
    if (previous == model)
        return;
    if (previous)
        disconnect(previous, nullptr, this, nullptr);

    VirtualItemView::setModel(model);
    // Persistent cell indexes of the old model are invalid now.
    recycleAllCells();
    connectColumnSignals(model);
    if (m_horizontalHeader)
        m_horizontalHeader->setLabelModel(model);
    if (m_verticalHeader)
        m_verticalHeader->setLabelModel(model);

    m_columns->setSectionCount(model ? model->columnCount() : 0);
    m_rowHeaders->setDefaultSectionSize(uniformItemHeight() > 0 ? uniformItemHeight()
                                                               : estimatedItemHeight());
    m_explicitRowHeights.clear();
    layoutHeaderWidgets();
    relayout();
}

void VirtualTableView::connectColumnSignals(QAbstractItemModel *model)
{
    if (!model)
        return;
    connect(model, &QAbstractItemModel::columnsInserted, this, &VirtualTableView::onColumnsInserted);
    connect(model, &QAbstractItemModel::columnsRemoved, this, &VirtualTableView::onColumnsRemoved);
    connect(model, &QAbstractItemModel::columnsMoved, this, &VirtualTableView::onColumnsMoved);
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        m_columns->setSectionCount(columnCount());
    });
}

void VirtualTableView::onColumnsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (parent.isValid())
        return;
    m_columns->setSectionCount(columnCount());
}

void VirtualTableView::onColumnsRemoved(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (parent.isValid())
        return;
    m_columns->setSectionCount(columnCount());
}

void VirtualTableView::onColumnsMoved(const QModelIndex &parent, int start, int end,
                                      const QModelIndex &destinationParent, int destinationColumn)
{
    if (parent.isValid() || destinationParent.isValid()) {
        m_columns->setSectionCount(columnCount());
        return;
    }
    m_columns->moveLogicalSections(start, end - start + 1, destinationColumn);
}

// ---------------------------------------------------------------------------
// Identity mapping (rows)
// ---------------------------------------------------------------------------

qsizetype VirtualTableView::viewItemCount() const
{
    QAbstractItemModel *m = model();
    return m ? qMax<qsizetype>(0, m->rowCount()) : 0;
}

QModelIndex VirtualTableView::viewIndex(qsizetype item, int column) const
{
    QAbstractItemModel *m = model();
    if (!m || item < 0 || item >= m->rowCount())
        return QModelIndex();
    return m->index(int(item), column);
}

qsizetype VirtualTableView::viewItemForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !model() || index.parent().isValid())
        return -1;
    if (index.row() < 0 || index.row() >= model()->rowCount())
        return -1;
    return index.row();
}

bool VirtualTableView::isLayoutParent(const QModelIndex &parent) const
{
    return !parent.isValid();
}

QModelIndex VirtualTableView::indexForNavigation(qsizetype item, const QModelIndex &current) const
{
    const QModelIndex target = viewIndex(item);
    if (!target.isValid() || !current.isValid())
        return target;
    // Vertical navigation keeps the current column.
    return target.siblingAtColumn(qBound(0, current.column(), qMax(0, columnCount() - 1)));
}

// ---------------------------------------------------------------------------
// Columns: geometry queries and state (HeaderGeometry is the authority)
// ---------------------------------------------------------------------------

int VirtualTableView::columnCount() const
{
    QAbstractItemModel *m = model();
    return m ? m->columnCount() : 0;
}

ColumnGeometry VirtualTableView::columnGeometry(int logicalIndex) const
{
    return m_columns->columnGeometry(logicalIndex);
}

int VirtualTableView::columnWidth(int logicalIndex) const
{
    return m_columns->sectionSize(logicalIndex);
}

VisibleRange VirtualTableView::visibleColumns() const
{
    return m_columns->visibleVisualRange(viewport()->width());
}

QVector<int> VirtualTableView::visibleColumnLogicalIndexes() const
{
    return layoutContext(QRect(0, 0, viewport()->width(), viewport()->height())).columnsToLayout();
}

void VirtualTableView::setColumnOverscan(int columns)
{
    const int clamped = qMax(0, columns);
    if (m_columnOverscan == clamped)
        return;
    m_columnOverscan = clamped;
    updateColumnLayout();
}

void VirtualTableView::setColumnWidth(int logicalIndex, int width)
{
    m_columns->resizeSection(logicalIndex, width);
}

void VirtualTableView::setColumnHidden(int logicalIndex, bool hidden)
{
    m_columns->setSectionHidden(logicalIndex, hidden);
}

bool VirtualTableView::isColumnHidden(int logicalIndex) const
{
    return m_columns->isSectionHidden(logicalIndex);
}

void VirtualTableView::moveColumn(int fromLogicalIndex, int toLogicalIndex)
{
    const int fromVisual = m_columns->visualIndex(fromLogicalIndex);
    const int toVisual = m_columns->visualIndex(toLogicalIndex);
    if (fromVisual < 0 || toVisual < 0)
        return;
    m_columns->moveSection(fromVisual, toVisual);
}

void VirtualTableView::setDefaultColumnWidth(int width)
{
    m_columns->setDefaultSectionSize(width);
}

int VirtualTableView::defaultColumnWidth() const
{
    return m_columns->defaultSectionSize();
}

void VirtualTableView::setColumnMinimumWidth(int width)
{
    m_columns->setMinimumSectionSize(width);
}

void VirtualTableView::setColumnMaximumWidth(int width)
{
    m_columns->setMaximumSectionSize(width);
}

void VirtualTableView::setStretchLastColumn(bool stretch)
{
    m_columns->setStretchLastSection(stretch);
}

bool VirtualTableView::stretchLastColumn() const
{
    return m_columns->stretchLastSection();
}

// ---------------------------------------------------------------------------
// Horizontal scrolling
// ---------------------------------------------------------------------------

qint64 VirtualTableView::horizontalOffset() const
{
    return m_columns->viewportOffset();
}

qint64 VirtualTableView::maximumHorizontalOffset() const
{
    return m_columns->maximumViewportOffset(viewport()->width());
}

qint64 VirtualTableView::horizontalContentExtent() const
{
    return m_columns->totalExtent();
}

void VirtualTableView::setHorizontalOffset(qint64 offset)
{
    const qint64 clamped = qBound<qint64>(qint64(0), offset, maximumHorizontalOffset());
    if (clamped == m_columns->viewportOffset())
        return;
    m_columns->setViewportOffset(clamped);
    // The offset change only shifts the header; the row widgets have to be told.
    updateColumnLayout();
    syncHorizontalScrollBar();
    emit horizontalOffsetChanged(clamped);
    emit columnGeometryChanged();
}

void VirtualTableView::scrollByHorizontalPixels(qint64 pixels)
{
    if (pixels == 0)
        return;
    setHorizontalOffset(m_columns->viewportOffset() + pixels);
}

void VirtualTableView::setHorizontalWheelPixels(int pixels)
{
    m_horizontalWheelPixels = qMax(1, pixels);
}

void VirtualTableView::syncHorizontalScrollBar()
{
    QScrollBar *bar = horizontalScrollBar();
    const int viewportWidth = viewport()->width();
    const qint64 maximum = maximumHorizontalOffset();

    const QSignalBlocker blocker(bar);
    bar->setRange(0, int(qMin<qint64>(maximum, qint64(std::numeric_limits<int>::max()))));
    bar->setPageStep(qMax(1, viewportWidth));
    bar->setSingleStep(qMax(1, viewportWidth / 20));
    const int value = int(qBound<qint64>(qint64(0), m_columns->viewportOffset(), maximum));
    bar->setValue(value);
    if (m_columns->viewportOffset() != value)
        m_columns->setViewportOffset(value);
}

// ---------------------------------------------------------------------------
// Row heights and the vertical header
// ---------------------------------------------------------------------------

int VirtualTableView::rowHeight(qsizetype row) const
{
    return m_rowLayout ? m_rowLayout->itemSize(row) : 0;
}

void VirtualTableView::setRowSizePolicy(RowSizePolicy policy)
{
    if (m_rowSizePolicy == policy)
        return;
    m_rowSizePolicy = policy;
    markDirty();
}

void VirtualTableView::setRowHeight(qsizetype row, int height)
{
    if (!m_rowLayout || row < 0 || row >= viewItemCount())
        return;
    const int clamped = qMax(1, height);
    if (rowHeight(row) == clamped && hasExplicitRowHeight(row))
        return;
    // A uniform-height table whose row is resized has to become variable: all
    // other rows keep their height (that height becomes the estimate).
    if (itemHeightMode() == ItemHeightMode::Uniform && uniformItemHeight() > 0)
        setEstimatedItemHeight(uniformItemHeight());
    if (itemHeightMode() == ItemHeightMode::Uniform)
        setItemHeightMode(ItemHeightMode::Variable);
    m_rowLayout->setItemSize(row, clamped);
    const QModelIndex index = viewIndex(row);
    if (index.isValid())
        m_explicitRowHeights.insert(QPersistentModelIndex(index), clamped);
    updateRowHeaderGeometry();
    // Deferred: setRowHeight() may be called from a header signal, and the
    // relayout rebuilds the materialized set.
    markDirty();
    emit rowHeightChanged(row, clamped);
}

void VirtualTableView::clearRowHeight(qsizetype row)
{
    const QModelIndex index = viewIndex(row);
    if (!index.isValid())
        return;
    const QPersistentModelIndex persistent(index);
    if (!m_explicitRowHeights.contains(persistent))
        return;
    m_explicitRowHeights.remove(persistent);
    markDirty();
}

bool VirtualTableView::hasExplicitRowHeight(qsizetype row) const
{
    const QModelIndex index = viewIndex(row);
    return index.isValid() && m_explicitRowHeights.contains(QPersistentModelIndex(index));
}

bool VirtualTableView::canMeasureItem(qsizetype item) const
{
    if (m_rowSizePolicy == RowSizePolicy::MeasuredWins)
        return true;
    return !hasExplicitRowHeight(item);
}

void VirtualTableView::onVerticalSectionResized(int logicalIndex, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    if (logicalIndex < 0 || qsizetype(logicalIndex) >= viewItemCount())
        return;
    if (m_rowHeaderUpdateActive)
        return; // programmatic mirroring of the layout into the geometry
    if (rowHeight(logicalIndex) == newSize)
        return;
    // The user dragged the row boundary: the row height becomes explicit.
    setRowHeight(logicalIndex, newSize);
}

void VirtualTableView::onVerticalHeaderUserResized(int row, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    if (m_rowHeaderUpdateActive)
        return; // programmatic mirroring, not a user gesture
    if (m_verticalHeader) {
        if (auto *native = dynamic_cast<NativeHeaderView *>(m_verticalHeader->headerWidget())) {
            if (native->isApplyingGeometry())
                return; // the geometry is being pushed into the header
        }
    }
    if (row < 0 || qsizetype(row) >= viewItemCount())
        return;
    setRowHeight(row, newSize);
}

void VirtualTableView::updateRowHeaderOffset()
{
    if (!m_rowHeaders)
        return;
    // The row-number strip consumes the same vertical offset as the body, so the
    // numbers stay glued to their rows while scrolling.
    m_rowHeaders->setViewportOffset(verticalOffset());
}

void VirtualTableView::updateRowHeaderGeometry()
{
    if (!m_rowHeaders)
        return;
    ensureHeaders();
    if (m_rowHeaderUpdateActive)
        return;
    m_rowHeaderUpdateActive = true;

    const qsizetype count = viewItemCount();
    const bool variable = itemHeightMode() == ItemHeightMode::Variable;
    if (variable && count > kRowHeaderMirrorLimit) {
        // Per-row heights cannot be mirrored into a native header at this scale
        // without an O(rows) structure; the row-number strip is disabled and the
        // widget header (v0.5) will lift this limitation.
        if (!m_verticalHeaderDisabled) {
            m_verticalHeaderDisabled = true;
            qWarning("VirtualItemViews: vertical header disabled for %lld variable-height rows; "
                     "use uniform row heights or a widget header (v0.5).",
                     qint64(count));
            setVerticalHeaderVisible(false);
        }
        m_rowHeaderUpdateActive = false;
        return;
    }

    const int defaultSize = (!variable && uniformItemHeight() > 0) ? uniformItemHeight()
                                                                  : estimatedItemHeight();
    if (m_rowHeaders->defaultSectionSize() != defaultSize)
        m_rowHeaders->setDefaultSectionSize(defaultSize);

    // Mirror per-row heights only when they are needed: variable-height rows, or
    // rows the user resized explicitly. A uniform table keeps an empty geometry
    // (default section size = row height) and costs no memory.
    const bool mirror = count > 0 && count <= kRowHeaderMirrorLimit
        && (variable || !m_explicitRowHeights.isEmpty());
    if (!mirror) {
        // Uniform heights: the default section size is exact and cheap, so no
        // per-row mirroring is needed (the header keeps the sections it gets
        // from the model).
        if (m_rowHeaders->sectionCount() != 0)
            m_rowHeaders->setSectionCount(0);
        m_rowHeaderUpdateActive = false;
        return;
    }
    if (m_rowHeaders->sectionCount() != int(count))
        m_rowHeaders->setSectionCount(int(count));

    // Collect the differences first: applying them emits sectionResized, and the
    // resulting handler may relayout and rebuild the materialized set.
    QVector<QPair<qsizetype, int>> pending;
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        const int size = rowHeight(row);
        if (size > 0 && m_rowHeaders->storedSectionSize(int(row)) != size)
            pending.append({row, size});
    }
    for (auto it = m_explicitRowHeights.constBegin(); it != m_explicitRowHeights.constEnd(); ++it) {
        const qsizetype row = viewItemForIndex(it.key());
        if (row < 0)
            continue;
        if (m_rowHeaders->storedSectionSize(int(row)) != it.value())
            pending.append({row, it.value()});
    }
    for (const QPair<qsizetype, int> &entry : pending) {
        if (m_rowHeaders->storedSectionSize(int(entry.first)) != entry.second)
            m_rowHeaders->resizeSection(int(entry.first), entry.second);
    }
    m_rowHeaderUpdateActive = false;
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------

void VirtualTableView::setSortingEnabled(bool enabled)
{
    if (m_sortingEnabled == enabled)
        return;
    m_sortingEnabled = enabled;
    ensureHeaders();
    m_horizontalHeader->setSortInteractionEnabled(enabled);
    if (!enabled)
        m_columns->setSortIndicator(-1, Qt::AscendingOrder);
}

void VirtualTableView::sortByColumn(int logicalIndex, Qt::SortOrder order)
{
    QAbstractItemModel *m = model();
    if (!m || logicalIndex < 0 || logicalIndex >= columnCount())
        return;
    m_sortGuard = true;
    m_columns->setSortIndicator(logicalIndex, order);
    m_sortGuard = false;
    emit sortIndicatorRequested(logicalIndex, order);
    m->sort(logicalIndex, order);
}

void VirtualTableView::setSortIndicator(int logicalIndex, Qt::SortOrder order)
{
    m_columns->setSortIndicator(logicalIndex, order);
}

void VirtualTableView::onSortIndicatorChanged(int logicalIndex, Qt::SortOrder order)
{
    if (m_sortGuard || !m_sortingEnabled || logicalIndex < 0)
        return;
    QAbstractItemModel *m = model();
    if (!m)
        return;
    emit sortIndicatorRequested(logicalIndex, order);
    m->sort(logicalIndex, order);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

QByteArray VirtualTableView::saveHeaderState() const
{
    return m_columns->saveState();
}

bool VirtualTableView::restoreHeaderState(const QByteArray &state)
{
    return m_columns->restoreState(state);
}

// ---------------------------------------------------------------------------
// Adapter
// ---------------------------------------------------------------------------

void VirtualTableView::setTableAdapter(TableWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_tableAdapter == adapter) {
        m_ownTableAdapter = m_ownTableAdapter || takeOwnership;
        return;
    }
    if (m_ownTableAdapter && m_tableAdapter)
        delete m_tableAdapter;
    m_tableAdapter = adapter;
    m_ownTableAdapter = takeOwnership;
    setAdapter(adapter, false); // the kernel uses the same adapter
}

TableWidgetAdapter *VirtualTableView::tableAdapter() const
{
    return m_tableAdapter;
}

// ---------------------------------------------------------------------------
// Materialization mode (Row Widget Mode / Cell Widget Mode)
// ---------------------------------------------------------------------------

void VirtualTableView::setMaterializationMode(MaterializationMode mode)
{
    if (m_materializationMode == mode)
        return;
    // Both directions have to release the widgets owned by the old mode: the
    // kernel does not recycle row widgets while cell mode is active.
    recycleAllCells();
    recycleAllItems();
    m_materializationMode = mode;
    relayout();
}

void VirtualTableView::setCellAdapter(CellWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_cellAdapter == adapter) {
        m_ownCellAdapter = m_ownCellAdapter || takeOwnership;
        return;
    }
    recycleAllCells();
    if (m_ownCellAdapter)
        delete m_cellAdapter;
    m_cellAdapter = adapter;
    m_ownCellAdapter = takeOwnership;
    relayout();
}

bool VirtualTableView::usesItemWidgets() const
{
    return m_materializationMode == MaterializationMode::RowWidgets;
}

QVector<int> VirtualTableView::columnsForCellMaterialization() const
{
    QVector<int> columns;
    if (!m_columns)
        return columns;
    const int count = m_columns->sectionCount();
    if (count <= 0)
        return columns;

    const VisibleRange visible = m_columns->visibleVisualRange(qMax(1, viewport()->width()));
    const VisibleRange window = VisibleRange::expanded(visible.first, visible.last, m_columnOverscan,
                                                      m_columnOverscan, count);
    columns.reserve(int(window.count()));
    for (qsizetype visual = window.first; visual >= 0 && visual <= window.last; ++visual) {
        const int logical = m_columns->logicalIndex(int(visual));
        if (logical < 0 || m_columns->isSectionHidden(logical))
            continue;
        columns.append(logical);
    }
    return columns;
}

QRect VirtualTableView::cellRect(qsizetype row, int logicalColumn) const
{
    if (!m_rowLayout || !m_columns)
        return QRect();
    const ColumnGeometry column = m_columns->columnGeometry(logicalColumn);
    if (!column.isValid() || column.hidden || column.width <= 0)
        return QRect();
    const QRect rowRect = m_rowLayout->itemRect(row, verticalOffset());
    if (rowRect.height() <= 0)
        return QRect();
    return QRect(column.viewportX, rowRect.y(), column.width, rowRect.height());
}

QWidget *VirtualTableView::createCellWidget(const QPersistentModelIndex &index)
{
    if (!m_cellAdapter)
        return nullptr;

    const QModelIndex modelIndex(index);
    const WidgetType type = m_cellAdapter->cellWidgetType(modelIndex);
    QWidget *widget = recycler()->acquire(type);
    if (!widget)
        return nullptr;

    if (widget->parentWidget() != viewport())
        widget->setParent(viewport());
    // Never show a cell before it has been bound to its new index.
    widget->hide();
    m_cellAdapter->bindCellWidget(widget, modelIndex);
    m_cellTypes.insert(widget, type);
    return widget;
}

void VirtualTableView::recycleCell(const QPersistentModelIndex &index, QWidget *widget)
{
    if (!widget || !m_cellAdapter)
        return;
    m_cellAdapter->unbindCellWidget(widget, QModelIndex(index));
    widget->hide();
    const WidgetType type = m_cellTypes.take(widget);
    recycler()->recycle(type, widget);
}

void VirtualTableView::recycleAllCells()
{
    if (m_cells.isEmpty()) {
        m_cellTypes.clear();
        return;
    }
    if (m_cellAdapter) {
        for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
            recycleCell(it.key(), it.value());
    }
    m_cells.clear();
    m_cellTypes.clear();
}

bool VirtualTableView::isCellPinned(const QPersistentModelIndex &index, const QWidget *widget) const
{
    if (index.isValid() && isItemPinned(QModelIndex(index)))
        return true;
    // A focused editor, an active IME composition or a popup must survive.
    return hasFocusWithin(widget);
}

void VirtualTableView::materializeItems(const VisibleRange &rows)
{
    if (m_materializationMode != MaterializationMode::CellWidgets || !m_cellAdapter || !model()) {
        recycleAllCells();
        return;
    }
    if (m_cellMaterializationActive)
        return;
    m_cellMaterializationActive = true;

    const QVector<int> columns = columnsForCellMaterialization();
    QHash<QPersistentModelIndex, QWidget *> next;
    for (qsizetype row = rows.first; row >= 0 && row <= rows.last; ++row) {
        for (int column : columns) {
            const QModelIndex index = model()->index(int(row), column);
            if (!index.isValid())
                continue;
            const QPersistentModelIndex persistent(index);
            QWidget *widget = m_cells.value(persistent, nullptr);
            if (!widget) {
                widget = createCellWidget(persistent);
                if (!widget)
                    continue;
            }
            next.insert(persistent, widget);
        }
    }

    // Keep pinned cells (focus/IME/popup/explicit pin) even outside the window,
    // recycle everything else.
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (next.contains(it.key()))
            continue;
        if (isCellPinned(it.key(), it.value())) {
            next.insert(it.key(), it.value());
            continue;
        }
        recycleCell(it.key(), it.value());
    }

    m_cells = next;
    updateCellGeometry();
    m_cellMaterializationActive = false;
}

void VirtualTableView::updateCellGeometry()
{
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        QWidget *widget = it.value();
        const QModelIndex index = it.key();
        if (!index.isValid()) {
            widget->hide();
            continue;
        }
        const QRect rect = cellRect(index.row(), index.column());
        if (rect.isValid()) {
            widget->setGeometry(rect);
            if (!widget->isVisible())
                widget->show();
        } else {
            widget->hide();
        }
    }
}

void VirtualTableView::rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    if (m_materializationMode != MaterializationMode::CellWidgets) {
        VirtualItemView::rebindItemsInRange(topLeft, bottomRight);
        return;
    }
    if (!m_cellAdapter)
        return;
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const QModelIndex index = it.key();
        if (!index.isValid() || index.parent() != topLeft.parent())
            continue;
        if (index.row() < topLeft.row() || index.row() > bottomRight.row())
            continue;
        if (index.column() < topLeft.column() || index.column() > bottomRight.column())
            continue;
        m_cellAdapter->bindCellWidget(it.value(), index);
    }
}

QModelIndex VirtualTableView::indexAt(const QPoint &viewportPos) const
{
    const QModelIndex rowIndex = VirtualItemView::indexAt(viewportPos);
    if (!rowIndex.isValid() || !m_columns)
        return rowIndex;
    const int column = m_columns->sectionAtOffset(m_columns->viewportOffset() + viewportPos.x());
    if (column < 0 || m_columns->isSectionHidden(column))
        return rowIndex;
    return rowIndex.siblingAtColumn(column);
}

QWidget *VirtualTableView::cellWidget(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    return m_cells.value(QPersistentModelIndex(index), nullptr);
}

QModelIndex VirtualTableView::cellIndexForWidget(const QWidget *widget) const
{
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (it.value() == widget)
            return QModelIndex(it.key());
    }
    return QModelIndex();
}

QList<QModelIndex> VirtualTableView::materializedCellIndexes() const
{
    QList<QModelIndex> indexes;
    indexes.reserve(m_cells.size());
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        indexes.append(QModelIndex(it.key()));
    return indexes;
}

VirtualViewStats VirtualTableView::stats() const
{
    VirtualViewStats result = VirtualItemView::stats();
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        result.materializedItems = m_cells.size();
        qsizetype pinned = 0;
        for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
            if (isCellPinned(it.key(), it.value()))
                ++pinned;
        }
        result.pinnedWidgets = pinned;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Kernel hooks
// ---------------------------------------------------------------------------

TableRowLayoutContext VirtualTableView::layoutContext(const QRect &viewportRect) const
{
    TableRowLayoutContext context;
    context.m_geometry = m_columns;
    context.m_columnCount = columnCount();
    context.m_viewportRect = viewportRect;
    context.m_horizontalOffset = m_columns->viewportOffset();
    const VisibleRange visible = m_columns->visibleVisualRange(qMax(1, viewportRect.width()));
    context.m_visibleColumns = VisibleRange::expanded(visible.first, visible.last, m_columnOverscan,
                                                     m_columnOverscan, m_columns->sectionCount());
    return context;
}

void VirtualTableView::applyColumnLayout(const MaterializedItem &item)
{
    if (!item.widget)
        return;

    const TableRowLayoutContext context = layoutContext(item.geometry);

    // Framework-managed column hosts (§27).
    const QList<ColumnHost *> hosts =
        item.widget->findChildren<ColumnHost *>(QString(), Qt::FindDirectChildrenOnly);
    if (!hosts.isEmpty()) {
        for (ColumnHost *host : hosts) {
            const ColumnGeometry geometry = context.column(host->logicalColumn());
            if (!geometry.isValid() || geometry.hidden) {
                host->setVisible(false);
                continue;
            }
            const int x = context.columnX(geometry.logicalIndex);
            host->setGeometry(x, 0, geometry.width, context.viewportRect().height());
            const bool intersects = x < context.viewportRect().width()
                && x + geometry.width > 0;
            host->setVisible(intersects);
        }
    }

    if (m_tableAdapter)
        m_tableAdapter->layoutRowWidget(item.widget, QModelIndex(item.index), context);
}

void VirtualTableView::updateColumnLayout()
{
    if (m_columnUpdateActive)
        return;
    m_columnUpdateActive = true;
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        updateCellGeometry();
    } else {
        // Snapshot: the adapter hook or a ColumnHost may trigger a relayout.
        const QList<MaterializedItem> items = materializedItems();
        for (const MaterializedItem &item : items)
            applyColumnLayout(item);
    }
    m_columnUpdateActive = false;
}

void VirtualTableView::onHeaderGeometryChanged()
{
    if (m_horizontalHeader)
        m_horizontalHeader->headerWidget()->update();
    syncHorizontalScrollBar();
    updateColumnLayout();
    if (m_materializationMode == MaterializationMode::CellWidgets) {
        // Column geometry changes also change which cells belong to the
        // materialized region, so the cell set has to be recomputed.
        markDirty();
    }
    emit columnGeometryChanged();
}

void VirtualTableView::afterMaterialize()
{
    VirtualItemView::afterMaterialize();
    updateRowHeaderOffset();
    updateRowHeaderGeometry();
    updateColumnLayout();
    syncHorizontalScrollBar();
}

void VirtualTableView::resizeEvent(QResizeEvent *event)
{
    layoutHeaderWidgets();
    VirtualItemView::resizeEvent(event);
    syncHorizontalScrollBar();
}

void VirtualTableView::showEvent(QShowEvent *event)
{
    ensureHeaders();
    layoutHeaderWidgets();
    VirtualItemView::showEvent(event);
    syncHorizontalScrollBar();
}

void VirtualTableView::scrollContentsBy(int dx, int dy)
{
    if (dx != 0 && !m_columnUpdateActive) {
        // Header and body consume the same offset: they cannot drift (§45.5).
        m_columns->setViewportOffset(horizontalScrollBar()->value());
        emit horizontalOffsetChanged(m_columns->viewportOffset());
    }
    VirtualItemView::scrollContentsBy(dx, dy);
}

void VirtualTableView::wheelEvent(QWheelEvent *event)
{
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    const int horizontal = !pixel.isNull() ? pixel.x() : angle.x();
    const bool shiftHorizontal = horizontal == 0 && (event->modifiers() & Qt::ShiftModifier);

    if (horizontal != 0 || shiftHorizontal) {
        qint64 delta = 0;
        if (!pixel.isNull()) {
            // High resolution input: a horizontal wheel/trackpad pan uses x, a
            // Shift+wheel keeps the vertical delta.
            delta = horizontal != 0 ? pixel.x() : pixel.y();
        } else {
            delta = qint64(horizontal != 0 ? angle.x() : angle.y()) * m_horizontalWheelPixels / 120;
        }
        if (delta != 0) {
            scrollByHorizontalPixels(-delta);
            event->accept();
            return;
        }
    }
    VirtualItemView::wheelEvent(event);
}

bool VirtualTableView::handleItemKeyPress(QKeyEvent *event)
{
    const QModelIndex current = currentIndex();
    if (!current.isValid())
        return false;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const int step = event->key() == Qt::Key_Left ? -1 : 1;
        int column = current.column() + step;
        while (column >= 0 && column < columnCount() && isColumnHidden(column))
            column += step;
        if (column < 0 || column >= columnCount())
            return true;
        setCurrentIndex(current.siblingAtColumn(column));
        scrollToColumn(column);
        return true;
    }
    default:
        break;
    }
    return false;
}

void VirtualTableView::scrollToColumn(int logicalIndex)
{
    const ColumnGeometry geometry = m_columns->columnGeometry(logicalIndex);
    if (!geometry.isValid() || geometry.hidden)
        return;
    const int viewportWidth = viewport()->width();
    const qint64 start = geometry.contentX;
    const qint64 end = start + geometry.width;
    const qint64 offset = m_columns->viewportOffset();
    if (start < offset)
        setHorizontalOffset(start);
    else if (end > offset + viewportWidth)
        setHorizontalOffset(end - viewportWidth);
}

} // namespace viv
