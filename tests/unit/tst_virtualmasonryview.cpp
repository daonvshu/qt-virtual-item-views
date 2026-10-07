#include <virtualitemviews/virtualmasonryview.h>
#include "vivtestfixtures.h"
#include <virtualitemviews/accessibility.h>

#include <QAccessible>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFrame>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QtTest>

namespace {

class MasonryAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(120, 80); }
};

class EstimatedAdapter : public MasonryAdapter
{
public:
    mutable int estimateCalls = 0;

    QSize estimatedSize(const QModelIndex &index) const override
    {
        ++estimateCalls;
        return index.data(Qt::UserRole).toSize();
    }
};

class MasonryDropModel : public QStandardItemModel
{
public:
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        return data && data->hasText() && action == Qt::CopyAction && column == -1
            && !parent.isValid() && row >= 0 && row <= rowCount();
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        ++calls;
        lastRow = row;
        lastColumn = column;
        if (!canDropMimeData(data, action, row, column, parent) || !submitSucceeds)
            return false;
        insertRow(row, new QStandardItem(data->text()));
        return true;
    }

    int calls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    bool submitSucceeds = true;
};

class PaintableMasonryView : public viv::VirtualMasonryView
{
public:
    using VirtualMasonryView::paintVisualStateBackgrounds;
    using VirtualMasonryView::paintGridLines;
    QModelIndex selected;

    VisualState visualState(const QModelIndex &index) const override
    {
        VisualState state;
        state.selected = index == selected;
        state.selectedProgress = state.selected ? 1.0 : 0.0;
        return state;
    }
};

} // namespace

class VirtualMasonryViewTest : public QObject
{
    Q_OBJECT
private slots:
    void materializesVisibleItemsAcrossColumns();
    void rootIndexBindsChildrenOnly();
    void scrollsAndHitsAcrossColumns();
    void publicGeometryTracksSourceIdentity();
    void adapterEstimatesAreLoadedAfterModel();
    void modelMutationsPreserveSourceIdentityAndAnchor();
    void modelMutationsRekeyLookupAcrossColumns();
    void modelDestructionClearsMasonryState();
    void resizeReflowsColumnsWithoutLosingIdentity();
    void heightUpdatePreservesScrolledAnchor();
    void keyboardUsesGeometricNeighbors();
    void keyboardSupportsPageAndEnds();
    void dropUsesSourceOrder();
    void dropSubmitsSourceOrderToModel();
    void heightAnimationCommitsTargetWhenDisabledByDuration();
    void heightAnimationMovesFollowingColumnItemsContinuously();
    void heightAnimationFinishesBeforeModelMutation();
    void stateBackgroundsRespectSpacingAndDpr();
    void gridLinesUseSinglePhysicalPixelBands();
    void accessibilityExposesCellsWithLiveColumn();
};

void VirtualMasonryViewTest::materializesVisibleItemsAcrossColumns()
{
    QStandardItemModel model;
    for (int row = 0; row < 80; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.resize(440, 240);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QVERIFY(view.columnCount() >= 2);
    QVERIFY(view.materializedItemCount() < model.rowCount());
    QSet<QPersistentModelIndex> identities;
    for (const auto &item : view.materializedItems())
        identities.insert(item.index);
    QCOMPARE(identities.size(), view.materializedItemCount());
    QVERIFY(view.contentMargins().left() >= 0);
}

void VirtualMasonryViewTest::rootIndexBindsChildrenOnly()
{
    QStandardItemModel model;
    auto *parentItem = new QStandardItem(QStringLiteral("parent"));
    for (int row = 0; row < 3; ++row)
        parentItem->appendRow(new QStandardItem(QStringLiteral("child-%1").arg(row)));
    model.appendRow(parentItem);

    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setFixedItemHeight(48);
    view.setModel(&model);
    vivtest::showView(&view, QSize(280, 180));

    const QModelIndex parent = model.index(0, 0);
    view.setRootIndex(parent);
    view.flushPendingRelayout();

    QCOMPARE(view.rootIndex(), parent);
    QCOMPARE(view.stats().logicalItems, qsizetype(3));
    QVERIFY(view.visualRect(parent).isEmpty());
    for (int row = 0; row < 3; ++row) {
        const QModelIndex child = model.index(row, 0, parent);
        QVERIFY(!view.visualRect(child).isEmpty());
        QVERIFY(view.itemColumn(child) >= 0);
        QCOMPARE(view.indexAt(view.visualRect(child).center()), child);
    }
}

void VirtualMasonryViewTest::scrollsAndHitsAcrossColumns()
{
    QStandardItemModel model;
    for (int row = 0; row < 120; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));

    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(48);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(460, 160));

    const QModelIndex target = model.index(50, 0);
    view.setCurrentIndex(target);
    view.scrollTo(target, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();

    QCOMPARE(view.currentIndex(), target);
    QCOMPARE(view.visualRect(target).top(), 0);
    QCOMPARE(view.indexAt(view.visualRect(target).center()), target);
    QVERIFY(view.stats().materializedItems < view.stats().logicalItems);
}

void VirtualMasonryViewTest::publicGeometryTracksSourceIdentity()
{
    QStandardItemModel model;
    for (int row = 0; row < 8; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();
    view.setItemHeight(model.index(0, 0), 180);
    view.setItemHeight(model.index(1, 0), 60);
    view.setItemHeight(model.index(2, 0), 100);

    const QRect first = view.visualRect(model.index(0, 0));
    const QRect second = view.visualRect(model.index(1, 0));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QCOMPARE(view.indexAt(first.center()), model.index(0, 0));
    QCOMPARE(view.indexAt(second.center()), model.index(1, 0));
    QVERIFY(view.itemColumn(model.index(0, 0)) >= 0);
    QVERIFY(view.itemColumn(model.index(1, 0)) >= 0);
}

void VirtualMasonryViewTest::adapterEstimatesAreLoadedAfterModel()
{
    QStandardItemModel model;
    for (const QSize &size : {QSize(140, 240), QSize(140, 60), QSize(140, 120)}) {
        auto *item = new QStandardItem;
        item->setData(size, Qt::UserRole);
        model.appendRow(item);
    }
    EstimatedAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 240);
    QCOMPARE(view.visualRect(model.index(1, 0)).height(), 60);
    QCOMPARE(view.visualRect(model.index(2, 0)).height(), 120);

    const int callsBeforeUpdate = adapter.estimateCalls;
    model.setData(model.index(1, 0), QSize(140, 180), Qt::UserRole);
    QCoreApplication::processEvents();
    QCOMPARE(view.visualRect(model.index(1, 0)).height(), 180);
    QCOMPARE(adapter.estimateCalls - callsBeforeUpdate, 1);
}

void VirtualMasonryViewTest::modelMutationsPreserveSourceIdentityAndAnchor()
{
    vivtest::StringListModel model;
    for (int row = 0; row < 80; ++row)
        model.appendRow(QStringLiteral("row-%1").arg(row));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 140));

    QPersistentModelIndex anchor(model.index(20, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    model.insertRowsAt(0, {QStringLiteral("inserted")});
    view.flushPendingRelayout();
    QVERIFY(anchor.isValid());
    QCOMPARE(anchor.row(), 21);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    QVERIFY(model.moveRow(anchor.row(), 2));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    QVERIFY(model.removeRowsAt(0, 1));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 1);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);

    model.replaceAll({QStringLiteral("fresh-0"), QStringLiteral("fresh-1")});
    view.flushPendingRelayout();
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.verticalOffset(), qint64(0));
}

void VirtualMasonryViewTest::modelMutationsRekeyLookupAcrossColumns()
{
    vivtest::StringListModel model;
    for (int row = 0; row < 80; ++row)
        model.appendRow(QStringLiteral("row-%1").arg(row));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(420, 180));

    QPersistentModelIndex anchor(model.index(20, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);

    model.insertRowsAt(0, {QStringLiteral("inserted")});
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 21);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());

    QVERIFY(model.moveRow(anchor.row(), 2));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 2);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());

    QVERIFY(model.removeRowsAt(0, 1));
    view.flushPendingRelayout();
    QCOMPARE(anchor.row(), 1);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());
}

void VirtualMasonryViewTest::modelDestructionClearsMasonryState()
{
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(3);
    view.setFixedItemHeight(40);
    auto *doomed = new QStandardItemModel(80, 1);
    view.setModel(doomed);
    vivtest::showView(&view, QSize(420, 180));

    QPersistentModelIndex current(doomed->index(20, 0));
    view.setCurrentIndex(current);
    view.scrollTo(current, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QVERIFY(view.widgetForIndex(current));
    QVERIFY(view.maximumVerticalOffset() > 0);

    delete doomed;
    vivtest::settle();
    QVERIFY(!view.model());
    QVERIFY(!view.currentIndex().isValid());
    QCOMPARE(view.stats().logicalItems, qsizetype(0));
    QCOMPARE(view.stats().materializedItems, qsizetype(0));
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    QCOMPARE(view.maximumVerticalOffset(), qint64(0));

    QStandardItemModel replacement(7, 1);
    view.setModel(&replacement);
    vivtest::settle();
    QCOMPARE(view.stats().logicalItems, qsizetype(7));
    for (int row = 0; row < replacement.rowCount(); ++row) {
        const QModelIndex index = replacement.index(row, 0);
        QVERIFY(view.widgetForIndex(index));
    }
}

void VirtualMasonryViewTest::resizeReflowsColumnsWithoutLosingIdentity()
{
    QStandardItemModel model;
    for (int row = 0; row < 120; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("row-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setFixedItemHeight(56);
    view.resize(760, 180);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QPersistentModelIndex anchor(model.index(30, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    const int wideColumns = view.columnCount();
    QVERIFY(wideColumns >= 3);

    view.resize(420, 180);
    view.flushPendingRelayout();
    const int narrowColumns = view.columnCount();
    QVERIFY(narrowColumns < wideColumns);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());

    view.resize(900, 180);
    view.flushPendingRelayout();
    QVERIFY(view.columnCount() > narrowColumns);
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QVERIFY(view.itemColumn(anchor) >= 0);
    QVERIFY(!view.visualRect(anchor).isEmpty());
}

void VirtualMasonryViewTest::heightUpdatePreservesScrolledAnchor()
{
    QStandardItemModel model;
    for (int row = 0; row < 100; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("row-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setRowSpacing(8);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    QPersistentModelIndex anchor(model.index(40, 0));
    view.setCurrentIndex(anchor);
    view.scrollTo(anchor, viv::VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QWidget *identityWidget = view.widgetForIndex(anchor);
    QVERIFY(identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);
    const qint64 offsetBefore = view.verticalOffset();
    QVERIFY(offsetBefore > 0);

    view.setItemHeight(model.index(0, 0), 120);
    view.flushPendingRelayout();
    QCOMPARE(view.currentIndex(), QModelIndex(anchor));
    QCOMPARE(view.widgetForIndex(anchor), identityWidget);
    QCOMPARE(view.visualRect(anchor).top(), 0);
    QCOMPARE(view.verticalOffset(), offsetBefore + 80);
}

void VirtualMasonryViewTest::keyboardUsesGeometricNeighbors()
{
    QStandardItemModel model;
    for (int row = 0; row < 12; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();
    view.setItemHeight(model.index(0, 0), 180);
    view.setItemHeight(model.index(1, 0), 60);
    view.setItemHeight(model.index(2, 0), 100);
    view.setCurrentIndex(model.index(0, 0));
    view.setFocus();

    QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(view.currentIndex(), model.index(3, 0));
    view.setCurrentIndex(model.index(0, 0));
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(3, 0));
}

void VirtualMasonryViewTest::keyboardSupportsPageAndEnds()
{
    QStandardItemModel model;
    for (int row = 0; row < 12; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();
    view.setCurrentIndex(model.index(0, 0));
    view.setFocus();

    QTest::keyClick(&view, Qt::Key_PageDown);
    QVERIFY(view.currentIndex().isValid());
    QVERIFY(view.currentIndex() != model.index(0, 0));
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.currentIndex(), model.index(0, 0));
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.currentIndex(), model.index(11, 0));
}

void VirtualMasonryViewTest::dropUsesSourceOrder()
{
    QStandardItemModel model;
    for (int row = 0; row < 8; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    const QRect first = view.visualRect(model.index(0, 0));
    const viv::VirtualItemView::DropTarget target =
        view.dropTargetAt(first.center() + QPoint(0, first.height() / 3));
    QVERIFY(target.isValid());
    QCOMPARE(target.parent, QModelIndex());
    QCOMPARE(target.row, 1);
    QVERIFY(!view.dropIndicatorRect(target).isEmpty());
}

void VirtualMasonryViewTest::dropSubmitsSourceOrderToModel()
{
    MasonryDropModel model;
    for (int row = 0; row < 4; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(60);
    view.setDragEnabled(true);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    const QRect first = view.visualRect(model.index(0, 0));
    QVERIFY(!first.isEmpty());
    const QPoint position = first.center() + QPoint(0, first.height() / 3);
    QMimeData data;
    data.setText(QStringLiteral("inserted"));

    QDragEnterEvent enter(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDragMoveEvent move(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(move.isAccepted());

    QSignalSpy submitted(&view, &viv::VirtualItemView::itemDropped);
    QDropEvent drop(QPointF(position), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &drop);
    QVERIFY(drop.isAccepted());
    vivtest::settle();
    QCOMPARE(model.calls, 1);
    QCOMPARE(model.lastRow, 1);
    QCOMPARE(model.lastColumn, -1);
    QCOMPARE(model.index(1, 0).data().toString(), QStringLiteral("inserted"));
    QCOMPARE(submitted.count(), 1);
}

void VirtualMasonryViewTest::heightAnimationCommitsTargetWhenDisabledByDuration()
{
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("item")));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(320, 240);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    view.setItemHeightAnimationDuration(0);
    view.setItemHeightAnimationEnabled(true);
    view.setItemHeight(model.index(0, 0), 180);
    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 180);
}

void VirtualMasonryViewTest::heightAnimationMovesFollowingColumnItemsContinuously()
{
    QStandardItemModel model;
    for (int row = 0; row < 6; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setColumnSpacing(10);
    view.setRowSpacing(10);
    view.setColumnCount(1);
    view.setFixedItemHeight(100);
    view.resize(420, 700);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    const QModelIndex source = model.index(0, 0);
    qsizetype followingItem = -1;
    const int sourceColumn = view.itemColumn(source);
    const int sourceTop = view.visualRect(source).top();
    for (int row = 1; row < model.rowCount(); ++row) {
        const QModelIndex candidate = model.index(row, 0);
        if (view.itemColumn(candidate) == sourceColumn
            && view.visualRect(candidate).top() > sourceTop) {
            followingItem = row;
            break;
        }
    }
    QVERIFY(followingItem >= 0);
    const QModelIndex following = model.index(int(followingItem), 0);
    const int initialTop = view.visualRect(following).top();
    view.setItemHeightAnimationDuration(80);
    view.setItemHeightAnimationEnabled(true);
    view.setItemHeight(source, 220);
    QCoreApplication::processEvents();
    QTest::qWait(30);
    QCoreApplication::processEvents();
    const int intermediateTop = view.visualRect(following).top();
    QVERIFY(intermediateTop > initialTop);
    QVERIFY(intermediateTop < initialTop + 120);
    QTRY_VERIFY_WITH_TIMEOUT(view.visualRect(source).height() == 220, 500);
    QCOMPARE(view.visualRect(following).top(), initialTop + 120);
}

void VirtualMasonryViewTest::heightAnimationFinishesBeforeModelMutation()
{
    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("original")));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setColumnCount(1);
    view.setFixedItemHeight(40);
    view.setItemHeightAnimationDuration(500);
    view.setItemHeightAnimationEnabled(true);
    view.setModel(&model);
    vivtest::showView(&view, QSize(260, 180));

    view.setItemHeight(model.index(0, 0), 100);
    model.insertRow(0, new QStandardItem(QStringLiteral("inserted")));
    view.flushPendingRelayout();

    QCOMPARE(view.visualRect(model.index(0, 0)).height(), 40);
    QCOMPARE(view.visualRect(model.index(1, 0)).height(), 100);
}

void VirtualMasonryViewTest::stateBackgroundsRespectSpacingAndDpr()
{
    QStandardItemModel model;
    for (int row = 0; row < 6; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));

    PaintableMasonryView view;
    MasonryAdapter adapter;
    view.setFrameShape(QFrame::NoFrame);
    view.setAdapter(&adapter);
    view.setFixedItemHeight(40);
    view.setColumnSpacing(8);
    view.setRowSpacing(8);
    view.setSelectedBackgroundColor(Qt::red);
    view.setStateBackgroundCornerRadius(0);
    view.setModel(&model);
    vivtest::showView(&view, QSize(320, 220));

    const auto paint = [&](qreal dpr) {
        QImage image(qCeil(view.viewport()->width() * dpr),
                     qCeil(view.viewport()->height() * dpr),
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::white);
        QPainter painter(&image);
        view.paintVisualStateBackgrounds(&painter, view.viewport()->rect());
        painter.end();
        return image;
    };

    view.setColumnCount(2);
    view.flushPendingRelayout();
    view.selected = model.index(0, 0);
    const QRect columnItem = view.visualRect(view.selected);
    QVERIFY(!columnItem.isEmpty());
    const QPoint columnGap(columnItem.right() + 1, columnItem.center().y());
    QVERIFY(columnGap.x() < view.viewport()->width());
    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
        view.setSelectedBackgroundThroughColumnSpacing(false);
        const auto withoutGap = paint(dpr);
        QCOMPARE(withoutGap.pixelColor(qRound(columnGap.x() * dpr),
                                       qRound(columnGap.y() * dpr)), QColor(Qt::white));
        view.setSelectedBackgroundThroughColumnSpacing(true);
        const auto withGap = paint(dpr);
        QCOMPARE(withGap.pixelColor(qRound(columnGap.x() * dpr),
                                    qRound(columnGap.y() * dpr)), QColor(Qt::red));
    }

    view.setColumnCount(1);
    view.flushPendingRelayout();
    const QRect rowItem = view.visualRect(view.selected);
    const QPoint rowGap(rowItem.center().x(), rowItem.bottom() + 1);
    QVERIFY(rowGap.y() < view.viewport()->height());
    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
        view.setSelectedBackgroundThroughRowSpacing(false);
        const auto withoutGap = paint(dpr);
        QCOMPARE(withoutGap.pixelColor(qRound(rowGap.x() * dpr),
                                       qRound(rowGap.y() * dpr)), QColor(Qt::white));
        view.setSelectedBackgroundThroughRowSpacing(true);
        const auto withGap = paint(dpr);
        QCOMPARE(withGap.pixelColor(qRound(rowGap.x() * dpr),
                                    qRound(rowGap.y() * dpr)), QColor(Qt::red));
    }
}

void VirtualMasonryViewTest::gridLinesUseSinglePhysicalPixelBands()
{
    QStandardItemModel model;
    for (int row = 0; row < 12; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    PaintableMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setColumnSpacing(12);
    view.setRowSpacing(12);
    view.setVerticalGridLineColor(QColor(220, 30, 40));
    view.setHorizontalGridLineColor(QColor(30, 80, 220));
    view.setVerticalGridLineWidth(1);
    view.setHorizontalGridLineWidth(1);
    view.resize(420, 500);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    QVector<QModelIndex> firstItems;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        bool hasEarlierInColumn = false;
        for (const QModelIndex &earlier : firstItems) {
            if (view.itemColumn(earlier) == view.itemColumn(index)) {
                hasEarlierInColumn = true;
                break;
            }
        }
        if (!hasEarlierInColumn)
            firstItems.append(index);
    }
    QVERIFY(firstItems.size() >= 2);
    const QModelIndex left = firstItems.at(0);
    const QModelIndex right = firstItems.at(1);
    const QRect leftRect = view.visualRect(left);
    const QRect rightRect = view.visualRect(right);
    QVERIFY(leftRect.right() < rightRect.left());

    QModelIndex below;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex candidate = model.index(row, 0);
        if (view.itemColumn(candidate) == view.itemColumn(left)
            && view.visualRect(candidate).top() > leftRect.bottom()) {
            below = candidate;
            break;
        }
    }
    QVERIFY(below.isValid());
    const QRect belowRect = view.visualRect(below);

    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
        QImage image(900, 900, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::white);
        QPainter painter(&image);
        view.paintGridLines(&painter);
        painter.end();

        const auto countNearVertical = [&](int logicalX, int logicalY, const QColor &color) {
            const int x = qRound(logicalX * dpr);
            const int y = qRound(logicalY * dpr);
            int count = 0;
            for (int px = qMax(0, x - 3); px <= qMin(image.width() - 1, x + 3); ++px)
                count += image.pixelColor(px, y) == color;
            return count;
        };
        const auto countNearHorizontal = [&](int logicalX, int logicalY, const QColor &color) {
            const int x = qRound(logicalX * dpr);
            const int y = qRound(logicalY * dpr);
            int count = 0;
            for (int py = qMax(0, y - 3); py <= qMin(image.height() - 1, y + 3); ++py)
                count += image.pixelColor(x, py) == color;
            return count;
        };

        QCOMPARE(countNearVertical(leftRect.right() + 1, leftRect.center().y(),
                                   QColor(220, 30, 40)), 1);
        QCOMPARE(countNearVertical(rightRect.left(), rightRect.center().y(),
                                   QColor(220, 30, 40)), 1);
        QCOMPARE(countNearHorizontal(leftRect.center().x(), leftRect.bottom() + 1,
                                     QColor(30, 80, 220)), 1);
        QCOMPARE(countNearHorizontal(belowRect.center().x(), belowRect.top(),
                                     QColor(30, 80, 220)), 1);
    }
}

void VirtualMasonryViewTest::accessibilityExposesCellsWithLiveColumn()
{
    QStandardItemModel model;
    for (int row = 0; row < 8; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item-%1").arg(row)));
    MasonryAdapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.resize(420, 300);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    viv::installAccessibilityFactory();
    QAccessibleInterface *viewNode = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(viewNode);
    QVERIFY(viewNode->childCount() > 0);
    QAccessibleInterface *cell = viewNode->child(0);
    QVERIFY(cell);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("item-0"));
    QVERIFY(!cell->state().disabled);
    QVERIFY(cell->state().selectable);
    QVERIFY(!cell->state().selected);
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5 exposes the Cell role through the generic Client role.
    QVERIFY(cell->role() == QAccessible::Cell || cell->role() == QAccessible::Client);
#else
    QCOMPARE(cell->role(), QAccessible::Cell);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto *cellInterface = static_cast<QAccessibleTableCellInterface *>(
        cell->interface_cast(QAccessible::TableCellInterface));
    QVERIFY(cellInterface);
    QCOMPARE(cellInterface->columnIndex(), view.itemColumn(model.index(0, 0)));
    QCOMPARE(cellInterface->rowIndex(), 0);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QCOMPARE(cell->rect().size(), view.visualRect(model.index(0, 0)).size());
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    view.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect);
    QCoreApplication::processEvents();
    QVERIFY(cell->state().selected);

    QVERIFY(viewNode->childCount() > 3);
    QAccessibleInterface *movingCell = viewNode->child(3);
    QVERIFY(movingCell);
    auto *movingCellInterface = static_cast<QAccessibleTableCellInterface *>(
        movingCell->interface_cast(QAccessible::TableCellInterface));
    QVERIFY(movingCellInterface);
    const QModelIndex movingIndex = model.index(3, 0);
    const int oldColumn = movingCellInterface->columnIndex();
    const int oldColumnCount = view.columnCount();
    view.resize(900, 300);
    view.flushPendingRelayout();
    QVERIFY(view.columnCount() != oldColumnCount);
    QCOMPARE(movingCellInterface->columnIndex(), view.itemColumn(movingIndex));
    QCOMPARE(movingCell->rect().size(), view.visualRect(movingIndex).size());
    QVERIFY(movingCellInterface->columnIndex() != oldColumn);
#endif
    viv::removeAccessibilityFactory();
}

QTEST_MAIN(VirtualMasonryViewTest)
#include "tst_virtualmasonryview.moc"
