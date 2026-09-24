#include <virtualitemviews/virtualtreeview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QScrollBar>
#include <QPainter>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 26;
constexpr int kViewWidth = 420;
constexpr int kViewHeight = 300;
constexpr int kIndentation = 20;

/// root
///   A (a0, a1)
///   B (b0 (b00, b01), b1)
///   C
QStandardItemModel *buildTree(QObject *parent)
{
    auto *model = new QStandardItemModel(parent);

    auto *a = new QStandardItem(QStringLiteral("A"));
    a->appendRow(new QStandardItem(QStringLiteral("a0")));
    a->appendRow(new QStandardItem(QStringLiteral("a1")));

    auto *b = new QStandardItem(QStringLiteral("B"));
    auto *b0 = new QStandardItem(QStringLiteral("b0"));
    b0->appendRow(new QStandardItem(QStringLiteral("b00")));
    b0->appendRow(new QStandardItem(QStringLiteral("b01")));
    b->appendRow(b0);
    b->appendRow(new QStandardItem(QStringLiteral("b1")));

    auto *c = new QStandardItem(QStringLiteral("C"));

    model->appendRow(a);
    model->appendRow(b);
    model->appendRow(c);
    return model;
}

QModelIndex indexOf(QStandardItemModel *model, const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QModelIndex();
    QStandardItem *item = model->findItems(parts.first(), Qt::MatchExactly).value(0);
    for (int i = 1; i < parts.size() && item; ++i) {
        QStandardItem *next = nullptr;
        for (int row = 0; row < item->rowCount(); ++row) {
            if (item->child(row, 0) && item->child(row, 0)->text() == parts.at(i)) {
                next = item->child(row, 0);
                break;
            }
        }
        item = next;
    }
    return item ? item->index() : QModelIndex();
}

QStringList visibleTexts(VirtualTreeView &view)
{
    QStringList texts;
    for (const MaterializedItem &item : view.materializedItems())
        texts << static_cast<TestRowWidget *>(item.widget)->text();
    return texts;
}

/// The branch indicators are painted by the viewport (they are not carried by
/// the row widgets), so a pass that moves rows must repaint them.
class PaintCounter : public QObject
{
public:
    explicit PaintCounter(const QRect &watchedRegion = QRect())
        : m_region(watchedRegion)
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) {
            ++paints;
            const auto *paintEvent = static_cast<QPaintEvent *>(event);
            if (m_region.isValid() && paintEvent->region().intersects(m_region))
                ++regionPaints;
        }
        return QObject::eventFilter(watched, event);
    }

    int paints = 0;
    /// Paint events that cover the watched region.
    int regionPaints = 0;

private:
    QRect m_region;
};

/// Counts the mouse events of a gesture, so a test can state which sequence it
/// actually simulated (a real double click reaches the widget as
/// press/release/double-click/release).
class GestureCounter : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            ++presses;
            break;
        case QEvent::MouseButtonRelease:
            ++releases;
            break;
        case QEvent::MouseButtonDblClick:
            ++doubleClicks;
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

    int presses = 0;
    int releases = 0;
    int doubleClicks = 0;
};

/// Mean y of the dark pixels in the depth 0 indicator zone around \a expectedY,
/// or -1 when that window holds no arrow.
///
/// The depth 0 indicator zone is [0, indentation); the row widget of a depth 0
/// row starts at the end of that zone, so scanning just inside the zone finds
/// the painted arrow and nothing else. The window is narrower than a row, so it
/// only ever sees the arrow of the probed row.
double arrowCentroidNear(const QImage &image, int expectedY)
{
    qint64 sum = 0;
    int count = 0;
    for (int y = qMax(0, expectedY - 8); y <= qMin(image.height() - 1, expectedY + 8); ++y) {
        for (int x = 2; x < kIndentation - 2; ++x) {
            if (qGray(image.pixel(x, y)) < 128) {
                sum += y;
                ++count;
            }
        }
    }
    return count > 0 ? double(sum) / double(count) : -1.0;
}

/// Records every cell the view offers and marks the cells that adjoin a row
/// with children, so a test can check both the states and the geometry without
/// depending on how an icon looks.
class RecordingBranchRenderer : public BranchIndicatorRenderer
{
public:
    struct Cell
    {
        BranchIndicatorState state;
        QRect rect;
        QModelIndex index;
    };

    void paintBranch(QPainter *painter, const BranchIndicatorState &state, const QModelIndex &index,
                     const QRect &cellRect) const override
    {
        cells.append(Cell{state, cellRect, index});
        if (state.adjoinsItem && state.hasChildren)
            painter->fillRect(cellRect.adjusted(1, 1, -1, -1), markerColor());
    }

    static const QColor &markerColor()
    {
        static const QColor color(0x10, 0xa0, 0x40);
        return color;
    }

    void clear() { cells.clear(); }

    /// Cell recorded for \a index at \a cellDepth (nullptr when there is none).
    const Cell *cellFor(const QModelIndex &index, int cellDepth) const
    {
        for (const Cell &cell : cells) {
            if (cell.index == index && cell.state.cellDepth == cellDepth)
                return &cell;
        }
        return nullptr;
    }

    mutable QVector<Cell> cells;
};
} // namespace

class TestVirtualTreeView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsTopLevelRowsOnlyWhileCollapsed();
    void expandInsertsChildRows();
    void indentationInsetsRowWidgets();
    void branchIndicatorClickToggles();
    void indicatorClickDoesNotReportARowClick();
    void doubleClickTogglesBranch();
    void doubleClickOnTheIndicatorTogglesOnce();
    void keyboardRightExpandsAndLeftCollapses();
    void keyboardMovesToParentAndFirstChild();
    void asteriskExpandsTheWholeSubtree();
    void indicatorsFollowScrolling();
    void branchStateFollowsTheQTreeViewVocabulary();
    void customRendererOwnsTheBranchDecoration();
    void rowsInsertedKeepsExpansionAndOrder();
    void rowsRemovedRecyclesTheSubtree();
    void modelResetCollapsesEverything();
    void anchorKeepsTheTopItemWhileExpandingAbove();
    void rootIndexRestrictsVisibleRows();
    void scrollingIsAllocationFree();

private:
    QStandardItemModel *m_model = nullptr;
    TestAdapter *m_adapter = nullptr;
    VirtualTreeView *m_view = nullptr;
};

void TestVirtualTreeView::init()
{
    m_model = buildTree(this);
    m_adapter = new TestAdapter(kRowHeight);
    m_view = new VirtualTreeView();
    m_view->setAdapter(m_adapter);
    m_view->setUniformItemHeight(kRowHeight);
    m_view->setIndentation(kIndentation);
    m_view->setModel(m_model);
    showView(m_view, QSize(kViewWidth, kViewHeight));
}

void TestVirtualTreeView::cleanup()
{
    delete m_view;
    m_view = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestVirtualTreeView::showsTopLevelRowsOnlyWhileCollapsed()
{
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
    QCOMPARE(m_view->materializedItemCount(), qsizetype(3));
    QCOMPARE(visibleTexts(*m_view), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                                 QStringLiteral("C")}));
    QCOMPARE(m_view->indexAt(QPoint(10, kRowHeight / 2)), indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_view->indexAt(QPoint(10, kRowHeight * 2 + 5)), indexOf(m_model, QStringLiteral("C")));
    QCOMPARE(m_view->itemDepth(indexOf(m_model, QStringLiteral("A"))), 0);
}

void TestVirtualTreeView::expandInsertsChildRows()
{
    m_view->expand(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));
    QCOMPARE(visibleTexts(*m_view), QStringList({QStringLiteral("A"), QStringLiteral("a0"),
                                                 QStringLiteral("a1"), QStringLiteral("B"),
                                                 QStringLiteral("C")}));
    QCOMPARE(m_view->itemDepth(indexOf(m_model, QStringLiteral("A/a1"))), 1);

    m_view->collapse(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
    QCOMPARE(visibleTexts(*m_view), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                                 QStringLiteral("C")}));
}

void TestVirtualTreeView::indentationInsetsRowWidgets()
{
    m_view->expand(indexOf(m_model, QStringLiteral("B")));
    m_view->expand(indexOf(m_model, QStringLiteral("B/b0")));
    m_view->flushPendingRelayout();

    const auto widgetOf = [this](const QString &path) -> QWidget * {
        return m_view->widgetForIndex(indexOf(m_model, path));
    };
    QWidget *top = widgetOf(QStringLiteral("B"));
    QWidget *child = widgetOf(QStringLiteral("B/b0"));
    QWidget *grandChild = widgetOf(QStringLiteral("B/b0/b00"));
    QVERIFY(top && child && grandChild);

    // One indentation step per level plus one for the branch indicator.
    QCOMPARE(top->x(), kIndentation * 1);
    QCOMPARE(child->x(), kIndentation * 2);
    QCOMPARE(grandChild->x(), kIndentation * 3);
    QVERIFY(grandChild->width() < top->width());
    QCOMPARE(top->height(), kRowHeight);

    m_view->setIndentation(32);
    m_view->flushPendingRelayout();
    QCOMPARE(widgetOf(QStringLiteral("B/b0/b00"))->x(), 32 * 3);
}

void TestVirtualTreeView::branchIndicatorClickToggles()
{
    QSignalSpy expandedSpy(m_view, &VirtualTreeView::expanded);
    QSignalSpy collapsedSpy(m_view, &VirtualTreeView::collapsed);

    // The indicator of a top level item sits inside [0, indentation).
    const QPoint indicator(kIndentation / 2, kRowHeight / 2);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QCOMPARE(expandedSpy.count(), 1);
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("A"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));
    // Toggling the branch must not change the current index.
    QVERIFY(!m_view->currentIndex().isValid());

    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QCOMPARE(collapsedSpy.count(), 1);
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));

    // Clicking the row body (right of the indentation) selects instead.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(kIndentation * 2, kRowHeight / 2));
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
}

void TestVirtualTreeView::doubleClickTogglesBranch()
{
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(kIndentation * 2, kRowHeight + kRowHeight / 2));
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("B"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));

    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(kIndentation * 2, kRowHeight + kRowHeight / 2));
    QVERIFY(!m_view->isExpanded(indexOf(m_model, QStringLiteral("B"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
}

void TestVirtualTreeView::indicatorClickDoesNotReportARowClick()
{
    QSignalSpy clickedSpy(m_view, &VirtualItemView::clicked);
    // First a real row click, so that the kernel holds a pressed index: toggling
    // the branch afterwards must not be mistaken for a click on that row.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(kIndentation * 2, kRowHeight / 2));
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A")));

    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(kIndentation / 2, kRowHeight / 2));
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("A"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));
    QCOMPARE(clickedSpy.count(), 1);
}

void TestVirtualTreeView::doubleClickOnTheIndicatorTogglesOnce()
{
    QSignalSpy expandedSpy(m_view, &VirtualTreeView::expanded);
    QSignalSpy collapsedSpy(m_view, &VirtualTreeView::collapsed);

    const QPoint indicator(kIndentation / 2, kRowHeight / 2);
    GestureCounter gesture;
    m_view->viewport()->installEventFilter(&gesture);
    // A real double click reaches the widget as press/release/double click/
    // release (QTest::mouseDClick sends the double click event only), so the
    // sequence is spelled out here.
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, indicator);
    m_view->viewport()->removeEventFilter(&gesture);
    // Guard the assumption of this test: the gesture really was delivered as a
    // double click, so the press toggled the branch and the double click must
    // not toggle it back.
    QCOMPARE(gesture.doubleClicks, 1);
    QCOMPARE(gesture.presses, 1);
    QCOMPARE(gesture.releases, 2);

    QCOMPARE(expandedSpy.count(), 1);
    QCOMPARE(collapsedSpy.count(), 0);
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("A"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));
}

void TestVirtualTreeView::keyboardRightExpandsAndLeftCollapses()
{
    m_view->setCurrentIndex(indexOf(m_model, QStringLiteral("A")));
    QTest::keyClick(m_view, Qt::Key_Right);
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("A"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));

    QTest::keyClick(m_view, Qt::Key_Left);
    QVERIFY(!m_view->isExpanded(indexOf(m_model, QStringLiteral("A"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
}

void TestVirtualTreeView::keyboardMovesToParentAndFirstChild()
{
    m_view->expand(indexOf(m_model, QStringLiteral("A")));
    m_view->setCurrentIndex(indexOf(m_model, QStringLiteral("A")));

    // Right on an expanded item moves to its first child.
    QTest::keyClick(m_view, Qt::Key_Right);
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A/a0")));

    // Left on a child moves to the parent.
    QTest::keyClick(m_view, Qt::Key_Left);
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A")));

    // Down/Up still walk the visible rows (children included).
    QTest::keyClick(m_view, Qt::Key_Down);
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A/a0")));
    QTest::keyClick(m_view, Qt::Key_Down);
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A/a1")));
    QTest::keyClick(m_view, Qt::Key_Up);
    QCOMPARE(m_view->currentIndex(), indexOf(m_model, QStringLiteral("A/a0")));
}

void TestVirtualTreeView::asteriskExpandsTheWholeSubtree()
{
    // B -> b0 -> (b00, b01), b1
    m_view->setCurrentIndex(indexOf(m_model, QStringLiteral("B")));
    QTest::keyClick(m_view, Qt::Key_Asterisk);
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("B"))));
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("B/b0"))));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(7));
    QCOMPARE(visibleTexts(*m_view),
             QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("b0"),
                          QStringLiteral("b00"), QStringLiteral("b01"), QStringLiteral("b1"),
                          QStringLiteral("C")}));

    // Collapsing keeps the expansion state of the descendants, so expanding the
    // item again brings the whole sub-tree back.
    m_view->collapse(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
    m_view->expand(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(7));

    // expandRecursively() is the public entry point of the `*` key.
    m_view->collapseAll();
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
    m_view->expandRecursively(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(7));
    QCOMPARE(m_view->itemDepth(indexOf(m_model, QStringLiteral("B/b0/b01"))), 2);
    // A leaf has nothing to expand and must stay a no-op.
    m_view->expandRecursively(indexOf(m_model, QStringLiteral("C")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(7));
}

void TestVirtualTreeView::indicatorsFollowScrolling()
{
    // Enough branch rows to scroll; every one of them carries a depth 0
    // indicator in [kIndicatorMargin, kIndicatorMargin + 8).
    for (int i = 0; i < 40; ++i) {
        auto *item = new QStandardItem(QStringLiteral("row %1").arg(i));
        item->appendRow(new QStandardItem(QStringLiteral("child %1").arg(i)));
        m_model->appendRow(item);
    }
    m_view->flushPendingRelayout();

    const auto renderViewport = [this]() {
        QImage image(m_view->viewport()->size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        m_view->viewport()->render(&image);
        return image;
    };

    // Row 10 sits at the top of the viewport; row 13 is probed because it is
    // fully visible at both offsets. Its indicator center is at
    // row * height + height / 2 - offset.
    const int topRowOffset = kRowHeight * 10;
    const int scrolledOffset = topRowOffset + 6;
    const int probeRowCenter = 13 * kRowHeight + kRowHeight / 2;
    const int firstExpectedY = probeRowCenter - topRowOffset;
    const int scrolledExpectedY = probeRowCenter - scrolledOffset;

    m_view->setVerticalOffset(topRowOffset);
    m_view->flushPendingRelayout();
    // Settle the repaints of the passes above, so that the counter only sees
    // what the scrolling pass itself invalidates.
    QApplication::processEvents();
    m_view->viewport()->repaint();
    const double before = arrowCentroidNear(renderViewport(), firstExpectedY);
    QVERIFY(before >= 0.0);
    QVERIFY2(qAbs(before - firstExpectedY) <= 1.5,
             qPrintable(QStringLiteral("before=%1 expected=%2").arg(before).arg(firstExpectedY)));

    // Scrolling by 6 px must move the painted indicator by 6 px as well: the row
    // widgets move themselves, but the indicators are painted by the viewport
    // and therefore have to be invalidated by the pass.
    const QRect watchedRegion(0, scrolledExpectedY - 8, kIndentation, 17);
    PaintCounter counter(watchedRegion);
    m_view->viewport()->installEventFilter(&counter);
    m_view->setVerticalOffset(scrolledOffset);
    m_view->flushPendingRelayout();
    QApplication::processEvents();
    const int paintsAfterScroll = counter.paints;
    const int regionPaintsAfterScroll = counter.regionPaints;
    m_view->viewport()->removeEventFilter(&counter);

    const double after = arrowCentroidNear(renderViewport(), scrolledExpectedY);
    QVERIFY(after >= 0.0);
    QVERIFY2(qAbs(after - scrolledExpectedY) <= 1.5,
             qPrintable(QStringLiteral("after=%1 expected=%2").arg(after).arg(scrolledExpectedY)));
    QVERIFY2(paintsAfterScroll > 0, "the pass did not repaint the viewport");
    // Nothing else covers the indicator zone of a row, so the strip has to be
    // part of what the pass repainted.
    QVERIFY2(regionPaintsAfterScroll > 0, "the pass did not repaint the indicator strip");
}

void TestVirtualTreeView::branchStateFollowsTheQTreeViewVocabulary()
{
    // A (a0, a1), B (b0 (b00, b01), b1), C
    const BranchIndicatorState a = m_view->branchState(indexOf(m_model, QStringLiteral("A")));
    QVERIFY(a.adjoinsItem);
    QVERIFY(a.hasChildren);
    QVERIFY(!a.isExpanded);
    QVERIFY(a.hasSiblings); // B and C follow A
    QVERIFY(a.isClosed());
    QVERIFY(!a.isLeaf());
    QCOMPARE(a.cellDepth, 0);
    QCOMPARE(a.itemDepth, 0);

    // QTreeView::branch:!has-children:!has-siblings:adjoins-item
    const BranchIndicatorState c = m_view->branchState(indexOf(m_model, QStringLiteral("C")));
    QVERIFY(c.adjoinsItem && c.isLeaf() && !c.hasSiblings);

    m_view->expand(indexOf(m_model, QStringLiteral("B")));
    m_view->expand(indexOf(m_model, QStringLiteral("B/b0")));
    const QModelIndex b00 = indexOf(m_model, QStringLiteral("B/b0/b00"));
    QCOMPARE(m_view->itemDepth(b00), 2);

    const BranchIndicatorState own = m_view->branchState(b00);
    QVERIFY(own.adjoinsItem);
    QVERIFY(own.isLeaf());
    QVERIFY(own.hasSiblings); // b01 follows b00
    QCOMPARE(own.cellDepth, 2);
    QCOMPARE(own.itemDepth, 2);
    // Cell depths above the row's own depth clamp to the row's cell.
    QCOMPARE(m_view->branchState(b00, 5).cellDepth, 2);

    // The ancestor cells: no indicator, but QTreeView would draw connectors here.
    const BranchIndicatorState parentCell = m_view->branchState(b00, 1);
    QVERIFY(!parentCell.adjoinsItem);
    QVERIFY(parentCell.hasChildren); // b0
    QVERIFY(parentCell.isOpen());
    QVERIFY(parentCell.hasSiblings); // b1 follows b0
    const BranchIndicatorState rootCell = m_view->branchState(b00, 0);
    QVERIFY(!rootCell.adjoinsItem);
    QVERIFY(rootCell.hasChildren); // B
    QVERIFY(rootCell.isOpen());
    QVERIFY(rootCell.hasSiblings); // B is followed by C

    // The last child of a parent has no sibling below it.
    const BranchIndicatorState b1 = m_view->branchState(indexOf(m_model, QStringLiteral("B/b1")));
    QVERIFY(b1.isLeaf() && !b1.hasSiblings);
}

void TestVirtualTreeView::customRendererOwnsTheBranchDecoration()
{
    m_view->expand(indexOf(m_model, QStringLiteral("B")));
    m_view->expand(indexOf(m_model, QStringLiteral("B/b0")));
    m_view->flushPendingRelayout();

    RecordingBranchRenderer renderer;
    m_view->setBranchIndicatorRenderer(&renderer);
    QCOMPARE(m_view->branchIndicatorRenderer(), &renderer);

    renderer.clear();
    m_view->viewport()->repaint();

    const QModelIndex b00 = indexOf(m_model, QStringLiteral("B/b0/b00"));
    // A row at depth 2 owns three cells: its own plus one per ancestor level.
    const RecordingBranchRenderer::Cell *own = renderer.cellFor(b00, 2);
    const RecordingBranchRenderer::Cell *parentCell = renderer.cellFor(b00, 1);
    const RecordingBranchRenderer::Cell *rootCell = renderer.cellFor(b00, 0);
    QVERIFY(own && parentCell && rootCell);
    QVERIFY(own->state.adjoinsItem);
    QVERIFY(!own->state.hasChildren);
    QVERIFY(!parentCell->state.adjoinsItem);
    QVERIFY(parentCell->state.hasChildren);
    QVERIFY(rootCell->state.hasChildren);

    // Cells are the indentation wide slots of their level, and the row's own
    // cell is the rightmost one.
    QCOMPARE(own->rect.x(), 2 * kIndentation);
    QCOMPARE(own->rect.width(), kIndentation);
    QCOMPARE(parentCell->rect.x(), 1 * kIndentation);
    QCOMPARE(rootCell->rect.x(), 0);
    QCOMPARE(own->rect.y(), parentCell->rect.y());
    QCOMPARE(own->rect.y(), rootCell->rect.y());

    // What the renderer paints really lands in the viewport.
    QImage image(m_view->viewport()->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    m_view->viewport()->render(&image);
    const RecordingBranchRenderer::Cell *b = renderer.cellFor(indexOf(m_model, QStringLiteral("B")), 0);
    QVERIFY(b && b->state.adjoinsItem && b->state.isOpen());
    QCOMPARE(QColor(image.pixel(b->rect.center())), RecordingBranchRenderer::markerColor());

    // A cell that is neither the row's own cell nor has children is not marked:
    // the renderer decides, the view only offers the cell.
    QVERIFY(QColor(image.pixel(own->rect.center())) != RecordingBranchRenderer::markerColor());
}

void TestVirtualTreeView::rowsInsertedKeepsExpansionAndOrder()
{
    m_view->expand(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));

    QStandardItem *a = m_model->findItems(QStringLiteral("A"), Qt::MatchExactly).value(0);
    a->insertRow(0, new QStandardItem(QStringLiteral("a-new")));

    QCOMPARE(m_view->visibleRowCount(), qsizetype(6));
    QVERIFY(m_view->isExpanded(indexOf(m_model, QStringLiteral("A"))));
    QCOMPARE(visibleTexts(*m_view), QStringList({QStringLiteral("A"), QStringLiteral("a-new"),
                                                 QStringLiteral("a0"), QStringLiteral("a1"),
                                                 QStringLiteral("B"), QStringLiteral("C")}));

    // Removing the children of a collapsed item is a no-op for the visible rows.
    m_view->collapse(indexOf(m_model, QStringLiteral("A")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
    a->removeRow(0);
    QCOMPARE(m_view->visibleRowCount(), qsizetype(3));
}

void TestVirtualTreeView::rowsRemovedRecyclesTheSubtree()
{
    m_view->expand(indexOf(m_model, QStringLiteral("B")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(5));
    QWidget *childWidget = m_view->widgetForIndex(indexOf(m_model, QStringLiteral("B/b0")));
    QVERIFY(childWidget != nullptr);

    // Remove the whole B sub-tree.
    m_model->removeRow(1);
    m_view->flushPendingRelayout();

    QCOMPARE(m_view->visibleRowCount(), qsizetype(2));
    QCOMPARE(visibleTexts(*m_view), QStringList({QStringLiteral("A"), QStringLiteral("C")}));
    QVERIFY(m_view->widgetForIndex(indexOf(m_model, QStringLiteral("B"))) == nullptr);
    QCOMPARE(m_adapter->boundWidgetCount(), int(m_view->materializedItemCount()));
}

void TestVirtualTreeView::modelResetCollapsesEverything()
{
    m_view->expand(indexOf(m_model, QStringLiteral("B")));
    m_view->expand(indexOf(m_model, QStringLiteral("B/b0")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(7));

    m_model->clear();
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->visibleRowCount(), qsizetype(0));
    QCOMPARE(m_view->materializedItemCount(), qsizetype(0));
}

void TestVirtualTreeView::anchorKeepsTheTopItemWhileExpandingAbove()
{
    auto *model = new QStandardItemModel(this);
    for (int row = 0; row < 40; ++row) {
        auto *parent = new QStandardItem(QStringLiteral("p%1").arg(row));
        parent->appendRow(new QStandardItem(QStringLiteral("c%1").arg(row)));
        model->appendRow(parent);
    }
    TestAdapter adapter(kRowHeight);
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QCOMPARE(view.visibleRowCount(), qsizetype(40));

    view.scrollTo(model->index(20, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    const QWidget *topWidget = nullptr;
    QModelIndex topIndex;
    for (const MaterializedItem &item : view.materializedItems()) {
        if (item.geometry.top() == 0) {
            topWidget = item.widget;
            topIndex = item.index;
        }
    }
    QVERIFY(topWidget != nullptr);
    QCOMPARE(topIndex, model->index(20, 0));

    // Expanding an item above the viewport must not scroll the anchored item away.
    view.expand(model->index(2, 0));
    view.flushPendingRelayout();
    QCOMPARE(view.widgetForIndex(topIndex), topWidget);
    const QRect rect = view.visualRect(topIndex);
    QCOMPARE(rect.top(), 0);
}

void TestVirtualTreeView::rootIndexRestrictsVisibleRows()
{
    const QModelIndex b = indexOf(m_model, QStringLiteral("B"));
    m_view->setRootIndex(b);
    QCOMPARE(m_view->visibleRowCount(), qsizetype(2));
    QCOMPARE(visibleTexts(*m_view), QStringList({QStringLiteral("b0"), QStringLiteral("b1")}));
    QCOMPARE(m_view->itemDepth(indexOf(m_model, QStringLiteral("B/b0"))), 0);

    m_view->expand(indexOf(m_model, QStringLiteral("B/b0")));
    QCOMPARE(m_view->visibleRowCount(), qsizetype(4));
    QCOMPARE(m_view->itemDepth(indexOf(m_model, QStringLiteral("B/b0/b00"))), 1);
}

void TestVirtualTreeView::scrollingIsAllocationFree()
{
    // A wide tree: 400 parents, each with 8 children (3600 visible rows).
    auto *model = new QStandardItemModel(this);
    for (int row = 0; row < 400; ++row) {
        auto *parent = new QStandardItem(QStringLiteral("p%1").arg(row));
        for (int child = 0; child < 8; ++child)
            parent->appendRow(new QStandardItem(QStringLiteral("c%1.%2").arg(row).arg(child)));
        model->appendRow(parent);
    }
    TestAdapter adapter(kRowHeight);
    VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QCOMPARE(view.visibleRowCount(), qsizetype(400));

    // Expand everything: the visible row count follows, widgets stay bounded.
    for (int row = 0; row < 400; ++row)
        view.expand(model->index(row, 0));
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(400 * 9));
    QVERIFY(view.materializedItemCount() < 40);

    const int createdBefore = adapter.createdCount();
    for (int step = 1; step <= 40; ++step) {
        view.verticalScrollBar()->setValue(step * kRowHeight);
        view.flushPendingRelayout();
        QVERIFY(view.materializedItemCount() < 40);
    }
    QCOMPARE(adapter.createdCount(), createdBefore);
    QCOMPARE(view.destroyedWidgetCount(), qsizetype(0));
}

QTEST_MAIN(TestVirtualTreeView)

#include "tst_virtualtreeview.moc"
