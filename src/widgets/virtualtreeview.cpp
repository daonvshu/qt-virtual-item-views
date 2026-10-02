#include <virtualitemviews/virtualtreeview.h>

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <limits>

namespace viv {

namespace {
inline QPoint eventPosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/// Ancestor of \a index \a levels above it (0 = the index itself).
QModelIndex ancestorAt(const QModelIndex &index, int levels)
{
    QModelIndex current = index;
    for (int level = 0; level < levels && current.isValid(); ++level)
        current = current.parent();
    return current;
}
} // namespace

VirtualTreeView::VirtualTreeView(QWidget *parent)
    : VirtualItemView(parent)
{
    m_visibility = new TreeVisibilityIndex(nullptr);
    m_rowLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_rowLayout, true);
    setSelectionBehavior(SelectionBehavior::SelectRows);
}

VirtualTreeView::~VirtualTreeView()
{
    qDeleteAll(m_rowGridLines);
    qDeleteAll(m_rowGridLinePool);
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    delete m_visibility;
}

void VirtualTreeView::setModel(QAbstractItemModel *model)
{
    QAbstractItemModel *previous = VirtualItemView::model();
    if (previous == model)
        return;
    if (previous)
        disconnect(previous, nullptr, this, nullptr);

    m_visibility->setModel(model);
    m_rootIndex = QModelIndex();
    m_visibility->setRootIndex(m_rootIndex);

    const quint64 previousChangeSerial = modelChangeSerial();
    VirtualItemView::setModel(model);
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    connectModelSignals(this->model());
    resetLayoutForNewModel();
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    relayout();
}

void VirtualTreeView::connectModelSignals(QAbstractItemModel *model)
{
    if (!model)
        return;

    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                if (!roles.isEmpty() && !roles.contains(NodeRowSpacingBelowRole)
                    && !roles.contains(NodeRowSpacingAboveRole))
                    return;
                if (auto *layout = m_rowLayout) {
                    layout->setItemSpacing(rowSpacing());
                    applyRowSpacingOverrides();
                }
                relayout();
            });
    // The anchor has to be captured before the model changes, and removed rows
    // must lose their widgets before their persistent indexes become invalid.
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex &, int, int) { setPendingAnchor(captureAnchor()); });
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &parent, int first, int last) {
                setPendingAnchor(captureAnchor());
                recycleItemsInModelRange(parent, first, last);
            });

    connect(model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { onStructureChanged(); });
    connect(model, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { onStructureChanged(); });
    connect(model, &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) { onStructureChanged(); });
    connect(model, &QAbstractItemModel::layoutChanged, this,
            [this](const QList<QPersistentModelIndex> &, QAbstractItemModel::LayoutChangeHint) {
                onStructureChanged();
            });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        m_rootIndex = QModelIndex();
        m_visibility->handleModelReset();
        refreshVisibility(false);
    });
    // The visibility index stores QModelIndex values (see TreeVisibilityIndex), and a column
    // change renames - or, for a removed column 0, invalidates - the cells they name, so the
    // mapping is re-derived. It is *not* onStructureChanged(): the row structure did not
    // change, so the measured heights and the anchor stay valid (P2 of the fourth review).
    // The base kernel releases the row widgets before the change, which is what keeps the
    // unbind/rebind pair honest.
    connect(model, &QAbstractItemModel::columnsInserted, this,
            [this]() { onColumnStructureChanged(); });
    connect(model, &QAbstractItemModel::columnsRemoved, this,
            [this]() { onColumnStructureChanged(); });
    connect(model, &QAbstractItemModel::columnsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                onColumnStructureChanged();
            });
}

void VirtualTreeView::onStructureChanged()
{
    // Visible rows, expansion state and item identity all live in the index.
    m_visibility->handleModelChanged();
    refreshVisibility(false);
}

void VirtualTreeView::onColumnStructureChanged()
{
    // Same reason as onStructureChanged(), minus the layout reset: the visible rows are
    // (row, 0) indexes, so a column change re-derives them - but the rows themselves did not
    // move, and their measured heights must survive. The expansion state names cells, not rows,
    // so a column change that renames column 0 can collapse it (see docs/history/model-signals.md:
    // column mutation is not part of the List / Tree contract); what this path guarantees is
    // that the mapping is re-derived instead of handing out stale cells.
    m_visibility->handleModelChanged();
    relayout();
}

void VirtualTreeView::refreshVisibility(bool keepAnchor)
{
    if (m_updatingVisibility)
        return;
    m_updatingVisibility = true;
    if (keepAnchor)
        setPendingAnchor(captureAnchor());
    // The visible row mapping changed: row sizes are keyed by view row, so they
    // are reset to the estimate (measurement re-measures the visible rows) while
    // ScrollAnchor keeps the anchored item in place.
    resetLayoutForNewModel();
    relayout();
    m_updatingVisibility = false;
}

// ---------------------------------------------------------------------------
// Identity mapping
// ---------------------------------------------------------------------------

qsizetype VirtualTreeView::viewItemCount() const
{
    return m_visibility->visibleRowCount();
}

QModelIndex VirtualTreeView::viewIndex(qsizetype item, int column) const
{
    const QModelIndex index = m_visibility->indexAtVisibleRow(item);
    if (!index.isValid() || column == index.column())
        return index;
    return index.siblingAtColumn(column);
}

qsizetype VirtualTreeView::viewItemForIndex(const QModelIndex &index) const
{
    return m_visibility->visibleRowForIndex(index);
}

bool VirtualTreeView::isLayoutParent(const QModelIndex &parent) const
{
    // The kernel works on visible rows, never on model rows: model mutations are
    // routed through TreeVisibilityIndex.
    Q_UNUSED(parent);
    return false;
}

int VirtualTreeView::itemDepth(const QModelIndex &index) const
{
    const int depth = m_visibility->depth(index);
    return depth < 0 ? 0 : depth;
}

int VirtualTreeView::effectiveRowSpacing(qsizetype row) const
{
    const QVariant value = viewIndex(row).data(NodeRowSpacingBelowRole);
    bool valid = false;
    const int spacing = value.toInt(&valid);
    const int below = valid && spacing >= 0 ? spacing : rowSpacing();
    const QVariant aboveValue = row + 1 < viewItemCount()
        ? viewIndex(row + 1).data(NodeRowSpacingAboveRole) : QVariant();
    const int above = aboveValue.toInt(&valid);
    return int(qMin<qint64>(qint64(below) + (valid ? qMax(0, above) : 0),
                           std::numeric_limits<int>::max()));
}

void VirtualTreeView::setRowGridLinesVisible(bool visible)
{
    if (m_rowGridLinesVisible == visible)
        return;
    m_rowGridLinesVisible = visible;
    relayout();
}

void VirtualTreeView::setRowGridLineWidth(int pixels)
{
    const int width = qMax(1, pixels);
    if (m_rowGridLineWidth == width)
        return;
    m_rowGridLineWidth = width;
    relayout();
}

void VirtualTreeView::setRowGridLineColor(const QColor &color)
{
    if (m_rowGridLineColor == color)
        return;
    m_rowGridLineColor = color;
    relayout();
}

void VirtualTreeView::setRowGridLineExtent(RowGridLineExtent extent)
{
    if (extent != RowGridLineExtent::NodeOnly && extent != RowGridLineExtent::NodeAndIcon
        && extent != RowGridLineExtent::FullWidth)
        return;
    if (m_rowGridLineExtent == extent)
        return;
    m_rowGridLineExtent = extent;
    relayout();
}

void VirtualTreeView::setVisualStateBackgroundVisible(bool visible)
{
    if (m_visualStateBackgroundVisible == visible)
        return;
    m_visualStateBackgroundVisible = visible;
    viewport()->update();
}

void VirtualTreeView::setVisualStateBackgroundExtent(VisualStateBackgroundExtent extent)
{
    if (extent != VisualStateBackgroundExtent::NodeOnly
        && extent != VisualStateBackgroundExtent::NodeAndIcon
        && extent != VisualStateBackgroundExtent::FullWidth)
        return;
    if (m_visualStateBackgroundExtent == extent)
        return;
    m_visualStateBackgroundExtent = extent;
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

int VirtualTreeView::rowGridLineInsetForDepth(int depth) const
{
    const qint64 cells = m_rowGridLineExtent == RowGridLineExtent::NodeOnly ? qint64(depth) + 1
        : m_rowGridLineExtent == RowGridLineExtent::NodeAndIcon ? qint64(depth) : 0;
    return int(qMin<qint64>(qMax(0, viewport()->width()), qMax<qint64>(0, cells) * m_indentation));
}

QColor VirtualTreeView::itemPaneSeparatorColor() const
{
    return m_rowGridLineColor.isValid() ? m_rowGridLineColor
                                        : VirtualItemView::itemPaneSeparatorColor();
}

void VirtualTreeView::configureRowSpacingWidget(QWidget *widget) const
{
    widget->setProperty("vivShowSpacingLines", m_rowGridLinesVisible);
    widget->setProperty("vivSpacingLineWidth", m_rowGridLineWidth);
    const int depth = widget->property("vivSpacingDepth").toInt();
    widget->setProperty("vivSpacingLineLeftInset", rowGridLineInsetForDepth(depth));
}

void VirtualTreeView::applyRowSpacingOverrides()
{
    if (!m_rowLayout)
        return;
    for (qsizetype row = 0; row < m_rowLayout->itemCount(); ++row) {
        const int spacing = effectiveRowSpacing(row);
        if (spacing != rowSpacing())
            m_rowLayout->setSpacingAfter(row, spacing);
    }
}

qsizetype VirtualTreeView::visibleRowCount() const
{
    return m_visibility->visibleRowCount();
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

void VirtualTreeView::setRootIndex(const QModelIndex &index)
{
    if (index == m_rootIndex)
        return;
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualTreeView::setRootIndex(): the index belongs to another model "
                 "(or the view has no model); the root is unchanged");
        return;
    }
    m_rootIndex = index;
    m_visibility->setRootIndex(index);
    refreshVisibility(false);
}

bool VirtualTreeView::hasChildren(const QModelIndex &index) const
{
    QAbstractItemModel *m = model();
    return m && index.isValid() && m->rowCount(index) > 0;
}

void VirtualTreeView::expand(const QModelIndex &index)
{
    if (!index.isValid() || m_visibility->isExpanded(index))
        return;
    // The anchor must be captured *before* the visible row mapping changes,
    // otherwise the view can jump by the number of inserted rows.
    setPendingAnchor(captureAnchor());
    m_visibility->expand(index);
    refreshVisibility(false);
    emit expanded(index);
}

void VirtualTreeView::collapse(const QModelIndex &index)
{
    if (!index.isValid() || !m_visibility->isExpanded(index))
        return;
    setPendingAnchor(captureAnchor());
    m_visibility->collapse(index);
    refreshVisibility(false);
    emit collapsed(index);
}

void VirtualTreeView::expandRecursively(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const bool wasExpanded = m_visibility->isExpanded(index);
    setPendingAnchor(captureAnchor());
    m_visibility->expandRecursively(index);
    refreshVisibility(false);
    if (!wasExpanded)
        emit expanded(index);
}

void VirtualTreeView::setExpanded(const QModelIndex &index, bool expanded)
{
    expanded ? expand(index) : collapse(index);
}

void VirtualTreeView::toggleExpanded(const QModelIndex &index)
{
    setExpanded(index, !isExpanded(index));
}

bool VirtualTreeView::isExpanded(const QModelIndex &index) const
{
    return m_visibility->isExpanded(index);
}

void VirtualTreeView::collapseAll()
{
    setPendingAnchor(captureAnchor());
    m_visibility->collapseAll();
    refreshVisibility(false);
}

// ---------------------------------------------------------------------------
// Geometry / branch UI
// ---------------------------------------------------------------------------

void VirtualTreeView::setIndentation(int pixels)
{
    const int clamped = qMax(0, pixels);
    if (m_indentation == clamped)
        return;
    m_indentation = clamped;
    relayout();
}

void VirtualTreeView::setBranchIndicatorsVisible(bool visible)
{
    if (m_branchIndicatorsVisible == visible)
        return;
    m_branchIndicatorsVisible = visible;
    // Turning them off must erase what is painted; turning them on must repaint
    // the strip (the rows themselves never carry the indicators).
    invalidateBranchIndicators();
}

void VirtualTreeView::setBranchIndicatorRenderer(BranchIndicatorRenderer *renderer, bool takeOwnership)
{
    if (m_branchRenderer == renderer) {
        m_ownBranchRenderer = m_ownBranchRenderer || (renderer && takeOwnership);
        return;
    }
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    m_branchRenderer = renderer;
    m_ownBranchRenderer = renderer && takeOwnership;
    // The strip that was painted with the old renderer may be wider (a renderer
    // can draw the ancestor cells too), so erase the previous one as well.
    invalidateBranchIndicators();
}

BranchIndicatorState VirtualTreeView::branchState(const QModelIndex &index, int cellDepth) const
{
    BranchIndicatorState state;
    if (!index.isValid() || !model())
        return state;

    const int itemDepth = qMax(0, this->itemDepth(index));
    state.itemDepth = itemDepth;
    // A negative cellDepth asks for the row's own cell.
    state.cellDepth = cellDepth < 0 ? itemDepth : qBound(0, cellDepth, itemDepth);

    // The cell at level L belongs to the ancestor at level L, so walk up as many
    // levels as separate the row from that ancestor.
    const QModelIndex cell = ancestorAt(index, itemDepth - state.cellDepth);
    if (!cell.isValid())
        return state;

    state.adjoinsItem = state.cellDepth == itemDepth;
    state.hasChildren = model()->rowCount(cell) > 0;
    state.isExpanded = m_visibility->isExpanded(cell);
    state.hasSiblings = cell.row() + 1 < model()->rowCount(cell.parent());
    return state;
}

QRect VirtualTreeView::geometryForViewRow(qsizetype row) const
{
    const QRect rowRect = VirtualItemView::geometryForViewRow(row);
    if (!rowRect.isValid())
        return rowRect;
    // One indentation step per level plus one for the branch indicator.
    const int left = m_indentation * (itemDepth(viewIndex(row)) + 1);
    return QRect(rowRect.x() + left, rowRect.y(), qMax(0, rowRect.width() - left), rowRect.height());
}

int VirtualTreeView::branchIndicatorX(qsizetype row) const
{
    const int depth = itemDepth(viewIndex(row));
    return depth * m_indentation + kBranchIndicatorMargin;
}

QRect VirtualTreeView::branchCellRect(qsizetype row, int cellDepth) const
{
    const QRect rowRect = VirtualItemView::geometryForViewRow(row);
    if (!rowRect.isValid() || m_indentation <= 0 || cellDepth < 0)
        return QRect();
    return QRect(rowRect.x() + cellDepth * m_indentation, rowRect.y(), m_indentation,
                 rowRect.height());
}

bool VirtualTreeView::isIndicatorPosition(const QModelIndex &index, const QPoint &viewportPos) const
{
    if (!m_branchIndicatorsVisible || !index.isValid())
        return false;
    const qsizetype row = viewItemForIndex(index);
    if (row < 0 || !hasBranchIndicator(row))
        return false;
    const int left = itemDepth(index) * m_indentation;
    return viewportPos.x() >= left && viewportPos.x() < left + m_indentation;
}

bool VirtualTreeView::hasBranchIndicator(qsizetype row) const
{
    if (!m_branchIndicatorsVisible)
        return false;
    const QModelIndex index = viewIndex(row);
    return index.isValid() && hasChildren(index);
}

void VirtualTreeView::afterMaterialize()
{
    VirtualItemView::afterMaterialize();
    invalidateBranchIndicators();
    syncRowGridLines();
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeView::refreshVisualStates()
{
    VirtualItemView::refreshVisualStates();
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeView::refreshVisualState(const QModelIndex &index)
{
    VirtualItemView::refreshVisualState(index);
    if (m_visualStateBackgroundVisible && index.isValid())
        viewport()->update(visualRect(index));
}

void VirtualTreeView::syncRowGridLines()
{
    QHash<qsizetype, QRect> desired;
    const QRect viewportRect = viewport()->geometry();
    if (m_rowGridLinesVisible && m_rowLayout && viewportRect.width() > 0) {
        for (const VisibleRange &range : visibleItemRanges()) {
            for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                if (row + 1 >= viewItemCount() || m_rowLayout->spacingAfter(row) > 0)
                    continue;
                const QRect rowRect = VirtualItemView::geometryForViewRow(row);
                if (!rowRect.intersects(viewport()->rect()))
                    continue;
                const int left = rowGridLineInsetForDepth(itemDepth(viewIndex(row)));
                const QRect lineRect(viewportRect.x() + left,
                                     viewportRect.y() + rowRect.bottom() - m_rowGridLineWidth + 1,
                                     viewportRect.width() - left, m_rowGridLineWidth);
                const QRect paneRect = itemPaneRect(itemPaneForRow(row))
                    .translated(viewportRect.topLeft());
                const QRect clipped = lineRect.intersected(paneRect);
                if (!clipped.isEmpty())
                    desired.insert(row, clipped);
            }
        }
    }
    for (auto it = m_rowGridLines.begin(); it != m_rowGridLines.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->hide();
        m_rowGridLinePool.append(it.value());
        it = m_rowGridLines.erase(it);
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        QWidget *line = m_rowGridLines.value(it.key(), nullptr);
        if (!line) {
            line = m_rowGridLinePool.isEmpty() ? new QWidget(this) : m_rowGridLinePool.takeLast();
            line->setObjectName(QStringLiteral("vivTreeRowGridLine"));
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setAutoFillBackground(true);
            m_rowGridLines.insert(it.key(), line);
        }
        QPalette colors = line->palette();
        colors.setColor(QPalette::Window, itemPaneSeparatorColor());
        line->setPalette(colors);
        line->setGeometry(it.value());
        line->clearMask();
        line->show();
        line->raise();
    }
}

void VirtualTreeView::invalidateBranchIndicators()
{
    if (!m_branchIndicatorsVisible) {
        // Erase the indicators that are already painted before giving up the
        // strip.
        if (m_indicatorStripWidth > 0) {
            viewport()->update(QRect(0, 0, m_indicatorStripWidth, viewport()->height()));
            m_indicatorStripWidth = 0;
        }
        return;
    }

    int width = 0;
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        // The built-in indicator sits in the row's own cell; a renderer may also
        // decorate the ancestor cells, which reach (depth + 1) * indentation.
        const int depth = itemDepth(item.index);
        width = qMax(width, (depth + 1) * m_indentation);
        if (!m_branchRenderer)
            width = qMax(width, branchIndicatorX(row) + kBranchIndicatorSize + 1);
    }
    // The strip invalidated by the previous pass is part of the region too: an
    // indicator that left the viewport must be erased where it was painted.
    const int strip = qMax(width, m_indicatorStripWidth);
    m_indicatorStripWidth = width;
    if (strip > 0)
        viewport()->update(QRect(0, 0, strip, viewport()->height()));
}

void VirtualTreeView::paintEvent(QPaintEvent *event)
{
    VirtualItemView::paintEvent(event);
    QPainter painter(viewport());
    if (m_visualStateBackgroundVisible)
        paintVisualStateBackgrounds(&painter, event->rect());
    if (m_branchIndicatorsVisible)
        paintBranchIndicators(&painter);
}

void VirtualTreeView::paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty)
{
    const QRect viewportRect = viewport()->rect();
    for (const MaterializedItem &item : materializedItems()) {
        const QRect rowRect(0, item.geometry.y(), viewportRect.width(), item.geometry.height());
        if (!item.index.isValid() || !rowRect.intersects(dirty))
            continue;
        const VisualState state = visualState(QModelIndex(item.index));
        if (state.hoverProgress <= 0.0 && state.selectedProgress <= 0.0)
            continue;
        const int depth = itemDepth(item.index);
        const qint64 cells = m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeOnly
            ? qint64(depth) + 1
            : m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeAndIcon
                ? qint64(depth) : 0;
        const int left = int(qMin<qint64>(viewportRect.width(),
                                         qMax<qint64>(0, cells) * m_indentation));
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        const QRect background = QRect(left, item.geometry.y(),
                                       viewportRect.width() - left, item.geometry.height())
            .intersected(itemPaneRect(itemPaneForRow(row))).intersected(viewportRect);
        if (background.isEmpty())
            continue;
        painter->save();
        painter->setOpacity(state.hoverProgress);
        painter->fillRect(background, hoverBackgroundColor());
        painter->setOpacity(state.selectedProgress);
        painter->fillRect(background, selectedBackgroundColor());
        painter->restore();
    }
}

void VirtualTreeView::paintBranchIndicators(QPainter *painter)
{
    const QRect viewportRect = viewport()->rect();
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;

        const QRect rowRect = VirtualItemView::geometryForViewRow(row);
        if (rowRect.height() <= 0 || !rowRect.intersects(viewportRect))
            continue;

        const QModelIndex index = item.index;
        const int depth = itemDepth(index);
        if (!m_branchRenderer) {
            if (hasBranchIndicator(row))
                BranchIndicatorRenderer::paintBuiltinBranch(
                    painter, branchState(index, depth), branchCellRect(row, depth), palette());
            continue;
        }

        // A custom renderer owns the whole decoration, so it also receives the
        // cells of the ancestors of this row (the connector lines).
        for (int cellDepth = 0; cellDepth <= depth; ++cellDepth) {
            const QRect cellRect = branchCellRect(row, cellDepth);
            if (cellRect.isEmpty() || !cellRect.intersects(viewportRect))
                continue;
            painter->save();
            m_branchRenderer->paintBranch(painter, branchState(index, cellDepth), index, cellRect);
            painter->restore();
        }
    }
}

void VirtualTreeView::toggleIfBranchClicked(const QPoint &viewportPos, bool *handled)
{
    if (handled)
        *handled = false;

    const QModelIndex index = indexAt(viewportPos);
    if (!isIndicatorPosition(index, viewportPos))
        return;

    toggleExpanded(index);
    if (handled)
        *handled = true;
}

void VirtualTreeView::mousePressEvent(QMouseEvent *event)
{
    // A new gesture: the previous indicator toggle (if any) no longer applies.
    m_indicatorPressToggled = false;
    if (event->button() == Qt::LeftButton) {
        bool handled = false;
        toggleIfBranchClicked(eventPosition(event), &handled);
        if (handled) {
            m_indicatorPressToggled = true;
            event->accept();
            return;
        }
    }
    VirtualItemView::mousePressEvent(event);
}

void VirtualTreeView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_indicatorPressToggled && event->button() == Qt::LeftButton) {
        // The press toggled a branch: this gesture is not a click on the row
        // (no clicked()/selection), and the flag stays set for the double click
        // event that follows a double click on the indicator.
        event->accept();
        return;
    }
    VirtualItemView::mouseReleaseEvent(event);
}

void VirtualTreeView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint viewportPos = eventPosition(event);
        const QModelIndex index = indexAt(viewportPos);
        // A double click on the indicator toggles once: the press of this very
        // gesture already did it.
        const bool pressToggledThisBranch = m_indicatorPressToggled
            && isIndicatorPosition(index, viewportPos);
        m_indicatorPressToggled = false;
        if (!pressToggledThisBranch && index.isValid() && hasChildren(index))
            toggleExpanded(index);
    }
    VirtualItemView::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
// Drag & drop (§38): drop between siblings or into an item
// ---------------------------------------------------------------------------

bool VirtualTreeView::acceptsDropInto(const QModelIndex &index) const
{
    QAbstractItemModel *targetModel = model();
    return targetModel && index.isValid()
        && (targetModel->flags(index) & Qt::ItemIsDropEnabled);
}

VirtualItemView::DropTarget VirtualTreeView::resolveDropTarget(const QPoint &viewportPos) const
{
    DropTarget target;
    QAbstractItemModel *treeModel = model();
    if (!treeModel)
        return target;

    // The rows of the tree are the children of the root: the empty area below
    // them (and an empty tree) extends that level instead of being "outside the
    // content", which is what the flat kernel would report.
    const qsizetype rows = viewItemCount();
    const QRect lastRect = rows > 0 ? geometryForViewRow(rows - 1) : QRect();
    if (rows <= 0 || viewportPos.y() > lastRect.bottom()) {
        target.parent = m_rootIndex.isValid() ? QModelIndex(m_rootIndex) : QModelIndex();
        target.row = treeModel->rowCount(target.parent);
        target.trailing = true;
        return target;
    }

    const QModelIndex index = indexAt(viewportPos);
    if (!index.isValid())
        return target;
    const qsizetype row = viewItemForIndex(index);
    if (row < 0)
        return target;

    // Three bands per row, like QTreeView: the top/bottom quarter inserts a
    // sibling, the middle drops into the item (only if the model takes it).
    const QRect rowRect = geometryForViewRow(row);
    const int y = qBound(rowRect.top(), viewportPos.y(), rowRect.bottom());
    const int band = qMax(3, rowRect.height() / 4);
    const bool above = y < rowRect.top() + band;
    const bool below = y > rowRect.bottom() - band;
    if (!above && !below && acceptsDropInto(index)) {
        target.parent = index;
        target.row = treeModel->rowCount(index);
        target.ontoItem = true;
        return target;
    }
    const bool insertAbove = above || (!below && y < rowRect.center().y());
    target.parent = index.parent();
    target.row = index.row() + (insertAbove ? 0 : 1);
    return target;
}

QRect VirtualTreeView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || !model())
        return QRect();
    if (target.ontoItem) {
        const qsizetype row = viewItemForIndex(target.parent);
        return row >= 0 ? geometryForViewRow(row) : QRect();
    }
    return insertionLineRect(target);
}

QRect VirtualTreeView::insertionLineRect(const DropTarget &target) const
{
    QAbstractItemModel *treeModel = model();
    if (!treeModel)
        return QRect();
    const QModelIndex parent = target.parent;
    const int row = target.row;
    const int rows = int(viewItemCount());
    // The empty area below the tree: the line marks the end of the content, which
    // is the only row boundary that area can borrow.
    if (target.trailing) {
        if (rows <= 0)
            return QRect(0, 0, viewport()->width(), 2);
        const QRect last = geometryForViewRow(rows - 1);
        return QRect(0, last.bottom(), viewport()->width(), 2);
    }
    // A row boundary of the visible tree: below the row that precedes the
    // insertion point (the hovered row itself when the drop is "after" it), or
    // above the row that follows it.
    if (row > 0) {
        const qsizetype before = viewItemForIndex(treeModel->index(row - 1, 0, parent));
        if (before >= 0) {
            const QRect rect = geometryForViewRow(before);
            return QRect(0, rect.bottom(), viewport()->width(), 2);
        }
    }
    const qsizetype after = viewItemForIndex(treeModel->index(row, 0, parent));
    if (after >= 0) {
        const QRect rect = geometryForViewRow(after);
        return QRect(0, rect.top() - 1, viewport()->width(), 2);
    }
    // Inside a collapsed branch the insertion is drawn directly below it.
    const qsizetype parentRow = viewItemForIndex(parent);
    if (parentRow >= 0) {
        const QRect rect = geometryForViewRow(parentRow);
        return QRect(0, rect.bottom(), viewport()->width(), 2);
    }
    if (rows > 0) {
        const QRect rect = geometryForViewRow(0);
        return QRect(0, rect.top(), viewport()->width(), 2);
    }
    return QRect(0, 0, viewport()->width(), 2);
}

// ---------------------------------------------------------------------------
// Keyboard navigation
// ---------------------------------------------------------------------------

bool VirtualTreeView::handleItemKeyPress(QKeyEvent *event)
{
    const QModelIndex current = currentIndex();
    if (!current.isValid())
        return false;

    switch (event->key()) {
    case Qt::Key_Right:
        if (hasChildren(current) && !isExpanded(current)) {
            // Collapsed branch: expand it.
            expand(current);
        } else if (hasChildren(current)) {
            // Expanded branch: move to the first child.
            const QModelIndex child = model()->index(0, 0, current);
            const qsizetype row = viewItemForIndex(child);
            if (row >= 0)
                moveCurrentToItem(row);
        }
        return true;
    case Qt::Key_Left:
        if (hasChildren(current) && isExpanded(current)) {
            // Expanded branch: collapse it.
            collapse(current);
        } else {
            // Otherwise move to the parent.
            const QModelIndex parent = current.parent();
            const qsizetype row = viewItemForIndex(parent);
            if (row >= 0)
                moveCurrentToItem(row);
        }
        return true;
    case Qt::Key_Asterisk: // expand the item and its descendants (QTreeView)
        if (hasChildren(current))
            expandRecursively(current);
        return true;
    default:
        break;
    }
    return false;
}

} // namespace viv
