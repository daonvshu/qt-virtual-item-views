#include <virtualitemviews/tablewidgetadapter.h>

#include <QStandardItemModel>
#include <QtTest>

struct RowData
{
    QString title;
    int progress = 0;
};
Q_DECLARE_METATYPE(RowData)

namespace {

class LegacyRowAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *) override { return nullptr; }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return {}; }
    void bindWidget(viv::WidgetType type, QWidget *, const QModelIndex &index) override
    {
        lastType = type;
        lastIndex = index;
        ++binds;
    }
    void visualStateChanged(viv::WidgetType type, QWidget *, const QModelIndex &index) override
    {
        lastType = type;
        lastIndex = index;
        ++states;
    }
    void unbindWidget(viv::WidgetType type, QWidget *, const QModelIndex &index) override
    {
        lastType = type;
        lastIndex = index;
        ++unbinds;
    }

    viv::WidgetType lastType = 0;
    QModelIndex lastIndex;
    int binds = 0;
    int states = 0;
    int unbinds = 0;
};

class LegacyCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *) override { return nullptr; }
    void bindCellWidget(QWidget *, const QModelIndex &index) override
    {
        lastIndex = index;
        ++binds;
    }
    void visualStateChanged(QWidget *, const QModelIndex &index) override
    {
        lastIndex = index;
        ++states;
    }
    void unbindCellWidget(QWidget *, const QModelIndex &index) override
    {
        lastIndex = index;
        ++unbinds;
    }

    QModelIndex lastIndex;
    int binds = 0;
    int states = 0;
    int unbinds = 0;
};

class ContextRowAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *) override { return nullptr; }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return {}; }
    void bindWidget(viv::WidgetType type, QWidget *, const viv::ItemBindingContext &context) override
    {
        lastType = type;
        lastContext = context;
    }
    void visualStateChanged(viv::WidgetType, QWidget *,
                            const viv::ItemBindingContext &context) override
    {
        lastContext = context;
    }
    void unbindWidget(viv::WidgetType, QWidget *, const viv::ItemBindingContext &context) override
    {
        lastContext = context;
    }

    viv::WidgetType lastType = 0;
    viv::ItemBindingContext lastContext;
};

class ContextCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *) override { return nullptr; }
    void bindCellWidget(QWidget *, const viv::ItemBindingContext &context) override
    {
        lastContext = context;
    }
    void visualStateChanged(QWidget *, const viv::ItemBindingContext &context) override
    {
        lastContext = context;
    }
    void unbindCellWidget(QWidget *, const viv::ItemBindingContext &context) override
    {
        lastContext = context;
    }

    viv::ItemBindingContext lastContext;
};

} // namespace

class TestBindingContext : public QObject
{
    Q_OBJECT

private slots:
    void legacyRowCallbacksRemainReachable();
    void legacyCellCallbacksRemainReachable();
    void contextOnlyAdaptersReceivePayloadAndMetadata();
};

void TestBindingContext::legacyRowCallbacksRemainReachable()
{
    QStandardItemModel model(1, 3);
    const viv::ItemBindingContext context{model.index(0, 0), {}, 0, 0};
    LegacyRowAdapter adapter;
    viv::WidgetAdapter &bridge = adapter;
    bridge.bindWidget(7, nullptr, context);
    bridge.visualStateChanged(7, nullptr, context);
    bridge.unbindWidget(7, nullptr, context);
    QCOMPARE(adapter.binds, 1);
    QCOMPARE(adapter.states, 1);
    QCOMPARE(adapter.unbinds, 1);
    QCOMPARE(adapter.lastType, viv::WidgetType(7));
    QCOMPARE(adapter.lastIndex, context.index);
}

void TestBindingContext::legacyCellCallbacksRemainReachable()
{
    QStandardItemModel model(1, 3);
    const viv::ItemBindingContext context{model.index(0, 2), {}, 2, 0};
    LegacyCellAdapter adapter;
    viv::CellWidgetAdapter &bridge = adapter;
    bridge.bindCellWidget(nullptr, context);
    bridge.visualStateChanged(nullptr, context);
    bridge.unbindCellWidget(nullptr, context);
    QCOMPARE(adapter.binds, 1);
    QCOMPARE(adapter.states, 1);
    QCOMPARE(adapter.unbinds, 1);
    QCOMPARE(adapter.lastIndex, context.index);
}

void TestBindingContext::contextOnlyAdaptersReceivePayloadAndMetadata()
{
    QStandardItemModel model(1, 3);
    model.setData(model.index(0, 0), QVariant::fromValue(RowData{QStringLiteral("entry"), 42}),
                  viv::kDefaultItemDataRole);
    const viv::ItemBindingContext context{
        model.index(0, 2), model.index(0, 0).data(viv::kDefaultItemDataRole), 2, 3};
    ContextRowAdapter row;
    ContextCellAdapter cell;
    viv::WidgetAdapter &rowBridge = row;
    viv::CellWidgetAdapter &cellBridge = cell;
    rowBridge.bindWidget(9, nullptr, context);
    cellBridge.bindCellWidget(nullptr, context);
    QCOMPARE(row.lastType, viv::WidgetType(9));
    QCOMPARE(row.lastContext.index, context.index);
    QCOMPARE(row.lastContext.rowData.value<RowData>().title, QStringLiteral("entry"));
    QCOMPARE(cell.lastContext.index.column(), 2);
    QCOMPARE(cell.lastContext.logicalColumn, 2);
    QCOMPARE(cell.lastContext.depth, 3);
    QCOMPARE(cell.lastContext.rowData.value<RowData>().progress, 42);

    viv::ItemBindingContext changed = context;
    changed.rowData = QVariant::fromValue(RowData{QStringLiteral("changed"), 80});
    rowBridge.visualStateChanged(9, nullptr, changed);
    cellBridge.visualStateChanged(nullptr, changed);
    QCOMPARE(row.lastContext.rowData.value<RowData>().title, QStringLiteral("changed"));
    QCOMPARE(cell.lastContext.rowData.value<RowData>().progress, 80);

    rowBridge.unbindWidget(9, nullptr, viv::ItemBindingContext{});
    cellBridge.unbindCellWidget(nullptr, viv::ItemBindingContext{});
    QVERIFY(!row.lastContext.index.isValid());
    QVERIFY(!row.lastContext.rowData.isValid());
    QVERIFY(!cell.lastContext.index.isValid());
    QVERIFY(!cell.lastContext.rowData.isValid());
}

QTEST_APPLESS_MAIN(TestBindingContext)

#include "tst_bindingcontext.moc"
