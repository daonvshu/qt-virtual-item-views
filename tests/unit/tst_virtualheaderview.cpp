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
        QString text = m_model->headerData(logicalIndex, Qt::Horizontal).toString();
        if (m_geometry && m_geometry->sortIndicatorSection() == logicalIndex) {
            text += m_geometry->sortIndicatorOrder() == Qt::AscendingOrder ? QStringLiteral(" ^")
                                                                         : QStringLiteral(" v");
        }
        static_cast<SectionWidget *>(widget)->setText(text);
    }

    void unbindSection(QWidget *widget, int) override
    {
        ++unbindCount;
        static_cast<SectionWidget *>(widget)->setText(QString());
    }

    QString textOf(VirtualHeaderView *header, int logicalIndex) const
    {
        QWidget *widget = header->sectionWidget(logicalIndex);
        return widget ? static_cast<SectionWidget *>(widget)->text() : QString();
    }

    int bindCount = 0;
    int unbindCount = 0;

private:
    QAbstractItemModel *m_model = nullptr;
    HeaderGeometry *m_geometry = nullptr;
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
    void verticalWidgetHeaderIsRefusedLoudly();
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
    void tableForwardsTheAnimationSettings();

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

void TestVirtualHeaderView::verticalWidgetHeaderIsRefusedLoudly()
{
    // P1-14: the renderer derives every x from a horizontal HeaderGeometry and packs its
    // sections along x, so a vertical instance can only ever show an empty strip. The
    // constructor is public, so it now says so instead of producing a silent blank.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("only Qt::Horizontal")));
    VirtualHeaderView vertical(Qt::Vertical);
    vertical.resize(120, 400);

    HeaderGeometry rows(Qt::Vertical, this);
    rows.setSectionCount(6);
    rows.setDefaultSectionSize(24);
    SectionAdapter adapter;
    vertical.setAdapter(&adapter);
    vertical.setGeometryModel(&rows);
    vertical.show();
    QApplication::processEvents();

    QVERIFY(vertical.materializedSections().isEmpty());
    QCOMPARE(adapter.created, 0);

    // Binding a geometry of the other axis is refused too: the header keeps the geometry
    // it was laid out against instead of dropping it and going blank.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("other orientation")));
    m_header->setGeometryModel(&rows);
    QApplication::processEvents();
    QVERIFY(m_header->geometryModel() == m_geometry);
    QVERIFY(!m_header->materializedSections().isEmpty());
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

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("only Qt::Horizontal")));
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

QTEST_MAIN(TestVirtualHeaderView)

#include "tst_virtualheaderview.moc"
