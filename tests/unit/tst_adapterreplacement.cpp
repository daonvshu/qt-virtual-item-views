#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtuallistview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {

// ---------------------------------------------------------------------------
// List adapters: two different widget classes, both in WidgetType 0.
// ---------------------------------------------------------------------------

class ListWidgetA : public QWidget
{
public:
    explicit ListWidgetA(QWidget *parent = nullptr) : QWidget(parent) {}
};

class ListWidgetB : public QWidget
{
public:
    explicit ListWidgetB(QWidget *parent = nullptr) : QWidget(parent) {}
};

class ListAdapterA : public WidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new ListWidgetA(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &) override
    {
        if (!dynamic_cast<ListWidgetA *>(widget))
            ++foreignWidgets;
        ++bound;
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 24); }

    int bound = 0;
    int foreignWidgets = 0;
};

class ListAdapterB : public ListAdapterA
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new ListWidgetB(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &) override
    {
        // A widget of the previous adapter's class must never end up here.
        if (!dynamic_cast<ListWidgetB *>(widget))
            ++foreignWidgets;
        ++bound;
    }
};

// ---------------------------------------------------------------------------
// Table row adapter that is owned by the view and counts its own lifecycle.
// ---------------------------------------------------------------------------

class OwnedRowWidget : public QWidget
{
public:
    explicit OwnedRowWidget(QWidget *parent = nullptr) : QWidget(parent) {}
};

class OwnedTableAdapter : public TableWidgetAdapter
{
public:
    ~OwnedTableAdapter() override { destroyed = true; }

    QWidget *createWidget(WidgetType, QWidget *parent) override
    {
        return new OwnedRowWidget(parent);
    }
    void bindWidget(QWidget *, const QModelIndex &) override
    {
        ++bound;
        ++liveWidgets;                // one widget currently carries data
    }
    void unbindWidget(QWidget *, const QModelIndex &) override
    {
        if (destroyed)
            ++unboundAfterDestruction;
        ++unbound;
        --liveWidgets;
        ++unboundTotal;
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 30); }

    /// Static counters: the view owns this adapter, so the test has to be able to
    /// read the result *after* the adapter was deleted.
    static bool destroyed;
    static int liveWidgets;
    static int unboundTotal;
    /// Non-zero when the view called this method on an adapter it had already deleted.
    static int unboundAfterDestruction;
    int bound = 0;
    int unbound = 0;
};

bool OwnedTableAdapter::destroyed = false;
int OwnedTableAdapter::liveWidgets = 0;
int OwnedTableAdapter::unboundTotal = 0;
int OwnedTableAdapter::unboundAfterDestruction = 0;

class OtherTableAdapter : public TableWidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QWidget(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &) override
    {
        if (dynamic_cast<OwnedRowWidget *>(widget))
            ++foreignWidgets;
        ++bound;
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 30); }

    int bound = 0;
    int foreignWidgets = 0;
};

// ---------------------------------------------------------------------------
// Cell adapter of the row <-> cell mode switch.
// ---------------------------------------------------------------------------

class ProbeCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (!dynamic_cast<QLabel *>(widget))
            ++foreignWidgets;
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
        ++bound;
    }

    int bound = 0;
    int foreignWidgets = 0;
};

// ---------------------------------------------------------------------------
// Header adapters.
// ---------------------------------------------------------------------------

class SectionWidgetA : public QWidget
{
public:
    explicit SectionWidgetA(QWidget *parent = nullptr) : QWidget(parent) {}
};

class SectionWidgetB : public QWidget
{
public:
    explicit SectionWidgetB(QWidget *parent = nullptr) : QWidget(parent) {}
};

class SectionAdapterA : public HeaderWidgetAdapter
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        return new SectionWidgetA(parent);
    }
    void bindSection(QWidget *widget, int) override
    {
        if (!dynamic_cast<SectionWidgetA *>(widget))
            ++foreignWidgets;
        ++bound;
    }

    int bound = 0;
    int foreignWidgets = 0;
};

class SectionAdapterB : public SectionAdapterA
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        return new SectionWidgetB(parent);
    }
    void bindSection(QWidget *widget, int) override
    {
        if (!dynamic_cast<SectionWidgetB *>(widget))
            ++foreignWidgets;
        ++bound;
    }
};

/// Adapter of the view-destruction test: the view has to call unbindWidget()
/// for every materialized row while the adapter is still alive.
class CountingListAdapter : public WidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &) override
    {
        ++bound;
        // A dataChanged rebinds the same widget without an unbind first, so the
        // "still bound" state is per widget, not a counter.
        m_boundWidgets.insert(widget);
    }
    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        ++unbound;
        m_boundWidgets.remove(widget);
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 24); }

    int boundWidgetCount() const { return m_boundWidgets.size(); }

    int bound = 0;
    int unbound = 0;

private:
    QSet<const QWidget *> m_boundWidgets;
};

int virtualHeaderCount(QWidget *root)
{
    int count = 0;
    const QList<QWidget *> children = root->findChildren<QWidget *>();
    for (QWidget *child : children) {
        if (dynamic_cast<VirtualHeaderView *>(child))
            ++count;
    }
    return count;
}

} // namespace

/// P0-1: the pool is a recycler concern, not an adapter concern. Widgets of the
/// old adapter must never be handed to the new one, even when both use
/// WidgetType 0 - the default of a single-pool adapter.
class TestAdapterReplacement : public QObject
{
    Q_OBJECT

private slots:
    void listAdapterReplacementNeverCrossesWidgetClasses();
    void ownedTableAdapterIsReleasedAfterUnbinding();
    void destroyingTheViewUnbindsRowsBeforeItDeletesTheOwnedTableAdapter();
    void rowAndCellModeNeverShareWidgets();
    void headerAdapterReplacementNeverCrossesWidgetClasses();
    void replacingAWidgetHeaderDestroysItsPaneRenderers();
    void destroyingTheViewUnbindsEveryMaterializedRow();
};

void TestAdapterReplacement::listAdapterReplacementNeverCrossesWidgetClasses()
{
    NumericListModel model(100);
    VirtualListView view;
    ListAdapterA adapterA;
    ListAdapterB adapterB;
    view.setAdapter(&adapterA);
    view.setModel(&model);
    view.setUniformItemHeight(24);
    showView(&view);
    QVERIFY(adapterA.bound > 0);

    view.setAdapter(&adapterB);
    settle();

    QVERIFY(adapterB.bound > 0);
    QCOMPARE(adapterB.foreignWidgets, 0);
    // Every widget A created was unbound (and returned to the pool) before B ran:
    QVERIFY(adapterA.bound > 0);
    QCOMPARE(adapterA.foreignWidgets, 0);
}

void TestAdapterReplacement::ownedTableAdapterIsReleasedAfterUnbinding()
{
    OwnedTableAdapter::destroyed = false;
    OwnedTableAdapter::liveWidgets = 0;
    OwnedTableAdapter::unboundTotal = 0;
    QStandardItemModel model(50, 3);
    VirtualTableView table;
    table.setModel(&model);
    table.setUniformItemHeight(30);
    auto *adapterA = new OwnedTableAdapter;
    table.setTableAdapter(adapterA, true);
    showView(&table);
    const int boundByA = adapterA->bound;
    QVERIFY(boundByA > 0);

    OtherTableAdapter adapterB;
    table.setTableAdapter(&adapterB, false);
    settle();

    // The old adapter saw every unbind before it was deleted (no UAF, no leak).
    QVERIFY(OwnedTableAdapter::unboundTotal > 0);
    QCOMPARE(OwnedTableAdapter::liveWidgets, 0);  // nothing is still bound to A
    QVERIFY(OwnedTableAdapter::destroyed);
    QVERIFY(adapterB.bound > 0);
    QCOMPARE(adapterB.foreignWidgets, 0);
}

void TestAdapterReplacement::destroyingTheViewUnbindsRowsBeforeItDeletesTheOwnedTableAdapter()
{
    // P0-1 of the second review: ~VirtualTableView() deleted the owned table adapter and
    // only then let the *base* destructor release the materialized rows - the final
    // unbindWidget() therefore ran on a freed object (deterministic use-after-free; the
    // first round only covered "replace the adapter before destroying the view").
    OwnedTableAdapter::destroyed = false;
    OwnedTableAdapter::liveWidgets = 0;
    OwnedTableAdapter::unboundTotal = 0;
    OwnedTableAdapter::unboundAfterDestruction = 0;

    {
        QStandardItemModel model(50, 3);
        VirtualTableView table;
        table.setModel(&model);
        table.setUniformItemHeight(30);
        table.setTableAdapter(new OwnedTableAdapter, true);
        showView(&table);
        QVERIFY(OwnedTableAdapter::liveWidgets > 0); // rows are materialized and bound
    }

    QVERIFY(OwnedTableAdapter::destroyed);
    QVERIFY(OwnedTableAdapter::unboundTotal > 0);
    QCOMPARE(OwnedTableAdapter::unboundAfterDestruction, 0); // no unbind on a dead adapter
    QCOMPARE(OwnedTableAdapter::liveWidgets, 0);             // nothing stayed bound
}

void TestAdapterReplacement::rowAndCellModeNeverShareWidgets()
{
    QStandardItemModel model(40, 3);
    VirtualTableView table;
    table.setModel(&model);
    table.setUniformItemHeight(30);
    OtherTableAdapter rowAdapter;
    table.setTableAdapter(&rowAdapter, false);
    showView(&table);
    QVERIFY(rowAdapter.bound > 0);

    ProbeCellAdapter cellAdapter;
    table.setCellAdapter(&cellAdapter);
    table.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    settle();

    QVERIFY(cellAdapter.bound > 0);
    QCOMPARE(cellAdapter.foreignWidgets, 0);

    // ... and back to row widgets: the row adapter must not receive a cell widget.
    rowAdapter.foreignWidgets = 0;
    rowAdapter.bound = 0;
    table.setMaterializationMode(VirtualTableView::MaterializationMode::RowWidgets);
    settle();
    QVERIFY(rowAdapter.bound > 0);
    QCOMPARE(rowAdapter.foreignWidgets, 0);
}

void TestAdapterReplacement::headerAdapterReplacementNeverCrossesWidgetClasses()
{
    QStandardItemModel model(4, 40);
    HeaderGeometry geometry(Qt::Horizontal);
    geometry.setSectionCount(model.columnCount());
    geometry.setDefaultSectionSize(80);

    VirtualHeaderView header(Qt::Horizontal);
    header.resize(400, 30);
    SectionAdapterA adapterA;
    SectionAdapterB adapterB;
    header.setAdapter(&adapterA);
    header.setGeometryModel(&geometry);
    header.setLabelModel(&model);
    header.show();
    settle();
    QVERIFY(adapterA.bound > 0);

    header.setAdapter(&adapterB);
    settle();

    QVERIFY(adapterB.bound > 0);
    QCOMPARE(adapterB.foreignWidgets, 0);
}

void TestAdapterReplacement::replacingAWidgetHeaderDestroysItsPaneRenderers()
{
    QStandardItemModel model(20, 8);
    VirtualTableView table;
    table.setModel(&model);
    table.setUniformItemHeight(30);
    OtherTableAdapter rowAdapter;
    table.setTableAdapter(&rowAdapter, false);

    SectionAdapterA sectionAdapter;
    auto *header = new VirtualHeaderView(Qt::Horizontal);
    header->setAdapter(&sectionAdapter);
    table.setHorizontalHeader(header);          // table takes ownership
    table.setFrozenColumns({0});                // one frozen pane -> pane renderer
    showView(&table);
    QVERIFY(virtualHeaderCount(&table) >= 1);

    // Replacing the primary header has to destroy the derived pane renderers in
    // the same call (deferred deleteLater() would leave them pointing at the
    // adapter of the header that is being replaced).
    table.setHorizontalHeader(nullptr);
    settle();
    QCOMPARE(virtualHeaderCount(&table), 0);
}

void TestAdapterReplacement::destroyingTheViewUnbindsEveryMaterializedRow()
{
    NumericListModel model(100);
    CountingListAdapter adapter;
    int unboundBeforeDestroy = 0;
    {
        VirtualListView view;
        view.setAdapter(&adapter);
        view.setModel(&model);
        view.setUniformItemHeight(24);
        showView(&view);
        QVERIFY(adapter.bound > 0);
        QVERIFY(adapter.boundWidgetCount() > 0);   // rows are materialized
        unboundBeforeDestroy = adapter.unbound;
    }
    // P1-15: the destructor releases the materialized widgets through the
    // adapter, so business code can stop its timers / async work there.
    QVERIFY(adapter.unbound > unboundBeforeDestroy);
    QVERIFY2(adapter.boundWidgetCount() == 0,
             qPrintable(QStringLiteral("still bound: %1 (bound=%2 unbound=%3)")
                            .arg(adapter.boundWidgetCount())
                            .arg(adapter.bound)
                            .arg(adapter.unbound)));
}

QTEST_MAIN(TestAdapterReplacement)

#include "tst_adapterreplacement.moc"
