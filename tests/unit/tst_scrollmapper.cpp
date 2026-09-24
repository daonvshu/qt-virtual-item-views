#include <virtualitemviews/scrollmapper.h>

#include <QtTest>

using namespace viv;

class TestScrollMapper : public QObject
{
    Q_OBJECT

private slots:
    void exactMappingWithoutScaling();
    void scaledContentIsCompressed();
    void roundTripValueToOffsetIsExact();
    void offsetToValueStaysWithinOneStep();
    void anchorPreservesCurrentPosition();
    void precisionNearAnchor();
    void mappingsAreMonotonic();
    void clampsToContentBounds();
    void handlesEmptyAndTinyContent();
    void shrinkExtentsKeepsConsistency();
};

void TestScrollMapper::exactMappingWithoutScaling()
{
    ScrollMapper mapper;
    mapper.setExtents(1000000, 400);

    QVERIFY(!mapper.isScaled());
    QCOMPARE(mapper.maximumOffset(), qint64(999600));
    QCOMPARE(mapper.scrollRange(), 999600);
    for (qint64 offset : {qint64(0), qint64(1), qint64(50000), qint64(999600)}) {
        QCOMPARE(mapper.toScrollBarValue(offset), int(offset));
        QCOMPARE(mapper.toLogicalOffset(int(offset)), offset);
    }
}

void TestScrollMapper::scaledContentIsCompressed()
{
    ScrollMapper mapper;
    mapper.setExtents(qint64(4) << 40, 800);

    QVERIFY(mapper.isScaled());
    QCOMPARE(mapper.scrollRange(), ScrollMapper::kMaxScrollRange);
    QCOMPARE(mapper.toScrollBarValue(0), 0);
    QCOMPARE(mapper.toScrollBarValue(mapper.maximumOffset()), ScrollMapper::kMaxScrollRange);
    QCOMPARE(mapper.toLogicalOffset(0), qint64(0));
    QCOMPARE(mapper.toLogicalOffset(ScrollMapper::kMaxScrollRange), mapper.maximumOffset());
}

void TestScrollMapper::roundTripValueToOffsetIsExact()
{
    ScrollMapper mapper;
    mapper.setExtents(qint64(7) << 40, 600);

    // Use a consistent anchor pair, exactly like the view does while scrolling.
    const qint64 anchorOffset = qint64(3) << 39;
    mapper.setAnchor(anchorOffset, mapper.toScrollBarValue(anchorOffset));
    const int anchorValue = mapper.anchorValue();
    const double scale = double(mapper.maximumOffset()) / double(ScrollMapper::kMaxScrollRange);

    for (int delta : {0, 1, -1, 1000, -1000, 100000}) {
        const int value = anchorValue + delta;
        if (value < 0 || value > mapper.scrollRange())
            continue;
        const qint64 offset = mapper.toLogicalOffset(value);
        // value -> offset -> value must be exact, that is what keeps the
        // scrollbar thumb from jumping while the content changes.
        QCOMPARE(mapper.toScrollBarValue(offset), value);
        // and offset -> value -> offset stays inside one scrollbar step.
        const qint64 back = mapper.toLogicalOffset(mapper.toScrollBarValue(offset));
        QVERIFY(qAbs(double(back - offset)) <= scale + 1.0);
    }

    // The extremes stay inside the logical space; next to the window edge the
    // windowed mapping may differ from the absolute mapping by up to one step.
    QCOMPARE(mapper.toScrollBarValue(mapper.toLogicalOffset(0)), 0);
    const qint64 topOffset = mapper.toLogicalOffset(ScrollMapper::kMaxScrollRange);
    QVERIFY(topOffset <= mapper.maximumOffset());
    QVERIFY(qAbs(double(mapper.maximumOffset() - topOffset)) <= scale + 1.0);
}

void TestScrollMapper::offsetToValueStaysWithinOneStep()
{
    ScrollMapper mapper;
    const qint64 content = qint64(9) << 40;
    mapper.setExtents(content, 700);
    mapper.setAnchor(content / 2, ScrollMapper::kMaxScrollRange / 2);

    const double scale = double(mapper.maximumOffset()) / double(ScrollMapper::kMaxScrollRange);
    for (qint64 delta : {qint64(0), qint64(1), qint64(700), qint64(100000), qint64(-50000)}) {
        const qint64 offset = qint64(mapper.anchorOffset()) + delta;
        if (offset < 0 || offset > mapper.maximumOffset())
            continue;
        const int value = mapper.toScrollBarValue(offset);
        const qint64 back = mapper.toLogicalOffset(value);
        QVERIFY(qAbs(double(back - offset)) <= scale + 1.0);
    }
}

void TestScrollMapper::anchorPreservesCurrentPosition()
{
    ScrollMapper mapper;
    mapper.setExtents(qint64(5) << 41, 500);

    const qint64 offset = qint64(4) << 40;
    const int value = mapper.toScrollBarValue(offset);
    mapper.setAnchor(offset, value);

    QCOMPARE(mapper.anchorOffset(), offset);
    QCOMPARE(mapper.anchorValue(), value);
    // A relayout must not move the scrollbar: the anchored pair is stable.
    QCOMPARE(mapper.toScrollBarValue(offset), value);
    QCOMPARE(mapper.toLogicalOffset(value), offset);
}

void TestScrollMapper::precisionNearAnchor()
{
    ScrollMapper mapper;
    const qint64 content = qint64(1) << 41;
    mapper.setExtents(content, 800);

    const qint64 anchorOffset = content / 3;
    const int anchorValue = mapper.toScrollBarValue(anchorOffset);
    mapper.setAnchor(anchorOffset, anchorValue);

    const double scale = double(mapper.maximumOffset()) / double(ScrollMapper::kMaxScrollRange);
    // Offsets inside a few viewports must stay within one scrollbar step.
    for (qint64 delta = 0; delta <= 5 * 800; delta += 100) {
        const qint64 offset = anchorOffset + delta;
        const qint64 back = mapper.toLogicalOffset(mapper.toScrollBarValue(offset));
        QVERIFY(qAbs(double(back - offset)) <= scale + 1.0);
    }
}

void TestScrollMapper::mappingsAreMonotonic()
{
    ScrollMapper mapper;
    mapper.setExtents(qint64(3) << 40, 400);
    mapper.setAnchor(qint64(1) << 40, ScrollMapper::kMaxScrollRange / 4);

    int previousValue = -1;
    qint64 previousOffset = -1;
    for (int step = 0; step <= 200; ++step) {
        const qint64 offset = mapper.maximumOffset() * step / 200;
        const int value = mapper.toScrollBarValue(offset);
        QVERIFY(value >= previousValue);
        previousValue = value;

        const qint64 back = mapper.toLogicalOffset(value);
        QVERIFY(back >= previousOffset);
        previousOffset = back;
    }
}

void TestScrollMapper::clampsToContentBounds()
{
    ScrollMapper mapper;
    mapper.setExtents(10000, 300);
    QCOMPARE(mapper.toScrollBarValue(-500), 0);
    QCOMPARE(mapper.toScrollBarValue(100000), mapper.scrollRange());
    QCOMPARE(mapper.toLogicalOffset(-100), qint64(0));
    QCOMPARE(mapper.toLogicalOffset(100000), mapper.maximumOffset());

    mapper.setAnchor(-10, -10);
    QCOMPARE(mapper.anchorOffset(), qint64(0));
    QCOMPARE(mapper.anchorValue(), 0);
}

void TestScrollMapper::handlesEmptyAndTinyContent()
{
    ScrollMapper mapper;
    mapper.setExtents(0, 0);
    QCOMPARE(mapper.maximumOffset(), qint64(0));
    QCOMPARE(mapper.scrollRange(), 0);
    QCOMPARE(mapper.toScrollBarValue(0), 0);
    QCOMPARE(mapper.toLogicalOffset(0), qint64(0));

    mapper.setExtents(50, 400);
    QCOMPARE(mapper.maximumOffset(), qint64(0));
    QCOMPARE(mapper.scrollRange(), 0);

    mapper.setExtents(1000, 1000);
    QCOMPARE(mapper.maximumOffset(), qint64(0));
    QVERIFY(!mapper.isScaled());
}

void TestScrollMapper::shrinkExtentsKeepsConsistency()
{
    ScrollMapper mapper;
    mapper.setExtents(qint64(4) << 40, 600);
    const qint64 offset = mapper.maximumOffset();
    mapper.setAnchor(offset, mapper.toScrollBarValue(offset));

    // The content shrinks below the anchor: the anchor is clamped and the
    // mapping stays usable.
    mapper.setExtents(100000, 600);
    QCOMPARE(mapper.maximumOffset(), qint64(99400));
    QVERIFY(mapper.anchorOffset() <= mapper.maximumOffset());
    QVERIFY(mapper.anchorValue() <= mapper.scrollRange());

    const qint64 restored = mapper.toLogicalOffset(mapper.toScrollBarValue(50000));
    QCOMPARE(restored, qint64(50000));
}

QTEST_APPLESS_MAIN(TestScrollMapper)

#include "tst_scrollmapper.moc"
