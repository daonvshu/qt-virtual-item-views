#include <virtualitemviews/widgetrecycler.h>

#include <QtTest>

#include <QWidget>

using namespace viv;

namespace {
constexpr WidgetType kTypeA = 1;
constexpr WidgetType kTypeB = 2;
}

class TestWidgetRecycler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void acquireCreatesThroughFactory();
    void recycleThenAcquireReuses();
    void poolsAreSeparatedByType();
    void recycleBeyondLimitSchedulesDestruction();
    void trimRespectsLimits();
    void clearDropsPooledWidgets();
    void countersTrackActivity();

private:
    QWidget *m_parent = nullptr;
    WidgetRecycler *m_recycler = nullptr;
    int m_factoryCalls = 0;
};

void TestWidgetRecycler::init()
{
    m_parent = new QWidget();
    m_factoryCalls = 0;
    m_recycler = new WidgetRecycler(m_parent);
    m_recycler->setFactory([this](WidgetType type, QWidget *parent) -> QWidget * {
        ++m_factoryCalls;
        QWidget *widget = new QWidget(parent);
        widget->setObjectName(QStringLiteral("type%1").arg(type));
        widget->resize(10, 10);
        return widget;
    });
}

void TestWidgetRecycler::cleanup()
{
    delete m_recycler;
    m_recycler = nullptr;
    delete m_parent;
    m_parent = nullptr;
}

void TestWidgetRecycler::acquireCreatesThroughFactory()
{
    QWidget *widget = m_recycler->acquire(kTypeA);
    QVERIFY(widget != nullptr);
    QCOMPARE(m_factoryCalls, 1);
    QCOMPARE(widget->parentWidget(), m_parent);
    QCOMPARE(m_recycler->createdCount(), qsizetype(1));
    QCOMPARE(m_recycler->activeCount(), qsizetype(1));
    QCOMPARE(m_recycler->pooledCount(kTypeA), qsizetype(0));
}

void TestWidgetRecycler::recycleThenAcquireReuses()
{
    QWidget *first = m_recycler->acquire(kTypeA);
    m_recycler->recycle(kTypeA, first);
    QCOMPARE(m_recycler->pooledCount(kTypeA), qsizetype(1));
    QCOMPARE(m_recycler->activeCount(), qsizetype(0));

    QWidget *second = m_recycler->acquire(kTypeA);
    QCOMPARE(second, first);
    QCOMPARE(m_factoryCalls, 1);
    QCOMPARE(m_recycler->reuseCount(), qsizetype(1));
    QCOMPARE(m_recycler->createdCount(), qsizetype(1));
}

void TestWidgetRecycler::poolsAreSeparatedByType()
{
    QWidget *a = m_recycler->acquire(kTypeA);
    QWidget *b = m_recycler->acquire(kTypeB);
    QVERIFY(a != b);

    m_recycler->recycle(kTypeA, a);
    m_recycler->recycle(kTypeB, b);
    QCOMPARE(m_recycler->pooledCount(kTypeA), qsizetype(1));
    QCOMPARE(m_recycler->pooledCount(kTypeB), qsizetype(1));
    QCOMPARE(m_recycler->pooledCount(), qsizetype(2));

    QCOMPARE(m_recycler->acquire(kTypeB), b);
    QCOMPARE(m_recycler->acquire(kTypeA), a);
    QCOMPARE(m_factoryCalls, 2);
}

void TestWidgetRecycler::recycleBeyondLimitSchedulesDestruction()
{
    m_recycler->setMaxPoolSize(kTypeA, 2);
    // Acquire several widgets at once, otherwise the same instance is reused and
    // the pool never grows.
    QList<QWidget *> widgets;
    for (int i = 0; i < 5; ++i)
        widgets.append(m_recycler->acquire(kTypeA));
    for (QWidget *widget : widgets)
        m_recycler->recycle(kTypeA, widget);

    QCOMPARE(m_recycler->pooledCount(kTypeA), qsizetype(2));
    QCOMPARE(m_recycler->pendingDestructionCount(), qsizetype(3));
}

void TestWidgetRecycler::trimRespectsLimits()
{
    m_recycler->setMaxPoolSize(kTypeA, 4);
    QList<QWidget *> widgets;
    for (int i = 0; i < 6; ++i)
        widgets.append(m_recycler->acquire(kTypeA));
    for (QWidget *widget : widgets)
        m_recycler->recycle(kTypeA, widget);
    QCOMPARE(m_recycler->pooledCount(kTypeA), qsizetype(4));

    m_recycler->setMaxPoolSize(kTypeA, 1);
    m_recycler->trim();
    QCOMPARE(m_recycler->pooledCount(kTypeA), qsizetype(1));
}

void TestWidgetRecycler::clearDropsPooledWidgets()
{
    m_recycler->setMaxPoolSize(kTypeA, -1);
    QList<QWidget *> widgets;
    for (int i = 0; i < 3; ++i)
        widgets.append(m_recycler->acquire(kTypeA));
    for (QWidget *widget : widgets)
        m_recycler->recycle(kTypeA, widget);
    QCOMPARE(m_recycler->pooledCount(), qsizetype(3));

    m_recycler->clear();
    QCOMPARE(m_recycler->pooledCount(), qsizetype(0));
    QCOMPARE(m_recycler->pendingDestructionCount(), qsizetype(3));
    m_recycler->trim();
    QCOMPARE(m_recycler->pooledCount(), qsizetype(0));
}

void TestWidgetRecycler::countersTrackActivity()
{
    QWidget *a = m_recycler->acquire(kTypeA);
    QWidget *b = m_recycler->acquire(kTypeA);
    QCOMPARE(m_recycler->acquireCount(), qsizetype(2));
    QCOMPARE(m_recycler->activeCount(), qsizetype(2));
    QCOMPARE(m_recycler->aliveCount(), qsizetype(2));

    m_recycler->recycle(kTypeA, a);
    QCOMPARE(m_recycler->activeCount(), qsizetype(1));

    m_recycler->acquire(kTypeA);
    QCOMPARE(m_recycler->reuseCount(), qsizetype(1));
    QCOMPARE(m_recycler->createdCount(), qsizetype(2));
    Q_UNUSED(b);
}

// QTEST_MAIN (not APPLESS): the tests create QWidget instances, which requires a
// QApplication.
QTEST_MAIN(TestWidgetRecycler)

#include "tst_widgetrecycler.moc"
