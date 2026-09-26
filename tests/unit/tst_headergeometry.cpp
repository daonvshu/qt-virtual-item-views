#include <virtualitemviews/headergeometry.h>

#include <QtTest>

using namespace viv;

class TestHeaderGeometry : public QObject
{
    Q_OBJECT

private slots:
    void defaultSectionGeometry();
    void resizeSectionRespectsLimits();
    void explicitSizesSurviveDefaultChanges();
    void visualOrderFollowsMoveSection();
    void hiddenSectionsAreSkipped();
    void sectionLookupByOffset();
    void viewportOffsetMovesViewportPositions();
    void moveLogicalSectionsFollowsModel();
    void sortIndicatorRoundTrip();
    void saveRestoreRoundTrip();
    void restoreRejectsMismatchedState();
    void sectionCountChangeKeepsState();
    void shrinkingTheSectionSetClearsAnOutOfRangeSortIndicator();
    void changingLimitsKeepsImplicitSizesImplicit();
    void changingLimitsClampsTheDefaultSize();
    void changingLimitsEmitsOneBulkGeometryChange();
    void changingLimitsNotifiesEvenWithoutClamping();
    void orderRevisionOnlyMovesWhenTheOrderCanChange();
    void insertedSectionsTakeTheSuccessorVisualSlot();
    void structuralRemapsReportTheSortIndicator();
};

void TestHeaderGeometry::defaultSectionGeometry()
{
    HeaderGeometry geometry(Qt::Horizontal);
    geometry.setDefaultSectionSize(120);
    geometry.setMinimumSectionSize(30);
    geometry.setSectionCount(4);

    QCOMPARE(geometry.sectionCount(), 4);
    QCOMPARE(geometry.visibleSectionCount(), 4);
    QCOMPARE(geometry.hiddenSectionCount(), 0);
    QCOMPARE(geometry.totalExtent(), qint64(480));
    for (int logical = 0; logical < 4; ++logical) {
        QCOMPARE(geometry.sectionSize(logical), 120);
        QCOMPARE(geometry.sectionPosition(logical), qint64(logical) * 120);
        QCOMPARE(geometry.visualIndex(logical), logical);
        QCOMPARE(geometry.logicalIndex(logical), logical);
    }

    const ColumnGeometry column = geometry.columnGeometry(2);
    QCOMPARE(column.logicalIndex, 2);
    QCOMPARE(column.visualIndex, 2);
    QCOMPARE(column.contentX, qint64(240));
    QCOMPARE(column.viewportX, 240);
    QCOMPARE(column.width, 120);
    QVERIFY(!column.hidden);

    // Out of range queries are safe.
    QCOMPARE(geometry.sectionSize(-1), 0);
    QCOMPARE(geometry.sectionSize(4), 0);
    QVERIFY(!geometry.columnGeometry(9).isValid());
    QCOMPARE(geometry.visualIndex(9), -1);
    QCOMPARE(geometry.logicalIndex(9), -1);
}

void TestHeaderGeometry::resizeSectionRespectsLimits()
{
    HeaderGeometry geometry;
    geometry.setMinimumSectionSize(40);
    geometry.setMaximumSectionSize(200);
    geometry.setSectionCount(3);
    geometry.resizeSection(1, 500);

    QCOMPARE(geometry.sectionSize(1), 200);
    geometry.resizeSection(1, 5);
    QCOMPARE(geometry.sectionSize(1), 40);

    QSignalSpy resizedSpy(&geometry, &HeaderGeometry::sectionResized);
    geometry.resizeSection(1, 150);
    QCOMPARE(geometry.sectionSize(1), 150);
    QCOMPARE(resizedSpy.count(), 1);
    QCOMPARE(resizedSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(resizedSpy.at(0).at(1).toInt(), 40);
    QCOMPARE(resizedSpy.at(0).at(2).toInt(), 150);
    QCOMPARE(geometry.totalExtent(), qint64(150 + 100 + 100));
}

void TestHeaderGeometry::explicitSizesSurviveDefaultChanges()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(3);
    geometry.resizeSection(1, 250);

    QVERIFY(geometry.isSectionSizeExplicit(1));
    QVERIFY(!geometry.isSectionSizeExplicit(0));

    geometry.setDefaultSectionSize(60);
    QCOMPARE(geometry.sectionSize(0), 60);
    QCOMPARE(geometry.sectionSize(1), 250);
    QCOMPARE(geometry.sectionSize(2), 60);

    geometry.clearExplicitSectionSize(1);
    geometry.setDefaultSectionSize(70);
    QCOMPARE(geometry.sectionSize(1), 70);
}

void TestHeaderGeometry::visualOrderFollowsMoveSection()
{
    HeaderGeometry geometry;
    geometry.setMinimumSectionSize(5);
    geometry.setDefaultSectionSize(50);
    geometry.setSectionCount(4);
    geometry.resizeSection(0, 10);
    geometry.resizeSection(1, 20);
    geometry.resizeSection(2, 30);
    geometry.resizeSection(3, 40);

    QSignalSpy movedSpy(&geometry, &HeaderGeometry::sectionMoved);
    geometry.moveSection(0, 2); // visual: 1, 2, 0, 3

    QCOMPARE(geometry.logicalIndex(0), 1);
    QCOMPARE(geometry.logicalIndex(1), 2);
    QCOMPARE(geometry.logicalIndex(2), 0);
    QCOMPARE(geometry.logicalIndex(3), 3);
    QCOMPARE(geometry.visualIndex(0), 2);
    QCOMPARE(geometry.visualIndex(3), 3);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(movedSpy.at(0).at(0).toInt(), 0);

    // Positions follow the visual order, sizes follow the columns.
    QCOMPARE(geometry.sectionPosition(1), qint64(0));
    QCOMPARE(geometry.sectionPosition(2), qint64(20));
    QCOMPARE(geometry.sectionPosition(0), qint64(50));
    QCOMPARE(geometry.sectionPosition(3), qint64(60));
    QCOMPARE(geometry.sectionSize(0), 10);
    QCOMPARE(geometry.totalExtent(), qint64(100));
}

void TestHeaderGeometry::hiddenSectionsAreSkipped()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(40);
    geometry.setSectionCount(4);

    QSignalSpy visibilitySpy(&geometry, &HeaderGeometry::sectionVisibilityChanged);
    geometry.setSectionHidden(1, true);

    QVERIFY(geometry.isSectionHidden(1));
    QCOMPARE(geometry.hiddenSectionCount(), 1);
    QCOMPARE(geometry.visibleSectionCount(), 3);
    QCOMPARE(geometry.sectionSize(1), 0);
    QCOMPARE(geometry.storedSectionSize(1), 40);
    QCOMPARE(geometry.totalExtent(), qint64(120));
    QCOMPARE(geometry.sectionPosition(2), qint64(40));
    QCOMPARE(visibilitySpy.count(), 1);
    QCOMPARE(visibilitySpy.at(0).at(0).toInt(), 1);
    QCOMPARE(visibilitySpy.at(0).at(1).toBool(), false);

    geometry.setSectionHidden(1, false);
    QCOMPARE(geometry.sectionSize(1), 40);
    QCOMPARE(geometry.totalExtent(), qint64(160));
}

void TestHeaderGeometry::sectionLookupByOffset()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(25);
    geometry.setSectionCount(4);
    geometry.setSectionHidden(2, true);

    QCOMPARE(geometry.sectionAtOffset(-5), 0);
    QCOMPARE(geometry.sectionAtOffset(0), 0);
    QCOMPARE(geometry.sectionAtOffset(24), 0);
    QCOMPARE(geometry.sectionAtOffset(25), 1);
    QCOMPARE(geometry.sectionAtOffset(49), 1);
    QCOMPARE(geometry.sectionAtOffset(50), 3); // 2 is hidden
    QCOMPARE(geometry.sectionAtOffset(1000), 3);
    QCOMPARE(geometry.visualSectionAtOffset(50), 3);

    const VisibleRange range = geometry.visibleVisualRange(60);
    QCOMPARE(range.first, qsizetype(0));
    QCOMPARE(range.last, qsizetype(3));
    QCOMPARE(geometry.visibleVisualRange(0).isValid(), false);

    HeaderGeometry empty;
    QCOMPARE(empty.sectionAtOffset(0), -1);
    QVERIFY(!empty.visibleVisualRange(100).isValid());
}

void TestHeaderGeometry::viewportOffsetMovesViewportPositions()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(5);
    geometry.setViewportOffset(150);

    QCOMPARE(geometry.viewportOffset(), qint64(150));
    QCOMPARE(geometry.sectionViewportPosition(0), -150);
    QCOMPARE(geometry.sectionViewportPosition(2), 50);
    QCOMPARE(geometry.columnGeometry(2).viewportX, 50);
    QCOMPARE(geometry.maximumViewportOffset(200), qint64(300));
    QCOMPARE(geometry.maximumViewportOffset(500), qint64(0));
    QCOMPARE(geometry.maximumViewportOffset(1000), qint64(0));

    geometry.setViewportOffset(-20);
    QCOMPARE(geometry.viewportOffset(), qint64(0));
}

void TestHeaderGeometry::moveLogicalSectionsFollowsModel()
{
    HeaderGeometry geometry;
    geometry.setMinimumSectionSize(5);
    geometry.setDefaultSectionSize(50);
    geometry.setSectionCount(4);
    geometry.resizeSection(0, 10);
    geometry.resizeSection(1, 20);
    geometry.resizeSection(2, 30);
    geometry.resizeSection(3, 40);
    geometry.setSectionHidden(1, true);

    // The model moved column 0 before column 2 (pre-move coordinates), the same
    // convention as QAbstractItemModel::beginMoveRows().
    geometry.moveLogicalSections(0, 1, 2);

    QCOMPARE(geometry.sectionCount(), 4);
    // Sizes and visibility travelled with the columns: 1, 0, 2, 3.
    QCOMPARE(geometry.storedSectionSize(0), 20);
    QCOMPARE(geometry.storedSectionSize(1), 10);
    QCOMPARE(geometry.storedSectionSize(2), 30);
    QCOMPARE(geometry.storedSectionSize(3), 40);
    QVERIFY(geometry.isSectionHidden(0));
    QVERIFY(!geometry.isSectionHidden(1));
    QCOMPARE(geometry.totalExtent(), qint64(10 + 30 + 40));
}

void TestHeaderGeometry::structuralRemapsReportTheSortIndicator()
{
    // P1-6 of the second review: the indicator names a *column*, so a structural remap that
    // renames it has to say so - otherwise code that only listens to sortIndicatorChanged
    // (or a renderer that rebuilds the section UI on that signal) keeps the old number.
    HeaderGeometry geometry;
    geometry.setSectionCount(6);
    geometry.setSortIndicator(4, Qt::DescendingOrder);
    // Qt 5 needs the metatype registered before QSignalSpy can record the enum argument (a
    // renderer's direct connection does not need it - Qt 6 registers enums automatically).
    qRegisterMetaType<Qt::SortOrder>("Qt::SortOrder");
    QSignalSpy spy(&geometry, &HeaderGeometry::sortIndicatorChanged);
    QVERIFY(spy.isValid());

    // Insert in front of it: the number shifts.
    geometry.insertLogicalSections(1, 1);
    QCOMPARE(geometry.sortIndicatorSection(), 5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 5);
    QCOMPARE(spy.at(0).at(1).toInt(), int(Qt::DescendingOrder));

    // Remove in front of it: the number shifts down.
    spy.clear();
    geometry.removeLogicalSections(0, 1);
    QCOMPARE(geometry.sortIndicatorSection(), 4);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 4);

    // Remove the sorted column itself: the indicator is cleared.
    spy.clear();
    geometry.removeLogicalSections(4, 1);
    QCOMPARE(geometry.sortIndicatorSection(), -1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), -1);

    // A model move remaps it as well.
    geometry.setSortIndicator(0, Qt::AscendingOrder);
    spy.clear();
    geometry.moveLogicalSections(0, 1, 3);
    QCOMPARE(geometry.sortIndicatorSection(), 2);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2);
}

void TestHeaderGeometry::insertedSectionsTakeTheSuccessorVisualSlot()
{
    // P1-4 of the second review: a middle insert used to append the new sections to the end
    // of the visual order, so an untouched header (visual order == logical order) showed
    // "A | B | C | X" after inserting X before B instead of "A | X | B | C" - which is what
    // QHeaderView does.
    HeaderGeometry identity(Qt::Horizontal);
    identity.setSectionCount(3);
    identity.resizeSection(1, 77);           // B keeps its own state
    identity.insertLogicalSections(1, 1);    // A X B C

    QCOMPARE(identity.sectionCount(), 4);
    QVector<int> visual;
    for (int slot = 0; slot < identity.sectionCount(); ++slot)
        visual.append(identity.logicalIndex(slot));
    QCOMPARE(visual, QVector<int>({0, 1, 2, 3}));            // stays in logical order
    QCOMPARE(identity.storedSectionSize(1), identity.defaultSectionSize()); // X is new
    QCOMPARE(identity.storedSectionSize(2), 77);             // B followed its item

    // A custom visual order keeps its shape: move A to the end first (B C A), then insert a
    // column before C (= logical 1). The new column takes C's visual slot.
    HeaderGeometry custom(Qt::Horizontal);
    custom.setSectionCount(3);
    custom.moveSection(0, 2);                                // visual B C A = logical 1 2 0
    custom.insertLogicalSections(1, 1);                      // Y B C A  (Y = the new logical 1)
    QVector<int> customVisual;
    for (int slot = 0; slot < custom.sectionCount(); ++slot)
        customVisual.append(custom.logicalIndex(slot));
    QCOMPARE(customVisual, QVector<int>({1, 2, 3, 0}));

    // Appending at the end still appends: there is no successor to take a slot from.
    HeaderGeometry appended(Qt::Horizontal);
    appended.setSectionCount(3);
    appended.insertLogicalSections(3, 2);
    QVector<int> appendedVisual;
    for (int slot = 0; slot < appended.sectionCount(); ++slot)
        appendedVisual.append(appended.logicalIndex(slot));
    QCOMPARE(appendedVisual, QVector<int>({0, 1, 2, 3, 4}));
}

void TestHeaderGeometry::sortIndicatorRoundTrip()
{
    HeaderGeometry geometry;
    geometry.setSectionCount(3);
    QSignalSpy sortSpy(&geometry, &HeaderGeometry::sortIndicatorChanged);

    geometry.setSortIndicator(1, Qt::DescendingOrder);
    QCOMPARE(geometry.sortIndicatorSection(), 1);
    QCOMPARE(geometry.sortIndicatorOrder(), Qt::DescendingOrder);
    QCOMPARE(sortSpy.count(), 1);

    geometry.setSortIndicator(99, Qt::AscendingOrder);
    QCOMPARE(geometry.sortIndicatorSection(), -1);
}

void TestHeaderGeometry::saveRestoreRoundTrip()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(90);
    geometry.setSectionCount(4);
    geometry.resizeSection(1, 200);
    geometry.setSectionHidden(3, true);
    geometry.moveSection(0, 3);
    geometry.setViewportOffset(77);
    geometry.setSortIndicator(2, Qt::DescendingOrder);
    geometry.setStretchLastSection(true);

    const QByteArray state = geometry.saveState();
    QVERIFY(!state.isEmpty());

    HeaderGeometry restored;
    restored.setSectionCount(4);
    QVERIFY(restored.restoreState(state));

    QCOMPARE(restored.sectionCount(), 4);
    QCOMPARE(restored.sectionSize(1), 200);
    QVERIFY(restored.isSectionHidden(3));
    QCOMPARE(restored.totalExtent(), geometry.totalExtent());
    QCOMPARE(restored.viewportOffset(), qint64(77));
    QCOMPARE(restored.sortIndicatorSection(), 2);
    QCOMPARE(restored.sortIndicatorOrder(), Qt::DescendingOrder);
    QVERIFY(restored.stretchLastSection());
    for (int visual = 0; visual < 4; ++visual)
        QCOMPARE(restored.logicalIndex(visual), geometry.logicalIndex(visual));

    // An orientation mismatch is rejected too.
    HeaderGeometry vertical(Qt::Vertical);
    vertical.setSectionCount(4);
    QVERIFY(!vertical.restoreState(state));
}

void TestHeaderGeometry::restoreRejectsMismatchedState()
{
    HeaderGeometry geometry;
    geometry.setSectionCount(3);
    const QByteArray state = geometry.saveState();

    HeaderGeometry other;
    other.setSectionCount(5);
    QVERIFY(!other.restoreState(state));
    QCOMPARE(other.sectionCount(), 5);

    QVERIFY(!geometry.restoreState(QByteArray()));
    QVERIFY(!geometry.restoreState(QByteArrayLiteral("garbage")));

    QByteArray tampered = state;
    tampered[0] = char(0x00);
    HeaderGeometry third;
    third.setSectionCount(3);
    QVERIFY(!third.restoreState(tampered));
}

void TestHeaderGeometry::sectionCountChangeKeepsState()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(3);
    geometry.resizeSection(1, 250);
    geometry.setSectionHidden(0, true);

    QSignalSpy countSpy(&geometry, &HeaderGeometry::sectionCountChanged);
    geometry.setSectionCount(5);

    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(countSpy.at(0).at(0).toInt(), 5);
    QCOMPARE(geometry.sectionSize(1), 250);
    QVERIFY(geometry.isSectionHidden(0));
    QCOMPARE(geometry.sectionSize(4), 100);
    QCOMPARE(geometry.visualIndex(4), 4);
    QCOMPARE(geometry.totalExtent(), qint64(250 + 100 + 100 + 100));

    geometry.setSectionCount(2);
    QCOMPARE(geometry.sectionCount(), 2);
    QCOMPARE(geometry.visualIndex(1), 1);
}

void TestHeaderGeometry::shrinkingTheSectionSetClearsAnOutOfRangeSortIndicator()
{
    // P1-7 of the second review: setSectionCount() is the path a model reset / setModel
    // takes, and it left a sort indicator pointing at an index that no longer exists.
    HeaderGeometry geometry;
    geometry.setSectionCount(10);
    geometry.setSortIndicator(8, Qt::DescendingOrder);
    QSignalSpy spy(&geometry, &HeaderGeometry::sortIndicatorChanged);
    QVERIFY(spy.isValid());

    geometry.setSectionCount(3);
    QCOMPARE(geometry.sectionCount(), 3);
    QCOMPARE(geometry.sortIndicatorSection(), -1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), -1);

    // A surviving indicator is left alone (and reports nothing).
    geometry.setSectionCount(10);
    geometry.setSortIndicator(2, Qt::AscendingOrder);
    spy.clear();
    geometry.setSectionCount(4);
    QCOMPARE(geometry.sortIndicatorSection(), 2);
    QCOMPARE(spy.count(), 0);
}

void TestHeaderGeometry::changingLimitsKeepsImplicitSizesImplicit()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(4);
    geometry.resizeSection(1, 250);                  // one explicit section
    QVERIFY(geometry.isSectionSizeExplicit(1));

    // Raising the minimum clamps the sections, but must not turn them explicit:
    // they still follow the default size afterwards.
    geometry.setMinimumSectionSize(150);
    QCOMPARE(geometry.sectionSize(0), 150);
    QCOMPARE(geometry.sectionSize(2), 150);
    QCOMPARE(geometry.sectionSize(1), 250);          // the explicit one keeps its size
    QVERIFY(!geometry.isSectionSizeExplicit(0));
    QVERIFY(!geometry.isSectionSizeExplicit(2));
    QVERIFY(geometry.isSectionSizeExplicit(1));

    geometry.setDefaultSectionSize(180);
    QCOMPARE(geometry.sectionSize(0), 180);
    QCOMPARE(geometry.sectionSize(1), 250);

    // ... and lowering the maximum clamps them without freezing them either.
    geometry.setMaximumSectionSize(200);
    QCOMPARE(geometry.sectionSize(1), 200);
    QVERIFY(geometry.isSectionSizeExplicit(1));
    QCOMPARE(geometry.sectionSize(0), 180);
    geometry.setDefaultSectionSize(60);              // clamped to the minimum 150
    QCOMPARE(geometry.sectionSize(0), 150);
    QVERIFY(!geometry.isSectionSizeExplicit(0));
}

void TestHeaderGeometry::changingLimitsClampsTheDefaultSize()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(2);

    // The default follows the range, so a section created later cannot start
    // below the minimum (or above the maximum).
    geometry.setMinimumSectionSize(200);
    QCOMPARE(geometry.defaultSectionSize(), 200);
    geometry.setSectionCount(3);
    QCOMPARE(geometry.sectionSize(2), 200);

    geometry.setMaximumSectionSize(120);
    QCOMPARE(geometry.maximumSectionSize(), 200);    // the minimum wins over the maximum
    QCOMPARE(geometry.defaultSectionSize(), 200);
    geometry.setMinimumSectionSize(24);
    QCOMPARE(geometry.minimumSectionSize(), 24);
    geometry.setMaximumSectionSize(120);
    QCOMPARE(geometry.maximumSectionSize(), 120);
    QCOMPARE(geometry.defaultSectionSize(), 120);
}

void TestHeaderGeometry::changingLimitsEmitsOneBulkGeometryChange()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(500);
    QSignalSpy geometrySpy(&geometry, &HeaderGeometry::geometryChanged);
    QSignalSpy resizeSpy(&geometry, &HeaderGeometry::sectionResized);

    // One pass, one bulk signal - not one full renderer sync per section.
    geometry.setMinimumSectionSize(150);
    QCOMPARE(geometrySpy.count(), 1);
    QCOMPARE(resizeSpy.count(), 500);
    QCOMPARE(geometry.sectionSize(499), 150);

    geometrySpy.clear();
    resizeSpy.clear();
    geometry.setMinimumSectionSize(150);              // no-op: nothing changes at all
    QCOMPARE(geometrySpy.count(), 0);
    QCOMPARE(resizeSpy.count(), 0);
}

void TestHeaderGeometry::changingLimitsNotifiesEvenWithoutClamping()
{
    // P1-3 of the second review: when a new minimum did not clamp anything, the setter
    // returned without a notification - but a renderer keeps its own copy of the range
    // (QHeaderView::setMinimumSectionSize()) and only re-reads it on that notification.
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(20);
    geometry.setMinimumSectionSize(24);
    QSignalSpy geometrySpy(&geometry, &HeaderGeometry::geometryChanged);
    QVERIFY(geometrySpy.isValid());

    geometry.setMinimumSectionSize(30);              // 100 > 30: nothing to clamp
    QCOMPARE(geometry.minimumSectionSize(), 30);
    QCOMPARE(geometrySpy.count(), 1);                // ... but the range changed

    geometrySpy.clear();
    geometry.setMaximumSectionSize(50000);           // nothing to clamp either
    QCOMPARE(geometry.maximumSectionSize(), 50000);
    QCOMPARE(geometrySpy.count(), 1);

    // A repeated set is still a no-op.
    geometrySpy.clear();
    geometry.setMinimumSectionSize(30);
    QCOMPARE(geometrySpy.count(), 0);
}

void TestHeaderGeometry::orderRevisionOnlyMovesWhenTheOrderCanChange()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(4);
    const quint32 initial = geometry.orderRevision();

    // Scrolling, resizing, hiding a *sort* indicator: none of them can change the
    // order a renderer laid out, so a renderer may skip re-deriving it.
    geometry.setViewportOffset(300);
    geometry.resizeSection(1, 150);
    geometry.setSortIndicator(2, Qt::AscendingOrder);
    geometry.setStretchLastSection(true);
    QCOMPARE(geometry.orderRevision(), initial);

    // These can, and they bump the revision.
    geometry.setSectionHidden(0, true);
    const quint32 afterHide = geometry.orderRevision();
    QVERIFY(afterHide != initial);
    geometry.moveSection(3, 0);
    QVERIFY(geometry.orderRevision() != afterHide);
    const quint32 afterMove = geometry.orderRevision();
    geometry.setSectionCount(6);              // appended sections join the order
    QVERIFY(geometry.orderRevision() != afterMove);

    // A model-side move remaps the logical indices of the visual order, so a renderer that
    // caches "logical index per visual slot" has to see a new revision as well (P1-5 of the
    // second review: moveLogicalSections() did not bump it).
    const quint32 afterAppend = geometry.orderRevision();
    geometry.moveLogicalSections(0, 1, 3);
    QVERIFY(geometry.orderRevision() != afterAppend);
}

QTEST_APPLESS_MAIN(TestHeaderGeometry)

#include "tst_headergeometry.moc"
