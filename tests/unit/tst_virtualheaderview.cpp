#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtableview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

#include <limits>

using namespace viv;

namespace {
constexpr int kSectionWidth = 80;
constexpr int kHeaderHeight = 30;

/// Section widget of the test: one label, so bind/unbind are observable.
class SectionWidget : public QWidget
{
public:
    explicit SectionWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("sectionLabel"));
        m_label->setGeometry(0, 0, kSectionWidth, kHeaderHeight);
    }

    void setText(const QString &text) { m_label->setText(text); }
    QString text() const { return m_label->text(); }

private:
    QLabel *m_label = nullptr;
};

class SectionAdapter : public HeaderWidgetAdapter
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        ++created;
        return new SectionWidget(parent);
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        ++bound;
        static_cast<SectionWidget *>(widget)->setText(QStringLiteral("s%1").arg(logicalIndex));
    }

    void unbindSection(QWidget *widget, int logicalIndex) override
    {
        ++unbound;
        static_cast<SectionWidget *>(widget)->setText(QString());
    }

    int created = 0;
    int bound = 0;
    int unbound = 0;
};

/// Sends a mouse event with an explicit button state: the drag logic needs the pressed
/// buttons, which QTest::mouseMove does not always carry.
void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

/// Row adapter of the table level test: rows are irrelevant here, the header is
/// the subject.
class PlainRowAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QWidget(parent); }
    void bindWidget(QWidget *, const QModelIndex &) override {}
    void unbindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(640, 24); }
};

/// Row adapter that gives every column a framework-managed ColumnHost, so the *body*
/// side of a header animation is observable (the hosts are the widgets the framework
/// positions from the committed column geometry).
class HostRowAdapter : public TableWidgetAdapter
{
public:
    explicit HostRowAdapter(int columns)
        : m_columns(columns)
    {
    }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        auto *row = new QWidget(parent);
        for (int column = 0; column < m_columns; ++column)
            new ColumnHost(column, row);
        return row;
    }

    void bindWidget(QWidget *, const QModelIndex &) override {}
    void unbindWidget(QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(const QModelIndex &) const override { return QSize(640, 24); }

    /// x of one column host inside \a rowWidget. The row widget covers the viewport, so
    /// this is the same x space the header sections report.
    static int hostX(QWidget *rowWidget, int column)
    {
        if (!rowWidget)
            return std::numeric_limits<int>::min();
        for (ColumnHost *host : rowWidget->findChildren<ColumnHost *>()) {
            if (host->logicalColumn() == column)
                return host->x();
        }
        return std::numeric_limits<int>::min();
    }

private:
    int m_columns = 0;
};

/// Minimal cell adapter: the framework positions the cells, business code only fills
/// them (Cell Widget Mode of the table body).
class PlainCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }
};

/// Section adapter of the "model changed" tests: binds the text from the model's
/// headerData() (that is what the README example does) and appends the sort marker of
/// the geometry, so both sources are observable on the widget.
class ModelSectionAdapter : public HeaderWidgetAdapter
{
public:
    ModelSectionAdapter(QAbstractItemModel *model, HeaderGeometry *geometry)
        : m_model(model)
        , m_geometry(geometry)
    {
    }

    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        return new SectionWidget(parent);
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        ++bindCount;
        QString text = labelModel()->headerData(logicalIndex, Qt::Horizontal).toString();
        // The geometry comes from the header's hook when it has one (the pattern this test now
        // teaches); the constructor argument is the fallback of an adapter that captured it.
        HeaderGeometry *geometry = hookedGeometry() ? hookedGeometry() : m_geometry;
        if (geometry && geometry->sortIndicatorSection() == logicalIndex) {
            text += geometry->sortIndicatorOrder() == Qt::AscendingOrder ? QStringLiteral(" ^")
                                                                        : QStringLiteral(" v");
        }
        static_cast<SectionWidget *>(widget)->setText(text);
    }

    void unbindSection(QWidget *widget, int) override
    {
        ++unbindCount;
        static_cast<SectionWidget *>(widget)->setText(QString());
    }

    /// The header tells the adapter which model the labels come from (the README example
    /// captures the model in the adapter, so this is the hook that keeps it in sync).
    void setLabelModel(QAbstractItemModel *model) override
    {
        ++labelModelChanges;
        m_hookedModel = model;
    }

    QAbstractItemModel *labelModel() const { return m_hookedModel ? m_hookedModel : m_model; }

    /// The geometry hook (P1.3 of the fourth review): the adapter that draws sort state gets
    /// the geometry from the header instead of capturing it once.
    void setGeometryModel(HeaderGeometry *geometry) override
    {
        ++geometryModelChanges;
        m_hookedGeometry = geometry;
    }

    QString textOf(VirtualHeaderView *header, int logicalIndex) const
    {
        QWidget *widget = header->sectionWidget(logicalIndex);
        return widget ? static_cast<SectionWidget *>(widget)->text() : QString();
    }

    int bindCount = 0;
    int unbindCount = 0;
    int labelModelChanges = 0;
    int geometryModelChanges = 0;
    HeaderGeometry *hookedGeometry() const { return m_hookedGeometry; }

private:
    QAbstractItemModel *m_model = nullptr;
    QAbstractItemModel *m_hookedModel = nullptr;
    HeaderGeometry *m_geometry = nullptr;
    HeaderGeometry *m_hookedGeometry = nullptr;
};
} // namespace

/// VirtualHeaderView materializes only the sections of its window (§17-§19).
class TestVirtualHeaderView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void materializesOnlyTheVisibleSections();
    void recyclesSectionsThatLeaveTheWindow();
    void sectionGeometryComesFromHeaderGeometry();
    void dragOnASectionEdgeResizesIt();
    void clickSetsTheSortIndicator();
    void paneFilterLimitsTheSections();
    void paneOffsetKeepsAPaneInItsOwnSpace();
    void sectionMoveAnimatesWhileTheCommittedGeometryStaysAuthoritative();
    void resizeAndDisabledAnimationStayImmediate();
    void programmaticReorderIsImmediateUnlessRequested();
    void verticalWidgetHeaderPacksAlongY();
    void externalRowOrderDropsThePreviewWhenNobodyMovesTheRow();
    void uniformGeometryDragsWithoutBuildingTheOrder();
    void verticalWidgetHeaderHandlesTenMillionUniformRows();
    void mismatchedHeaderOrientationsAreRefused();
    void tableAnimationIsOptInPerMove();
    void smallJitterIsStillAClick();
    void dragPreviewsAndCommitsOnce();
    void animationDefaultsToOutCubicWith300ms();
    void dragRoomEasesAndCanBeTurnedOff();
    void dragReordersInsideItsPaneOnly();
    void escapeCancelsTheDrag();
    void hoverOverASectionWidgetUpdatesTheCursor();
    void childrenAddedAfterBindingAreWatchedToo();
    void modelChangesRebindTheMaterializedSections();
    void sortIndicatorChangesRebindTheSections();
    void switchingTheLabelModelRebindsTheSections();
    void installingAnAdapterHandsOverTheCurrentLabelModel();
    void theAdapterFollowsTheGeometryCollaborator();
    void switchingTheGeometryInvalidatesThePaneCache();
    void restoringASortStateRebindsTheSections();
    void tableForwardsTheAnimationSettings();
    void bodyFollowsTheSectionsWhileAMoveAnimates();
    void bodyFollowsTheDragPreviewAndTheCommit();
    void bodyFollowCanBeTurnedOff();
    void bodyFollowsAFrozenPaneDrag();
    void cellsFollowTheHeaderWhileTheBodyDoes();
    void frozenPaneHeaderKeepsItsColumnsAfterAReorder();
    void standaloneHeaderStretchesItsSectionsToItsWidth();

private:
    QStandardItemModel *m_model = nullptr;
    HeaderGeometry *m_geometry = nullptr;
    VirtualHeaderView *m_header = nullptr;
    SectionAdapter *m_adapter = nullptr;
};

void TestVirtualHeaderView::init()
{
    m_model = new QStandardItemModel(10, 40, this);
    QStringList labels;
    for (int column = 0; column < 40; ++column)
        labels << QStringLiteral("c%1").arg(column);
    m_model->setHorizontalHeaderLabels(labels);
    m_geometry = new HeaderGeometry(Qt::Horizontal, this);
    m_geometry->setSectionCount(m_model->columnCount());
    m_geometry->setDefaultSectionSize(kSectionWidth);
    m_adapter = new SectionAdapter;
    m_header = new VirtualHeaderView(Qt::Horizontal);
    m_header->resize(400, kHeaderHeight);
    m_header->setAdapter(m_adapter);
    m_header->setGeometryModel(m_geometry);
    m_header->setLabelModel(m_model);
    m_header->setSortInteractionEnabled(true);
    m_header->show();
    QApplication::processEvents();
}

void TestVirtualHeaderView::cleanup()
{
    delete m_header;
    m_header = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_geometry;
    m_geometry = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestVirtualHeaderView::materializesOnlyTheVisibleSections()
{
    // 400 px wide, 80 px sections, overscan 1: far fewer widgets than columns.
    const QList<int> sections = m_header->materializedSections();
    QVERIFY(!sections.isEmpty());
    QVERIFY(sections.size() <= 8);
    QVERIFY(m_header->materializedSectionCount() < m_model->columnCount());
    QCOMPARE(m_adapter->created, int(m_header->materializedSectionCount()));
    // The widget count follows the viewport, not the column count.
    m_geometry->setSectionCount(400);
    m_header->resize(200, kHeaderHeight);
    QApplication::processEvents();
    QVERIFY(m_header->materializedSectionCount() <= 8);
}

void TestVirtualHeaderView::recyclesSectionsThatLeaveTheWindow()
{
    const int createdAfterFirstPass = m_adapter->created;
    // Scroll ten sections: the first ones leave the window, the pool serves the
    // newly visible ones.
    m_geometry->setViewportOffset(10 * kSectionWidth);
    QApplication::processEvents();

    // Pooled widgets are reused: at most one extra widget for the slightly larger
    // window, and never one per column.
    QVERIFY(m_adapter->created <= createdAfterFirstPass + 1);
    QVERIFY(m_adapter->created <= 10);
    QVERIFY(m_adapter->unbound > 0);
    const QList<int> sections = m_header->materializedSections();
    QVERIFY(!sections.isEmpty());
    for (int logical : sections) {
        QWidget *widget = m_header->sectionWidget(logical);
        QVERIFY(widget != nullptr);
        QCOMPARE(static_cast<SectionWidget *>(widget)->text(), QStringLiteral("s%1").arg(logical));
    }
}

void TestVirtualHeaderView::sectionGeometryComesFromHeaderGeometry()
{
    m_geometry->resizeSection(0, 150);
    m_geometry->setSectionHidden(2, true);
    QApplication::processEvents();
    QWidget *first = m_header->sectionWidget(0);
    QVERIFY(first != nullptr);
    QCOMPARE(first->width(), 150);
    QCOMPARE(first->x(), 0);

    // A hidden section owns no widget; the following one closes the gap.
    QVERIFY(m_header->sectionWidget(2) == nullptr);
    QWidget *third = m_header->sectionWidget(3);
    QVERIFY(third != nullptr);
    QCOMPARE(third->x(), 150 + kSectionWidth);
}

void TestVirtualHeaderView::dragOnASectionEdgeResizesIt()
{
    const int edgeX = kSectionWidth - 1;
    QTest::mousePress(m_header, Qt::LeftButton, Qt::NoModifier, QPoint(edgeX, kHeaderHeight / 2));
    // QTest::mouseMove does not carry the pressed button in Qt 5, so the move is
    // sent explicitly for both Qt versions.
    // The six argument form (with the global position) exists in Qt 5 and Qt 6; the
    // five argument one is deprecated in Qt 6 (it has no global position).
    const QPointF local(edgeX + 40, kHeaderHeight / 2);
    QMouseEvent move(QEvent::MouseMove, local, m_header->mapToGlobal(local.toPoint()), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_header, &move);
    QTest::mouseRelease(m_header, Qt::LeftButton, Qt::NoModifier,
                        QPoint(edgeX + 40, kHeaderHeight / 2));
    QCOMPARE(m_geometry->storedSectionSize(0), kSectionWidth + 40);
    QCOMPARE(m_header->sectionWidget(0)->width(), kSectionWidth + 40);
}

void TestVirtualHeaderView::clickSetsTheSortIndicator()
{
    const QPoint center(kSectionWidth / 2, kHeaderHeight / 2);
    QTest::mouseClick(m_header, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(m_geometry->sortIndicatorSection(), 0);
    QCOMPARE(m_geometry->sortIndicatorOrder(), Qt::AscendingOrder);
    QTest::mouseClick(m_header, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(m_geometry->sortIndicatorOrder(), Qt::DescendingOrder);
}

void TestVirtualHeaderView::paneFilterLimitsTheSections()
{
    m_header->setPaneFilter(QVector<int>({1, 3}), true);
    QApplication::processEvents();
    QCOMPARE(m_header->materializedSections(), QList<int>({1, 3}));
    QVERIFY(m_header->sectionWidget(0) == nullptr);
    QVERIFY(m_header->sectionWidget(1) != nullptr);
    QVERIFY(m_header->sectionWidget(2) == nullptr);
    QVERIFY(m_header->sectionWidget(3) != nullptr);

    m_header->clearPaneFilter();
    QApplication::processEvents();
    QVERIFY(m_header->materializedSections().size() > 2);
}

void TestVirtualHeaderView::paneOffsetKeepsAPaneInItsOwnSpace()
{
    // §43 "advanced panes": a pane packs its own columns from its own left edge and
    // shifts them by its own offset, so a frozen pane stays pinned while the
    // geometry scrolls and a scrolling pane of a non-primary group follows the
    // offset of that group instead of the geometry's.
    const auto sectionX = [this](int logicalIndex) {
        QWidget *widget = m_header->sectionWidget(logicalIndex);
        return widget ? widget->x() : std::numeric_limits<int>::min();
    };
    m_geometry->setViewportOffset(3 * kSectionWidth);

    // A frozen pane: offset 0, sections packed from the pane's left edge, no
    // matter how far the geometry has scrolled.
    m_header->setPaneFilter(QVector<int>({1, 3}), true);
    m_header->setPaneOffset(0);
    QApplication::processEvents();
    QCOMPARE(sectionX(1), 0);
    QCOMPARE(sectionX(3), kSectionWidth);

    // A scrolling pane of a group other than the primary one: the same packing,
    // shifted by the offset of that group instead of the geometry's.
    m_header->setPaneOffset(kSectionWidth / 2);
    QApplication::processEvents();
    QCOMPARE(sectionX(1), -kSectionWidth / 2);
    QCOMPARE(sectionX(3), kSectionWidth - kSectionWidth / 2);

    // Back to the committed geometry: the pane follows the geometry's offset again
    // (the columns that are inside the window, since the header only materializes
    // what it shows).
    m_header->setPaneFilter(QVector<int>({3, 4}), false);
    m_header->setPaneOffset(HeaderViewInterface::kFollowGeometryOffset);
    QApplication::processEvents();
    QCOMPARE(sectionX(3), 0);
    QCOMPARE(sectionX(4), kSectionWidth);

    // A pane whose columns are not contiguous still packs its own columns.
    m_header->setPaneFilter(QVector<int>({2, 5}), false);
    m_geometry->setViewportOffset(0);
    m_header->setPaneOffset(10);
    QApplication::processEvents();
    QCOMPARE(sectionX(2), -10);
    QCOMPARE(sectionX(5), kSectionWidth - 10);
}

void TestVirtualHeaderView::sectionMoveAnimatesWhileTheCommittedGeometryStaysAuthoritative()
{
    // §23: a section move is committed before the user sees the result, so the
    // renderer slides the section there. The body reads the committed geometry, so
    // it never follows an intermediate frame - the header does.
    m_header->setSectionAnimationDuration(240);
    QVERIFY(m_header->sectionAnimationEnabled());
    QWidget *moved = m_header->sectionWidget(0);
    QVERIFY(moved != nullptr);
    QCOMPARE(moved->x(), 0);

    m_header->setSectionMoveAnimated(true);
    m_geometry->moveSection(0, 3); // visual order: 1, 2, 3, 0, 4, ...
    QApplication::processEvents();

    // Committed geometry (body, scroll bar, column queries): final at once.
    QCOMPARE(m_geometry->columnGeometry(0).viewportX, 3 * kSectionWidth);
    QCOMPARE(m_geometry->columnGeometry(1).viewportX, 0);
    QCOMPARE(m_geometry->columnGeometry(2).viewportX, kSectionWidth);

    // Visual geometry (the header section widget): still on its way.
    const int startedAt = moved->x();
    QVERIFY(startedAt < 3 * kSectionWidth);
    QTest::qWait(120);
    const int midway = moved->x();
    QVERIFY(midway >= startedAt);
    QVERIFY(midway <= 3 * kSectionWidth);

    QTest::qWait(200);
    QCOMPARE(moved->x(), 3 * kSectionWidth);
    QCOMPARE(m_header->sectionWidget(1)->x(), 0);
    QCOMPARE(m_header->sectionWidget(2)->x(), kSectionWidth);
}

void TestVirtualHeaderView::resizeAndDisabledAnimationStayImmediate()
{
    // A resize is one of the cases where the body follows every frame (§24), so the
    // header must not lag behind it.
    m_header->setSectionAnimationDuration(240);
    m_geometry->moveSection(0, 2); // visual order: 1, 2, 0, 3, ...
    QTest::qWait(300);
    QCOMPARE(m_header->sectionWidget(0)->x(), 2 * kSectionWidth);

    m_geometry->resizeSection(0, 2 * kSectionWidth);
    QApplication::processEvents();
    QCOMPARE(m_header->sectionWidget(0)->width(), 2 * kSectionWidth);
    // Everything after the resized section moves immediately, no easing.
    QCOMPARE(m_header->sectionWidget(3)->x(), 4 * kSectionWidth);

    // With the animation off a move is immediate as well.
    m_header->setSectionAnimationEnabled(false);
    QVERIFY(!m_header->sectionAnimationEnabled());
    m_geometry->moveSection(2, 0); // back to 0, 1, 2, 3, ...
    QApplication::processEvents();
    QCOMPARE(m_header->sectionWidget(0)->x(), 0);
}

void TestVirtualHeaderView::programmaticReorderIsImmediateUnlessRequested()
{
    m_header->setSectionAnimationDuration(240);
    QWidget *moved = m_header->sectionWidget(0);
    QVERIFY(moved != nullptr);
    QCOMPARE(moved->x(), 0);

    // A plain order change (what a programmatic reorder produces) is applied at once:
    // no animation the caller never asked for.
    m_geometry->moveSection(0, 2); // visual order: 1, 2, 0, 3, ...
    QApplication::processEvents();
    QCOMPARE(moved->x(), 2 * kSectionWidth);
    QCOMPARE(m_header->sectionWidget(1)->x(), 0);

    // Asking for it explicitly runs the same transition a gesture would get.
    m_header->setSectionMoveAnimated(true);
    m_geometry->moveSection(2, 0); // back to 0, 1, 2, 3, ...
    QApplication::processEvents();
    QVERIFY(moved->x() != 0); // still on its way
    QTest::qWait(400);
    QCOMPARE(moved->x(), 0);
    // The request is one-shot: the next programmatic change is immediate again.
    m_geometry->moveSection(0, 1);
    QApplication::processEvents();
    QCOMPARE(moved->x(), kSectionWidth);
}

void TestVirtualHeaderView::verticalWidgetHeaderPacksAlongY()
{
    // One renderer, two axes: a vertical instance is the row-number strip - sections
    // packed along y, sized by the row heights, resized at the row boundary and
    // reordered by dragging along the axis.
    VirtualHeaderView strip(Qt::Vertical);
    QCOMPARE(strip.orientation(), Qt::Vertical);
    strip.resize(48, 220);

    HeaderGeometry rows(Qt::Vertical, this);
    rows.setSectionCount(4);
    rows.setDefaultSectionSize(24);   // the geometry minimum is 24
    SectionAdapter adapter;
    strip.setAdapter(&adapter);
    strip.setGeometryModel(&rows);
    strip.setLabelModel(m_model);   // headerData(row, Qt::Vertical)
    strip.show();
    QApplication::processEvents();

    QCOMPARE(strip.materializedSections(), QList<int>({0, 1, 2, 3}));
    for (int row = 0; row < 4; ++row) {
        QWidget *section = strip.sectionWidget(row);
        QVERIFY(section != nullptr);
        QCOMPARE(section->y(), row * 24);
        QCOMPARE(section->height(), 24);
        QCOMPARE(section->width(), strip.width());   // the cross axis spans the strip
    }

    // Dragging the boundary between two rows resizes the row (the gesture is the same
    // one the column header uses, just along the other axis).
    sendMouse(&strip, QEvent::MouseButtonPress, QPoint(24, 23), Qt::LeftButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 43), Qt::NoButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseButtonRelease, QPoint(24, 43), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QCOMPARE(rows.sectionSize(0), 44);
    QCOMPARE(strip.sectionWidget(0)->height(), 44);
    QCOMPARE(strip.sectionWidget(1)->y(), 44);

    // A drag along the axis is the row equivalent of a column drag: the dragged section
    // follows the pointer, the others make room, and the release commits once.
    QWidget *dragged = strip.sectionWidget(1);
    QVERIFY(dragged != nullptr);
    sendMouse(&strip, QEvent::MouseButtonPress, QPoint(24, 56), Qt::LeftButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 58), Qt::NoButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 130), Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    // Preview only: the committed order is untouched while the drag is in flight.
    QCOMPARE(rows.visualIndex(1), 1);
    QVERIFY(dragged->y() > 44);
    sendMouse(&strip, QEvent::MouseButtonRelease, QPoint(24, 130), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QCOMPARE(rows.visualIndex(1), 3);   // row 1 landed behind the last row
    QTest::qWait(300);
    QCOMPARE(dragged->y(), 44 + 24 + 24);   // its committed slot

    // Binding a geometry of the other axis is still refused: the header keeps the
    // geometry it was laid out against instead of dropping it and going blank.
    HeaderGeometry columns(Qt::Horizontal, this);
    columns.setSectionCount(3);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("other orientation")));
    strip.setGeometryModel(&columns);
    QApplication::processEvents();
    QVERIFY(strip.geometryModel() == &rows);
    QVERIFY(!strip.materializedSections().isEmpty());
}

void TestVirtualHeaderView::mismatchedHeaderOrientationsAreRefused()
{
    // P2-4: a renderer of the wrong axis used to be laid out against the geometry of the
    // axis it does not belong to - a broken horizontal header built from a vertical
    // renderer, for instance. The view refuses the renderer and keeps the one in place.
    QStandardItemModel model(20, 8);
    QVERIFY(m_header->orientation() == Qt::Horizontal);
    PlainRowAdapter rowAdapter;
    VirtualTableView view;
    view.setTableAdapter(&rowAdapter);
    view.setModel(&model);
    view.setUniformItemHeight(24);
    view.setDefaultColumnWidth(kSectionWidth);
    vivtest::showView(&view, QSize(400, 200));

    HeaderViewInterface *const horizontalInPlace = view.horizontalHeader();
    HeaderViewInterface *const verticalInPlace = view.verticalHeader();
    QVERIFY(horizontalInPlace != nullptr);
    QVERIFY(verticalInPlace != nullptr);
    QVERIFY(horizontalInPlace->headerWidget()->width() > 0);

    auto *verticalRenderer = new VirtualHeaderView(Qt::Vertical);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not horizontal")));
    view.setHorizontalHeader(verticalRenderer);
    QCOMPARE(view.horizontalHeader(), horizontalInPlace);
    QVERIFY(horizontalInPlace->headerWidget()->width() > 0);
    delete verticalRenderer; // refused, so the view never took ownership

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not vertical")));
    view.setVerticalHeader(m_header);
    QCOMPARE(view.verticalHeader(), verticalInPlace);
    QVERIFY(verticalInPlace->headerWidget()->width() > 0);
    // The fixture header is untouched by the refused call, so it is still a stand-alone
    // horizontal renderer.
    QVERIFY(m_header->parentWidget() == nullptr);
    QCOMPARE(m_header->geometryModel(), m_geometry);
}

void TestVirtualHeaderView::tableAnimationIsOptInPerMove()
{
    QStandardItemModel model(20, 40);
    SectionAdapter adapter;
    PlainRowAdapter rowAdapter;
    VirtualTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&adapter);
    view.setHorizontalHeader(header);
    view.setTableAdapter(&rowAdapter);
    view.setUniformItemHeight(24);
    view.setDefaultColumnWidth(kSectionWidth);
    view.setModel(&model);
    vivtest::showView(&view, QSize(400, 200));
    view.setHeaderAnimationDuration(240);

    QWidget *moved = header->sectionWidget(0);
    QVERIFY(moved != nullptr);

    // Default: the move is immediate, in the header and in the body.
    view.moveColumn(0, 3);
    QApplication::processEvents();
    QCOMPARE(view.columnGeometry(0).viewportX, 3 * kSectionWidth);
    QCOMPARE(moved->x(), 3 * kSectionWidth);

    // Explicitly animated: the committed geometry is final while the header slides.
    view.moveColumn(0, 1, VirtualTableView::MoveAnimation::Animate);
    QApplication::processEvents();
    QCOMPARE(view.columnGeometry(0).viewportX, 0);
    QVERIFY(moved->x() != 0);
    QTest::qWait(400);
    QCOMPARE(moved->x(), 0);
}

void TestVirtualHeaderView::smallJitterIsStillAClick()
{
    const QPoint press(kSectionWidth + kSectionWidth / 2, kHeaderHeight / 2);
    sendMouse(m_header, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    // Below the drag distance: still a click, so nothing is reordered and the release
    // sets the sort indicator like any other click.
    sendMouse(m_header, QEvent::MouseMove, press + QPoint(2, 0), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(m_header, QEvent::MouseButtonRelease, press + QPoint(2, 0), Qt::LeftButton,
              Qt::NoButton);
    QApplication::processEvents();

    QCOMPARE(m_geometry->logicalIndex(0), 0);
    QCOMPARE(m_geometry->logicalIndex(1), 1);
    QCOMPARE(m_header->sectionWidget(0)->x(), 0);
    QCOMPARE(m_header->sectionWidget(1)->x(), kSectionWidth);
    QCOMPARE(m_geometry->sortIndicatorSection(), 1);
    QCOMPARE(m_geometry->sortIndicatorOrder(), Qt::AscendingOrder);
}

void TestVirtualHeaderView::dragPreviewsAndCommitsOnce()
{
    m_header->setSectionAnimationDuration(200);
    QWidget *dragged = m_header->sectionWidget(0);
    QWidget *neighbour = m_header->sectionWidget(1);
    QWidget *third = m_header->sectionWidget(2);
    QVERIFY(dragged != nullptr);
    QVERIFY(neighbour != nullptr);
    QVERIFY(third != nullptr);

    // Pick section 0 up in its middle and pull it towards the end of section 2.
    sendMouse(m_header, QEvent::MouseButtonPress, QPoint(kSectionWidth / 2, 5), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(m_header, QEvent::MouseMove, QPoint(kSectionWidth / 2 + 2, 5), Qt::NoButton,
              Qt::LeftButton);
    // Below the drag distance nothing happened yet, not even visually.
    QCOMPARE(dragged->x(), 0);
    QCOMPARE(m_geometry->visualIndex(0), 0);

    const QPoint target(2 * kSectionWidth + 60, 5);
    sendMouse(m_header, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    // Preview: the committed order is untouched and the dragged section follows the
    // pointer exactly.
    QCOMPARE(m_geometry->visualIndex(0), 0);
    QVERIFY(dragged->x() > 2 * kSectionWidth);
    // The sections it passed are still on their way - making room eases instead of
    // jumping - and the preview timer keeps them going while the pointer stands still.
    QVERIFY(neighbour->x() > 0);
    QVERIFY(third->x() > kSectionWidth);
    QTest::qWait(250);
    QCOMPARE(neighbour->x(), 0);
    QCOMPARE(third->x(), kSectionWidth);

    // Release: one commit (0 lands behind 2) and the transition settles from where the
    // preview left the section - it does not jump to the committed position.
    sendMouse(m_header, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QCOMPARE(m_geometry->visualIndex(0), 2);
    QVERIFY(dragged->x() >= 2 * kSectionWidth);
    QTest::qWait(400);
    QCOMPARE(dragged->x(), 2 * kSectionWidth);
    QCOMPARE(neighbour->x(), 0);
    QCOMPARE(third->x(), kSectionWidth);
}
void TestVirtualHeaderView::animationDefaultsToOutCubicWith300ms()
{
    QCOMPARE(m_header->sectionAnimationDuration(), 300);
    QVERIFY(m_header->sectionAnimationEnabled());

    // The "make room" tween uses the same curve: at half of the duration an OutCubic
    // easing is already 87.5% of the way, far past what a linear one would show (50%).
    QWidget *dragged = m_header->sectionWidget(0);
    QWidget *neighbour = m_header->sectionWidget(1);
    QVERIFY(dragged != nullptr);
    QVERIFY(neighbour != nullptr);
    sendMouse(m_header, QEvent::MouseButtonPress, QPoint(kSectionWidth / 2, 5), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(m_header, QEvent::MouseMove, QPoint(2 * kSectionWidth + 60, 5), Qt::NoButton,
              Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY(neighbour->x() > 0); // not teleported
    QTest::qWait(150);           // half of the default duration
    QVERIFY(neighbour->x() < kSectionWidth / 4);
    QTest::qWait(300);
    QCOMPARE(neighbour->x(), 0);

    // Cancel and leave the header in a clean state for the other tests.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(m_header, &escape);
    QApplication::processEvents();
    QCOMPARE(dragged->x(), 0);
}

void TestVirtualHeaderView::dragRoomEasesAndCanBeTurnedOff()
{
    // The arrangement of the other sections is part of the animation: with it switched
    // off (or a duration of 0) making room is immediate, like everything else.
    m_header->setSectionAnimationEnabled(false);
    QWidget *dragged = m_header->sectionWidget(0);
    QWidget *neighbour = m_header->sectionWidget(1);
    QVERIFY(dragged != nullptr);
    QVERIFY(neighbour != nullptr);

    sendMouse(m_header, QEvent::MouseButtonPress, QPoint(kSectionWidth / 2, 5), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(m_header, QEvent::MouseMove, QPoint(2 * kSectionWidth + 60, 5), Qt::NoButton,
              Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(neighbour->x(), 0); // immediate, no easing
    QVERIFY(dragged->x() > 2 * kSectionWidth);

    // Escape drops the preview; the order never changed.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(m_header, &escape);
    QApplication::processEvents();
    QCOMPARE(neighbour->x(), kSectionWidth);
    QCOMPARE(dragged->x(), 0);
    QCOMPARE(m_geometry->visualIndex(0), 0);
}

void TestVirtualHeaderView::dragReordersInsideItsPaneOnly()
{
    // A pane header shows only its own columns: a drag reorders inside that pane and
    // leaves every other column alone.
    m_header->setSectionAnimationDuration(160);
    m_header->setPaneFilter(QVector<int>({1, 3}), true);
    m_header->setPaneOffset(0); // the pane packs its own columns (§31/§43)
    QApplication::processEvents();
    QWidget *one = m_header->sectionWidget(1);
    QWidget *three = m_header->sectionWidget(3);
    QVERIFY(one != nullptr);
    QVERIFY(three != nullptr);
    QCOMPARE(one->x(), 0);
    QCOMPARE(three->x(), kSectionWidth);

    sendMouse(m_header, QEvent::MouseButtonPress, QPoint(kSectionWidth / 2, 5), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(m_header, QEvent::MouseMove, QPoint(2 * kSectionWidth - 5, 5), Qt::NoButton,
              Qt::LeftButton);
    QApplication::processEvents();
    // Nothing committed while the drag is in flight.
    QCOMPARE(m_geometry->visualIndex(1), 1);
    sendMouse(m_header, QEvent::MouseButtonRelease, QPoint(2 * kSectionWidth - 5, 5),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    // Column 1 now sits after column 3 inside the pane, while the columns outside the
    // moved span keep their relative order (the ones in between shift, which is what a
    // reorder in a flat order does). The transition starts at the preview position, so
    // let it settle before comparing.
    QTest::qWait(300);
    QCOMPARE(three->x(), 0);
    QCOMPARE(one->x(), kSectionWidth);
    QCOMPARE(m_geometry->visualIndex(0), 0);
    QVERIFY(m_geometry->visualIndex(1) > m_geometry->visualIndex(3));
    QVERIFY(m_geometry->visualIndex(4) > m_geometry->visualIndex(3));
}

void TestVirtualHeaderView::escapeCancelsTheDrag()
{
    QWidget *dragged = m_header->sectionWidget(0);
    QVERIFY(dragged != nullptr);
    const int widthBefore = dragged->width();

    sendMouse(m_header, QEvent::MouseButtonPress, QPoint(kSectionWidth / 2, 5), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(m_header, QEvent::MouseMove, QPoint(2 * kSectionWidth + 40, 5), Qt::NoButton,
              Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY(dragged->x() > kSectionWidth);

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(m_header, &escape);
    QApplication::processEvents();
    // Cancelled: back to the committed geometry, order untouched.
    QCOMPARE(dragged->x(), 0);
    QCOMPARE(dragged->width(), widthBefore);
    QCOMPARE(m_geometry->logicalIndex(0), 0);
    QCOMPARE(m_geometry->logicalIndex(1), 1);
}

void TestVirtualHeaderView::hoverOverASectionWidgetUpdatesTheCursor()
{
    // §25: the section widgets cover the header, so the cursor has to follow *their*
    // mouse events. Otherwise the resize cursor sticks to the whole header (children
    // inherit the parent's cursor) and an ordinary hover looks like a resize zone.
    QWidget *section = m_header->sectionWidget(1);
    QVERIFY(section != nullptr);
    QCOMPARE(m_header->cursor().shape(), Qt::ArrowCursor);

    const auto moveInside = [](QWidget *widget, const QPoint &local) {
        QMouseEvent event(QEvent::MouseMove, local, widget->mapToGlobal(local), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    };

    // Middle of the section: nothing to resize.
    moveInside(section, QPoint(kSectionWidth / 2, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::ArrowCursor);
    // Its trailing edge: the width cursor appears even though the header itself saw no
    // mouse move at all.
    moveInside(section, QPoint(kSectionWidth - 1, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::SplitHCursor);
    // Back into the middle: the cursor must not stick.
    moveInside(section, QPoint(kSectionWidth / 2, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::ArrowCursor);

    // A widget the business put inside the section reports as well.
    auto *label = section->findChild<QLabel *>(QStringLiteral("sectionLabel"));
    QVERIFY(label != nullptr);
    moveInside(label, QPoint(label->width() / 2, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::ArrowCursor);
    moveInside(label, QPoint(label->width() - 1, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::SplitHCursor);
}

void TestVirtualHeaderView::childrenAddedAfterBindingAreWatchedToo()
{
    // The filter cannot only be installed on the subtree that exists when the section
    // is bound: a business widget that appears later (a state label, a button for a
    // row that became editable) would then stop reporting its position, and the
    // resize cursor would stick again for that part of the header (§25).
    QWidget *section = m_header->sectionWidget(1);
    QVERIFY(section != nullptr);
    QCOMPARE(section->x(), kSectionWidth);

    // A container added after the binding, and a label added inside that container
    // afterwards: both are only reachable through ChildAdded.
    auto *late = new QWidget(section);
    late->setGeometry(kSectionWidth - 20, 0, 20, kHeaderHeight);
    auto *lateLabel = new QLabel(late);
    lateLabel->setGeometry(0, 0, 20, kHeaderHeight);
    QApplication::processEvents();

    const auto moveInside = [](QWidget *widget, const QPoint &local) {
        QMouseEvent event(QEvent::MouseMove, local, widget->mapToGlobal(local), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    };

    // The label covers the trailing edge of section 1 (header x = 180), so a move near
    // its own right edge has to switch to the width cursor...
    moveInside(lateLabel, QPoint(19, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::SplitHCursor);
    // ... and back in the middle of the header it has to go away again.
    moveInside(lateLabel, QPoint(5, 5));
    QCOMPARE(m_header->cursor().shape(), Qt::ArrowCursor);

    // Both the container and its later child are watched (mouse tracking included),
    // which is what makes the assertion above work with real mouse events as well.
    QVERIFY(late->hasMouseTracking());
    QVERIFY(lateLabel->hasMouseTracking());
}

void TestVirtualHeaderView::modelChangesRebindTheMaterializedSections()
{
    // P0-3 of the second review: the header connected the model's change signals to
    // relayout(), but relayout() only bound *newly acquired* widgets - an already
    // materialized section kept the label it was bound with, so a renamed column and a
    // structural change both left the visible sections showing the wrong identity.
    auto *adapter = new ModelSectionAdapter(m_model, m_geometry);
    m_header->setAdapter(adapter, true); // the header owns it, so it outlives the test body
    QApplication::processEvents();
    QVERIFY(adapter->bindCount > 0);
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("c1"));

    // (a) headerDataChanged: the visible section has to show the new title.
    m_model->setHeaderData(1, Qt::Horizontal, QStringLiteral("renamed"));
    QApplication::processEvents();
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("renamed"));

    // (b) columnsInserted in the middle: every materialized section shows the label of the
    // logical column it occupies *now*, and the widget that used to be column 1 is not
    // left showing the old labels.
    m_model->insertColumn(1);
    m_model->setHeaderData(1, Qt::Horizontal, QStringLiteral("inserted"));
    QApplication::processEvents();
    for (int logical : m_header->materializedSections()) {
        QCOMPARE(adapter->textOf(m_header, logical),
                 m_model->headerData(logical, Qt::Horizontal).toString());
    }

    // (c) columnsRemoved: same contract.
    m_model->removeColumn(0);
    m_model->setHeaderData(0, Qt::Horizontal, QStringLiteral("first"));
    QApplication::processEvents();
    for (int logical : m_header->materializedSections()) {
        QCOMPARE(adapter->textOf(m_header, logical),
                 m_model->headerData(logical, Qt::Horizontal).toString());
    }

    // (d) modelReset: the sections are rebuilt from scratch and bound again.
    m_model->clear();
    m_model->setColumnCount(12);
    QStringList labels;
    for (int column = 0; column < 12; ++column)
        labels << QStringLiteral("reset%1").arg(column);
    m_model->setHorizontalHeaderLabels(labels);
    QApplication::processEvents();
    QVERIFY(!m_header->materializedSections().isEmpty());
    for (int logical : m_header->materializedSections()) {
        QCOMPARE(adapter->textOf(m_header, logical),
                 m_model->headerData(logical, Qt::Horizontal).toString());
    }
}

void TestVirtualHeaderView::sortIndicatorChangesRebindTheSections()
{
    // The section UI is built in bindSection(), so a sort change that only updated the
    // geometry would leave the arrow of the business widget stale.
    auto *adapter = new ModelSectionAdapter(m_model, m_geometry);
    m_header->setAdapter(adapter, true);
    QApplication::processEvents();

    QTest::mouseClick(m_header, Qt::LeftButton, Qt::NoModifier,
                      QPoint(kSectionWidth + kSectionWidth / 2, kHeaderHeight / 2));
    QApplication::processEvents();
    QCOMPARE(m_geometry->sortIndicatorSection(), 1);
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("c1 ^"));

    QTest::mouseClick(m_header, Qt::LeftButton, Qt::NoModifier,
                      QPoint(kSectionWidth + kSectionWidth / 2, kHeaderHeight / 2));
    QApplication::processEvents();
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("c1 v"));
}

void TestVirtualHeaderView::switchingTheLabelModelRebindsTheSections()
{
    // P1 of the third review: the adapter usually captures the model (the README example
    // does), so switching the header's label model has to (a) tell the adapter and (b)
    // re-bind the sections that are already on screen - relayout() alone left them showing
    // the titles of the outgoing model.
    auto *adapter = new ModelSectionAdapter(m_model, m_geometry);
    m_header->setAdapter(adapter, true);
    QApplication::processEvents();
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("c1"));
    const int changesBefore = adapter->labelModelChanges;

    QStandardItemModel otherModel(1, 40);
    QStringList labels;
    for (int column = 0; column < 40; ++column)
        labels << QStringLiteral("other%1").arg(column);
    otherModel.setHorizontalHeaderLabels(labels);

    m_header->setLabelModel(&otherModel);
    QApplication::processEvents();

    QCOMPARE(adapter->labelModelChanges, changesBefore + 1);
    QCOMPARE(adapter->labelModel(), static_cast<QAbstractItemModel *>(&otherModel));
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("other1"));
    QCOMPARE(adapter->textOf(m_header, 0), QStringLiteral("other0"));
}

void TestVirtualHeaderView::installingAnAdapterHandsOverTheCurrentLabelModel()
{
    // P1.1 of the fourth review: setAdapter() installed the new collaborator without telling it
    // about the label model the header already had, so the two call orders behaved differently:
    //     setLabelModel(model); setAdapter(adapter);   // adapter never heard about the model
    //     setAdapter(adapter); setLabelModel(model);   // worked
    // A header that is set up like the first order (model first) is the natural one for a
    // business that builds the header once and swaps adapters.
    auto *adapter = new ModelSectionAdapter(nullptr, m_geometry);
    QCOMPARE(adapter->labelModel(), nullptr);

    m_header->setLabelModel(m_model);
    m_header->setAdapter(adapter, true);
    QApplication::processEvents();

    QCOMPARE(adapter->labelModel(), static_cast<QAbstractItemModel *>(m_model));
    QCOMPARE(adapter->textOf(m_header, 1), QStringLiteral("c1"));

    // The other order keeps working, and a second replacement sees the model as well.
    auto *second = new ModelSectionAdapter(nullptr, m_geometry);
    m_header->setAdapter(second, true);   // owns (and deletes) the first adapter
    QApplication::processEvents();
    QCOMPARE(second->labelModel(), static_cast<QAbstractItemModel *>(m_model));
    QCOMPARE(second->textOf(m_header, 2), QStringLiteral("c2"));
}

void TestVirtualHeaderView::theAdapterFollowsTheGeometryCollaborator()
{
    // P1.3 of the fourth review: setGeometryModel() is a public, replaceable collaborator, and a
    // section widget that draws sort state or reads a column width has to read it. Without a
    // hook the adapter captured the pointer once - so a replacement left it on the old geometry
    // and a destroyed geometry left it dangling. The header now hands the geometry over (also on
    // setAdapter()) and reports nullptr when the collaborator goes away.
    auto *adapter = new ModelSectionAdapter(m_model, nullptr);
    QCOMPARE(adapter->hookedGeometry(), nullptr);
    m_header->setAdapter(adapter, true);
    QCOMPARE(adapter->hookedGeometry(), m_geometry);   // setAdapter() hands over what it has

    auto *replacement = new HeaderGeometry(Qt::Horizontal, this);
    replacement->setSectionCount(m_model->columnCount());
    replacement->setDefaultSectionSize(kSectionWidth);
    const int changesBefore = adapter->geometryModelChanges;
    m_header->setGeometryModel(replacement);
    QApplication::processEvents();
    QVERIFY(adapter->geometryModelChanges > changesBefore);
    QCOMPARE(adapter->hookedGeometry(), replacement);
    QCOMPARE(m_header->geometryModel(), replacement);
    // The sort state the section UI draws comes from the *current* geometry.
    replacement->setSortIndicator(2, Qt::DescendingOrder);
    QApplication::processEvents();
    QCOMPARE(adapter->textOf(m_header, 2), QStringLiteral("c2 v"));

    // A destroyed collaborator is reported as "no geometry", not as a dangling pointer.
    delete replacement;
    QCOMPARE(m_header->geometryModel(), nullptr);
    QCOMPARE(adapter->hookedGeometry(), nullptr);
}

void TestVirtualHeaderView::switchingTheGeometryInvalidatesThePaneCache()
{
    // P1 of the third review: the pane cache (packing order + prefix sums) described the
    // geometry that was just replaced, so a standalone header that switches geometries kept
    // packing its pane with the old widths.
    auto *adapter = new ModelSectionAdapter(m_model, m_geometry);
    m_header->setAdapter(adapter, true);
    m_header->setPaneFilter(QVector<int>({0, 1}), false);
    m_header->setPaneOffset(0);
    QApplication::processEvents();
    QWidget *second = m_header->sectionWidget(1);
    QVERIFY(second != nullptr);
    QCOMPARE(second->x(), kSectionWidth);

    auto *narrowGeometry = new HeaderGeometry(Qt::Horizontal, this);
    narrowGeometry->setSectionCount(m_model->columnCount());
    narrowGeometry->setDefaultSectionSize(kSectionWidth / 2);
    m_header->setGeometryModel(narrowGeometry);
    QApplication::processEvents();

    QCOMPARE(m_header->geometryModel(), narrowGeometry);
    // The pane packs with the *new* widths.
    QWidget *first = m_header->sectionWidget(0);
    QVERIFY(first != nullptr);
    QCOMPARE(first->x(), 0);
    QCOMPARE(second->x(), kSectionWidth / 2);
    QCOMPARE(second->width(), kSectionWidth / 2);
}

void TestVirtualHeaderView::restoringASortStateRebindsTheSections()
{
    // P1 of the third review: restoreState() replaced the sort state without emitting
    // sortIndicatorChanged, so a custom section UI that draws the arrow kept the state it
    // had before the restore.
    auto *adapter = new ModelSectionAdapter(m_model, m_geometry);
    m_header->setAdapter(adapter, true);
    m_header->setSortInteractionEnabled(true);
    QApplication::processEvents();

    m_geometry->setSortIndicator(2, Qt::AscendingOrder);
    QApplication::processEvents();
    QCOMPARE(adapter->textOf(m_header, 2), QStringLiteral("c2 ^"));
    const QByteArray state = m_geometry->saveState();

    m_geometry->setSortIndicator(5, Qt::DescendingOrder);
    QApplication::processEvents();
    QCOMPARE(adapter->textOf(m_header, 5), QStringLiteral("c5 v"));

    QVERIFY(m_geometry->restoreState(state));
    QApplication::processEvents();
    QCOMPARE(m_geometry->sortIndicatorSection(), 2);
    QCOMPARE(adapter->textOf(m_header, 2), QStringLiteral("c2 ^"));
    QCOMPARE(adapter->textOf(m_header, 5), QStringLiteral("c5"));   // no stale arrow
}

void TestVirtualHeaderView::tableForwardsTheAnimationSettings()
{
    QStandardItemModel model(20, 40);
    SectionAdapter adapter;
    PlainRowAdapter rowAdapter;
    VirtualTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&adapter);
    view.setHorizontalHeader(header);
    view.setTableAdapter(&rowAdapter);
    view.setUniformItemHeight(24);
    view.setDefaultColumnWidth(kSectionWidth);
    view.setModel(&model);
    vivtest::showView(&view, QSize(400, 200));
    QVERIFY(header->width() > 0);

    view.setHeaderAnimationDuration(160);
    QCOMPARE(header->sectionAnimationDuration(), 160);
    view.setHeaderAnimationEnabled(false);
    QVERIFY(!header->sectionAnimationEnabled());
    view.setHeaderAnimationEnabled(true);
    QVERIFY(header->sectionAnimationEnabled());

    // End to end: the committed column geometry is final while the header section
    // is still sliding to it.
    view.moveColumn(0, 3, VirtualTableView::MoveAnimation::Animate);
    QApplication::processEvents();
    QCOMPARE(view.columnGeometry(0).viewportX, 3 * kSectionWidth);
    QWidget *moved = header->sectionWidget(0);
    QVERIFY(moved != nullptr);
    QVERIFY(moved->x() < 3 * kSectionWidth);
    QTest::qWait(300);
    QCOMPARE(moved->x(), 3 * kSectionWidth);
}

// ---------------------------------------------------------------------------
// The body follows the header's visual section geometry (§23/§24)
// ---------------------------------------------------------------------------

namespace {

/// Table with a widget header and one ColumnHost per column: the header/body pair the
/// three tests below need. The view, the model and the adapters are members so the
/// fixture reads as a single statement per test.
struct HeaderBody
{
    QStandardItemModel model;
    SectionAdapter adapter;
    HostRowAdapter rowAdapter;
    VirtualTableView view;
    VirtualHeaderView *header = nullptr;

    HeaderBody()
        : model(20, 20)
        , rowAdapter(20)
    {
        header = new VirtualHeaderView(Qt::Horizontal);
        header->setAdapter(&adapter);
        view.setHorizontalHeader(header);
        view.setTableAdapter(&rowAdapter);
        view.setUniformItemHeight(24);
        view.setDefaultColumnWidth(kSectionWidth);
        view.setModel(&model);
        vivtest::showView(&view, QSize(400, 200));
    }

    /// First materialized row widget - the framework's container of the column hosts.
    QWidget *rowWidget() const
    {
        const QList<MaterializedItem> rows = view.materializedItems();
        return rows.isEmpty() ? nullptr : rows.first().widget;
    }
    int committedX(int column) const { return view.columnGeometry(column).viewportX; }
    int bodyX(int column) const { return HostRowAdapter::hostX(rowWidget(), column); }
    int sectionX(int column) const
    {
        QWidget *section = header->sectionWidget(column);
        return section ? section->x() : std::numeric_limits<int>::min();
    }
    /// Viewport x of a materialized section of \a renderer (the section's x is relative
    /// to its renderer, which sits on its pane rect, not on the viewport origin).
    int viewportXOf(const VirtualHeaderView *renderer, int column) const
    {
        QWidget *section = renderer->sectionWidget(column);
        return section ? section->x() + renderer->x() - view.viewport()->x()
                       : std::numeric_limits<int>::min();
    }
};

} // namespace

void TestVirtualHeaderView::bodyFollowsTheSectionsWhileAMoveAnimates()
{
    HeaderBody fixture;
    fixture.view.setHeaderAnimationDuration(300);

    // Default: on. Without it the body would wait for the commit.
    QVERIFY(fixture.view.columnFollowsHeaderVisual());
    QVERIFY(fixture.rowWidget() != nullptr);
    // The row widget covers the viewport and the header is placed on it, so the two
    // x spaces coincide - which is what lets the test compare them directly.
    QCOMPARE(fixture.bodyX(0), fixture.committedX(0));
    QCOMPARE(fixture.bodyX(0), fixture.sectionX(0));

    QWidget *moved = fixture.header->sectionWidget(0);
    QVERIFY(moved != nullptr);
    fixture.view.moveColumn(0, 3, VirtualTableView::MoveAnimation::Animate);
    QApplication::processEvents();

    // Committed geometry (columnGeometry(), hit testing, the scroll bar): final at once.
    QCOMPARE(fixture.committedX(0), 3 * kSectionWidth);
    // Visual geometry: the section is on its way, and the body's column is drawn where
    // the section is - not where the commit says.
    QVERIFY(moved->x() < 3 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), moved->x());
    QVERIFY(fixture.bodyX(0) != 3 * kSectionWidth);
    QCOMPARE(fixture.bodyX(1), fixture.sectionX(1));

    // Mid-flight, still glued together.
    QTest::qWait(150);
    QCOMPARE(fixture.bodyX(0), moved->x());
    QCOMPARE(fixture.bodyX(0), fixture.sectionX(0));
    QVERIFY(fixture.bodyX(0) > 0);

    // A layout pass of the *view* during the transition (a resize re-lays out every
    // row) must not tear the body back onto the committed geometry.
    fixture.view.resize(fixture.view.width(), fixture.view.height() - 20);
    QApplication::processEvents();
    QCOMPARE(fixture.bodyX(0), fixture.sectionX(0));

    QTest::qWait(400);
    QCOMPARE(moved->x(), 3 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), 3 * kSectionWidth);
    QCOMPARE(fixture.bodyX(1), fixture.committedX(1));
}

void TestVirtualHeaderView::bodyFollowsTheDragPreviewAndTheCommit()
{
    HeaderBody fixture;
    fixture.view.setHeaderAnimationDuration(200);
    QWidget *headerWidget = fixture.view.horizontalHeader()->headerWidget();
    QVERIFY(fixture.rowWidget() != nullptr);

    // Pick column 0 up in its middle and pull it past column 2.
    sendMouse(headerWidget, QEvent::MouseButtonPress,
              QPoint(kSectionWidth / 2, kHeaderHeight / 2), Qt::LeftButton, Qt::LeftButton);
    sendMouse(headerWidget, QEvent::MouseMove,
              QPoint(kSectionWidth / 2 + 2, kHeaderHeight / 2), Qt::NoButton, Qt::LeftButton);
    sendMouse(headerWidget, QEvent::MouseMove, QPoint(2 * kSectionWidth + 60, kHeaderHeight / 2),
              Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();

    // The committed geometry is untouched while the drag is in flight, but the body has
    // already left it: the dragged column follows the pointer, like its section.
    QCOMPARE(fixture.committedX(0), 0);
    QVERIFY(fixture.bodyX(0) > 2 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), fixture.sectionX(0));

    // The sections making room tween to their slot, and the columns come along.
    QTest::qWait(250);
    QCOMPARE(fixture.bodyX(1), 0);
    QCOMPARE(fixture.bodyX(2), kSectionWidth);
    QCOMPARE(fixture.bodyX(1), fixture.sectionX(1));

    // Escape drops the preview: header *and* body return to the committed geometry.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(headerWidget, &escape);
    QApplication::processEvents();
    QCOMPARE(fixture.committedX(0), 0);
    QCOMPARE(fixture.bodyX(0), 0);
    QCOMPARE(fixture.bodyX(1), kSectionWidth);
    QCOMPARE(fixture.bodyX(2), 2 * kSectionWidth);

    // Now the same gesture, but committing: the column converges from the position the
    // preview left it at, together with the section.
    sendMouse(headerWidget, QEvent::MouseButtonPress,
              QPoint(kSectionWidth / 2, kHeaderHeight / 2), Qt::LeftButton, Qt::LeftButton);
    sendMouse(headerWidget, QEvent::MouseMove,
              QPoint(kSectionWidth / 2 + 2, kHeaderHeight / 2), Qt::NoButton, Qt::LeftButton);
    sendMouse(headerWidget, QEvent::MouseMove, QPoint(2 * kSectionWidth + 60, kHeaderHeight / 2),
              Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    sendMouse(headerWidget, QEvent::MouseButtonRelease,
              QPoint(2 * kSectionWidth + 60, kHeaderHeight / 2), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    // One commit, and the body is still where the preview left it - no jump.
    QCOMPARE(fixture.committedX(0), 2 * kSectionWidth);
    QVERIFY(fixture.bodyX(0) > 2 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), fixture.sectionX(0));

    QTest::qWait(350);
    QCOMPARE(fixture.bodyX(0), 2 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), fixture.sectionX(0));
    QCOMPARE(fixture.bodyX(1), fixture.committedX(1));
}

void TestVirtualHeaderView::bodyFollowCanBeTurnedOff()
{
    HeaderBody fixture;
    fixture.view.setHeaderAnimationDuration(300);
    fixture.view.setColumnFollowsHeaderVisual(false);
    QVERIFY(!fixture.view.columnFollowsHeaderVisual());

    QWidget *moved = fixture.header->sectionWidget(0);
    QVERIFY(moved != nullptr);
    fixture.view.moveColumn(0, 3, VirtualTableView::MoveAnimation::Animate);
    QApplication::processEvents();

    // The header still slides, the body is on the committed geometry at once - the
    // behaviour of the library before the setting existed.
    QVERIFY(moved->x() < 3 * kSectionWidth);
    QCOMPARE(fixture.committedX(0), 3 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), 3 * kSectionWidth);
    QTest::qWait(400);
    QCOMPARE(moved->x(), 3 * kSectionWidth);
    QCOMPARE(fixture.bodyX(0), 3 * kSectionWidth);
}

void TestVirtualHeaderView::bodyFollowsAFrozenPaneDrag()
{
    HeaderBody fixture;
    fixture.view.setHeaderAnimationDuration(200);
    // Two frozen columns: they get their own renderer (§31/§43), and a drag inside that
    // pane is a gesture the primary header never sees.
    fixture.view.setFrozenColumns(QVector<int>({0, 1}));
    QApplication::processEvents();

    VirtualHeaderView *frozenPane = nullptr;
    for (VirtualHeaderView *candidate : fixture.view.findChildren<VirtualHeaderView *>()) {
        if (candidate != fixture.header)
            frozenPane = candidate;
    }
    QVERIFY(frozenPane != nullptr);
    QWidget *frozenSection = frozenPane->sectionWidget(0);
    QVERIFY(frozenSection != nullptr);
    QVERIFY(fixture.rowWidget() != nullptr);
    QCOMPARE(fixture.committedX(0), 0);
    QCOMPARE(fixture.bodyX(0), 0);

    QWidget *paneWidget = frozenPane->headerWidget();
    sendMouse(paneWidget, QEvent::MouseButtonPress,
              QPoint(kSectionWidth / 2, kHeaderHeight / 2), Qt::LeftButton, Qt::LeftButton);
    sendMouse(paneWidget, QEvent::MouseMove, QPoint(kSectionWidth / 2 + 2, kHeaderHeight / 2),
              Qt::NoButton, Qt::LeftButton);
    sendMouse(paneWidget, QEvent::MouseMove, QPoint(2 * kSectionWidth - 5, kHeaderHeight / 2),
              Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();

    // The frozen column of the body follows the section of *its* pane, even though the
    // primary header is not the one being dragged.
    QCOMPARE(fixture.committedX(0), 0);
    QVERIFY(fixture.bodyX(0) > 0);
    QCOMPARE(fixture.bodyX(0), frozenSection->x());

    sendMouse(paneWidget, QEvent::MouseButtonRelease, QPoint(2 * kSectionWidth - 5, kHeaderHeight / 2),
              Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    // Committed inside the pane: column 1 first, column 0 after it. The body converges
    // from the preview position with the section.
    QCOMPARE(fixture.committedX(0), kSectionWidth);
    QCOMPARE(fixture.committedX(1), 0);
    QCOMPARE(fixture.bodyX(0), frozenSection->x());
    QTest::qWait(350);
    QCOMPARE(fixture.bodyX(0), kSectionWidth);
    QCOMPARE(fixture.bodyX(1), 0);
    QCOMPARE(frozenSection->x(), kSectionWidth);
}

void TestVirtualHeaderView::cellsFollowTheHeaderWhileTheBodyDoes()
{
    // Cell Widget Mode has its own geometry pass (one widget per cell instead of one
    // ColumnHost per column), so the follow has to work there too.
    QStandardItemModel model(20, 20);
    SectionAdapter sectionAdapter;
    PlainCellAdapter cellAdapter;
    VirtualTableView view;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&sectionAdapter);
    view.setHorizontalHeader(header);
    view.setCellAdapter(&cellAdapter);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setUniformItemHeight(24);
    view.setDefaultColumnWidth(kSectionWidth);
    view.setModel(&model);
    vivtest::showView(&view, QSize(400, 200));
    view.setHeaderAnimationDuration(300);

    QWidget *cell = view.cellWidget(model.index(0, 0));
    QVERIFY(cell != nullptr);
    QCOMPARE(cell->x(), view.columnGeometry(0).viewportX);
    QCOMPARE(cell->x(), header->sectionWidget(0)->x());

    view.moveColumn(0, 3, VirtualTableView::MoveAnimation::Animate);
    QApplication::processEvents();
    QCOMPARE(view.columnGeometry(0).viewportX, 3 * kSectionWidth);
    QTest::qWait(150);
    // The cell is drawn where its section is - the committed geometry never moved.
    QCOMPARE(cell->x(), header->sectionWidget(0)->x());
    QVERIFY(cell->x() < 3 * kSectionWidth);
    QTest::qWait(400);
    QCOMPARE(cell->x(), 3 * kSectionWidth);
}

void TestVirtualHeaderView::uniformGeometryDragsWithoutBuildingTheOrder()
{
    // §7/§3 of the vertical-header decision: on a ten-million-row *uniform* strip the drag
    // path must not build the packed order (that vector would be 40 MB and rebuilt per
    // mouse move). The identity order answers every lookup directly.
    constexpr int kRows = 10'000'000;
    constexpr int kRowHeight = 24;
    vivtest::NumericListModel model(kRows);
    HeaderGeometry rows(Qt::Vertical, this);
    rows.setSectionCount(kRows);
    rows.setDefaultSectionSize(kRowHeight);

    VirtualHeaderView strip(Qt::Vertical);
    strip.resize(48, 800);
    SectionAdapter adapter;
    strip.setAdapter(&adapter);
    strip.setGeometryModel(&rows);
    strip.setLabelModel(&model);
    strip.setSectionOrderExternal(true);
    QSignalSpy requestSpy(&strip, &VirtualHeaderView::sectionMoveRequested);
    strip.show();
    QApplication::processEvents();

    // Start a drag on the first visible row and pull it far down: the preview must resolve
    // through the identity order (no order vector), and the commit must report the packed
    // target index.
    sendMouse(&strip, QEvent::MouseButtonPress, QPoint(24, 10), Qt::LeftButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 12), Qt::NoButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 300), Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY(rows.storedSectionStateCount() == 0);   // still the compact representation
    QCOMPARE(rows.visualIndex(0), 0);               // preview only
    sendMouse(&strip, QEvent::MouseButtonRelease, QPoint(24, 300), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    QCOMPARE(requestSpy.count(), 1);
    const int from = requestSpy.first().at(0).toInt();
    const int to = requestSpy.first().at(1).toInt();
    QCOMPARE(from, 0);
    // The pointer travelled 290 px from the grab point at row 0's centre: the target is the
    // packed slot whose centre is still left of the dragged centre.
    QCOMPARE(to, (290 + 12) / kRowHeight);
    // Nothing was reordered here (external order), and nothing was materialised per row.
    QCOMPARE(rows.storedSectionStateCount(), 0);
    QVERIFY(strip.materializedSectionCount() < 100);
}
void TestVirtualHeaderView::externalRowOrderDropsThePreviewWhenNobodyMovesTheRow()
{
    // §9: with an external order the strip only *asks* for the move. Nothing has moved
    // here (a model that refuses moveRows, or an application that ignored the request), so
    // the drag must not leave a preview behind: the sections are back on the committed
    // order and the geometry was never reordered.
    VirtualHeaderView strip(Qt::Vertical);
    strip.resize(48, 200);
    HeaderGeometry rows(Qt::Vertical, this);
    rows.setSectionCount(4);
    rows.setDefaultSectionSize(24);
    SectionAdapter adapter;
    strip.setAdapter(&adapter);
    strip.setGeometryModel(&rows);
    strip.setLabelModel(m_model);
    strip.setSectionOrderExternal(true);
    QSignalSpy requestSpy(&strip, &VirtualHeaderView::sectionMoveRequested);
    strip.show();
    QApplication::processEvents();

    sendMouse(&strip, QEvent::MouseButtonPress, QPoint(24, 30), Qt::LeftButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 32), Qt::NoButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseMove, QPoint(24, 130), Qt::NoButton, Qt::LeftButton);
    sendMouse(&strip, QEvent::MouseButtonRelease, QPoint(24, 130), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(requestSpy.first().at(0).toInt(), 1);
    QCOMPARE(requestSpy.first().at(1).toInt(), 3);
    for (int row = 0; row < 4; ++row) {
        QCOMPARE(rows.visualIndex(row), row);          // nobody moved the row
        QCOMPARE(strip.sectionWidget(row)->y(), row * 24);   // no stale preview
    }
}
void TestVirtualHeaderView::verticalWidgetHeaderHandlesTenMillionUniformRows()
{
    // §7/§11 of the vertical-header decision: a uniform geometry stores nothing per row,
    // so the strip materializes only the window it shows - and scrolling near the
    // beginning, the middle and the end stays O(visible + overscan), never O(rowCount).
    constexpr int kRows = 10'000'000;
    constexpr int kRowHeight = 24;
    constexpr int kStripExtent = 800;
    vivtest::NumericListModel model(kRows);   // O(1) rows, no per-row storage

    HeaderGeometry rows(Qt::Vertical, this);
    rows.setSectionCount(kRows);
    rows.setDefaultSectionSize(kRowHeight);
    QCOMPARE(rows.sectionCount(), kRows);
    QCOMPARE(rows.storedSectionStateCount(), 0);

    VirtualHeaderView strip(Qt::Vertical);
    strip.resize(48, kStripExtent);
    SectionAdapter adapter;
    strip.setAdapter(&adapter);
    strip.setGeometryModel(&rows);
    strip.setLabelModel(&model);
    strip.show();
    QApplication::processEvents();

    // A window is 800/24 = 34 rows; the strip keeps the visible ones plus overscan.
    const auto checkWindow = [&](int firstRow) {
        QVERIFY(strip.materializedSectionCount() <= 100);
        QVERIFY(adapter.created <= 300);   // pooled widgets, never one per row
        QWidget *first = strip.sectionWidget(firstRow);
        QVERIFY(first != nullptr);
        QVERIFY(first->isVisible());
        QCOMPARE(first->y(), 0);          // the window starts at the top of the strip
        QCOMPARE(first->width(), strip.width());
        QCOMPARE(first->height(), kRowHeight);
        QVERIFY(strip.materializedSections().contains(firstRow));
    };

    checkWindow(0);

    // Middle: 5,000,000 rows away from the origin.
    const qint64 middle = qint64(kRows / 2) * kRowHeight;
    rows.setViewportOffset(middle);
    QApplication::processEvents();
    checkWindow(kRows / 2);

    // End: the last row owns a widget at its place in the window.
    rows.setViewportOffset(rows.totalExtent() - kStripExtent);
    QApplication::processEvents();
    QVERIFY(strip.sectionWidget(kRows - 1) != nullptr);
    QVERIFY(strip.sectionWidget(kRows - 1)->isVisible());
    QCOMPARE(strip.sectionWidget(kRows - 1)->y(), kStripExtent - kRowHeight);
    QVERIFY(strip.materializedSectionCount() <= 100);
    QVERIFY(adapter.created <= 300);

    // ... and the geometry is still the compact one after all that scrolling.
    QCOMPARE(rows.storedSectionStateCount(), 0);
}

void TestVirtualHeaderView::frozenPaneHeaderKeepsItsColumnsAfterAReorder()
{
    // 冻结列是**集合**而不是视觉序的前缀（§31/§43）：把冻结列拖到可滚动列后面之后，
    // 可滚动 pane 自己的打包顺序就不再等于 flat committed 几何的顺序。pane 表头必须按
    // 自己那一组列来打包与物化，否则 section 会落在错的 x 上、甚至整列不再被物化
    // （用户看到的现象：固定之后表头消失，随便再拖一下才回来）。
    HeaderBody fixture;
    // 每一块物化出来的 section 都正好压在自己那一列的 committed x 上，而且左端这几个
    // 列都必须真的被物化（"表头消失"就是这里没有 section）。
    const auto checkSectionsMatchTheBody = [&fixture]() {
        // The view also owns the row-number strip (the same renderer class on the other
        // axis): this test is about the *column* renderers.
        QList<VirtualHeaderView *> renderers;
        for (VirtualHeaderView *candidate : fixture.view.findChildren<VirtualHeaderView *>()) {
            if (candidate->orientation() == Qt::Horizontal)
                renderers.append(candidate);
        }
        QVERIFY(renderers.size() >= 2); // 主表头 + 冻结 pane 的克隆
        const auto showsAnywhere = [&renderers](int column) {
            for (VirtualHeaderView *renderer : renderers) {
                if (renderer->sectionWidget(column))
                    return true;
            }
            return false;
        };
        for (int column = 0; column <= 4; ++column)
            QVERIFY(showsAnywhere(column));
        for (VirtualHeaderView *renderer : renderers) {
            for (int column : renderer->materializedSections()) {
                QVERIFY(renderer->sectionWidget(column) != nullptr);
                QCOMPARE(fixture.viewportXOf(renderer, column),
                         fixture.view.columnGeometry(column).viewportX);
            }
        }
    };

    // 顺序 A：先冻结，再把两列冻结列挪到列 2、3 后面
    // （committed 视觉序 = 2, 0, 3, 1, 4, ...）。
    fixture.view.setFrozenColumns(QVector<int>({0, 1}));
    QApplication::processEvents();
    fixture.view.moveColumn(0, 2);
    fixture.view.moveColumn(1, 3);
    QApplication::processEvents();
    // body 的 committed 几何：冻结 pane 在左（0、80），可滚动 pane 从 160 开始。
    QCOMPARE(fixture.view.columnGeometry(0).viewportX, 0);
    QCOMPARE(fixture.view.columnGeometry(1).viewportX, kSectionWidth);
    QCOMPARE(fixture.view.columnGeometry(2).viewportX, 2 * kSectionWidth);
    QCOMPARE(fixture.view.columnGeometry(3).viewportX, 3 * kSectionWidth);
    QCOMPARE(fixture.view.columnGeometry(4).viewportX, 4 * kSectionWidth);
    checkSectionsMatchTheBody();

    // 顺序 B（用户报的那条）：先换序，再冻结点到的两列 —— 结果状态一样，
    // 冻结那一次就得把表头摆对，不能等下一次拖动才"刷新"出来。
    fixture.view.setFrozenColumns(QVector<int>());
    QApplication::processEvents();
    fixture.view.moveColumn(0, 2);
    fixture.view.moveColumn(1, 3);
    QApplication::processEvents();
    fixture.view.setFrozenColumns(QVector<int>({0, 1}));
    QApplication::processEvents();
    checkSectionsMatchTheBody();
}

void TestVirtualHeaderView::standaloneHeaderStretchesItsSectionsToItsWidth()
{
    // A standalone header has no table to name the extent, so it keeps the geometry's
    // stretch target on its own width: the sections that keep their size take theirs first
    // and the factors split the rest.
    QCOMPARE(m_geometry->stretchExtent(), qint64(m_header->width()));

    m_geometry->setSectionCount(4);
    m_header->setSectionStretchFactor(1, 1.0);
    m_header->setSectionStretchFactor(2, 2.0);
    QCOMPARE(m_header->sectionStretchFactor(2), 2.0);
    QCOMPARE(m_geometry->sectionStretchFactor(1), 1.0);
    QApplication::processEvents();

    const qint64 leftover = m_header->width() - 2 * qint64(kSectionWidth);
    QVERIFY(leftover > 0);
    QCOMPARE(m_geometry->sectionSize(0), kSectionWidth);
    QCOMPARE(m_geometry->sectionSize(1), int(leftover / 3));
    QCOMPARE(m_geometry->sectionSize(2), int(leftover - leftover / 3));
    QCOMPARE(m_geometry->sectionSize(3), kSectionWidth);
    QCOMPARE(m_geometry->totalExtent(), qint64(m_header->width()));

    // The section widgets come from the geometry like everywhere else - the renderer keeps
    // no width of its own, it just reads what the stretch pass committed.
    QWidget *middle = m_header->sectionWidget(2);
    QVERIFY(middle != nullptr);
    QCOMPARE(middle->x(), m_geometry->sectionSize(0) + m_geometry->sectionSize(1));
    QCOMPARE(middle->width(), m_geometry->sectionSize(2));

    // A resize moves the target and re-distributes the same factors.
    m_header->resize(m_header->width() + 300, kHeaderHeight);
    QApplication::processEvents();
    QCOMPARE(m_geometry->stretchExtent(), qint64(m_header->width()));
    QCOMPARE(m_geometry->totalExtent(), qint64(m_header->width()));
    const qint64 wider = m_header->width() - 2 * qint64(kSectionWidth);
    QCOMPARE(m_geometry->sectionSize(1), int(wider / 3));
    QCOMPARE(m_geometry->sectionSize(2), int(wider - wider / 3));
}

QTEST_MAIN(TestVirtualHeaderView)

#include "tst_virtualheaderview.moc"
