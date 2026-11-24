#include "../../src/core/scrollrefreshstate_p.h"
#include "../../src/layout/flowtablelayout_p.h"
#include <QtTest>

using Machine = viv::detail::ScrollRefreshState;
using State = Machine::State;

class TestScrollRefreshState : public QObject
{
    Q_OBJECT
private slots:
    void edges()
    {
        const qint64 maximum = qint64(1) << 40;
        QCOMPARE(Machine::edges(0, 0, Qt::TopEdge, Qt::BottomEdge), Qt::TopEdge | Qt::BottomEdge);
        QCOMPARE(Machine::edges(maximum - 1, maximum, Qt::LeftEdge, Qt::RightEdge), Qt::Edges());
        QCOMPARE(Machine::edges(maximum, maximum, Qt::LeftEdge, Qt::RightEdge), Qt::Edges(Qt::RightEdge));
        Machine machine;
        QVERIFY(machine.edgeChanged(Qt::BottomEdge, true));
        QVERIFY(!machine.edgeChanged(Qt::BottomEdge, true));
        QVERIFY(machine.edgeChanged(Qt::BottomEdge, false));
        QVERIFY(!machine.edgeChanged(Qt::BottomEdge, false));
    }

    void releaseAndCancel()
    {
        Machine machine;
        machine.pull(32);
        QCOMPARE(machine.progress(), qreal(0.5));
        QVERIFY(!machine.begin(State::Refreshing, 0));
        machine.pull(100);
        QCOMPARE(machine.state, State::Armed);
        QCOMPARE(machine.distance, 100);
        QCOMPARE(machine.progress(), qreal(1));
        machine.pull(63);
        QCOMPARE(machine.state, State::Pulling);
        machine.cancelPull();
        QCOMPARE(machine.state, State::Idle);
        QCOMPARE(machine.progress(), qreal(0));
        machine.pull(64);
        const auto id = machine.begin(State::Refreshing, 0);
        QVERIFY(id);
        QVERIFY(!machine.canLoad(true, true));
        QVERIFY(!machine.begin(State::LoadingMore, 0));
        QVERIFY(machine.finish(id, State::Refreshing, true, true, 8));
        QVERIFY(machine.canLoad(true, true));
    }

    void fillViewport()
    {
        Machine machine;
        QVERIFY(machine.canLoad(true, true));
        auto id = machine.begin(State::LoadingMore, 0);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 4));
        QVERIFY(!machine.canLoad(true, false));
        QVERIFY(machine.canLoad(true, true));
        id = machine.begin(State::LoadingMore, 4);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 8));
        QVERIFY(!machine.canLoad(false, false));
        machine.edgeChanged(Qt::BottomEdge, true);
        machine.edgeChanged(Qt::BottomEdge, false);
        QVERIFY(machine.canLoad(true, false));
        id = machine.begin(State::LoadingMore, 8);
        QVERIFY(machine.finish(id, State::LoadingMore, false, true, 12));
        QVERIFY(!machine.canLoad(true, true));
    }

    void staleFailureAndEmptyPage()
    {
        Machine machine;
        const auto oldId = machine.begin(State::LoadingMore, 5);
        machine.reset();
        const auto id = machine.begin(State::LoadingMore, 0);
        QVERIFY(!machine.finish(oldId, State::LoadingMore, false, false, 0));
        QVERIFY(!machine.finish(id, State::Refreshing, true, true, 4));
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 0));
        QVERIFY(!machine.canLoad(true, true));
        machine.reset();
        const auto failed = machine.begin(State::LoadingMore, 0);
        QVERIFY(machine.finish(failed, State::LoadingMore, true, false, 4));
        QVERIFY(!machine.canLoad(true, true));
    }

    void fillPartialGridRow()
    {
        Machine machine;
        const qint64 gridExtent = 800;
        const qint64 maximumOffset = 160;
        auto id = machine.begin(State::LoadingMore, 12, gridExtent);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 14));
        QVERIFY(machine.canLoad(true, machine.canContinueAtBottom(true, maximumOffset, gridExtent)));
        QVERIFY(!machine.canLoad(true, machine.canContinueAtBottom(true, maximumOffset + 80, gridExtent + 80)));
        QVERIFY(!machine.canLoad(true, machine.canContinueAtBottom(false, maximumOffset, gridExtent)));

        id = machine.begin(State::LoadingMore, 14, gridExtent);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 14));
        QVERIFY(!machine.canLoad(true, machine.canContinueAtBottom(true, maximumOffset, gridExtent)));
    }

    void inlineFooterDuringAppend_data()
    {
        QTest::addColumn<int>("viewport");
        QTest::addColumn<int>("openShift");
        QTest::addColumn<int>("closingShift");
        QTest::newRow("footer fits before append") << 600 << 0 << 0;
        QTest::newRow("footer partly fits") << 570 << 18 << 4;
        QTest::newRow("already scrollable") << 500 << 28 << 14;
    }

    void inlineFooterDuringAppend()
    {
        QFETCH(int, viewport);
        QFETCH(int, openShift);
        QFETCH(int, closingShift);
        Machine machine;
        const auto id = machine.begin(State::LoadingMore, 14, 560);
        QCOMPARE(machine.inlineFooterShift(28, viewport, 560), openShift);
        QCOMPARE(machine.inlineFooterShift(28, viewport, 640), openShift);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 16));
        QCOMPARE(machine.inlineFooterShift(28, viewport, 640), openShift);
        QCOMPARE(machine.inlineFooterShift(14, viewport, 640), closingShift);
        QCOMPARE(machine.inlineFooterShift(0, viewport, 640), 0);
    }

    void fillWideFlowViewportWithFixedPages()
    {
        viv::FlowTableLayout layout;
        layout.setWidthRange(110, 180);
        layout.setFixedHeight(80);
        layout.setCrossExtent(2160);
        constexpr qsizetype pageSize = 64;
        constexpr qint64 viewportHeight = 1080;
        Machine machine;
        const auto canLoad = [&](qint64 offset) {
            const qint64 extent = layout.contentExtent();
            const qint64 maximum = qMax<qint64>(0, extent - viewportHeight);
            machine.edgeChanged(Qt::BottomEdge, offset >= maximum);
            return machine.canLoadAtOffset(offset, maximum, extent, true);
        };
        for (qsizetype pageNumber = 0; pageNumber < 3; ++pageNumber) {
            QVERIFY(canLoad(0));
            const auto id = machine.begin(State::LoadingMore, layout.itemCount(), layout.contentExtent());
            QVERIFY(id);
            QVERIFY(!canLoad(0));
            layout.insertItems(layout.itemCount(), pageSize, 0);
            QVERIFY(machine.finish(id, State::LoadingMore, true, true, layout.itemCount()));
        }
        QCOMPARE(layout.itemCount(), 3 * pageSize);
        QVERIFY(layout.contentExtent() > viewportHeight);
        QVERIFY(!canLoad(0));
        QVERIFY(canLoad(layout.contentExtent() - viewportHeight));
    }

    void suspensionSurvivesModelReplacement()
    {
        Machine machine;
        const auto stale = machine.begin(State::LoadingMore, 0);
        machine.suspended = true;
        machine.reset();
        QVERIFY(!machine.finish(stale, State::LoadingMore, true, true, 8));
        QVERIFY(!machine.canLoad(true, false));
        QVERIFY(!machine.canLoad(true, true));
        machine.suspended = false;
        QVERIFY(machine.canLoad(true, false));
    }

    void resumesOnceAfterFirstPage()
    {
        Machine machine;
        machine.suspended = true;
        QVERIFY(!machine.canLoad(true, false));
        machine.suspended = false;
        QVERIFY(machine.canLoad(true, machine.canContinueAtBottom(true, 0, 400)));
        const auto id = machine.begin(State::LoadingMore, 0, 400);
        QVERIFY(id);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 12));
        QVERIFY(machine.canLoad(true, machine.canContinueAtBottom(true, 0, 400)));
        const auto second = machine.begin(State::LoadingMore, 12, 400);
        QVERIFY(second);
        QVERIFY(machine.finish(second, State::LoadingMore, true, true, 24));
        QVERIFY(!machine.canLoad(true, machine.canContinueAtBottom(true, 400, 900)));
    }

    void stopsWhenNoNextPage()
    {
        Machine machine;
        QVERIFY(machine.setHasMore(false));
        QVERIFY(!machine.canLoad(true, false));
        QVERIFY(!machine.canLoad(true, true));
    }

    void failurePauseSurvivesHasMoreReporting()
    {
        Machine machine;
        const auto id = machine.begin(State::LoadingMore, 0);
        QVERIFY(machine.finish(id, State::LoadingMore, true, false, 0));
        QVERIFY(!machine.setHasMore(true));
        QVERIFY(machine.setHasMore(false));
        QVERIFY(machine.setHasMore(true));
        QVERIFY(!machine.canLoad(true, true));
        QVERIFY(machine.retryLoadMore());
        QVERIFY(machine.canLoad(true, true));
    }

    void newCursorAfterEmptyLastPage_data()
    {
        QTest::addColumn<bool>("inferEnd");
        QTest::newRow("reported-end") << false;
        QTest::newRow("probed-end") << true;
    }

    void newCursorAfterEmptyLastPage()
    {
        QFETCH(bool, inferEnd);
        Machine machine;
        const auto id = machine.begin(State::LoadingMore, 12);
        QVERIFY(machine.finish(id, State::LoadingMore, inferEnd, true, 12, inferEnd));
        QVERIFY(!machine.hasMore);
        QCOMPARE(machine.terminalFooter, inferEnd);
        QVERIFY(!machine.canLoad(true, true));
        QVERIFY(machine.setHasMore(true));
        QVERIFY(!machine.terminalFooter);
        QVERIFY(machine.canLoad(true, false));
        const auto next = machine.begin(State::LoadingMore, 4);
        QVERIFY(machine.finish(next, State::LoadingMore, true, true, 4));
        QVERIFY(!machine.setHasMore(true));
        QVERIFY(!machine.canLoad(true, true));
        QVERIFY(machine.retryLoadMore());
        QVERIFY(machine.canLoad(true, false));
    }

    void knownAndProbedEnd()
    {
        Machine machine;
        auto id = machine.begin(State::LoadingMore, 8);
        QVERIFY(machine.finish(id, State::LoadingMore, false, true, 12));
        QVERIFY(!machine.hasMore);
        QVERIFY(!machine.terminalFooter);
        machine.reset();
        id = machine.begin(State::LoadingMore, 0);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 8, true));
        QVERIFY(machine.hasMore);
        id = machine.begin(State::LoadingMore, 8);
        QVERIFY(machine.finish(id, State::LoadingMore, true, true, 8, true));
        QVERIFY(machine.terminalFooter);
        QVERIFY(!machine.canLoad(true, true));
        machine.pull(64);
        id = machine.begin(State::Refreshing, 8);
        QVERIFY(machine.finish(id, State::Refreshing, true, true, 4));
        QVERIFY(machine.hasMore);
        QVERIFY(!machine.terminalFooter);
    }

    void externalPageSupersedesRequests()
    {
        Machine machine;
        const auto append = machine.begin(State::LoadingMore, 8);
        const auto first = machine.beginExternalPage();
        const auto second = machine.beginExternalPage();
        QVERIFY(!machine.isCurrent(append));
        QVERIFY(!machine.finish(append, State::LoadingMore, true, true, 16));
        QVERIFY(!machine.isCurrent(first));
        QVERIFY(!machine.finishExternalPage(first, false, false));
        QVERIFY(machine.isCurrent(second));
        QVERIFY(!machine.canLoad(true, true));
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::ExternalPage);
        QVERIFY(!machine.finish(second, State::LoadingMore, true, true, 8));
        QVERIFY(machine.finishExternalPage(second, true, true));
        QVERIFY(!machine.isCurrent(second));
        QVERIFY(machine.canLoad(true, false));
        const auto last = machine.beginExternalPage();
        QVERIFY(machine.finishExternalPage(last, false, true));
        QVERIFY(!machine.canLoad(true, true));
    }

    void externalPageFailureAndReset()
    {
        Machine machine;
        const auto failed = machine.beginExternalPage();
        QVERIFY(machine.finishExternalPage(failed, false, false));
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::ExternalPageFailed);
        QVERIFY(!machine.retryLoadMore());
        QVERIFY(!machine.canLoad(true, true));
        machine.suspended = true;
        const auto retry = machine.beginExternalPage();
        QVERIFY(machine.finishExternalPage(retry, true, true));
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::Suspended);
        machine.suspended = false;
        QVERIFY(machine.canLoad(true, false));
        const auto stale = machine.beginExternalPage();
        machine.reset();
        QVERIFY(!machine.isCurrent(stale));
        QVERIFY(!machine.finishExternalPage(stale, false, true));
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::None);
    }

    void refreshSupersedesPaging()
    {
        Machine machine;
        const auto load = machine.begin(State::LoadingMore, 3);
        const auto refresh = machine.begin(State::Refreshing, 3, 0, true);
        QVERIFY(refresh && refresh != load);
        QVERIFY(!machine.cancelRequest(load));
        QVERIFY(!machine.finish(load, State::LoadingMore, true, true, 6));
        QVERIFY(machine.finish(refresh, State::Refreshing, false, false, 3));
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::RefreshFailed);
        QVERIFY(!machine.retryLoadMore());

        const auto external = machine.beginExternalPage();
        const auto next = machine.begin(State::Refreshing, 0, 0, true);
        QVERIFY(!machine.finishExternalPage(external, false, true));
        QVERIFY(machine.finish(next, State::Refreshing, true, true, 4));
        QVERIFY(machine.canLoadAtOffset(0, 0, 80, true));
    }

    void cancelledRequestsStayIdle()
    {
        Machine machine;
        const auto load = machine.begin(State::LoadingMore, 0);
        QVERIFY(machine.cancelRequest(load));
        QVERIFY(!machine.isCurrent(load));
        QVERIFY(!machine.cancelRequest(load));
        QCOMPARE(machine.state, State::Idle);
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::RequestCancelled);
        QVERIFY(!machine.canLoadAtOffset(0, 0, 0, true));
        QVERIFY(machine.retryLoadMore());
        QVERIFY(machine.canLoadAtOffset(0, 0, 0, true));
        const auto external = machine.beginExternalPage();
        QVERIFY(machine.cancelRequest(external));
        QVERIFY(!machine.finishExternalPage(external, true, true));
        machine.suspended = true;
        const auto refresh = machine.begin(State::Refreshing, 0, 0, true);
        QVERIFY(machine.cancelRequest(refresh));
        QCOMPARE(machine.pauseReason(), Machine::PauseReason::Suspended);
        QVERIFY(!machine.finish(refresh, State::Refreshing, true, true, 4));
    }
};

QTEST_APPLESS_MAIN(TestScrollRefreshState)
#include "tst_scrollrefreshstate.moc"
