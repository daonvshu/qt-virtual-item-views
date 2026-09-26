#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/nativeheaderview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QLabel>
#include <QRegularExpression>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {

class LabelAdapter : public WidgetAdapter
{
public:
    QWidget *createWidget(WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 24); }
};

/// Section widgets of the standalone header tests.
class SectionLabelAdapter : public HeaderWidgetAdapter
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setText(QStringLiteral("section"));
        return label;
    }
    void bindSection(QWidget *widget, int logicalIndex) override
    {
        static_cast<QLabel *>(widget)->setText(QStringLiteral("s%1").arg(logicalIndex));
    }
    void unbindSection(QWidget *widget, int) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

} // namespace

/// P0-2: the view watches the model and the selection model, so a business that
/// deletes either of them first cannot leave a dangling pointer behind.
class TestModelLifetime : public QObject
{
    Q_OBJECT

private slots:
    void deletingTheModelBeforeTheViewIsSafe();
    void deletingTheExternalSelectionModelIsSafe();
    void switchingTheModelDropsASelectionModelOfTheOldModel();
    void selectionModelOfAnotherModelIsRefused();
    void deletingTheModelBehindATreeIsSafe();
    void deletingTheLabelModelBehindAWidgetHeaderIsSafe();
    void deletingTheGeometryBehindAStandaloneHeaderIsSafe();
};

void TestModelLifetime::deletingTheModelBeforeTheViewIsSafe()
{
    LabelAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    auto *model = new QStandardItemModel(500, 1);
    view.setModel(model);
    view.setUniformItemHeight(24);
    showView(&view);
    QVERIFY(view.model() != nullptr);

    delete model;
    QVERIFY(view.model() == nullptr);

    // Everything the view does afterwards has to survive the missing model: it
    // may not dereference the deleted pointer.
    view.resize(320, 240);
    view.scrollByPixels(120);
    view.scrollTo(QModelIndex());
    view.clearLifecycleLog();
    view.setVerticalOffset(0);
    settle();
    QCOMPARE(view.materializedItemCount(), qsizetype(0));
    // The view's own selection model survives (it is a child of the view), but it
    // must not point at the deleted model any more.
    QVERIFY(view.selectionModel() == nullptr || view.selectionModel()->model() == nullptr);
}

void TestModelLifetime::deletingTheExternalSelectionModelIsSafe()
{
    NumericListModel model(50);
    LabelAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setModel(&model);
    view.setUniformItemHeight(24);
    showView(&view);

    auto *selection = new QItemSelectionModel(&model);
    view.setSelectionModel(selection);
    QCOMPARE(view.selectionModel(), selection);

    delete selection;
    QVERIFY(view.selectionModel() == nullptr);

    // No selection model must not stop the view from working.
    view.setCurrentIndex(model.index(10, 0));
    view.scrollByPixels(20);
    settle();
    QVERIFY(view.currentIndex() == QModelIndex() || view.currentIndex().isValid());
}

void TestModelLifetime::switchingTheModelDropsASelectionModelOfTheOldModel()
{
    NumericListModel modelA(20);
    NumericListModel modelB(30);
    LabelAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setModel(&modelA);
    view.setUniformItemHeight(24);
    showView(&view);

    auto *selectionForA = new QItemSelectionModel(&modelA);
    view.setSelectionModel(selectionForA);
    QCOMPARE(view.selectionModel()->model(), &modelA);

    view.setModel(&modelB);
    settle();

    // Invariant: the selection model belongs to the current model, or there is
    // none - never to the outgoing one.
    QAbstractItemModel *selectionOwner = view.selectionModel() ? view.selectionModel()->model() : nullptr;
    QVERIFY(selectionOwner == nullptr || selectionOwner == &modelB);
    QVERIFY(view.selectionModel() != selectionForA);
    QVERIFY(selectionForA->model() == &modelA); // still the caller's object
    delete selectionForA;
}

void TestModelLifetime::selectionModelOfAnotherModelIsRefused()
{
    NumericListModel modelA(20);
    NumericListModel modelB(30);
    LabelAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setModel(&modelA);
    view.setUniformItemHeight(24);
    showView(&view);

    QItemSelectionModel *before = view.selectionModel();
    QVERIFY(before != nullptr);
    QItemSelectionModel selectionForB(&modelB);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("another model")));
    view.setSelectionModel(&selectionForB);
    QCOMPARE(view.selectionModel(), before);
}

void TestModelLifetime::deletingTheModelBehindATreeIsSafe()
{
    // P0-2 of the second review: the base view watches its model through a QPointer, but
    // the tree's TreeVisibilityIndex kept a raw pointer *and* a flat list of plain
    // QModelIndex - deleting the model left both dangling, and the next relayout could
    // hand a stale index to the adapter.
    LabelAdapter adapter;
    VirtualTreeView view;
    view.setAdapter(&adapter);
    auto *model = new QStandardItemModel;
    for (int root = 0; root < 20; ++root) {
        auto *item = new QStandardItem(QStringLiteral("root-%1").arg(root));
        for (int child = 0; child < 3; ++child)
            item->appendRow(new QStandardItem(QStringLiteral("child-%1").arg(child)));
        model->appendRow(item);
    }
    view.setModel(model);
    view.setUniformItemHeight(24);
    showView(&view, QSize(320, 240));
    view.expand(model->index(0, 0));
    view.flushPendingRelayout();
    QVERIFY(view.visibleRowCount() > qsizetype(20));

    delete model;

    // Everything afterwards has to behave like "no model": no stale rows, no stale
    // index handed out, no dereference of the deleted pointer.
    QVERIFY(view.model() == nullptr);
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
    QCOMPARE(view.indexAt(QPoint(4, 4)), QModelIndex());
    QVERIFY(!view.isExpanded(QModelIndex()));
    view.resize(360, 300);
    view.scrollByPixels(50);
    view.setVerticalOffset(0);
    settle();
    QCOMPARE(view.materializedItemCount(), qsizetype(0));
}

void TestModelLifetime::deletingTheLabelModelBehindAWidgetHeaderIsSafe()
{
    // P0-2: VirtualHeaderView::m_labelModel was a raw pointer, so a standalone header
    // kept dereferencing a deleted model on its next relayout.
    SectionLabelAdapter sectionAdapter;
    auto *model = new QStandardItemModel(1, 8);
    HeaderGeometry geometry(Qt::Horizontal);
    geometry.setSectionCount(8);
    geometry.setDefaultSectionSize(60);
    VirtualHeaderView header(Qt::Horizontal);
    header.resize(300, 30);
    header.setAdapter(&sectionAdapter);
    header.setGeometryModel(&geometry);
    header.setLabelModel(model);
    header.show();
    settle();
    QVERIFY(header.labelModel() != nullptr);
    QVERIFY(!header.materializedSections().isEmpty());

    delete model;
    QVERIFY(header.labelModel() == nullptr);

    // The geometry is still the source of truth for the layout, so the sections keep
    // being materialized - but the header must not touch the deleted model.
    header.resize(320, 30);
    settle();
    header.setPaneFilter(QVector<int>({0, 1}), false);
    settle();
    header.clearPaneFilter();
    settle();
    QVERIFY(header.sectionWidget(0) != nullptr);
    QCOMPARE(static_cast<QLabel *>(header.sectionWidget(0))->text(), QStringLiteral("s0"));
}

void TestModelLifetime::deletingTheGeometryBehindAStandaloneHeaderIsSafe()
{
    // P0-2: the geometry of a standalone renderer is a non-owning collaborator and was a
    // raw pointer in both renderers.
    SectionLabelAdapter sectionAdapter;
    auto *geometry = new HeaderGeometry(Qt::Horizontal);
    geometry->setSectionCount(6);
    geometry->setDefaultSectionSize(60);
    VirtualHeaderView header(Qt::Horizontal);
    header.resize(300, 30);
    header.setAdapter(&sectionAdapter);
    header.setGeometryModel(geometry);
    header.show();
    settle();
    QVERIFY(!header.materializedSections().isEmpty());

    delete geometry;
    QVERIFY(header.geometryModel() == nullptr);
    header.resize(320, 30);
    settle();
    QCOMPARE(header.materializedSectionCount(), 0);

    auto *nativeGeometry = new HeaderGeometry(Qt::Horizontal);
    nativeGeometry->setSectionCount(6);
    NativeHeaderView native(Qt::Horizontal);
    native.setGeometryModel(nativeGeometry);
    native.show();
    settle();
    delete nativeGeometry;
    QVERIFY(native.geometryModel() == nullptr);
    native.resize(200, 30);
    settle();
}

QTEST_MAIN(TestModelLifetime)

#include "tst_modellifetime.moc"
