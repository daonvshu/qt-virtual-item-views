#include <virtualitemviews/virtuallistview.h>

#include "vivtestfixtures.h"

#include <QtTest>

#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 30;
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 300;

QPoint centerOfRow(const VirtualListView &view, int row)
{
    const QRect rect = view.visualRect(view.model()->index(row, 0));
    return rect.center();
}

} // namespace

/// P1-9: click, keyboard navigation and Space all have to apply the same
/// SelectionMode / SelectionBehavior rules - a NoSelection view must never select
/// anything, and SelectRows must toggle whole rows.
class TestSelection : public QObject
{
    Q_OBJECT

    QStandardItemModel m_model;
    TestAdapter m_adapter {kRowHeight};
    VirtualListView m_view;

private slots:
    void init();
    void noSelectionNeverSelectsAnything();
    void spaceTogglesTheCurrentItem();
    void spaceTogglesWholeRowsWhenRowsAreSelected();
    void selectRowsTogglesWholeRowsOnCtrlClick();
    void multiSelectionTogglesWholeRowsOnEveryClick();
};

void TestSelection::init()
{
    m_model.clear();
    m_model.insertRows(0, 20);
    for (int row = 0; row < 20; ++row)
        m_model.setItem(row, 0, new QStandardItem(QStringLiteral("row-%1").arg(row)));

    m_view.setAdapter(&m_adapter);
    m_view.setUniformItemHeight(kRowHeight);
    m_view.setModel(&m_model);
    showView(&m_view, QSize(kViewWidth, kViewHeight));
    m_view.setFocus();
}

void TestSelection::noSelectionNeverSelectsAnything()
{
    m_view.setSelectionMode(VirtualItemView::SelectionMode::NoSelection);
    m_view.setCurrentIndex(m_model.index(3, 0));

    QTest::keyClick(&m_view, Qt::Key_Space);
    settle();

    QVERIFY(m_view.selectionModel() != nullptr);
    QVERIFY(!m_view.selectionModel()->hasSelection());
    QVERIFY(!m_view.selectionModel()->isSelected(m_model.index(3, 0)));
    QVERIFY(m_view.selectionModel()->selectedIndexes().isEmpty());

    // A click in NoSelection mode only moves the current index.
    QTest::mouseClick(m_view.viewport(), Qt::LeftButton, Qt::NoModifier, centerOfRow(m_view, 5));
    settle();
    QVERIFY(!m_view.selectionModel()->hasSelection());
    QCOMPARE(m_view.currentIndex(), m_model.index(5, 0));
}

void TestSelection::spaceTogglesTheCurrentItem()
{
    m_view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    m_view.setCurrentIndex(m_model.index(4, 0));
    // setCurrentIndex() selects the item, like QAbstractItemView.
    QVERIFY(m_view.selectionModel()->isSelected(m_model.index(4, 0)));

    QTest::keyClick(&m_view, Qt::Key_Space);      // toggles it off
    settle();
    QVERIFY(!m_view.selectionModel()->isSelected(m_model.index(4, 0)));
    QVERIFY(!m_view.selectionModel()->hasSelection());

    QTest::keyClick(&m_view, Qt::Key_Space);      // ... and back on
    settle();
    QVERIFY(m_view.selectionModel()->isSelected(m_model.index(4, 0)));
    QCOMPARE(m_view.currentIndex(), m_model.index(4, 0));
}

void TestSelection::spaceTogglesWholeRowsWhenRowsAreSelected()
{
    // The review's checklist pairs "SelectRows + Ctrl click" with "SelectRows + Space":
    // the keyboard has to go through the same selection flags as the mouse, so Space
    // toggles the *row* and not just the current cell.
    m_view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    m_view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectRows);
    m_view.setCurrentIndex(m_model.index(7, 0));
    QVERIFY(m_view.selectionModel()->isRowSelected(7, QModelIndex()));

    QTest::keyClick(&m_view, Qt::Key_Space); // toggles the whole row off
    settle();
    QVERIFY(!m_view.selectionModel()->isRowSelected(7, QModelIndex()));
    QVERIFY(!m_view.selectionModel()->hasSelection());
    QVERIFY(m_view.selectionModel()->selectedIndexes().isEmpty());

    QTest::keyClick(&m_view, Qt::Key_Space); // ... and back on, still a whole row
    settle();
    QVERIFY(m_view.selectionModel()->isRowSelected(7, QModelIndex()));
    QVERIFY(m_view.selectionModel()->selectedRows().contains(m_model.index(7, 0)));
    QCOMPARE(m_view.currentIndex(), m_model.index(7, 0));

    // MultiSelection behaves the same way (every Space toggles that row).
    m_view.setSelectionMode(VirtualItemView::SelectionMode::MultiSelection);
    m_view.setCurrentIndex(m_model.index(2, 0));
    QVERIFY(m_view.selectionModel()->isRowSelected(2, QModelIndex()));
    QTest::keyClick(&m_view, Qt::Key_Space);
    settle();
    QVERIFY(!m_view.selectionModel()->isRowSelected(2, QModelIndex()));
}

void TestSelection::selectRowsTogglesWholeRowsOnCtrlClick()
{
    m_view.setSelectionMode(VirtualItemView::SelectionMode::ExtendedSelection);
    m_view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectRows);
    m_view.setCurrentIndex(m_model.index(2, 0));
    QVERIFY(m_view.selectionModel()->isRowSelected(2, QModelIndex()));

    // Ctrl-click row 6 toggles that *row*, not just the clicked cell.
    QTest::mouseClick(m_view.viewport(), Qt::LeftButton, Qt::ControlModifier,
                      centerOfRow(m_view, 6));
    settle();
    QVERIFY(m_view.selectionModel()->isRowSelected(6, QModelIndex()));
    QVERIFY(m_view.selectionModel()->selectedRows().contains(m_model.index(6, 0)));
    // The first row is still selected (a toggle keeps the rest of the selection).
    QVERIFY(m_view.selectionModel()->isRowSelected(2, QModelIndex()));

    // Ctrl-clicking it again deselects the whole row.
    QTest::mouseClick(m_view.viewport(), Qt::LeftButton, Qt::ControlModifier,
                      centerOfRow(m_view, 6));
    settle();
    QVERIFY(!m_view.selectionModel()->isRowSelected(6, QModelIndex()));
    QVERIFY(m_view.selectionModel()->isRowSelected(2, QModelIndex()));
}

void TestSelection::multiSelectionTogglesWholeRowsOnEveryClick()
{
    m_view.setSelectionMode(VirtualItemView::SelectionMode::MultiSelection);
    m_view.setSelectionBehavior(VirtualItemView::SelectionBehavior::SelectRows);

    QTest::mouseClick(m_view.viewport(), Qt::LeftButton, Qt::NoModifier, centerOfRow(m_view, 1));
    settle();
    QVERIFY(m_view.selectionModel()->isRowSelected(1, QModelIndex()));

    QTest::mouseClick(m_view.viewport(), Qt::LeftButton, Qt::NoModifier, centerOfRow(m_view, 3));
    settle();
    QVERIFY(m_view.selectionModel()->isRowSelected(1, QModelIndex()));
    QVERIFY(m_view.selectionModel()->isRowSelected(3, QModelIndex()));

    QTest::mouseClick(m_view.viewport(), Qt::LeftButton, Qt::NoModifier, centerOfRow(m_view, 1));
    settle();
    QVERIFY(!m_view.selectionModel()->isRowSelected(1, QModelIndex()));
    QVERIFY(m_view.selectionModel()->isRowSelected(3, QModelIndex()));
}

QTEST_MAIN(TestSelection)

#include "tst_selection.moc"
