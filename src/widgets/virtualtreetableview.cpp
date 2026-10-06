#include <virtualitemviews/virtualtreetableview.h>
#include "treeexpansiontransition_p.h"
#include "nodebackgroundspacing_p.h"
#include "../core/pixelalignedlines_p.h"

#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/labelheaderview.h>
#include <virtualitemviews/headergeometry.h>

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QRegion>
#include <QSet>

#include <climits>
#include <limits>
#include <functional>

namespace viv {
namespace {

QPoint mousePosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QModelIndex ancestorAt(const QModelIndex &index, int levels)
{
    QModelIndex current = index;
    for (int i = 0; i < levels && current.isValid(); ++i)
        current = current.parent();
    return current;
}

class BranchOverlay : public QWidget
{
public:
    explicit BranchOverlay(std::function<void(QPainter *)> paint, QWidget *parent)
        : QWidget(parent), m_paint(std::move(paint))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        m_paint(&painter);
    }

private:
    std::function<void(QPainter *)> m_paint;
};

class VisibleRowHeaderAdapter : public LabelHeaderAdapter
{
protected:
    QString labelText(int logicalIndex, Qt::Orientation orientation) const override
    {
        if (orientation == Qt::Vertical)
            return QString::number(logicalIndex + 1);
        return LabelHeaderAdapter::labelText(logicalIndex, orientation);
    }
};

} // namespace

VirtualTreeTableView::VirtualTreeTableView(QWidget *parent)
    : VirtualTableView(parent), m_visibility(new TreeVisibilityIndex)
{
    const auto animationGeometry = [this](const QModelIndex &index) {
        const qsizetype row = viewItemForIndex(index);
        if (row < 0)
            return QRect();
        const QRect geometry = geometryForViewRow(row);
        const auto *layout = dynamic_cast<ListLayout *>(layoutPolicy());
        return QRect(0, viewport()->geometry().top() + geometry.top(), width(),
                     geometry.height() + (layout ? layout->spacingAfter(row) : 0));
    };
    m_expansionTransition = new TreeExpansionTransition(this, [this, animationGeometry]() {
        QVector<TreeExpansionTransition::Row> rows;
        // Frozen row boundaries use the fade fallback instead of crossing pane clips.
        if (frozenRows() || frozenBottomRows())
            return rows;
        const VisibleRange range = visibleItemRange();
        if (range.isValid()) {
            for (qsizetype row = range.first; row <= range.last; ++row) {
                const QModelIndex index = viewIndex(row);
                rows.append({QPersistentModelIndex(index), animationGeometry(index),
                             geometryForViewRow(row).height()});
            }
        }
        return rows;
    }, animationGeometry);
    auto *rowHeader = new LabelHeaderView(Qt::Vertical);
    rowHeader->setAdapter(new VisibleRowHeaderAdapter, true);
    setVerticalHeader(rowHeader);
    m_branchOverlay = new BranchOverlay([this](QPainter *painter) { paintBranches(painter); }, viewport());
    m_branchOverlay->setGeometry(viewport()->rect());
    m_branchOverlay->show();
    connect(this, &VirtualTableView::horizontalOffsetChanged, this,
            [this](qint64) { m_expansionTransition->stop(); invalidateBranches(); });
    connect(this, &VirtualTableView::columnGeometryChanged, this,
            [this]() { m_expansionTransition->stop(); invalidateBranches(); });
}

VirtualTreeTableView::~VirtualTreeTableView()
{
    recycleAllCells();
    recycleAllItems();
    delete m_branchOverlay;
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    delete m_visibility;
}

void VirtualTreeTableView::setModel(QAbstractItemModel *treeModel)
{
    if (model() == treeModel)
        return;
    m_expansionTransition->stop();
    const QPointer<QAbstractItemModel> requestedModel(treeModel);
    const quint64 rootSerial = ++m_rootChangeSerial;
    ++m_mappingSerial;
    const quint64 previousChangeSerial = modelChangeSerial();
    recycleAllCells();
    if (modelChangeSerial() != previousChangeSerial || m_rootChangeSerial != rootSerial)
        return;
    recycleAllItems();
    if (modelChangeSerial() != previousChangeSerial || m_rootChangeSerial != rootSerial)
        return;
    treeModel = requestedModel.data();
    m_columnSelectionSnapshot.clear();
    m_columnCurrentNode = QModelIndex();
    m_columnSelectionModel = nullptr;
    m_columnIdentityChangePending = false;
    m_columnHadCurrent = false;
    m_columnMappingPending = false;
    m_rootIndex = QModelIndex();
    m_visibility->setModel(treeModel);
    VirtualTableView::setModel(treeModel);
    if (modelChangeSerial() != previousChangeSerial + 1)
        return;
    m_expansionTransition->watchModel(model());
    connectTreeSignals(model());
    invalidateBranches();
    emit visibleRowsChanged();
}

void VirtualTreeTableView::connectTreeSignals(QAbstractItemModel *treeModel)
{
    if (!treeModel)
        return;
    connect(treeModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                if (!roles.isEmpty() && !roles.contains(NodeRowSpacingBelowRole)
                    && !roles.contains(NodeRowSpacingAboveRole))
                    return;
                if (auto *layout = dynamic_cast<ListLayout *>(layoutPolicy())) {
                    layout->setItemSpacing(rowSpacing());
                    applyRowSpacingOverrides();
                }
                relayout();
            });
    connect(treeModel, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex &, int, int) { setPendingAnchor(captureAnchor()); });
    connect(treeModel, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &parent, int first, int last) {
                setPendingAnchor(captureAnchor());
                recycleItemsInModelRange(parent, first, last);
            });
    connect(treeModel, &QAbstractItemModel::rowsAboutToBeMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                setPendingAnchor(captureAnchor());
            });
    const QPointer<QAbstractItemModel> watchedModel(treeModel);
    const auto columnsChanging = [this, watchedModel](const QModelIndex &parent, int first, int) {
        const quint64 modelSerial = modelChangeSerial();
        const quint64 rootSerial = m_rootChangeSerial;
        const auto current = [this, watchedModel, modelSerial, rootSerial]() {
            return watchedModel && model() == watchedModel.data()
                && modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial;
        };
        if (!current())
            return;
        if (first == 0) {
            captureColumnIdentitySelection();
            if (!current())
                return;
            clearPinsForColumnIdentityChange(parent);
            if (!current())
                return;
        }
        recycleAllItems();
        if (!current())
            return;
        recycleAllCells();
        if (!current())
            return;
        m_columnMappingPending = true;
        ++m_mappingSerial;
    };
    connect(treeModel, &QAbstractItemModel::columnsAboutToBeInserted, this, columnsChanging);
    connect(treeModel, &QAbstractItemModel::columnsAboutToBeRemoved, this, columnsChanging);
    connect(treeModel, &QAbstractItemModel::columnsAboutToBeMoved, this,
            [this, watchedModel](const QModelIndex &sourceParent, int start, int,
                   const QModelIndex &destinationParent,
                   int destinationColumn) {
                const quint64 modelSerial = modelChangeSerial();
                const quint64 rootSerial = m_rootChangeSerial;
                const auto current = [this, watchedModel, modelSerial, rootSerial]() {
                    return watchedModel && model() == watchedModel.data()
                        && modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial;
                };
                if (!current())
                    return;
                if (start == 0 || destinationColumn == 0) {
                    captureColumnIdentitySelection();
                    if (!current())
                        return;
                    if (start == 0)
                        clearPinsForColumnIdentityChange(sourceParent);
                    if (!current())
                        return;
                    if (destinationColumn == 0
                        && (start != 0 || destinationParent != sourceParent))
                        clearPinsForColumnIdentityChange(destinationParent);
                    if (!current())
                        return;
                }
                recycleAllItems();
                if (!current())
                    return;
                recycleAllCells();
                if (!current())
                    return;
                m_columnMappingPending = true;
                ++m_mappingSerial;
            });
    connect(treeModel, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { onStructureChanged(true); });
    connect(treeModel, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { onStructureChanged(true); });
    connect(treeModel, &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                onStructureChanged(true);
            });
    connect(treeModel, &QAbstractItemModel::layoutChanged, this,
            [this](const QList<QPersistentModelIndex> &, QAbstractItemModel::LayoutChangeHint) {
                onStructureChanged(true);
            });
    connect(treeModel, &QAbstractItemModel::modelReset, this, [this]() {
        const quint64 mappingSerial = ++m_mappingSerial;
        const quint64 modelSerial = modelChangeSerial();
        const quint64 rootSerial = m_rootChangeSerial;
        const QPointer<QAbstractItemModel> activeModel(model());
        m_columnSelectionSnapshot.clear();
        m_columnCurrentNode = QModelIndex();
        m_columnSelectionModel = nullptr;
        m_columnIdentityChangePending = false;
        m_columnHadCurrent = false;
        m_columnMappingPending = false;
        m_rootIndex = QModelIndex();
        rekeyPersistentTableState();
        if (!activeModel || model() != activeModel.data()
            || modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial
            || m_mappingSerial != mappingSerial)
            return;
        m_visibility->handleModelReset();
        if (!activeModel || model() != activeModel.data()
            || modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial
            || m_mappingSerial != mappingSerial)
            return;
        refreshVisibility(true);
    });
    const auto columnsChanged = [this, watchedModel]() {
        if (!watchedModel || model() != watchedModel.data())
            return;
        const quint64 modelSerial = modelChangeSerial();
        const quint64 rootSerial = m_rootChangeSerial;
        const QPointer<QAbstractItemModel> activeModel(model());
        onStructureChanged(true, true);
        if (m_columnMappingPending) {
            m_columnMappingPending = false;
            if (activeModel && model() == activeModel.data()
                && modelChangeSerial() == modelSerial)
                onStructureChanged(true);
        }
        if (!activeModel || model() != activeModel.data()
            || modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial)
            return;
        reconcileColumnIdentitySelection();
    };
    connect(treeModel, &QAbstractItemModel::columnsInserted, this, columnsChanged);
    connect(treeModel, &QAbstractItemModel::columnsRemoved, this, columnsChanged);
    connect(treeModel, &QAbstractItemModel::columnsMoved, this, columnsChanged);
}

bool VirtualTreeTableView::isNodeWithinRoot(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != model())
        return false;
    if (!m_rootIndex.isValid())
        return true;
    for (QModelIndex node = index.siblingAtColumn(0); node.isValid(); node = node.parent()) {
        if (node == m_rootIndex)
            return true;
    }
    return false;
}

void VirtualTreeTableView::clearStateOutsideRoot()
{
    QVector<QModelIndex> pins;
    for (const QPersistentModelIndex &pinned : explicitPinnedIndexes()) {
        if (!isNodeWithinRoot(pinned))
            pins.append(pinned);
    }
    for (const QModelIndex &pin : pins)
        setItemPinned(pin, false);

    QItemSelectionModel *selection = selectionModel();
    if (!selection)
        return;
    const QModelIndexList selected = selection->selectedIndexes();
    for (const QModelIndex &index : selected) {
        if (!isNodeWithinRoot(index))
            selection->select(index, QItemSelectionModel::Deselect);
    }
    const QModelIndex current = selection->currentIndex();
    if (current.isValid() && !isNodeWithinRoot(current))
        selection->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
}

void VirtualTreeTableView::clearPinsForColumnIdentityChange(const QModelIndex &parent)
{
    QVector<QModelIndex> affected;
    for (const QPersistentModelIndex &pinned : explicitPinnedIndexes()) {
        QModelIndex ancestor = pinned.parent();
        while (ancestor.isValid() && ancestor != parent)
            ancestor = ancestor.parent();
        if (!parent.isValid() || ancestor == parent)
            affected.append(pinned);
    }
    for (const QModelIndex &index : affected)
        setItemPinned(index, false);
}

void VirtualTreeTableView::captureColumnIdentitySelection()
{
    m_columnSelectionSnapshot.clear();
    m_columnCurrentNode = QModelIndex();
    m_columnSelectionModel = selectionModel();
    m_columnIdentityChangePending = true;
    m_columnHadCurrent = false;
    QItemSelectionModel *selection = m_columnSelectionModel.data();
    if (!selection)
        return;
    const QModelIndex current = selection->currentIndex();
    m_columnHadCurrent = current.isValid();
    if (m_columnHadCurrent)
        m_columnCurrentNode = current.siblingAtColumn(0);
    for (const QModelIndex &cell : selection->selectedIndexes())
        m_columnSelectionSnapshot.append({cell, cell.siblingAtColumn(0)});
}

void VirtualTreeTableView::reconcileColumnIdentitySelection()
{
    if (!m_columnIdentityChangePending)
        return;
    QVector<SelectedCellIdentity> snapshot;
    snapshot.swap(m_columnSelectionSnapshot);
    const QPersistentModelIndex currentNode = m_columnCurrentNode;
    const QPointer<QItemSelectionModel> sourceSelection = m_columnSelectionModel;
    const bool hadCurrent = m_columnHadCurrent;
    m_columnIdentityChangePending = false;
    m_columnCurrentNode = QModelIndex();
    m_columnSelectionModel = nullptr;
    m_columnHadCurrent = false;
    QItemSelectionModel *selection = selectionModel() == sourceSelection.data()
        ? sourceSelection.data() : nullptr;
    if (selection) {
        const auto hasStableColumnIdentity = [this](const QModelIndex &index) {
            return isPersistentRowStateValid(index) && index.column() == 0;
        };
        // A row keeps its selection only while its original column-zero node survives.
        QItemSelection staleRows;
        QSet<QModelIndex> seenRows;
        for (const SelectedCellIdentity &entry : snapshot) {
            if (hasStableColumnIdentity(entry.node) || !entry.cell.isValid())
                continue;
            const QModelIndex row = QModelIndex(entry.cell).siblingAtColumn(0);
            const QModelIndex key = row.isValid() ? row : QModelIndex(entry.cell);
            if (!seenRows.contains(key)) {
                seenRows.insert(key);
                staleRows.select(key, key);
            }
        }
        if (!staleRows.isEmpty())
            selection->select(staleRows, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
        if (hadCurrent && !hasStableColumnIdentity(currentNode)
            && sourceSelection && selectionModel() == sourceSelection.data()) {
            sourceSelection->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
        }
    }
}

void VirtualTreeTableView::onStructureChanged(bool resetSizes, bool columnChange)
{
    const quint64 mappingSerial = ++m_mappingSerial;
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const QPointer<QAbstractItemModel> activeModel(model());
    if (!activeModel)
        return;
    const auto requestIsCurrent = [this, modelSerial, rootSerial, mappingSerial,
                                   &activeModel]() {
        return activeModel && model() == activeModel.data()
            && modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial
            && m_mappingSerial == mappingSerial;
    };
    if (m_rootIndex.isValid() && !isPersistentRowStateValid(m_rootIndex))
        m_rootIndex = QModelIndex();
    if (m_visibility->rootIndex() != m_rootIndex)
        m_visibility->setRootIndex(m_rootIndex);
    if (!requestIsCurrent())
        return;
    rekeyPersistentTableState();
    if (!requestIsCurrent())
        return;
    m_visibility->handleModelChanged();
    if (!requestIsCurrent())
        return;
    clearStateOutsideRoot();
    if (!requestIsCurrent())
        return;
    if (columnChange)
        m_columnMappingPending = false;
    horizontalHeaderGeometry()->setSectionCount(columnCount());
    if (!requestIsCurrent())
        return;
    if (currentIndex().isValid()
        && (viewItemForIndex(currentIndex()) < 0 || currentIndex().column() >= columnCount()))
        setCurrentIndex(QModelIndex());
    if (!requestIsCurrent())
        return;
    refreshVisibility(resetSizes);
}

bool VirtualTreeTableView::isPersistentRowStateValid(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != model())
        return false;
    if (index.column() != 0)
        return false;
    for (QModelIndex ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent()) {
        if (ancestor.column() != 0)
            return false;
    }
    return true;
}

void VirtualTreeTableView::refreshVisibility(bool resetSizes)
{
    if (m_updatingVisibility) {
        m_visibilityRefreshPending = true;
        return;
    }
    if (relayoutActive()) {
        if (!m_visibilityRefreshQueued) {
            m_visibilityRefreshQueued = true;
            QMetaObject::invokeMethod(this, [this]() {
                m_visibilityRefreshQueued = false;
                refreshVisibility(true);
            }, Qt::QueuedConnection);
        }
        return;
    }
    m_updatingVisibility = true;
    const QPointer<VirtualTreeTableView> self(this);
    for (;;) {
        m_visibilityRefreshPending = false;
        const quint64 modelSerial = modelChangeSerial();
        const quint64 rootSerial = m_rootChangeSerial;
        const quint64 mappingSerial = m_mappingSerial;
        const QPointer<QAbstractItemModel> activeModel(model());
        const bool hadModel = activeModel;
        const auto requestIsCurrent = [self, modelSerial, rootSerial, mappingSerial,
                                       &activeModel, hadModel]() {
            return self && self->modelChangeSerial() == modelSerial
                && self->m_rootChangeSerial == rootSerial
                && self->m_mappingSerial == mappingSerial
                && self->model() == activeModel.data() && (!hadModel || activeModel);
        };
        if (resetSizes) {
            verticalHeaderGeometry()->clearExplicitSectionSizes();
            if (!requestIsCurrent() || m_visibilityRefreshPending)
                continue;
            resetLayoutForNewModel();
        }
        if (!requestIsCurrent() || m_visibilityRefreshPending) {
            resetSizes = true;
            continue;
        }
        if (resetSizes)
            reapplyExplicitRowHeights();
        if (!requestIsCurrent() || m_visibilityRefreshPending) {
            resetSizes = true;
            continue;
        }
        relayout();
        if (!self)
            return;
        if (!requestIsCurrent() || m_visibilityRefreshPending) {
            resetSizes = true;
            continue;
        }
        refreshRowHeaderLabels();
        if (!requestIsCurrent() || m_visibilityRefreshPending) {
            resetSizes = true;
            continue;
        }
        invalidateBranches();
        if (!requestIsCurrent() || m_visibilityRefreshPending) {
            resetSizes = true;
            continue;
        }
        break;
    }
    m_updatingVisibility = false;
    emit visibleRowsChanged();
}

void VirtualTreeTableView::refreshVisibilitySplice(qsizetype first, qsizetype removed,
                                                   qsizetype inserted, qsizetype previousCount)
{
    auto *layout = dynamic_cast<ListLayout *>(layoutPolicy());
    if (!layout || layout->itemCount() != previousCount || first < 0
        || first > previousCount || removed < 0 || inserted < 0
        || removed > previousCount - first
        || previousCount - removed + inserted != viewItemCount()) {
        refreshVisibility(true);
        return;
    }
    if (removed > 0)
        layout->removeItems(first, removed);
    if (inserted > 0)
        layout->insertItems(first, inserted, estimateItemSize(first));
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = m_mappingSerial;
    HeaderGeometry *header = verticalHeaderGeometry();
    if (header->sectionCount() == previousCount) {
        if (removed > 0)
            header->removeLogicalSections(int(first), int(removed));
        if (modelChangeSerial() != modelSerial || m_mappingSerial != mappingSerial)
            return;
        if (inserted > 0)
            header->insertLogicalSections(int(first), int(inserted));
        if (modelChangeSerial() != modelSerial || m_mappingSerial != mappingSerial)
            return;
    }
    // The boundary before the splice also changes its next node's above gap.
    QVector<QPair<int, int>> spacings;
    spacings.reserve(m_headerSpacingOverrides.size() + int(inserted));
    const qsizetype firstGap = qMax(qsizetype(0), first - 1);
    for (const auto &entry : m_headerSpacingOverrides) {
        if (entry.first >= firstGap)
            break;
        spacings.append(entry);
    }
    m_rowHeaderSpacingDirty = true;
    for (qsizetype row = firstGap; row < first + inserted; ++row) {
        const int spacing = effectiveRowSpacing(row);
        layout->setSpacingAfter(row, spacing);
        if (spacing == rowSpacing())
            continue;
        spacings.append(qMakePair(int(row), spacing));
    }
    const qsizetype afterRemoved = first + removed;
    const qsizetype shift = inserted - removed;
    for (const auto &entry : m_headerSpacingOverrides) {
        if (entry.first >= afterRemoved)
            spacings.append(qMakePair(int(entry.first + shift), entry.second));
    }
    m_headerSpacingOverrides.swap(spacings);
    if (inserted > 0)
        reapplyExplicitRowHeightsInRange(first, first + inserted - 1);
    refreshVisibility(false);
}

QModelIndex VirtualTreeTableView::nodeIndex(const QModelIndex &index) const
{
    return index.isValid() && index.model() == model() ? index.siblingAtColumn(0)
                                                     : QModelIndex();
}

void VirtualTreeTableView::setRootIndex(const QModelIndex &index)
{
    m_expansionTransition->stop();
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualTreeTableView::setRootIndex(): index belongs to another model");
        return;
    }
    const QPersistentModelIndex root(nodeIndex(index));
    if (root.isValid() && !isPersistentRowStateValid(root)) {
        qWarning("VirtualTreeTableView::setRootIndex(): node is outside column-zero hierarchy");
        return;
    }
    if (root == m_rootIndex)
        return;
    const quint64 changeSerial = ++m_rootChangeSerial;
    ++m_mappingSerial;
    const quint64 modelSerial = modelChangeSerial();
    const bool requestedNode = root.isValid();
    const QPointer<VirtualTreeTableView> self(this);
    const auto requestIsCurrent = [self, changeSerial, modelSerial, requestedNode, &root]() {
        return self && self->m_rootChangeSerial == changeSerial
            && self->modelChangeSerial() == modelSerial
            && (!requestedNode || self->isPersistentRowStateValid(root));
    };
    recycleAllCells();
    if (!requestIsCurrent())
        return;
    recycleAllItems();
    if (!requestIsCurrent())
        return;
    m_rootIndex = root;
    m_visibility->setRootIndex(root);
    if (!requestIsCurrent())
        return;
    clearStateOutsideRoot();
    if (!requestIsCurrent())
        return;
    if (currentIndex().isValid()
        && (viewItemForIndex(currentIndex()) < 0 || currentIndex().column() >= columnCount()))
        setCurrentIndex(QModelIndex());
    if (!requestIsCurrent())
        return;
    horizontalHeaderGeometry()->setSectionCount(columnCount());
    if (!requestIsCurrent())
        return;
    refreshVisibility(true);
}

QModelIndex VirtualTreeTableView::columnSchemaParent() const
{
    return m_rootIndex;
}

int VirtualTreeTableView::columnCount() const
{
    return model() ? model()->columnCount(m_rootIndex) : 0;
}

qsizetype VirtualTreeTableView::viewItemCount() const
{
    return m_visibility->visibleRowCount();
}

QModelIndex VirtualTreeTableView::viewIndex(qsizetype item, int column) const
{
    if (m_columnMappingPending)
        return QModelIndex();
    const QModelIndex node = m_visibility->indexAtVisibleRow(item);
    if (!node.isValid() || column < 0 || column >= columnCount()
        || column >= model()->columnCount(node.parent()))
        return QModelIndex();
    return column == 0 ? node : node.siblingAtColumn(column);
}

qsizetype VirtualTreeTableView::viewItemForIndex(const QModelIndex &index) const
{
    if (m_columnMappingPending || !index.isValid() || index.model() != model()
        || index.column() < 0 || index.column() >= columnCount()
        || index.column() >= model()->columnCount(index.parent()))
        return -1;
    return m_visibility->visibleRowForIndex(index);
}

bool VirtualTreeTableView::isLayoutParent(const QModelIndex &) const
{
    return false;
}

int VirtualTreeTableView::itemDepth(const QModelIndex &index) const
{
    return qMax(0, m_visibility->depth(index));
}

QModelIndex VirtualTreeTableView::indexForNavigation(qsizetype item,
                                                      const QModelIndex &current) const
{
    const QModelIndex node = viewIndex(item);
    if (!node.isValid() || !current.isValid())
        return node;
    const int last = qMin(columnCount(), model()->columnCount(node.parent())) - 1;
    const int column = qBound(0, current.column(), qMax(0, last));
    if (!isColumnHidden(column))
        return node.siblingAtColumn(column);
    for (int candidate = column - 1; candidate >= 0; --candidate) {
        if (!isColumnHidden(candidate))
            return node.siblingAtColumn(candidate);
    }
    for (int candidate = column + 1; candidate <= last; ++candidate) {
        if (!isColumnHidden(candidate))
            return node.siblingAtColumn(candidate);
    }
    return node.siblingAtColumn(column);
}

QItemSelection VirtualTreeTableView::selectionRange(const QModelIndex &anchor,
                                                     const QModelIndex &target) const
{
    const qsizetype anchorRow = viewItemForIndex(anchor);
    const qsizetype targetRow = viewItemForIndex(target);
    if (anchorRow < 0 || targetRow < 0)
        return VirtualTableView::selectionRange(anchor, target);

    const bool wholeRows = selectionBehavior() == SelectionBehavior::SelectRows;
    const int firstColumn = wholeRows ? 0 : qMin(anchor.column(), target.column());
    const int lastColumn = wholeRows ? columnCount() - 1 : qMax(anchor.column(), target.column());
    QItemSelection selection;
    QModelIndex runFirst;
    QModelIndex runLast;
    int runFirstColumn = -1;
    int runLastColumn = -1;
    const auto flush = [&]() {
        if (runFirst.isValid())
            selection.select(runFirst.siblingAtColumn(runFirstColumn),
                             runLast.siblingAtColumn(runLastColumn));
        runFirst = QModelIndex();
    };

    for (qsizetype row = qMin(anchorRow, targetRow); row <= qMax(anchorRow, targetRow); ++row) {
        const QModelIndex node = viewIndex(row);
        const int available = node.isValid()
            ? qMin(columnCount(), model()->columnCount(node.parent())) : 0;
        if (available <= firstColumn) {
            flush();
            continue;
        }
        const int endColumn = qMin(lastColumn, available - 1);
        if (runFirst.isValid() && runLast.parent() == node.parent()
            && runLast.row() + 1 == node.row()
            && runFirstColumn == firstColumn && runLastColumn == endColumn) {
            runLast = node;
            continue;
        }
        flush();
        runFirst = node;
        runLast = node;
        runFirstColumn = firstColumn;
        runLastColumn = endColumn;
    }
    flush();
    return selection;
}

qsizetype VirtualTreeTableView::visibleRowCount() const
{
    return m_visibility->visibleRowCount();
}

bool VirtualTreeTableView::hasChildren(const QModelIndex &index) const
{
    const QModelIndex node = nodeIndex(index);
    return node.isValid() && model()->hasChildren(node);
}

void VirtualTreeTableView::setExpansionAnimationEnabled(bool enabled)
{
    m_expansionTransition->setEnabled(enabled);
}

bool VirtualTreeTableView::expansionAnimationEnabled() const
{
    return m_expansionTransition->isEnabled();
}

void VirtualTreeTableView::setExpansionAnimationDuration(int milliseconds)
{
    m_expansionTransition->setDuration(milliseconds);
}

int VirtualTreeTableView::expansionAnimationDuration() const
{
    return m_expansionTransition->duration();
}

void VirtualTreeTableView::expand(const QModelIndex &index)
{
    const QPersistentModelIndex node(nodeIndex(index));
    if (!node.isValid() || viewItemForIndex(node) < 0
        || !hasChildren(node) || isExpanded(node))
        return;
    const QPersistentModelIndex expandedNode(node);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    const auto before = m_expansionTransition->capture();
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    setPendingAnchor(captureAnchor());
    const qsizetype first = viewItemForIndex(node) + 1;
    const qsizetype previousCount = viewItemCount();
    const quint64 mappingSerial = ++m_mappingSerial;
    m_visibility->expand(node);
    if (modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial
        || m_mappingSerial != mappingSerial)
        return;
    refreshVisibilitySplice(first, 0, viewItemCount() - previousCount, previousCount);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot) {
        m_expansionTransition->start(before);
        if (!animationGuard)
            return;
    }
    if (modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial
        && expandedNode.isValid() && isExpanded(expandedNode))
        emit expanded(expandedNode);
}

void VirtualTreeTableView::collapse(const QModelIndex &index)
{
    const QPersistentModelIndex node(nodeIndex(index));
    if (!node.isValid() || !isExpanded(node))
        return;
    const QPersistentModelIndex collapsedNode(node);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    const auto before = m_expansionTransition->capture(m_visibility, node);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    setPendingAnchor(captureAnchor());
    const qsizetype row = viewItemForIndex(node);
    const qsizetype previousCount = viewItemCount();
    const quint64 mappingSerial = ++m_mappingSerial;
    m_visibility->collapse(node);
    if (modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial
        || m_mappingSerial != mappingSerial)
        return;
    if (row >= 0)
        refreshVisibilitySplice(row + 1, previousCount - viewItemCount(), 0, previousCount);
    else
        refreshVisibility(false);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot)
        m_expansionTransition->start(before);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial
        && collapsedNode.isValid() && !isExpanded(collapsedNode))
        emit collapsed(collapsedNode);
}

void VirtualTreeTableView::expandRecursively(const QModelIndex &index)
{
    const QPersistentModelIndex node(nodeIndex(index));
    if (!node.isValid() || !hasChildren(node))
        return;
    const QPersistentModelIndex expandedNode(node);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const bool wasExpanded = isExpanded(node);
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    const auto before = m_expansionTransition->capture();
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    setPendingAnchor(captureAnchor());
    const quint64 mappingSerial = ++m_mappingSerial;
    m_visibility->expandRecursively(node);
    if (modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial
        || m_mappingSerial != mappingSerial)
        return;
    refreshVisibility(true);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot)
        m_expansionTransition->start(before);
    if (!animationGuard)
        return;
    if (!wasExpanded && modelChangeSerial() == modelSerial
        && m_rootChangeSerial == rootSerial && expandedNode.isValid()
        && isExpanded(expandedNode))
        emit expanded(expandedNode);
}

void VirtualTreeTableView::collapseAll()
{
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    const auto before = m_expansionTransition->capture(m_visibility);
    if (!animationGuard || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    setPendingAnchor(captureAnchor());
    ++m_mappingSerial;
    m_visibility->collapseAll();
    refreshVisibility(true);
    if (!animationGuard)
        return;
    if (modelChangeSerial() == animationSerial && m_rootIndex == animationRoot)
        m_expansionTransition->start(before);
}

void VirtualTreeTableView::setExpanded(const QModelIndex &index, bool expanded)
{
    expanded ? expand(index) : collapse(index);
}

void VirtualTreeTableView::toggleExpanded(const QModelIndex &index)
{
    setExpanded(index, !isExpanded(index));
}

bool VirtualTreeTableView::isExpanded(const QModelIndex &index) const
{
    return m_visibility->isExpanded(nodeIndex(index));
}

void VirtualTreeTableView::setIndentation(int pixels)
{
    const int width = qMax(0, pixels);
    if (m_indentation == width)
        return;
    m_indentation = width;
    relayout();
    invalidateBranches();
}

int VirtualTreeTableView::effectiveRowSpacing(qsizetype row) const
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

void VirtualTreeTableView::applyRowSpacingOverrides()
{
    m_rowHeaderSpacingDirty = true;
    m_headerSpacingOverrides.clear();
    auto *layout = dynamic_cast<ListLayout *>(layoutPolicy());
    if (layout) {
        for (qsizetype row = 0; row < layout->itemCount(); ++row) {
            const int spacing = effectiveRowSpacing(row);
            if (spacing != rowSpacing()) {
                layout->setSpacingAfter(row, spacing);
                m_headerSpacingOverrides.append(qMakePair(int(row), spacing));
            }
        }
    }
    VirtualTableView::applyRowSpacingOverrides();
}

void VirtualTreeTableView::configureRowSpacingWidget(QWidget *widget) const
{
    VirtualTableView::configureRowSpacingWidget(widget);
    const int depth = widget->property("vivSpacingDepth").toInt();
    const QRect excluded = rowGridLineExclusion(depth);
    widget->setProperty("vivSpacingLineLeftInset", 0);
    widget->setProperty("vivSpacingLineSkipX", excluded.x());
    widget->setProperty("vivSpacingLineSkipWidth", excluded.width());
    widget->setProperty("vivFillSpacingBackground", !verticalSpacingLineThroughRowSpacing()
        && (!m_visualStateBackgroundVisible
            || (!m_hoverBackgroundThroughRowSpacing && !m_selectedBackgroundThroughRowSpacing)));
}

void VirtualTreeTableView::configureColumnSpacingWidget(QWidget *widget) const
{
    VirtualTableView::configureColumnSpacingWidget(widget);
    widget->setProperty("vivPaintSpacingStates", false);
    widget->setProperty("vivFillSpacingBackground", !m_visualStateBackgroundVisible
        || (!m_hoverBackgroundThroughColumnSpacing && !m_selectedBackgroundThroughColumnSpacing));
}

void VirtualTreeTableView::setRowGridLineExtent(RowGridLineExtent extent)
{
    if (m_rowGridLineExtent == extent)
        return;
    m_rowGridLineExtent = extent;
    relayout();
}

QRect VirtualTreeTableView::decorationExclusion(int cells) const
{
    if (cells <= 0 || m_indentation <= 0)
        return QRect();
    const ColumnGeometry column = columnGeometry(0);
    const int paneIndex = paneIndexOfColumn(0);
    const QVector<TablePane> currentPanes = panes();
    if (!column.isValid() || column.hidden || paneIndex < 0
        || paneIndex >= currentPanes.size())
        return QRect();
    const int width = int(qMin<qint64>(column.width, qint64(cells) * m_indentation));
    int x = column.viewportX;
    columnVisualX(0, &x);
    return QRect(x, 0, width, qMax(1, horizontalGridLineWidth()))
        .intersected(currentPanes.at(paneIndex).viewportRect)
        .intersected(viewport()->rect());
}

QRect VirtualTreeTableView::rowGridLineExclusion(int depth) const
{
    const int cells = m_rowGridLineExtent == RowGridLineExtent::NodeOnly
        ? int(qMin<qint64>(qint64(depth) + 1, INT_MAX))
        : m_rowGridLineExtent == RowGridLineExtent::NodeAndIcon ? depth : 0;
    return decorationExclusion(cells);
}

void VirtualTreeTableView::setVisualStateBackgroundVisible(bool visible)
{
    if (m_visualStateBackgroundVisible == visible)
        return;
    m_visualStateBackgroundVisible = visible;
    relayout();
    viewport()->update();
}

void VirtualTreeTableView::setVisualStateBackgroundExtent(VisualStateBackgroundExtent extent)
{
    if (m_visualStateBackgroundExtent == extent)
        return;
    m_visualStateBackgroundExtent = extent;
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeTableView::refreshVisualStates()
{
    VirtualTableView::refreshVisualStates();
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}


void VirtualTreeTableView::refreshVisualState(const QModelIndex &index)
{
    VirtualTableView::refreshVisualState(index);
    if (m_visualStateBackgroundVisible && index.isValid()) {
        if (m_hoverBackgroundThroughRowSpacing || m_selectedBackgroundThroughRowSpacing
            || m_hoverBackgroundThroughColumnSpacing || m_selectedBackgroundThroughColumnSpacing) {
            viewport()->update();
            return;
        }
        const QModelIndex anchor = visualStateScope() == VisualStateScope::Cell
            ? anchorIndex(index) : index;
        const QRect rect = visualStateScope() == VisualStateScope::Cell
            ? spanRect(anchor) : visualRect(anchor);
        viewport()->update(rect);
    }
}

void VirtualTreeTableView::paintEvent(QPaintEvent *event)
{
    VirtualItemView::paintEvent(event);
    if (!m_visualStateBackgroundVisible)
        return;
    QPainter painter(viewport());
    paintVisualStateBackgrounds(&painter, event->rect());
}

void VirtualTreeTableView::paintVisualStateBackgrounds(QPainter *painter,
                                                        const QRect &dirty) const
{
    const bool rowScope = visualStateScope() == VisualStateScope::Row;
    const int viewportWidth = viewport()->width();
    const auto cells = [this](int depth) {
        return m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeOnly
            ? int(qMin<qint64>(qint64(depth) + 1, INT_MAX))
            : m_visualStateBackgroundExtent == VisualStateBackgroundExtent::NodeAndIcon ? depth : 0;
    };
    const QVector<int> columns = rowScope ? QVector<int>() : visibleColumnLogicalIndexes();
    const QVector<TablePane> currentPanes = panes();
    const auto *layout = dynamic_cast<const ListLayout *>(layoutPolicy());
    const bool throughSpacing = m_hoverBackgroundThroughRowSpacing || m_selectedBackgroundThroughRowSpacing;
    const auto ownedSpacing = [this, layout](qsizetype row) {
        return nodeBackgroundSpacing(viewIndex(row), row > 0 ? viewIndex(row - 1) : QModelIndex(),
            NodeRowSpacingBelowRole, rowSpacing(), layout && row > 0 ? layout->spacingAfter(row - 1) : 0,
            layout && row + 1 < viewItemCount() ? layout->spacingAfter(row) : 0);
    };
    QRegion columnGaps;
    QHash<int, int> spacingAtColumnEnd;
    int terminalColumn = -1;
    for (const TablePane &pane : currentPanes) {
        if (!pane.logicalColumns.isEmpty())
            terminalColumn = pane.logicalColumns.last();
    }
    for (const TablePane &pane : currentPanes) {
        for (int logical : pane.logicalColumns) {
            if (logical == terminalColumn)
                continue;
            const ColumnGeometry column = columnGeometry(logical);
            int x = column.viewportX;
            columnVisualX(logical, &x);
            spacingAtColumnEnd.insert(x + column.width, columnSpacing());
            columnGaps += QRect(x + column.width, 0, columnSpacing(), viewport()->height())
                .intersected(pane.viewportRect);
        }
    }
    QSet<QModelIndex> paintedCells;
    const auto paintState = [this, painter, rowScope, &columnGaps](const QRegion &clip, const QRect &rect,
                                            const QModelIndex &index, const QPair<int, int> &rowGaps, int columnGap) {
        const VisualState state = visualState(index);
        if (clip.isEmpty() || (state.hoverProgress <= 0.0 && state.selectedProgress <= 0.0))
            return;
        const auto paint = [&](bool throughRow, bool throughColumn, qreal opacity, const QColor &color) {
            const QRect background = rect.adjusted(0, throughRow ? -rowGaps.first : 0,
                throughColumn ? columnGap : 0, throughRow ? rowGaps.second : 0);
            QRegion stateClip = clip;
            stateClip &= background;
            if (rowScope && !throughColumn)
                stateClip -= columnGaps;
            painter->save();
            painter->setOpacity(opacity);
            fillPixelAlignedRegion(painter, stateClip, color);
            painter->restore();
        };
        paint(m_hoverBackgroundThroughRowSpacing, m_hoverBackgroundThroughColumnSpacing,
              state.hoverProgress, hoverBackgroundColor());
        paint(m_selectedBackgroundThroughRowSpacing, m_selectedBackgroundThroughColumnSpacing,
              state.selectedProgress, selectedBackgroundColor());
    };
    for (const VisibleRange &range : visibleItemRanges()) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            const QModelIndex node = viewIndex(row);
            QRect rowRect = geometryForViewRow(row);
            int visualY = rowRect.y();
            if (rowVisualY(row, &visualY))
                rowRect.moveTop(visualY);
            const auto rowGaps = ownedSpacing(row);
            if (!node.isValid() || !rowRect.adjusted(0, throughSpacing ? -rowGaps.first : 0,
                0, throughSpacing ? rowGaps.second : 0).intersects(dirty))
                continue;
            const QRect fullRow(0, rowRect.y(), viewportWidth, rowRect.height());
            const QRect pane = itemPaneRect(itemPaneForRow(row));
            const QRect excluded = decorationExclusion(cells(itemDepth(node)));
            if (rowScope) {
                const QRect expanded = fullRow.adjusted(0, throughSpacing ? -rowGaps.first : 0,
                    0, throughSpacing ? rowGaps.second : 0);
                QRegion clip(expanded.intersected(pane).intersected(viewport()->rect()));
                if (!excluded.isEmpty())
                    clip -= QRect(excluded.x(), expanded.y(), excluded.width(), expanded.height());
                paintState(clip, fullRow, node, rowGaps, 0);
                continue;
            }
            for (int logical : columns) {
                const QModelIndex cell = viewIndex(row, logical);
                if (!cell.isValid())
                    continue;
                const QModelIndex anchor = anchorIndex(cell);
                if (!anchor.isValid() || paintedCells.contains(anchor))
                    continue;
                paintedCells.insert(anchor);
                QRect rect = spanRect(anchor);
                const int columnPane = paneIndexOfColumn(anchor.column());
                const qsizetype anchorRow = viewItemForIndex(anchor);
                if (anchorRow < 0)
                    continue;
                int anchorY = geometryForViewRow(anchorRow).top();
                const int committedY = anchorY;
                if (rowVisualY(anchorRow, &anchorY))
                    rect.translate(0, anchorY - committedY);
                if (rect.isEmpty() || anchorRow < 0
                    || columnPane < 0 || columnPane >= currentPanes.size())
                    continue;
                const TableSpan span = spanAt(anchor);
                const QPair<int, int> cellRowGaps(ownedSpacing(anchorRow).first,
                    ownedSpacing(anchorRow + span.rowSpan - 1).second);
                const int cellColumnGap = spacingAtColumnEnd.value(rect.right() + 1, 0);
                const bool throughColumn = m_hoverBackgroundThroughColumnSpacing || m_selectedBackgroundThroughColumnSpacing;
                const QRect expanded = rect.adjusted(0, throughSpacing ? -cellRowGaps.first : 0,
                    throughColumn ? cellColumnGap : 0, throughSpacing ? cellRowGaps.second : 0);
                if (!expanded.intersects(dirty))
                    continue;
                QRegion clip(expanded.intersected(itemPaneRect(itemPaneForRow(anchorRow)))
                    .intersected(currentPanes.at(columnPane).viewportRect)
                    .intersected(viewport()->rect()));
                if (!excluded.isEmpty() && anchorIndex(viewIndex(row, 0)) == anchor)
                    clip -= QRect(excluded.x(), expanded.y(), excluded.width(), expanded.height());
                paintState(clip, rect, anchor, cellRowGaps, cellColumnGap);
            }
        }
    }
}

void VirtualTreeTableView::setBranchIndicatorsVisible(bool visible)
{
    if (m_branchIndicatorsVisible == visible)
        return;
    m_branchIndicatorsVisible = visible;
    invalidateBranches();
}

void VirtualTreeTableView::setBranchIndicatorRenderer(BranchIndicatorRenderer *renderer,
                                                       bool takeOwnership)
{
    if (m_branchRenderer == renderer) {
        m_ownBranchRenderer = m_ownBranchRenderer || (renderer && takeOwnership);
        return;
    }
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    m_branchRenderer = renderer;
    m_ownBranchRenderer = renderer && takeOwnership;
    invalidateBranches();
}

BranchIndicatorState VirtualTreeTableView::branchState(const QModelIndex &index,
                                                       int cellDepth) const
{
    BranchIndicatorState state;
    const QModelIndex node = nodeIndex(index);
    if (!node.isValid())
        return state;
    state.itemDepth = itemDepth(node);
    state.cellDepth = cellDepth < 0 ? state.itemDepth
                                    : qBound(0, cellDepth, state.itemDepth);
    const QModelIndex cell = ancestorAt(node, state.itemDepth - state.cellDepth);
    if (!cell.isValid())
        return state;
    state.adjoinsItem = state.cellDepth == state.itemDepth;
    state.hasChildren = hasChildren(cell);
    state.isExpanded = isExpanded(cell);
    state.hasSiblings = cell.row() + 1 < model()->rowCount(cell.parent());
    return state;
}

QRect VirtualTreeTableView::branchCellRect(qsizetype row, int cellDepth, bool visual) const
{
    const QModelIndex node = viewIndex(row);
    const ColumnGeometry column = columnGeometry(0);
    QRect item = geometryForViewRow(row);
    int visualY = item.y();
    if (visual && rowVisualY(row, &visualY))
        item.moveTop(visualY);
    const int paneIndex = paneIndexOfColumn(0);
    const QVector<TablePane> currentPanes = panes();
    if (!node.isValid() || !column.isValid() || column.hidden || m_indentation <= 0
        || item.height() <= 0 || paneIndex < 0 || paneIndex >= currentPanes.size())
        return QRect();
    int columnX = column.viewportX;
    columnVisualX(0, &columnX);
    const qint64 x = qint64(columnX) + qint64(cellDepth) * m_indentation;
    const qint64 right = qMin(x + m_indentation,
                             qint64(columnX) + column.width);
    const QRect pane = currentPanes.at(paneIndex).viewportRect;
    const qint64 clippedLeft = qMax<qint64>(x, qMax(0, pane.left()));
    const qint64 clippedRight = qMin<qint64>(right, qMin(viewport()->width(), pane.right() + 1));
    if (clippedRight <= clippedLeft)
        return QRect();
    // Keep the full row height for icon centering, even at a viewport/pane edge.
    return QRect(int(clippedLeft), item.y(), int(clippedRight - clippedLeft), item.height());
}

bool VirtualTreeTableView::isIndicatorPosition(const QModelIndex &index,
                                                const QPoint &position) const
{
    const QModelIndex node = nodeIndex(index);
    const qsizetype row = viewItemForIndex(node);
    return m_branchIndicatorsVisible && hasChildren(node) && row >= 0
        && branchCellRect(row, itemDepth(node))
            .intersected(itemPaneRect(itemPaneForRow(row)))
            .intersected(viewport()->rect()).contains(position);
}

void VirtualTreeTableView::paintBranches(QPainter *painter) const
{
    if (!m_branchIndicatorsVisible || isColumnHidden(0))
        return;
    const int paneIndex = paneIndexOfColumn(0);
    const QVector<TablePane> currentPanes = panes();
    if (paneIndex < 0 || paneIndex >= currentPanes.size())
        return;
    painter->save();
    painter->setClipRect(currentPanes.at(paneIndex).viewportRect);
    for (const VisibleRange &range : visibleItemRanges()) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            const QModelIndex node = viewIndex(row);
            if (!node.isValid())
                continue;
            painter->setClipRegion(QRegion(currentPanes.at(paneIndex).viewportRect)
                .intersected(QRegion(itemPaneRect(itemPaneForRow(row))))
                .intersected(QRegion(viewport()->rect())));
            const int depth = itemDepth(node);
            if (!m_branchRenderer) {
                if (hasChildren(node))
                    BranchIndicatorRenderer::paintBuiltinBranch(
                        painter, branchState(node), branchCellRect(row, depth, true), palette());
                continue;
            }
            QVector<QModelIndex> ancestors(depth + 1);
            QModelIndex ancestor = node;
            for (int cellDepth = depth; cellDepth >= 0 && ancestor.isValid(); --cellDepth) {
                ancestors[cellDepth] = ancestor;
                ancestor = ancestor.parent();
            }
            for (int cellDepth = 0; cellDepth <= depth; ++cellDepth) {
                const QRect rect = branchCellRect(row, cellDepth, true);
                const QModelIndex cell = ancestors.at(cellDepth);
                if (rect.isEmpty() || !cell.isValid())
                    continue;
                BranchIndicatorState state;
                state.itemDepth = depth;
                state.cellDepth = cellDepth;
                state.adjoinsItem = cellDepth == depth;
                state.hasChildren = hasChildren(cell);
                state.isExpanded = isExpanded(cell);
                state.hasSiblings = cell.row() + 1 < model()->rowCount(cell.parent());
                painter->save();
                m_branchRenderer->paintBranch(painter, state, node, rect);
                painter->restore();
            }
        }
    }
    painter->restore();
}

void VirtualTreeTableView::invalidateBranches()
{
    if (m_branchOverlay) {
        m_branchOverlay->setGeometry(viewport()->rect());
        m_branchOverlay->raise();
        m_branchOverlay->update();
    }
}

void VirtualTreeTableView::afterMaterialize()
{
    VirtualTableView::afterMaterialize();
    if (m_rowHeaderSpacingDirty) {
        m_rowHeaderSpacingDirty = false;
        if (HeaderGeometry *header = verticalHeaderGeometry())
            header->setSparseSectionSpacingOverrides(m_headerSpacingOverrides);
    }
    invalidateBranches();
}

void VirtualTreeTableView::visualColumnGeometryChanged()
{
    syncRowGridLines();
    invalidateBranches();
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualTreeTableView::resizeEvent(QResizeEvent *event)
{
    VirtualTableView::resizeEvent(event);
    invalidateBranches();
}

void VirtualTreeTableView::moveRowsForStripDrag(int fromRow, int toRow)
{
    const QModelIndex source = viewIndex(fromRow);
    const QModelIndex destination = viewIndex(toRow);
    if (!source.isValid() || !destination.isValid() || source == destination
        || source.parent() != destination.parent())
        return;
    const QPointer<QAbstractItemModel> moveModel(model());
    const QPersistentModelIndex sourceNode(source);
    const QPersistentModelIndex destinationNode(destination);
    const quint64 rootSerial = m_rootChangeSerial;
    emit rowMoveRequested(fromRow, toRow);
    // Application slots may perform the move themselves or change the model.
    // The original visible row numbers must not be applied to different nodes.
    if (!moveModel || model() != moveModel || m_rootChangeSerial != rootSerial
        || !sourceNode.isValid() || !destinationNode.isValid()
        || sourceNode.row() != source.row()
        || destinationNode.row() != destination.row()
        || sourceNode.parent() != destinationNode.parent())
        return;
    const int destinationChild = destination.row() + (toRow > fromRow ? 1 : 0);
    moveModel->moveRows(sourceNode.parent(), sourceNode.row(), 1,
                        destinationNode.parent(), destinationChild);
}

bool VirtualTreeTableView::isSpanValid(const QModelIndex &anchor,
                                       const TableSpan &span) const
{
    if (!anchor.isValid() || anchor.model() != model()
        || span.rowSpan < 1 || span.columnSpan < 1)
        return false;
    const QModelIndex parent = anchor.parent();
    const qsizetype first = viewItemForIndex(anchor);
    if (first < 0)
        return false;
    const int visual = horizontalHeaderGeometry()->visualIndex(anchor.column());
    const int anchorPane = paneIndexOfColumn(anchor.column());
    if (visual < 0 || anchorPane < 0
        || anchor.column() >= model()->columnCount(parent))
        return false;
    const int parentRows = model()->rowCount(parent);
    if (anchor.row() >= parentRows)
        return false;
    const int lastOffset = qMin(span.rowSpan - 1, parentRows - anchor.row() - 1);
    if (lastOffset == 0)
        return true;
    // Expanded descendants between siblings increase the visible-row distance.
    const QModelIndex last = model()->index(anchor.row() + lastOffset, 0, parent);
    const qsizetype lastRow = viewItemForIndex(last);
    return lastRow == first + lastOffset
        && itemPaneForRow(lastRow) == itemPaneForRow(first);
}

QRect VirtualTreeTableView::spanRowRect(qsizetype row) const
{
    return geometryForViewRow(row);
}

int VirtualTreeTableView::leadingCellInset(const QModelIndex &index) const
{
    if (index.column() != 0)
        return 0;
    const qint64 inset = (qint64(itemDepth(index)) + 1) * m_indentation;
    return int(qBound<qint64>(qint64(0), inset,
                              qint64(qMax(0, columnGeometry(0).width))));
}

bool VirtualTreeTableView::handleItemKeyPress(QKeyEvent *event)
{
    const QModelIndex current = currentIndex();
    const QModelIndex node = nodeIndex(current);
    if (!node.isValid())
        return false;
    if (event->key() == Qt::Key_Asterisk) {
        expandRecursively(node);
        return true;
    }
    const bool treeNavigation = selectionBehavior() == SelectionBehavior::SelectRows
        || (event->modifiers() & Qt::AltModifier);
    if (!treeNavigation)
        return VirtualTableView::handleItemKeyPress(event);
    if (event->key() == Qt::Key_Right) {
        if (hasChildren(node) && !isExpanded(node))
            expand(node);
        else if (hasChildren(node)) {
            const QModelIndex child = model()->index(0, 0, node);
            const qsizetype row = viewItemForIndex(child);
            if (row >= 0)
                moveCurrentToItem(row);
        }
        return true;
    }
    if (event->key() == Qt::Key_Left) {
        if (isExpanded(node))
            collapse(node);
        else {
            const qsizetype row = viewItemForIndex(node.parent());
            if (row >= 0)
                moveCurrentToItem(row);
        }
        return true;
    }
    return VirtualTableView::handleItemKeyPress(event);
}

void VirtualTreeTableView::mousePressEvent(QMouseEvent *event)
{
    m_indicatorPressToggled = false;
    if (event->button() == Qt::LeftButton) {
        const QPoint position = mousePosition(event);
        const QModelIndex index = VirtualItemView::indexAt(position);
        if (isIndicatorPosition(index, position)) {
            const QPointer<VirtualTreeTableView> self(this);
            toggleExpanded(index);
            if (!self)
                return;
            m_indicatorPressToggled = true;
            event->accept();
            return;
        }
    }
    VirtualTableView::mousePressEvent(event);
}

void VirtualTreeTableView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_indicatorPressToggled && event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }
    VirtualTableView::mouseReleaseEvent(event);
}

void VirtualTreeTableView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPointer<VirtualTreeTableView> self(this);
    const quint64 modelSerial = modelChangeSerial();
    if (event->button() == Qt::LeftButton) {
        const QPoint position = mousePosition(event);
        const QModelIndex index = VirtualItemView::indexAt(position);
        const bool alreadyToggled = m_indicatorPressToggled && isIndicatorPosition(index, position);
        m_indicatorPressToggled = false;
        if (!alreadyToggled && hasChildren(index))
            toggleExpanded(index);
    }
    if (!self || modelChangeSerial() != modelSerial)
        return;
    VirtualTableView::mouseDoubleClickEvent(event);
}

QModelIndex VirtualTreeTableView::dragNodeIndex(const QModelIndex &index) const
{
    return nodeIndex(index);
}

VirtualItemView::DropTarget VirtualTreeTableView::resolveDropTarget(const QPoint &viewportPos) const
{
    DropTarget target;
    if (!model())
        return target;
    const qsizetype rows = viewItemCount();
    const QRect last = rows > 0 ? geometryForViewRow(rows - 1) : QRect();
    if (rows == 0 || viewportPos.y() > last.bottom()) {
        target.parent = m_rootIndex;
        target.row = model()->rowCount(target.parent);
        target.trailing = true;
        return target;
    }
    QModelIndex node = nodeIndex(VirtualItemView::indexAt(viewportPos));
    qsizetype visibleRow = viewItemForIndex(node);
    if (visibleRow < 0 && layoutPolicy()) {
        // A spacing gap has no item hit, but it still belongs to the preceding
        // visible row for insertion. Use the same pane offset as the base drop path.
        const ItemPane::Type pane = itemPaneAtY(viewportPos.y());
        const qint64 offset = itemPaneScrollOffset(pane) + qMax(0, viewportPos.y());
        if (offset >= 0 && offset < layoutPolicy()->contentExtent()) {
            const qsizetype candidate = layoutPolicy()->indexAtOffset(offset);
            if (candidate >= 0 && candidate < rows
                && itemPaneForRow(candidate) == pane) {
                const QRect candidateRect = geometryForViewRow(candidate);
                if (viewportPos.y() > candidateRect.bottom()) {
                    visibleRow = candidate;
                    node = viewIndex(candidate);
                    if (candidate + 1 < rows) {
                        const QModelIndex next = viewIndex(candidate + 1);
                        if (next.parent() == node) {
                            // The gap after an expanded parent precedes its first child.
                            // Treat it as an insertion before that child.
                            visibleRow = candidate + 1;
                            node = next;
                        }
                    }
                }
            }
        }
    }
    if (visibleRow < 0)
        return target;
    const QRect rect = geometryForViewRow(visibleRow);
    const int y = qBound(rect.top(), viewportPos.y(), rect.bottom());
    const int band = qMax(3, rect.height() / 4);
    const bool above = y < rect.top() + band;
    const bool below = y > rect.bottom() - band;
    if (!above && !below && (model()->flags(node) & Qt::ItemIsDropEnabled)) {
        target.parent = node;
        target.row = model()->rowCount(node);
        target.ontoItem = true;
        return target;
    }
    target.parent = node.parent();
    target.row = node.row() + ((above || (!below && y < rect.center().y())) ? 0 : 1);
    if (selectionBehavior() == SelectionBehavior::SelectItems) {
        const int column = columnAtViewportX(viewportPos.x());
        if (column >= 0 && column < model()->columnCount(target.parent)
            && column < model()->columnCount(node.parent())) {
            const QModelIndex cell = viewIndex(visibleRow, column);
            const QModelIndex anchor = anchorIndex(cell);
            target.column = anchor.isValid() ? anchor.column() : column;
        }
    }
    return target;
}

QRect VirtualTreeTableView::insertionLineRect(const DropTarget &target) const
{
    if (!target.isValid() || !model())
        return QRect();
    if (target.trailing) {
        const qsizetype count = viewItemCount();
        return QRect(0, count > 0 ? geometryForViewRow(count - 1).bottom() : 0,
                     viewport()->width(), 2);
    }
    const QModelIndex parent = target.parent;
    if (target.row < model()->rowCount(parent)) {
        const qsizetype after = viewItemForIndex(model()->index(target.row, 0, parent));
        if (after >= 0)
            return QRect(0, geometryForViewRow(after).top() - 1, viewport()->width(), 2);
    }
    if (target.row > 0) {
        QModelIndex precedingNode = model()->index(target.row - 1, 0, parent);
        qsizetype before = viewItemForIndex(precedingNode);
        if (before >= 0) {
            while (isExpanded(precedingNode)) {
                const int children = model()->rowCount(precedingNode);
                if (children == 0)
                    break;
                precedingNode = model()->index(children - 1, 0, precedingNode);
            }
            const qsizetype lastDescendant = viewItemForIndex(precedingNode);
            if (lastDescendant >= 0)
                before = lastDescendant;
            return QRect(0, geometryForViewRow(before).bottom(), viewport()->width(), 2);
        }
    }
    const qsizetype parentRow = viewItemForIndex(parent);
    if (parentRow >= 0)
        return QRect(0, geometryForViewRow(parentRow).bottom(), viewport()->width(), 2);
    return QRect(0, 0, viewport()->width(), 2);
}

QRect VirtualTreeTableView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!model())
        return QRect();
    QRect rect;
    if (target.ontoItem) {
        const qsizetype row = viewItemForIndex(target.parent);
        rect = row >= 0 ? geometryForViewRow(row) : QRect();
    } else {
        rect = insertionLineRect(target);
    }
    if (rect.isEmpty() || target.column < 0)
        return rect;
    const ColumnGeometry column = columnGeometry(target.column);
    if (!column.isValid() || column.hidden)
        return rect;
    const QModelIndex cell = model()->index(target.row, target.column, target.parent);
    const QModelIndex anchor = anchorIndex(cell);
    const QRect merged = anchor.isValid() ? spanRect(anchor) : QRect();
    return QRect(merged.isEmpty() ? column.viewportX : merged.x(), rect.y(),
                 merged.isEmpty() ? column.width : merged.width(), rect.height());
}

} // namespace viv
