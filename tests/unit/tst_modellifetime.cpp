#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtuallistview.h>

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

QTEST_MAIN(TestModelLifetime)

#include "tst_modellifetime.moc"
