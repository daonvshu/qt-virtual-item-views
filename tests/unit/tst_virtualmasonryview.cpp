#include <virtualitemviews/virtualmasonryview.h>
#include "vivtestfixtures.h"
#include <virtualitemviews/accessibility.h>

#include <QAccessible>
#include <QImage>
#include <QLabel>
#include <QPainter>
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

class PaintableMasonryView : public viv::VirtualMasonryView
{
public:
    using VirtualMasonryView::paintGridLines;
};

} // namespace

class VirtualMasonryViewTest : public QObject
{
    Q_OBJECT
private slots:
    void materializesVisibleItemsAcrossColumns();
    void publicGeometryTracksSourceIdentity();
    void adapterEstimatesAreLoadedAfterModel();
    void modelMutationsPreserveSourceIdentityAndAnchor();
    void modelMutationsRekeyLookupAcrossColumns();
    void modelDestructionClearsMasonryState();
    void keyboardUsesGeometricNeighbors();
    void keyboardSupportsPageAndEnds();
    void dropUsesSourceOrder();
    void heightAnimationCommitsTargetWhenDisabledByDuration();
    void heightAnimationMovesFollowingColumnItemsContinuously();
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
    viv::removeAccessibilityFactory();
}

QTEST_MAIN(VirtualMasonryViewTest)
#include "tst_virtualmasonryview.moc"
