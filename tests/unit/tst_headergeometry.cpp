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
    void granularChangesDoNotEmitTheBulkSignal();
    void restoringAStateBumpsTheOrderRevision();
    void uniformLargeCountStoresNothingPerSection();
    void perSectionEditMaterialisesTheStoredState();
    void sparseSizeOverridesKeepTheLargeCountCompact();
    void stretchFactorsShareTheLeftoverExtent();
    void stretchWithoutAnExtentChangesNothing();
    void stretchFollowsVisibilityAndTheSizeRange();
    void resizingAStretchingSectionFixesItsWidth();
    void stretchFactorsSurviveTheStateRoundTrip();
    void sparseStretchKeepsTheLargeCountCompact();
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

void TestHeaderGeometry::restoringAStateBumpsTheOrderRevision()
{
    // P1 of the third review: restoreState() replaces the visual order and the hidden set -
    // both are exactly what orderRevision() is a contract for, but the revision stayed put,
    // so a renderer that skips re-deriving the order (VirtualHeaderView does) kept the old
    // one.
    HeaderGeometry geometry;
    geometry.setSectionCount(6);
    const QByteArray state = geometry.saveState();
    const quint32 before = geometry.orderRevision();

    geometry.setSectionHidden(2, true);          // a change that bumps the revision
    geometry.moveSection(0, 3);
    const quint32 changed = geometry.orderRevision();
    QVERIFY(changed != before);

    QVERIFY(geometry.restoreState(state));
    QVERIFY(geometry.orderRevision() != changed);
    QVERIFY(!geometry.isSectionHidden(2));
}

void TestHeaderGeometry::granularChangesDoNotEmitTheBulkSignal()
{
    // P1-10 of the second review: HeaderGeometry notified "something changed" through
    // geometryChanged() for *every* change, so a renderer that applied sectionResized()
    // granularly still ran its O(sections) full sync. The bulk signal is now reserved for
    // the changes that have no granular counterpart.
    HeaderGeometry geometry;
    geometry.setSectionCount(20);
    QSignalSpy bulkSpy(&geometry, &HeaderGeometry::bulkGeometryChanged);
    QSignalSpy resizedSpy(&geometry, &HeaderGeometry::sectionResized);
    QSignalSpy geometrySpy(&geometry, &HeaderGeometry::geometryChanged);
    QVERIFY(bulkSpy.isValid());

    // A resize: granular, no bulk.
    geometry.resizeSection(3, 150);
    QCOMPARE(resizedSpy.count(), 1);
    QCOMPARE(bulkSpy.count(), 0);
    QCOMPARE(geometrySpy.count(), 1);   // the generic signal is unchanged for everyone else

    // Visibility and the sort indicator are granular as well.
    geometry.setSectionHidden(4, true);
    geometry.setSortIndicator(5, Qt::AscendingOrder);
    QCOMPARE(bulkSpy.count(), 0);

    // The size range, the default size, the stretch flag and a restored state are bulk.
    geometry.setMinimumSectionSize(30);
    QCOMPARE(bulkSpy.count(), 1);
    geometry.setDefaultSectionSize(90);
    QCOMPARE(bulkSpy.count(), 2);
    geometry.setStretchLastSection(true);
    QCOMPARE(bulkSpy.count(), 3);
    const QByteArray state = geometry.saveState();
    geometry.setSectionCount(5);
    geometry.setSectionCount(20);
    geometry.restoreState(state);
    QVERIFY(bulkSpy.count() >= 4);

    // A model-side move renames many sections at once: bulk.
    const int before = bulkSpy.count();
    geometry.moveLogicalSections(0, 1, 6);
    QCOMPARE(bulkSpy.count(), before + 1);
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
    // A geometry whose sections are all default-sized is *uniform*: it stores nothing per
    // section, so a limit change is one bulk change - a renderer re-reads the geometry on
    // bulkGeometryChanged() (that is what its documented purpose is). The per-section
    // signals belong to the stored representation: a geometry that owns per-section state
    // reports each clamped section, because there each section can differ.
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(500);
    QSignalSpy geometrySpy(&geometry, &HeaderGeometry::geometryChanged);
    QSignalSpy resizeSpy(&geometry, &HeaderGeometry::sectionResized);

    // Uniform: one pass, one bulk signal, no per-section signal at all.
    geometry.setMinimumSectionSize(150);
    QCOMPARE(geometrySpy.count(), 1);
    QCOMPARE(resizeSpy.count(), 0);
    QCOMPARE(geometry.sectionSize(499), 150);
    QCOMPARE(geometry.storedSectionStateCount(), 0);

    // A size edit stays in the sparse tier (one entry), so the clamping is still a bulk
    // change - and the one section that *has* stored state is reported individually.
    geometry.resizeSection(3, 300);
    QCOMPARE(geometry.storedSectionStateCount(), 1);
    geometrySpy.clear();
    resizeSpy.clear();
    geometry.setMinimumSectionSize(400);
    QCOMPARE(geometrySpy.count(), 1);
    QCOMPARE(resizeSpy.count(), 1);
    QCOMPARE(geometry.sectionSize(3), 400);
    QCOMPARE(geometry.defaultSectionSize(), 400);

    // Structural state (an order or visibility change) is the indexed representation: the
    // clamping then reports every stored section.
    geometry.setSectionHidden(5, true);
    QCOMPARE(geometry.storedSectionStateCount(), 500);
    geometrySpy.clear();
    resizeSpy.clear();
    geometry.setMinimumSectionSize(500);
    QCOMPARE(geometrySpy.count(), 1);
    QCOMPARE(resizeSpy.count(), 500);
    QCOMPARE(geometry.defaultSectionSize(), 500);

    geometrySpy.clear();
    resizeSpy.clear();
    geometry.setMinimumSectionSize(500);              // no-op: nothing changes at all
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

void TestHeaderGeometry::uniformLargeCountStoresNothingPerSection()
{
    // The point of the uniform representation: the logical section count is no longer
    // bound to the number of stored per-section states. A 10M-row strip needs
    // "count + default size" and nothing per row - that is what keeps a uniform
    // ten-million-row table from being densified just to draw row numbers.
    constexpr int kSections = 10'000'000;
    constexpr int kSize = 24;
    HeaderGeometry geometry(Qt::Vertical);
    geometry.setSectionCount(kSections);
    geometry.setDefaultSectionSize(kSize);

    QCOMPARE(geometry.sectionCount(), kSections);
    QCOMPARE(geometry.storedSectionStateCount(), 0);
    QVERIFY(geometry.isUniform());

    // Sizes, order and positions: all implied, all in constant time, all exact in 64 bit.
    QCOMPARE(geometry.sectionSize(0), kSize);
    QCOMPARE(geometry.sectionSize(kSections - 1), kSize);
    QCOMPARE(geometry.visualIndex(12'345), 12'345);
    QCOMPARE(geometry.logicalIndex(12'345), 12'345);
    QCOMPARE(geometry.sectionPosition(0), qint64(0));
    QCOMPARE(geometry.sectionPosition(kSections - 1), qint64(kSections - 1) * kSize);
    QCOMPARE(geometry.totalExtent(), qint64(kSections) * kSize);
    QCOMPARE(geometry.columnGeometry(kSections - 1).width, kSize);
    QVERIFY(!geometry.columnGeometry(kSections - 1).hidden);

    // Offset lookup: beginning, middle, near the end, the last section.
    QCOMPARE(geometry.sectionAtOffset(0), 0);
    QCOMPARE(geometry.sectionAtOffset(kSize * 5 + 3), 5);
    const qint64 middle = qint64(kSections / 2) * kSize;
    QCOMPARE(geometry.sectionAtOffset(middle), kSections / 2);
    QCOMPARE(geometry.sectionAtOffset(geometry.totalExtent() - 1), kSections - 1);

    // Visible windows: a scroll must resolve in O(1), not by walking the sections.
    for (qint64 offset : {qint64(0), middle - 3 * kSize, geometry.totalExtent() - 1000}) {
        geometry.setViewportOffset(offset);
        const VisibleRange range = geometry.visibleVisualRange(800);
        QVERIFY(range.isValid());
        QCOMPARE(range.first, int(offset / kSize));
        QCOMPARE(range.count(), qsizetype(800 / kSize + 1));
    }

    // A count change on a uniform geometry is still O(1) and still stores nothing.
    geometry.setSectionCount(kSections + 4096);
    QCOMPARE(geometry.storedSectionStateCount(), 0);
    QCOMPARE(geometry.totalExtent(), qint64(kSections + 4096) * kSize);

    // State round trip: the serialised form is per-section, so it is generated from the
    // implied state. (Done on a small count - writing 10M sections is an explicit O(N)
    // operation, not something the suite should pay for.)
    constexpr int kSmall = 5000;
    HeaderGeometry small(Qt::Vertical);
    small.setSectionCount(kSmall);
    small.setDefaultSectionSize(kSize);
    QCOMPARE(small.storedSectionStateCount(), 0);
    const QByteArray state = small.saveState();
    HeaderGeometry restored(Qt::Vertical);
    restored.setSectionCount(kSmall);
    QVERIFY(restored.restoreState(state));
    QCOMPARE(restored.sectionSize(1234), kSize);
    QCOMPARE(restored.storedSectionStateCount(), kSmall);
    QCOMPARE(restored.totalExtent(), small.totalExtent());
}

void TestHeaderGeometry::perSectionEditMaterialisesTheStoredState()
{
    // A *size* edit is the sparse case: it stays uniform and stores one entry, whatever
    // the section count is (the row-boundary drag at ten million rows, §5/§12 of the
    // vertical-header decision).
    constexpr int kSections = 100'000;
    HeaderGeometry geometry(Qt::Vertical);
    geometry.setSectionCount(kSections);
    QCOMPARE(geometry.storedSectionStateCount(), 0);

    geometry.resizeSection(3, 40);
    QCOMPARE(geometry.storedSectionStateCount(), 1);
    QCOMPARE(geometry.sectionSize(3), 40);
    QCOMPARE(geometry.sectionSize(4), geometry.defaultSectionSize());
    QVERIFY(geometry.isSectionSizeExplicit(3));
    QCOMPARE(geometry.sectionPosition(4), qint64(3) * geometry.defaultSectionSize() + 40);
    QCOMPARE(geometry.totalExtent(),
             qint64(kSections) * geometry.defaultSectionSize() + (40 - geometry.defaultSectionSize()));

    // Clearing it returns to the fully compact state - still nothing per section.
    geometry.clearExplicitSectionSize(3);
    QCOMPARE(geometry.storedSectionStateCount(), 0);
    QCOMPARE(geometry.totalExtent(), qint64(kSections) * geometry.defaultSectionSize());

    // A *structural* edit (order or visibility) is the indexed case: it materialises the
    // per-section state once, and the sparse overrides become ordinary state.
    HeaderGeometry structural(Qt::Vertical);
    structural.setSectionCount(kSections);
    structural.resizeSection(3, 40);
    structural.setSectionHidden(7, true);
    QCOMPARE(structural.storedSectionStateCount(), kSections);
    QVERIFY(!structural.isUniform());
    QCOMPARE(structural.sectionSize(3), 40);
    QVERIFY(structural.isSectionSizeExplicit(3));

    // ... and a per-section edit that changes nothing keeps the compact representation.
    HeaderGeometry untouched(Qt::Vertical);
    untouched.setSectionCount(kSections);
    untouched.resizeSection(3, untouched.defaultSectionSize());
    untouched.setSectionHidden(3, false);
    untouched.clearExplicitSectionSize(9);
    QCOMPARE(untouched.storedSectionStateCount(), 0);
    QVERIFY(untouched.isUniform());
}

void TestHeaderGeometry::sparseSizeOverridesKeepTheLargeCountCompact()
{
    // Ten million uniform rows, one of them resized: one stored entry, and every
    // position/extent/lookup still accounts for it exactly.
    constexpr int kSections = 10'000'000;
    constexpr int kSize = 24;
    HeaderGeometry geometry(Qt::Vertical);
    geometry.setSectionCount(kSections);
    geometry.setDefaultSectionSize(kSize);
    geometry.resizeSection(123, 48);

    QCOMPARE(geometry.storedSectionStateCount(), 1);
    QVERIFY(geometry.isUniform());
    QCOMPARE(geometry.sectionSize(123), 48);
    QCOMPARE(geometry.sectionSize(124), kSize);
    QVERIFY(geometry.isSectionSizeExplicit(123));
    QVERIFY(!geometry.isSectionSizeExplicit(124));
    QCOMPARE(geometry.sectionPosition(123), qint64(123) * kSize);
    QCOMPARE(geometry.sectionPosition(124), qint64(123) * kSize + 48);
    QCOMPARE(geometry.sectionPosition(kSections - 1), qint64(kSections) * kSize + 24 - kSize);
    QCOMPARE(geometry.totalExtent(), qint64(kSections) * kSize + 24);

    // Offset lookup around the override, and far away from it.
    QCOMPARE(geometry.sectionAtOffset(qint64(123) * kSize), 123);
    QCOMPARE(geometry.sectionAtOffset(qint64(123) * kSize + 47), 123);
    QCOMPARE(geometry.sectionAtOffset(qint64(123) * kSize + 48), 124);
    QCOMPARE(geometry.sectionAtOffset(geometry.totalExtent() - 1), kSections - 1);
    QCOMPARE(geometry.columnGeometry(124).contentX, qint64(123) * kSize + 48);
    QCOMPARE(geometry.columnGeometry(124).width, kSize);

    // A second override on the other side of the set stays exact too.
    geometry.resizeSection(kSections - 1, 60);
    QCOMPARE(geometry.storedSectionStateCount(), 2);
    QCOMPARE(geometry.totalExtent(), qint64(kSections) * kSize + 24 + 36);
    QCOMPARE(geometry.sectionPosition(kSections - 1), qint64(kSections - 1) * kSize + 24);
    QCOMPARE(geometry.sectionAtOffset(geometry.totalExtent() - 1), kSections - 1);
}

void TestHeaderGeometry::stretchFactorsShareTheLeftoverExtent()
{
    // A table hands in the width its columns have to cover: the sections that keep their
    // own size take theirs first, the ones with a factor split the rest - here 1 : 2 : 1.
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(5);
    geometry.resizeSection(0, 60);
    geometry.setSectionStretchFactor(2, 1.0);
    geometry.setSectionStretchFactor(3, 2.0);
    geometry.setSectionStretchFactor(4, 1.0);
    QCOMPARE(geometry.sectionStretchFactor(3), 2.0);
    QCOMPARE(geometry.sectionStretchFactor(1), 0.0);
    // Without an extent nothing is distributed - and nothing claims to stretch: the columns
    // keep their own sizes.
    QVERIFY(!geometry.hasStretchSections());
    QCOMPARE(geometry.totalExtent(), qint64(460));

    geometry.setStretchExtent(1060);
    QVERIFY(geometry.hasStretchSections());
    QCOMPARE(geometry.stretchExtent(), qint64(1060));
    QCOMPARE(geometry.sectionSize(0), 60);       // fixed
    QCOMPARE(geometry.sectionSize(1), 100);      // fixed
    QCOMPARE(geometry.sectionSize(2), 225);      // 900 * 1/4
    QCOMPARE(geometry.sectionSize(3), 450);      // 900 * 2/4
    QCOMPARE(geometry.sectionSize(4), 225);      // the leftover (rounding included)
    QCOMPARE(geometry.totalExtent(), qint64(1060));
    QVERIFY(!geometry.isSectionSizeExplicit(2)); // the width is derived, not set
    QVERIFY(geometry.isSectionSizeExplicit(0));

    // Positions and lookups follow the derived widths.
    QCOMPARE(geometry.sectionPosition(3), qint64(385));
    QCOMPARE(geometry.columnGeometry(4).contentX, qint64(835));
    QCOMPARE(geometry.columnGeometry(4).width, 225);
    QCOMPARE(geometry.sectionAtOffset(835), 4);
    QCOMPARE(geometry.sectionAtOffset(834), 3);

    // A narrower extent re-distributes the same ratios.
    geometry.setStretchExtent(1000);
    QCOMPARE(geometry.sectionSize(2), 210);
    QCOMPARE(geometry.sectionSize(3), 420);
    QCOMPARE(geometry.sectionSize(4), 210);
    QCOMPARE(geometry.totalExtent(), qint64(1000));

    // Fractional factors are allowed and still add up: 1 : 1.5 : 0.5.
    geometry.setSectionStretchFactor(2, 1.0);
    geometry.setSectionStretchFactor(3, 1.5);
    geometry.setSectionStretchFactor(4, 0.5);
    QCOMPARE(geometry.sectionSize(2) + geometry.sectionSize(3) + geometry.sectionSize(4),
             840);
    QVERIFY(geometry.sectionSize(3) > geometry.sectionSize(2));
    QVERIFY(geometry.sectionSize(2) > geometry.sectionSize(4));

    // Clearing every factor leaves the sections at the width they were stretched to, with
    // no target in play any more.
    for (int logical = 2; logical <= 4; ++logical)
        geometry.setSectionStretchFactor(logical, 0.0);
    QVERIFY(!geometry.hasStretchSections());
    const qint64 frozen = geometry.totalExtent();
    geometry.setStretchExtent(5000);
    QCOMPARE(geometry.totalExtent(), frozen);
}

void TestHeaderGeometry::stretchWithoutAnExtentChangesNothing()
{
    // The default state: factors are remembered, but a geometry nobody measured against an
    // extent behaves exactly like one without a stretch (this is what keeps every existing
    // table - and the 10M-row strip - untouched by the feature).
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(3);
    geometry.setSectionStretchFactor(0, 2.0);
    QVERIFY(!geometry.hasStretchSections());
    QCOMPARE(geometry.sectionStretchFactor(0), 2.0);
    QCOMPARE(geometry.totalExtent(), qint64(300));
    QCOMPARE(geometry.sectionSize(0), 100);

    // stretchLastSection() is the one-line version of the same thing: whatever is left goes
    // to the last visible section.
    HeaderGeometry lastOnly;
    lastOnly.setDefaultSectionSize(100);
    lastOnly.setSectionCount(4);
    lastOnly.setStretchLastSection(true);
    QCOMPARE(lastOnly.totalExtent(), qint64(400));
    lastOnly.setStretchExtent(700);
    QCOMPARE(lastOnly.sectionSize(0), 100);
    QCOMPARE(lastOnly.sectionSize(2), 100);
    QCOMPARE(lastOnly.sectionSize(3), 400);
    QCOMPARE(lastOnly.totalExtent(), qint64(700));
    QVERIFY(lastOnly.hasStretchSections());

    // A hidden last section hands the space to the one before it, and disabling the flag
    // keeps the width it grew to.
    lastOnly.setSectionHidden(3, true);
    QCOMPARE(lastOnly.sectionSize(2), 500);
    QCOMPARE(lastOnly.totalExtent(), qint64(700));
    lastOnly.setStretchLastSection(false);
    QVERIFY(!lastOnly.hasStretchSections());
    lastOnly.setStretchExtent(900);
    QCOMPARE(lastOnly.totalExtent(), qint64(700));

    // An extent with no participant at all is inert as well.
    lastOnly.setStretchExtent(900);
    QCOMPARE(lastOnly.totalExtent(), qint64(700));
}

void TestHeaderGeometry::stretchFollowsVisibilityAndTheSizeRange()
{
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(4);
    geometry.setSectionStretchFactor(1, 1.0);
    geometry.setSectionStretchFactor(2, 1.0);
    geometry.setStretchExtent(600);
    QCOMPARE(geometry.sectionSize(1), 200);      // 600 - 100 (col 0) - 100 (col 3), split 1:1
    QCOMPARE(geometry.sectionSize(2), 200);

    // A hidden participant leaves the pass: the other one takes the whole leftover.
    geometry.setSectionHidden(1, true);
    QCOMPARE(geometry.sectionSize(2), 400);
    QCOMPARE(geometry.totalExtent(), qint64(600));

    // Coming back joins again.
    geometry.setSectionHidden(1, false);
    QCOMPARE(geometry.sectionSize(1), 200);
    QCOMPARE(geometry.sectionSize(2), 200);

    // The minimum still wins over the computed share - then the columns do not fit and the
    // extent stays below their sum (a scroll bar appears, exactly like fixed columns).
    geometry.setMinimumSectionSize(150);
    geometry.setStretchExtent(120);
    QCOMPARE(geometry.sectionSize(1), 150);
    QCOMPARE(geometry.sectionSize(2), 150);
    QCOMPARE(geometry.totalExtent(), qint64(600));   // the minimum clamped every section

    // A maximum caps the share the same way.
    geometry.setStretchExtent(1200);
    geometry.setMaximumSectionSize(200);
    QCOMPARE(geometry.sectionSize(1), 200);
    QCOMPARE(geometry.sectionSize(2), 200);
    QCOMPARE(geometry.totalExtent(), qint64(700));

    // The default size changes the sizes the fixed sections keep, so the leftover moves.
    geometry.setMaximumSectionSize(100000);
    geometry.setMinimumSectionSize(24);
    geometry.setStretchExtent(600);
    geometry.resizeSection(3, 100);              // pin the last column at its own width
    geometry.setDefaultSectionSize(50);
    QCOMPARE(geometry.sectionSize(0), 50);
    QCOMPARE(geometry.sectionSize(1), 225);      // 600 - 50 - 100 (col 3 keeps its own)
    QCOMPARE(geometry.sectionSize(3), 100);
    QCOMPARE(geometry.totalExtent(), qint64(600));
}

void TestHeaderGeometry::resizingAStretchingSectionFixesItsWidth()
{
    // QHeaderView's "Stretch becomes Interactive": the user drags a section edge, and from
    // then on that section keeps the dragged width while the others share the leftover.
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(3);
    geometry.setSectionStretchFactor(1, 1.0);
    geometry.setSectionStretchFactor(2, 1.0);
    geometry.setStretchExtent(900);
    QCOMPARE(geometry.sectionSize(1), 400);
    QCOMPARE(geometry.sectionSize(2), 400);

    QSignalSpy stretchSpy(&geometry, &HeaderGeometry::sectionStretchFactorChanged);
    QSignalSpy resizeSpy(&geometry, &HeaderGeometry::sectionResized);
    geometry.resizeSection(1, 300);
    QCOMPARE(geometry.sectionStretchFactor(1), 0.0);
    QCOMPARE(stretchSpy.count(), 0);             // a resize reports sizes, not factors
    QVERIFY(resizeSpy.count() >= 1);
    QVERIFY(geometry.isSectionSizeExplicit(1));
    QCOMPARE(geometry.sectionSize(1), 300);
    QCOMPARE(geometry.sectionSize(2), 500);      // the only participant takes the rest
    QCOMPARE(geometry.totalExtent(), qint64(900));

    // Dragging the same width again still leaves the pass (the factor is gone for good).
    geometry.setSectionStretchFactor(1, 1.0);
    QCOMPARE(geometry.sectionSize(1), 400);
    QCOMPARE(geometry.sectionSize(2), 400);
    geometry.resizeSection(1, geometry.sectionSize(1));
    QCOMPARE(geometry.sectionStretchFactor(1), 0.0);
    QCOMPARE(geometry.sectionSize(1), 400);

    // The setter is the only way back in, and it is idempotent.
    QSignalSpy factorSpy(&geometry, &HeaderGeometry::sectionStretchFactorChanged);
    geometry.setSectionStretchFactor(1, 1.0);
    QCOMPARE(factorSpy.count(), 1);
    QCOMPARE(geometry.sectionSize(1), 400);
    QCOMPARE(geometry.sectionSize(2), 400);
    geometry.setSectionStretchFactor(1, 1.0);
    QCOMPARE(factorSpy.count(), 1);
}

void TestHeaderGeometry::stretchFactorsSurviveTheStateRoundTrip()
{
    // The factors belong to the column state an application saves; the extent belongs to
    // the view showing it, so a restored state is measured against the new view.
    HeaderGeometry geometry;
    geometry.setDefaultSectionSize(100);
    geometry.setSectionCount(4);
    geometry.resizeSection(0, 80);
    geometry.setSectionStretchFactor(1, 1.0);
    geometry.setSectionStretchFactor(3, 3.0);
    geometry.setStretchExtent(1000);
    const int sizeOfOne = geometry.sectionSize(1);
    const int sizeOfThree = geometry.sectionSize(3);
    QCOMPARE(sizeOfOne + sizeOfThree, 1000 - 80 - 100);

    const QByteArray state = geometry.saveState();
    HeaderGeometry restored;
    restored.setSectionCount(4);
    restored.setStretchExtent(2000);
    QVERIFY(restored.restoreState(state));
    QCOMPARE(restored.sectionStretchFactor(1), 1.0);
    QCOMPARE(restored.sectionStretchFactor(3), 3.0);
    QCOMPARE(restored.sectionStretchFactor(2), 0.0);
    QCOMPARE(restored.sectionSize(0), 80);
    QCOMPARE(restored.totalExtent(), qint64(2000));   // measured against *its* extent
    QCOMPARE(restored.sectionSize(1) + restored.sectionSize(3), 2000 - 80 - 100);
    QVERIFY(!restored.isSectionSizeExplicit(3));

    // A state with a factor for a section that does not exist is refused, like every other
    // tampered state.
    HeaderGeometry small;
    small.setSectionCount(2);
    QVERIFY(!small.restoreState(state));
}

void TestHeaderGeometry::sparseStretchKeepsTheLargeCountCompact()
{
    // Ten million sections, two of them sharing the leftover: two sparse entries, no
    // per-section state, and the exact positions a renderer needs.
    constexpr int kSections = 10'000'000;
    constexpr int kSize = 100;
    HeaderGeometry geometry(Qt::Vertical);
    geometry.setSectionCount(kSections);
    geometry.setDefaultSectionSize(kSize);
    geometry.setSectionStretchFactor(1, 1.0);
    geometry.setSectionStretchFactor(2, 3.0);
    QVERIFY(geometry.isUniform());
    QCOMPARE(geometry.storedSectionStateCount(), 2);

    // The target is the extent the sections have to cover: the two participants share the
    // 800 px the other 9'999'998 sections leave, which is 200 + 600 (1 : 3).
    geometry.setStretchExtent(qint64(kSections) * kSize + 600);
    QCOMPARE(geometry.storedSectionStateCount(), 2);
    QVERIFY(geometry.isUniform());
    QCOMPARE(geometry.sectionSize(0), kSize);
    QCOMPARE(geometry.sectionSize(1), 200);
    QCOMPARE(geometry.sectionSize(2), 600);
    QCOMPARE(geometry.sectionSize(3), kSize);
    QCOMPARE(geometry.totalExtent(), qint64(kSections) * kSize + 600);
    QCOMPARE(geometry.sectionPosition(3), qint64(3) * kSize + 100 + 500);
    QCOMPARE(geometry.sectionAtOffset(qint64(3) * kSize + 599), 2);
    QCOMPARE(geometry.sectionAtOffset(qint64(3) * kSize + 600), 3);
    QCOMPARE(geometry.sectionAtOffset(geometry.totalExtent() - 1), kSections - 1);

    // A structural edit materialises the per-section state (here on a small geometry - the
    // 10M one would allocate ten million Sections for the same assertion) and keeps the
    // factors with the columns.
    HeaderGeometry structural;
    structural.setSectionCount(6);
    structural.setSectionStretchFactor(2, 2.0);
    structural.setStretchExtent(600);
    QCOMPARE(structural.sectionSize(2), 100);    // the extent is exactly the sum: no leftover
    structural.setSectionHidden(0, true);
    QCOMPARE(structural.storedSectionStateCount(), 6);
    QCOMPARE(structural.sectionStretchFactor(2), 2.0);
    QCOMPARE(structural.sectionSize(2), 200);    // the hidden column frees its 100 px
    QCOMPARE(structural.totalExtent(), qint64(600));
}
QTEST_APPLESS_MAIN(TestHeaderGeometry)

#include "tst_headergeometry.moc"
