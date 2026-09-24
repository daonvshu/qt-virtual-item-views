#include <virtualitemviews/virtuallistview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QApplication>
#include <QLineEdit>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 24;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;

QStringList numberedRows(int count)
{
    QStringList rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i)
        rows.append(QStringLiteral("row-%1").arg(i));
    return rows;
}

/// Row widget containing a real editor: used for focus pinning scenarios.
class EditorRowWidget : public QWidget
{
public:
    explicit EditorRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_editor = new QLineEdit(this);
        m_editor->setGeometry(2, 2, 180, kRowHeight - 4);
    }

    QLineEdit *editor() const { return m_editor; }

private:
    QLineEdit *m_editor = nullptr;
};

class EditorAdapter : public WidgetAdapter
{
public:
    QWidget *createWidget(WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++createdCount;
        return new EditorRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<EditorRowWidget *>(widget)->editor()->setText(index.data(Qt::DisplayRole).toString());
    }

    void unbindWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        static_cast<EditorRowWidget *>(widget)->editor()->clear();
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(kViewWidth, kRowHeight);
    }

    int createdCount = 0;
};
} // namespace

class TestListViewInteraction : public QObject
{
    Q_OBJECT

private slots:
    void mouseClickSelectsRow();
    void clickOnChildWidgetSelectsRow();
    void doubleClickEmitsSignals();
    void focusPinningKeepsEditorsAlive();
    void scrollingKeepsBoundDataFresh();
};

void TestListViewInteraction::mouseClickSelectsRow()
{
    NumericListModel model(1000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QSignalSpy clickedSpy(&view, &VirtualItemView::clicked);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 3 * kRowHeight + 4));
    QCoreApplication::processEvents();

    QCOMPARE(view.currentIndex(), model.index(3, 0));
    QVERIFY2(view.selectionModel()->isSelected(model.index(3, 0)),
             qPrintable(QStringLiteral("selected=%1 current=%2 expected=%3")
                            .arg(view.selectionModel()->selectedIndexes().size())
                            .arg(view.selectionModel()->currentIndex().row())
                            .arg(model.index(3, 0).row())));
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(clickedSpy.at(0).at(0).toModelIndex(), model.index(3, 0));

    // Ctrl+click toggles the selection without clearing the other row.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, QPoint(20, 5 * kRowHeight + 4));
    QCoreApplication::processEvents();
    QVERIFY(view.selectionModel()->isSelected(model.index(3, 0)));
    QVERIFY(view.selectionModel()->isSelected(model.index(5, 0)));
    QCOMPARE(view.currentIndex(), model.index(5, 0));
}

void TestListViewInteraction::clickOnChildWidgetSelectsRow()
{
    NumericListModel model(1000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QWidget *rowWidget = adapter.widgetForRow(2);
    QVERIFY(rowWidget != nullptr);
    auto *label = rowWidget->findChild<QLabel *>(QStringLiteral("rowLabel"));
    QVERIFY(label != nullptr);
    QVERIFY(view.materializedItems().size() > 2);

    // The click is delivered to the label inside the item widget; it must bubble
    // up to the view and update the current index.
    QTest::mouseClick(label, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QCoreApplication::processEvents();

    QCOMPARE(view.currentIndex(), model.index(2, 0));
    QVERIFY(view.selectionModel()->isSelected(model.index(2, 0)));
}

void TestListViewInteraction::doubleClickEmitsSignals()
{
    NumericListModel model(1000);
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QSignalSpy doubleSpy(&view, &VirtualItemView::doubleClicked);
    QSignalSpy activatedSpy(&view, &VirtualItemView::activated);
    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 4 * kRowHeight + 4));
    QCoreApplication::processEvents();

    QCOMPARE(doubleSpy.count(), 1);
    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(doubleSpy.at(0).at(0).toModelIndex(), model.index(4, 0));
    QCOMPARE(activatedSpy.at(0).at(0).toModelIndex(), model.index(4, 0));
}

void TestListViewInteraction::focusPinningKeepsEditorsAlive()
{
    NumericListModel model(1000);
    EditorAdapter adapter;
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QWidget *editorRow = view.widgetForIndex(model.index(1, 0));
    QVERIFY(editorRow != nullptr);
    auto *editor = static_cast<EditorRowWidget *>(editorRow)->editor();

    view.activateWindow();
    editor->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    if (QApplication::focusWidget() != editor)
        QSKIP("The platform does not deliver real focus, focus pinning cannot be verified.");

    // The focused editor must survive leaving the overscan window.
    view.scrollTo(model.index(500, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QCOMPARE(view.widgetForIndex(model.index(1, 0)), editorRow);
    QCOMPARE(view.pinnedItemCount(), qsizetype(1));
    QCOMPARE(QApplication::focusWidget(), editor);

    // Once the focus is gone, the widget is recyclable again.
    editor->clearFocus();
    QCoreApplication::processEvents();
    view.scrollTo(model.index(600, 0), VirtualItemView::PositionAtTop);
    view.flushPendingRelayout();
    QCOMPARE(view.widgetForIndex(model.index(1, 0)), nullptr);
    QCOMPARE(view.pinnedItemCount(), qsizetype(0));
}

void TestListViewInteraction::scrollingKeepsBoundDataFresh()
{
    StringListModel model(numberedRows(400));
    TestAdapter adapter(kRowHeight);
    VirtualListView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(kRowHeight);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Scroll with real wheel events and check after every step that no item
    // widget shows data of a previous row.
    for (int step = 0; step < 40; ++step) {
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(view.viewport(), &wheel);
        QCoreApplication::processEvents();

        QVERIFY(view.materializedItemCount() > 0);
        for (const MaterializedItem &item : view.materializedItems()) {
            const QString expected = model.data(QModelIndex(item.index), Qt::DisplayRole).toString();
            const QString actual = static_cast<TestRowWidget *>(item.widget)->text();
            QVERIFY2(actual == expected,
                     qPrintable(QStringLiteral("step %1: widget shows '%2' for '%3'")
                                    .arg(step).arg(actual).arg(expected)));
        }
    }
    QVERIFY(view.verticalOffset() > 0);
}

QTEST_MAIN(TestListViewInteraction)

#include "tst_listviewinteraction.moc"
