#include <virtualitemviews/virtualtreetableview.h>
#include "../core/performanceprobe_p.h"
#include "treeexpansiontransition_p.h"
#include "treedragplaceholder_p.h"
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
#include <QPixmap>
#include <QResizeEvent>
#include <QRegion>
#include <QSet>
#include <QtMath>

#include <climits>
#include <limits>
#include <functional>

namespace viv {
constexpr int VirtualTreeTableView::NodeRowSpacingRole;
constexpr int VirtualTreeTableView::NodeRowSpacingBelowRole;
constexpr int VirtualTreeTableView::NodeRowSpacingAboveRole;

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
        setObjectName(QStringLiteral("vivBranchOverlay"));
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
    verticalHeader()->setAdapter(new VisibleRowHeaderAdapter, true);
    m_expansionTransition = new TreeExpansionTransition(this,
        static_cast<ListLayout *>(layoutPolicy()),
        [this]() {
            const QPointer<VirtualTreeTableView> guard(this);
            relayout();
            if (guard) {
                invalidateBranches();
                viewport()->update();
            }
        },
        verticalHeaderGeometry());
    m_branchOverlay = new BranchOverlay([this](QPainter *painter) { paintBranches(painter); }, viewport());
    m_branchOverlay->setGeometry(viewport()->rect());
    m_branchOverlay->show();
    connect(this, &VirtualTableView::horizontalOffsetChanged, this,
            [this](qint64) {
                if (!m_expansionTransition->isUpdatingGeometry())
                    m_expansionTransition->stop();
                invalidateBranches();
            });
    connect(this, &VirtualTableView::columnGeometryChanged, this,
            [this]() {
                if (!m_expansionTransition->isUpdatingGeometry())
                    m_expansionTransition->stop();
                invalidateBranches();
            });
}

VirtualTreeTableView::~VirtualTreeTableView()
{
    clearDragFeedback();
    delete m_dragPlaceholderOutline;
    m_dragPlaceholderOutline = nullptr;
    m_expansionTransition->cancel();
    m_expansionTransition->setEnabled(false);
    recycleAllCells();
    recycleAllItems();
    delete m_branchOverlay;
    if (m_ownBranchRenderer)
        delete m_branchRenderer;
    delete m_expansionTransition;
    delete m_visibility;
}

void VirtualTreeTableView::setModel(QAbstractItemModel *treeModel)
{
    if (model() == treeModel)
        return;
    m_expansionTransition->cancel();
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
    detail::PerformanceScope performance(this, "tree-table.visibility");
    const QPointer<VirtualTreeTableView> feedbackGuard(this);
    clearDragFeedback();
    if (!feedbackGuard)
        return;
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
    detail::PerformanceScope performance(this, "tree-table.visibility-splice");
    const QPointer<VirtualTreeTableView> feedbackGuard(this);
    clearDragFeedback();
    if (!feedbackGuard)
        return;
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
    m_expansionTransition->cancel();
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

void VirtualTreeTableView::setExpansionFadeEnabled(bool enabled)
{
    m_expansionTransition->setFadeEnabled(enabled);
}

bool VirtualTreeTableView::expansionFadeEnabled() const
{
    return m_expansionTransition->isFadeEnabled();
}

void VirtualTreeTableView::expand(const QModelIndex &index)
{
    const QPointer<VirtualTreeTableView> operationGuard(this);
    const QPersistentModelIndex requested(nodeIndex(index));
    if (!m_expansionTransition->affects(requested))
        m_expansionTransition->stop();
    if (!operationGuard)
        return;
    if (m_expansionTransition->resumeExpansion(requested)) {
        if (operationGuard && requested.isValid())
            emit expanded(requested);
        return;
    }
    detail::PerformanceScope performance(this, "tree-table.expand");
    const QPersistentModelIndex node(nodeIndex(requested));
    if (!node.isValid() || viewItemForIndex(node) < 0
        || !hasChildren(node) || isExpanded(node))
        return;
    const QPersistentModelIndex expandedNode(node);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    const qsizetype animationFirst = viewItemForIndex(node) + 1;
    const qsizetype animationCountBefore = viewItemCount();
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
        m_expansionTransition->expand(node, animationFirst, viewItemCount() - animationCountBefore);
        if (!animationGuard)
            return;
    }
    if (modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial
        && expandedNode.isValid() && isExpanded(expandedNode))
        emit expanded(expandedNode);
}

void VirtualTreeTableView::collapse(const QModelIndex &index)
{
    const QPointer<VirtualTreeTableView> operationGuard(this);
    const QPersistentModelIndex requested(nodeIndex(index));
    if (!m_expansionTransition->affects(requested))
        m_expansionTransition->stop();
    if (!operationGuard)
        return;
    if (m_expansionTransition->isCollapsing(requested))
        return;
    detail::PerformanceScope performance(this, "tree-table.collapse");
    const QPersistentModelIndex node(nodeIndex(requested));
    if (!node.isValid() || !isExpanded(node))
        return;
    const QPersistentModelIndex collapsedNode(node);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    if (m_expansionTransition->collapse(m_visibility, node,
            [animationGuard, node, animationSerial, animationRoot]() {
                if (animationGuard && node.isValid()
                    && animationGuard->modelChangeSerial() == animationSerial
                    && animationGuard->rootIndex() == animationRoot)
                    animationGuard->collapse(node);
            }))
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
    if (modelChangeSerial() == modelSerial && m_rootChangeSerial == rootSerial
        && collapsedNode.isValid() && !isExpanded(collapsedNode))
        emit collapsed(collapsedNode);
}

void VirtualTreeTableView::expandRecursively(const QModelIndex &index)
{
    const QPointer<VirtualTreeTableView> operationGuard(this);
    const QPersistentModelIndex requested(index);
    m_expansionTransition->stop();
    if (!operationGuard)
        return;
    detail::PerformanceScope performance(this, "tree-table.expand-recursively");
    const QPersistentModelIndex node(nodeIndex(requested));
    if (!node.isValid() || !hasChildren(node))
        return;
    const QPersistentModelIndex expandedNode(node);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 rootSerial = m_rootChangeSerial;
    const bool wasExpanded = isExpanded(node);
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || !node.isValid() || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    const qsizetype animationFirst = viewItemForIndex(node) + 1;
    const qsizetype animationCountBefore = viewItemCount();
    setPendingAnchor(captureAnchor());
    const quint64 mappingSerial = ++m_mappingSerial;
    m_visibility->expandRecursively(node);
    if (modelChangeSerial() != modelSerial || m_rootChangeSerial != rootSerial
        || m_mappingSerial != mappingSerial)
        return;
    refreshVisibility(true);
    if (!animationGuard)
        return;
    if (!wasExpanded && modelChangeSerial() == animationSerial && m_rootIndex == animationRoot)
        m_expansionTransition->expand(node, animationFirst, viewItemCount() - animationCountBefore);
    if (!animationGuard)
        return;
    if (!wasExpanded && modelChangeSerial() == modelSerial
        && m_rootChangeSerial == rootSerial && expandedNode.isValid()
        && isExpanded(expandedNode))
        emit expanded(expandedNode);
}

void VirtualTreeTableView::collapseAll()
{
    const QPointer<VirtualTreeTableView> operationGuard(this);
    m_expansionTransition->stop();
    if (!operationGuard)
        return;
    detail::PerformanceScope performance(this, "tree-table.collapse-all");
    const QPointer<VirtualTreeTableView> animationGuard(this);
    const quint64 animationSerial = modelChangeSerial();
    const QPersistentModelIndex animationRoot(m_rootIndex);
    if (!animationGuard || modelChangeSerial() != animationSerial || m_rootIndex != animationRoot)
        return;
    setPendingAnchor(captureAnchor());
    ++m_mappingSerial;
    m_visibility->collapseAll();
    refreshVisibility(true);
    if (!animationGuard)
        return;
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
    return !m_expansionTransition->isCollapsing(nodeIndex(index))
        && m_visibility->isExpanded(nodeIndex(index));
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

void VirtualTreeTableView::setBranchAreaVisible(bool visible)
{
    if (m_branchAreaVisible == visible)
        return;
    const QPointer<VirtualTreeTableView> guard(this);
    m_expansionTransition->stop();
    if (!guard)
        return;
    m_branchAreaVisible = visible;
    relayout();
    if (guard) {
        invalidateBranches();
        viewport()->update();
    }
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

void VirtualTreeTableView::setVisualStateBackgroundVisible(bool visible)
{
    if (m_visualStateBackgroundVisible == visible)
        return;
    m_visualStateBackgroundVisible = visible;
    relayout();
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
        const QModelIndex anchor = selectionBehavior() == SelectionBehavior::SelectItems
            ? anchorIndex(index) : index;
        const QRect rect = selectionBehavior() == SelectionBehavior::SelectItems
            ? spanRect(anchor) : visualRect(anchor);
        viewport()->update(rect);
    }
}

void VirtualTreeTableView::paintEvent(QPaintEvent *event)
{
    VirtualTableView::paintEvent(event);
}

void VirtualTreeTableView::setBranchIndicatorsVisible(bool visible)
{
    if (m_branchIndicatorsVisible == visible)
        return;
    m_branchIndicatorsVisible = visible;
    invalidateBranches();
}

void VirtualTreeTableView::setExpandedBranchIcon(const QIcon &icon)
{
    m_expandedBranchIcon = icon;
    invalidateBranches();
}

void VirtualTreeTableView::setCollapsedBranchIcon(const QIcon &icon)
{
    m_collapsedBranchIcon = icon;
    invalidateBranches();
}

void VirtualTreeTableView::setBranchIconSize(int pixels)
{
    pixels = qMax(1, pixels);
    if (m_branchIconSize == pixels)
        return;
    m_branchIconSize = pixels;
    invalidateBranches();
}

void VirtualTreeTableView::setBranchIconColor(const QColor &color)
{
    if (m_branchIconColor == color)
        return;
    m_branchIconColor = color;
    invalidateBranches();
}

void VirtualTreeTableView::setBranchIndicatorAlignment(Qt::Alignment alignment)
{
    if (!(alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter)))
        alignment |= Qt::AlignLeft;
    if (!(alignment & (Qt::AlignTop | Qt::AlignBottom | Qt::AlignVCenter)))
        alignment |= Qt::AlignVCenter;
    if (m_branchIndicatorAlignment == alignment)
        return;
    m_branchIndicatorAlignment = alignment;
    invalidateBranches();
}

void VirtualTreeTableView::setBranchIndicatorPadding(const QMargins &padding)
{
    const QMargins clamped(qMax(0, padding.left()), qMax(0, padding.top()),
                           qMax(0, padding.right()), qMax(0, padding.bottom()));
    if (m_branchIndicatorPadding == clamped)
        return;
    m_branchIndicatorPadding = clamped;
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
    if (!m_branchAreaVisible || !node.isValid() || !column.isValid() || column.hidden || m_indentation <= 0
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
    if (layoutPolicy()->hasTransientGeometry())
        item.setHeight(layoutPolicy()->sizeIndex()->sizeOf(row));
    return QRect(int(clippedLeft), item.y(), int(clippedRight - clippedLeft), item.height());
}

bool VirtualTreeTableView::isIndicatorPosition(const QModelIndex &index,
                                                const QPoint &position) const
{
    const QModelIndex node = nodeIndex(index);
    const qsizetype row = viewItemForIndex(node);
    return m_branchAreaVisible && m_branchIndicatorsVisible && hasChildren(node) && row >= 0
        && branchCellRect(row, itemDepth(node))
            .intersected(itemPaneRect(itemPaneForRow(row)))
            .intersected(viewport()->rect()).contains(position);
}

void VirtualTreeTableView::paintBranches(QPainter *painter) const
{
    detail::PerformanceScope performance(this, "paint.tree-table-branches", detail::PerformanceDomain::Paint);
    if (!m_branchAreaVisible || !m_branchIndicatorsVisible || isColumnHidden(0))
        return;
    const int paneIndex = paneIndexOfColumn(0);
    const QVector<TablePane> currentPanes = panes();
    if (paneIndex < 0 || paneIndex >= currentPanes.size())
        return;
    painter->save();
    painter->setClipRect(currentPanes.at(paneIndex).viewportRect);
    for (const VisibleRange &range : dragVisualRowRanges()) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            const QModelIndex node = viewIndex(row);
            if (!node.isValid())
                continue;
            painter->setOpacity(layoutPolicy()->itemOpacity(row));
            painter->setClipRegion(QRegion(currentPanes.at(paneIndex).viewportRect)
                .intersected(QRegion(itemPaneRect(itemPaneForRow(row))))
                .intersected(QRegion(viewport()->rect())));
            if (layoutPolicy()->hasTransientGeometry())
                painter->setClipRect(geometryForViewRow(row), Qt::IntersectClip);
            const int depth = itemDepth(node);
            if (!m_branchRenderer) {
                if (hasChildren(node))
                    paintDefaultBranchIndicator(painter, branchState(node), node,
                                                branchCellRect(row, depth, true));
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
                detail::performanceCall(this, "callback.branch.paint", [&] {
                    return m_branchRenderer->paintBranch(painter, state, node, rect);
                });
                painter->restore();
            }
        }
    }
    painter->restore();
}

void VirtualTreeTableView::paintDefaultBranchIndicator(QPainter *painter,
                                                      const BranchIndicatorState &state,
                                                      const QModelIndex &index,
                                                      const QRect &cellRect) const
{
    if (!state.adjoinsItem || !state.hasChildren)
        return;
    const qreal progress = m_expansionTransition->expansionProgress(index, state.isExpanded);
    const bool hasExpandedIcon = !m_expandedBranchIcon.isNull();
    const bool hasCollapsedIcon = !m_collapsedBranchIcon.isNull();
    if (!hasExpandedIcon && !hasCollapsedIcon) {
        // The built-in decoration is one expanded triangle reused for both states.
        BranchIndicatorState expandedState = state;
        expandedState.isExpanded = true;
        BranchIndicatorRenderer::paintBuiltinBranch(painter, expandedState, cellRect, palette(),
                                                    m_branchIconSize, m_branchIconColor,
                                                    m_branchIndicatorAlignment,
                                                    m_branchIndicatorPadding,
                                                    (progress - 1.0) * 90.0);
        return;
    }
    const bool separateIcons = hasExpandedIcon && hasCollapsedIcon;
    const QIcon &icon = (separateIcons ? state.isExpanded : hasExpandedIcon)
        ? m_expandedBranchIcon : m_collapsedBranchIcon;
    const qreal rotation = separateIcons ? 0.0
        : (hasExpandedIcon ? progress - 1.0 : progress) * 90.0;
    const QRectF target = BranchIndicatorRenderer::branchIndicatorTargetRectF(
        QRectF(cellRect), qreal(m_branchIconSize), m_branchIndicatorAlignment,
        QMarginsF(m_branchIndicatorPadding));
    if (target.isEmpty())
        return;
    painter->save();
    painter->setClipRect(QRectF(cellRect), Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->translate(target.center());
    painter->rotate(rotation);
    painter->translate(-target.center());
    if (!m_branchIconColor.isValid()) {
        painter->translate(target.topLeft());
        icon.paint(painter, QRect(QPoint(), target.size().toSize()));
        painter->restore();
        return;
    }
    const qreal dpr = painter->device()->devicePixelRatioF();
    const qreal size = target.width();
    QPixmap pixmap = icon.pixmap(QSize(qMax(1, qCeil(size * dpr)),
                                     qMax(1, qCeil(size * dpr))));
    if (pixmap.isNull()) {
        painter->restore();
        return;
    }
    pixmap.setDevicePixelRatio(dpr);
    {
        QPainter tint(&pixmap);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(pixmap.rect(), m_branchIconColor);
    }
    QSizeF logicalSize = pixmap.size();
    logicalSize.scale(target.size(), Qt::KeepAspectRatio);
    const QRectF imageRect(target.center()
        - QPointF(logicalSize.width() / 2.0, logicalSize.height() / 2.0), logicalSize);
    painter->drawPixmap(imageRect, pixmap, QRectF(pixmap.rect()));
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
    if (m_visualStateBackgroundVisible
        || (itemBackgroundColor().isValid() && itemBackgroundColor().alpha() > 0))
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
    const qint64 inset = (qint64(itemDepth(index)) + int(m_branchAreaVisible)) * m_indentation;
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

bool VirtualTreeTableView::prepareDrag(const QModelIndexList &indexes)
{
    if (dragFeedbackMode() != DragFeedbackMode::RowMoveAnimation
        || selectionBehavior() != SelectionBehavior::SelectRows || indexes.isEmpty())
        return true;
    const QPersistentModelIndex node(nodeIndex(indexes.first()));
    for (const QModelIndex &index : indexes) {
        if (!index.isValid() || index.model() != model() || nodeIndex(index) != node)
            return true;
    }
    const QPointer<VirtualTreeTableView> guard(this);
    const QPointer<QAbstractItemModel> sourceModel(model());
    const quint64 serial = modelChangeSerial();
    const QPersistentModelIndex root(rootIndex());
    const auto current = [=]() {
        return guard && sourceModel && guard->model() == sourceModel.data()
            && guard->modelChangeSerial() == serial && guard->rootIndex() == root
            && guard->isDragEnabled() && node.isValid()
            && guard->viewItemForIndex(node) >= 0;
    };
    if (!current())
        return false;
    clearDragFeedback();
    if (!current())
        return false;
    m_expansionTransition->stop();
    if (!current())
        return false;
    if (m_visibility->isExpanded(node)) {
        setPendingAnchor(captureAnchor());
        if (!current())
            return false;
        const qsizetype first = viewItemForIndex(node) + 1;
        const qsizetype previousCount = viewItemCount();
        const quint64 mappingSerial = ++m_mappingSerial;
        // Collapse before preview capture, without starting an expansion tween.
        m_visibility->collapse(node);
        if (!current() || m_mappingSerial != mappingSerial)
            return false;
        refreshVisibilitySplice(first, previousCount - viewItemCount(), 0, previousCount);
        if (!current())
            return false;
        if (!isExpanded(node))
            emit collapsed(node);
    }
    // Cancellation keeps the node collapsed; callbacks may invalidate or reopen it.
    return current() && !isExpanded(node);
}

qsizetype VirtualTreeTableView::visibleSubtreeEnd(QModelIndex node) const
{
    if (!model())
        return 0;
    while (node.isValid() && node != rootIndex()) {
        const QModelIndex next = model()->index(node.row() + 1, 0, node.parent());
        if (next.isValid()) {
            const qsizetype row = viewItemForIndex(next);
            if (row >= 0)
                return row;
        }
        node = node.parent();
    }
    return viewItemCount();
}

bool VirtualTreeTableView::resolveDragRowMove(const DropTarget &target,
                                             const QList<QPersistentModelIndex> &sources,
                                             qsizetype *sourceRow, qsizetype *destination)
{
    const QPersistentModelIndex source(nodeIndex(sources.first()));
    for (const auto &index : sources) {
        if (!index.isValid() || index.model() != model() || nodeIndex(index) != source)
            return false;
    }
    *sourceRow = viewItemForIndex(source);
    const qsizetype end = *sourceRow + 1;
    if (*sourceRow < 0 || visibleSubtreeEnd(source) != end
        || target.row < 0 || target.row > model()->rowCount(target.parent))
        return false;
    *destination = -1;
    if (target.row < model()->rowCount(target.parent))
        *destination = viewItemForIndex(model()->index(target.row, 0, target.parent));
    else if (target.parent == rootIndex())
        *destination = viewItemCount();
    else if (viewItemForIndex(target.parent) >= 0 && isExpanded(target.parent))
        *destination = visibleSubtreeEnd(target.parent);
    if (*destination < 0)
        return false;

    const QPointer<VirtualTreeTableView> guard(this);
    const QPointer<QAbstractItemModel> activeModel(model());
    const quint64 serial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 revision = m_dragFeedbackRevision;
    const QPersistentModelIndex root(rootIndex());
    const auto current = [=]() {
        return guard && activeModel && guard->model() == activeModel.data()
            && guard->modelChangeSerial() == serial && guard->viewMappingSerial() == mappingSerial
            && guard->rootIndex() == root && guard->m_dragFeedbackRevision == revision
            && source.isValid();
    };
    bool ambiguous = false;
    QModelIndex node = *destination > 0 ? viewIndex(*destination - 1) : QModelIndex();
    while (node.isValid() && node != root && visibleSubtreeEnd(node) == *destination) {
        DropTarget alternative;
        alternative.parent = node.parent();
        alternative.row = node.row() + 1;
        if (alternative.parent != target.parent || alternative.row != target.row) {
            const bool originalSlot = alternative.parent == source.parent()
                && ((*destination == *sourceRow && alternative.row == source.row())
                    || (*destination == end && alternative.row == source.row() + 1));
            const bool accepted = canDropAtCurrentDragTarget(alternative, originalSlot);
            if (!current())
                return false;
            if (accepted) {
                ambiguous = true;
                break;
            }
        }
        node = node.parent();
    }
    m_dragTargetParent = target.parent;
    m_dragTargetRow = target.row;
    m_dragTargetAmbiguous = ambiguous;
    return true;
}

void VirtualTreeTableView::clearDragFeedback()
{
    ++m_dragFeedbackRevision;
    m_dragTargetParent = QPersistentModelIndex();
    m_dragTargetRow = -1;
    m_dragTargetAmbiguous = false;
    const QPointer<VirtualTreeTableView> guard(this);
    if (m_dragPlaceholderOutline)
        m_dragPlaceholderOutline->hide();
    if (guard)
        VirtualTableView::clearDragFeedback();
}

int VirtualTreeTableView::dropTargetLeft(const DropTarget &target, bool includeBranchCell) const
{
    const ColumnGeometry column = columnGeometry(0);
    if (!column.isValid() || column.hidden)
        return 0;
    const int depth = target.parent == rootIndex() ? 0 : itemDepth(target.parent) + 1;
    const qint64 cells = qint64(depth) + int(includeBranchCell && m_branchAreaVisible);
    int columnX = column.viewportX;
    columnVisualX(0, &columnX);
    const qint64 left = qMin(qint64(columnX) + column.width, qint64(columnX) + cells * indentation());
    // Frozen columns can clip column zero before its logical content origin.
    const int paneIndex = paneIndexOfColumn(0);
    const auto currentPanes = panes();
    const int paneLeft = paneIndex >= 0 && paneIndex < currentPanes.size()
        ? currentPanes.at(paneIndex).viewportRect.left() : 0;
    return int(qBound(qint64(qBound(0, paneLeft, viewport()->width())), left,
                      qint64(viewport()->width())));
}

void VirtualTreeTableView::dragReorderFrameChanged()
{
    const QPointer<VirtualTreeTableView> guard(this);
    invalidateBranches();
    if (!guard)
        return;
    QRect rect = m_dragTargetAmbiguous && m_dragTargetRow >= 0 ? dragPlaceholderRect() : QRect();
    if (!rect.isEmpty()) {
        DropTarget target;
        target.parent = m_dragTargetParent;
        target.row = m_dragTargetRow;
        rect.setLeft(dropTargetLeft(target, false));
    }
    if (rect.isEmpty() || !rect.intersects(viewport()->rect()) || dropIndicatorWidth() <= 0) {
        if (m_dragPlaceholderOutline)
            m_dragPlaceholderOutline->hide();
        return;
    }
    if (!m_dragPlaceholderOutline)
        m_dragPlaceholderOutline = new TreeDragPlaceholderOutline(this);
    const QColor color = dropIndicatorColor().isValid()
        ? dropIndicatorColor() : palette().color(QPalette::Highlight);
    m_dragPlaceholderOutline->setFrame(rect, color, dropIndicatorWidth());
    m_dragPlaceholderOutline->setGeometry(viewport()->geometry());
    if (!guard || !m_dragPlaceholderOutline)
        return;
    m_dragPlaceholderOutline->show();
    if (guard && m_dragPlaceholderOutline)
        m_dragPlaceholderOutline->raise();
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
    const int left = dropTargetLeft(target, true);
    const auto lineAt = [this, left](int y) {
        return QRect(left, y, viewport()->width() - left, 2);
    };
    if (target.trailing) {
        const qsizetype count = viewItemCount();
        return lineAt(count > 0 ? geometryForViewRow(count - 1).bottom() : 0);
    }
    const QModelIndex parent = target.parent;
    if (target.row > 0) {
        const QModelIndex precedingNode = model()->index(target.row - 1, 0, parent);
        const qsizetype before = viewItemForIndex(precedingNode);
        if (before >= 0) {
            const qsizetype last = qMax(before, visibleSubtreeEnd(precedingNode) - 1);
            return lineAt(geometryForViewRow(last).bottom());
        }
    }
    const qsizetype after = viewItemForIndex(model()->index(target.row, 0, parent));
    if (after >= 0)
        return lineAt(geometryForViewRow(after).top() - 1);
    const qsizetype parentRow = viewItemForIndex(parent);
    if (parentRow >= 0)
        return lineAt(geometryForViewRow(parentRow).bottom());
    return lineAt(0);
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
