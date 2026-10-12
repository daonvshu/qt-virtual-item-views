#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/virtualflowtableview.h>
#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>

#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
#include <smoothscroll/smoothscrollcontroller.h>
#endif

#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QWheelEvent>
#include <QtTest>

#include <memory>
#include <functional>

class ScrollPaintLabel : public QLabel
{
public:
    using QLabel::QLabel;
    std::function<void()> checkPaint;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (checkPaint)
            checkPaint();
        QLabel::paintEvent(event);
    }
};

class ScrollAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(150, 40); }
};

class DemoScrollingTest : public QObject
{
    Q_OBJECT
private slots:
    void allVirtualViewsScrollAndInstallOnce_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("list") << 0;
        QTest::newRow("table") << 1;
        QTest::newRow("tree") << 2;
        QTest::newRow("tree-table") << 3;
        QTest::newRow("flow") << 4;
        QTest::newRow("masonry") << 5;
    }

    void allVirtualViewsScrollAndInstallOnce()
    {
        QFETCH(int, kind);
        QStringList names;
        for (int row = 0; row < 100; ++row)
            names.append(QString::number(row));
        QStringListModel model(names);
        ScrollAdapter adapter;
        std::unique_ptr<viv::VirtualItemView> view;
        switch (kind) {
        case 0: view = std::make_unique<viv::VirtualListView>(); break;
        case 1: view = std::make_unique<viv::VirtualTableView>(); break;
        case 2: view = std::make_unique<viv::VirtualTreeView>(); break;
        case 3: view = std::make_unique<viv::VirtualTreeTableView>(); break;
        case 4: view = std::make_unique<viv::VirtualFlowTableView>(); break;
        case 5: view = std::make_unique<viv::VirtualMasonryView>(); break;
        }
        view->resize(320, 240);
        view->setWheelScrollPixels(48);
        view->setAdapter(&adapter);
        view->setModel(&model);
        QScrollBar *original = view->verticalScrollBar();
        view->show();
        QTest::qWait(30);
        QVERIFY(original->maximum() > 0);
        QCOMPARE(view->verticalScrollBar(), original);
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        auto controllers = view->findChildren<sscroll::SmoothScrollController *>();
        QCOMPARE(controllers.size(), 1);
        QCOMPARE(controllers.front()->scrollArea(), view.get());
        QVERIFY(controllers.front()->isEnabled());
        QCOMPARE(controllers.front()->settings().minimumWheelStep, 1);
        QVERIFY(controllers.front()->settings().wheelMomentumEnabled);
#endif
        view->hide();
        view->show();
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        QCOMPARE(view->findChildren<sscroll::SmoothScrollController *>().size(), 1);
#endif
        const QPoint center = view->viewport()->rect().center();
        QWheelEvent wheel(QPointF(center), QPointF(view->viewport()->mapToGlobal(center)),
                          QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        QCOMPARE(view->verticalOffset(), qint64(0));
        QTRY_VERIFY(view->verticalOffset() > 0);
#else
        QVERIFY(view->verticalOffset() > 0);
#endif
        QCOMPARE(qint64(original->value()), view->verticalOffset());
    }

    void masonryScrollbarOffsetsUpdateGeometrySynchronously()
    {
        QStringList names;
        for (int row = 0; row < 100; ++row)
            names.append(QString::number(row));
        QStringListModel model(names);
        ScrollAdapter adapter;
        viv::VirtualMasonryView view;
        view.resize(320, 240);
        view.setAdapter(&adapter);
        view.setModel(&model);
        view.show();
        QTest::qWait(30);
        QSignalSpy layouts(&view, &viv::VirtualItemView::virtualizationUpdated);
        QScrollBar *bar = view.verticalScrollBar();
        const QModelIndex first = model.index(0, 0);
        view.setItemPinned(first, true);
        for (int offset : {40, 100, 60, 80}) {
            const int previousLayouts = layouts.size();
            bar->setValue(offset);
            QCOMPARE(view.verticalOffset(), qint64(offset));
            QVERIFY(layouts.size() > previousLayouts);
            QCOMPARE(view.widgetForIndex(first)->mapTo(view.viewport(), QPoint()).y(), -offset);
        }
        view.setVerticalOffset(120);
        QCOMPARE(bar->value(), 120);
        QCOMPARE(view.widgetForIndex(first)->mapTo(view.viewport(), QPoint()).y(), -120);
        QTest::qWait(30);
        QCOMPARE(view.verticalOffset(), qint64(120));
        QCOMPARE(bar->value(), 120);
    }

    void masonryWheelRestartsKeepGeometryMonotonic()
    {
        class VariableAdapter : public ScrollAdapter
        {
        public:
            std::function<void(QWidget *, const QModelIndex &)> checkPaint;
            QWidget *createWidget(viv::WidgetType, QWidget *parent) override
            {
                return new ScrollPaintLabel(parent);
            }
            void bindWidget(viv::WidgetType type, QWidget *widget, const QModelIndex &index) override
            {
                ScrollAdapter::bindWidget(type, widget, index);
                static_cast<ScrollPaintLabel *>(widget)->checkPaint = [this, widget, index]() {
                    if (checkPaint)
                        checkPaint(widget, index);
                };
            }
            QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
            {
                return QSize(150, 80 + (index.row() * 47) % 220);
            }
        } adapter;
        QStringList names;
        for (int row = 0; row < 200; ++row)
            names.append(QString::number(row));
        QStringListModel model(names);
        viv::VirtualMasonryView view;
        bool paintGeometryChanged = false;
        int paints = 0;
        adapter.checkPaint = [&](QWidget *widget, const QModelIndex &index) {
            const QRect rect = view.visualRect(index);
            paintGeometryChanged |= widget->mapTo(view.viewport(), QPoint()) != rect.topLeft()
                || widget->size() != rect.size();
            ++paints;
        };
        view.resize(640, 480);
        view.setAdapter(&adapter);
        view.setModel(&model);
        view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        view.setRowSpacing(12);
        view.setColumnSpacing(12);
        view.setWheelScrollPixels(48);
        view.show();
        QTest::qWait(30);
        view.setVerticalOffset(600);
        QModelIndex tracked;
        for (int row = 0; row < model.rowCount(); ++row) {
            const QModelIndex index = model.index(row, 0);
            if (view.visualRect(index).contains(view.viewport()->rect().center())) {
                tracked = index;
                break;
            }
            if (!tracked.isValid() && view.visualRect(index).intersects(view.viewport()->rect()))
                tracked = index;
        }
        QVERIFY(tracked.isValid());
        view.setItemPinned(tracked, true);
        const QRect initial = view.visualRect(tracked);
        const qint64 logicalTop = initial.top() + view.verticalOffset();
        int direction = 1;
        qint64 previous = view.verticalOffset();
        bool reversed = false;
        bool geometryChanged = false;
        bool rawReversed = false;
        int previousRaw = view.verticalScrollBar()->value();
        connect(view.verticalScrollBar(), &QScrollBar::valueChanged, &view, [&](int value) {
            rawReversed |= direction * (value - previousRaw) < 0;
            previousRaw = value;
        });
        int updates = 0;
        connect(&view, &viv::VirtualItemView::virtualizationUpdated, &view, [&]() {
            const qint64 offset = view.verticalOffset();
            if (direction > 0 ? offset < previous : offset > previous)
                qWarning() << "layout reversed" << previous << "->" << offset;
            reversed |= direction > 0 ? offset < previous : offset > previous;
            previous = offset;
            const QRect rect = view.visualRect(tracked);
            QWidget *widget = view.widgetForIndex(tracked);
            geometryChanged |= rect.size() != initial.size()
                || rect.top() + offset != logicalTop || !widget
                || (widget && (widget->mapTo(view.viewport(), QPoint()) != rect.topLeft()
                               || widget->size() != rect.size()));
            ++updates;
        });
        const QPoint center = view.viewport()->rect().center();
        const auto wheel = [&](int angle) {
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
            const int before = view.verticalScrollBar()->value();
#endif
            QWheelEvent event(QPointF(center), QPointF(view.viewport()->mapToGlobal(center)),
                              QPoint(), QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(view.viewport(), &event);
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
            QCOMPARE(view.verticalScrollBar()->value(), before);
#endif
        };
        for (int input = 0; input < 6; ++input) {
            wheel(-120);
            QTest::qWait(40);
        }
        QTest::qWait(1200);
        QVERIFY(previous > 600);
        QVERIFY2(!reversed, "downward wheel restart moved the layout upward");
        QVERIFY2(!geometryChanged, "scrolling changed card dimensions or relative placement");
        QVERIFY(updates > 0);
        QCOMPARE(view.verticalOffset(), qint64(view.verticalScrollBar()->value()));
        const qint64 stopped = previous;
        for (int input = 0; input < 6; ++input) {
            wheel(-120);
            QTest::qWait(40);
        }
        QTest::qWait(1200);
        QVERIFY(previous > stopped);
        QVERIFY2(!rawReversed, "wheel restart published a reverse scrollbar value");
        QVERIFY2(!reversed, "restarting after idle moved the layout upward");
        direction = -1;
        const qint64 bottom = previous;
        for (int input = 0; input < 3; ++input) {
            wheel(120);
            QTest::qWait(40);
        }
        QTest::qWait(1200);
        QVERIFY(previous < bottom);
        QVERIFY2(!reversed, "upward wheel restart moved the layout downward");
        QVERIFY(!geometryChanged);
        QVERIFY(!rawReversed);
        QVERIFY2(!paintGeometryChanged, "paint used card geometry from an earlier scroll offset");
        QVERIFY(paints > 0);
        QCOMPARE(view.verticalOffset(), qint64(view.verticalScrollBar()->value()));
    }

    void horizontalTableWheelKeepsLogicalOffset()
    {
        QStandardItemModel model(100, 10);
        ScrollAdapter adapter;
        viv::VirtualTableView view;
        view.resize(320, 240);
        view.setDefaultColumnWidth(160);
        view.setAdapter(&adapter);
        view.setModel(&model);
        view.show();
        QTest::qWait(30);
        QVERIFY(view.horizontalScrollBar()->maximum() > 0);
        const QPoint center = view.viewport()->rect().center();
        QWheelEvent wheel(QPointF(center), QPointF(view.viewport()->mapToGlobal(center)),
                          QPoint(), QPoint(-120, 0), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &wheel);
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        QCOMPARE(view.horizontalOffset(), qint64(0));
        QTRY_VERIFY(view.horizontalOffset() > 0);
#else
        QVERIFY(view.horizontalOffset() > 0);
#endif
        QCOMPARE(qint64(view.horizontalScrollBar()->value()), view.horizontalOffset());
    }

    void standardListsAndExistingControllers()
    {
        QListWidget list;
        for (int row = 0; row < 100; ++row)
            list.addItem(QString::number(row));
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        auto *existing = new sscroll::SmoothScrollController(&list, &list);
        auto settings = existing->settings();
        settings.animationDuration = 77;
        existing->setSettings(settings);
        existing->setEnabled(false);
#endif
        list.show();
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        QCOMPARE(list.findChildren<sscroll::SmoothScrollController *>(
            QString(), Qt::FindDirectChildrenOnly).size(), 1);
        QCOMPARE(existing->settings().animationDuration, 77);
        QVERIFY(!existing->isEnabled());
        QListWidget automatic;
        automatic.show();
        QCOMPARE(automatic.findChildren<sscroll::SmoothScrollController *>().size(), 1);
#else
        QVERIFY(list.verticalScrollBar()->maximum() > 0);
#endif
    }
};

QTEST_MAIN(DemoScrollingTest)
#include "tst_demoscrolling.moc"
