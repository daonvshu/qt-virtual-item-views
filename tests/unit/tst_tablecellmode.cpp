#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/virtualtableview.h>
#include "vivtestfixtures.h"

#include <QtTest>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollBar>
#include <QStandardItemModel>

using namespace viv;
using namespace vivtest;

namespace {
constexpr int kRowHeight = 26;
constexpr int kColumnWidth = 90;
constexpr int kViewWidth = 460;
constexpr int kViewHeight = 300;
constexpr int kColumns = 100;
constexpr int kRows = 1000000;

/// Two cell types: a plain label cell and an editable cell (focus/IME pinning).
constexpr WidgetType kLabelCell = 1;
constexpr WidgetType kEditorCell = 2;

class LabelCell : public QWidget
{
public:
    explicit LabelCell(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("cellLabel"));
        m_label->setGeometry(2, 0, 80, 18);
    }

    QLabel *label() const { return m_label; }

private:
    QLabel *m_label = nullptr;
};

class EditorCell : public LabelCell
{
public:
    explicit EditorCell(QWidget *parent = nullptr)
        : LabelCell(parent)
    {
        m_editor = new QLineEdit(this);
        m_editor->setObjectName(QStringLiteral("cellEditor"));
        m_editor->setGeometry(2, 0, 80, 18);
    }

    QLineEdit *editor() const { return m_editor; }

private:
    QLineEdit *m_editor = nullptr;
};

class CellTestAdapter : public CellWidgetAdapter
{
public:
    WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() % 10 == 3 ? kEditorCell : kLabelCell;
    }

    QWidget *createCellWidget(WidgetType type, QWidget *parent) override
    {
        ++created;
        return type == kEditorCell ? static_cast<QWidget *>(new EditorCell(parent))
                                   : static_cast<QWidget *>(new LabelCell(parent));
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        ++bound;
        const QString text = index.data(Qt::DisplayRole).toString();
        if (auto *editor = widget->findChild<QLineEdit *>(QStringLiteral("cellEditor")))
            editor->setText(text);
        if (auto *label = widget->findChild<QLabel *>(QStringLiteral("cellLabel")))
            label->setText(text);
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        Q_UNUSED(index);
        ++unbound;
        if (auto *editor = widget->findChild<QLineEdit *>(QStringLiteral("cellEditor")))
            editor->clear();
        if (auto *label = widget->findChild<QLabel *>(QStringLiteral("cellLabel")))
            label->clear();
    }

    int created = 0;
    int bound = 0;
    int unbound = 0;
};

/// Cell that paints one thin stripe of its column colour: everything else stays
/// transparent, so anything painted underneath shows through unless the widget
/// that covers it fills its background.
class StripeCell : public QWidget
{
public:
    explicit StripeCell(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    void setColumn(int column)
    {
        m_column = column;
        update();
    }

    static QColor colorFor(int column)
    {
        // Distinct, saturated colours: easy to spot in a rendered viewport.
        static const QColor colors[] = {
            QColor(200, 0, 0),     QColor(0, 160, 0),   QColor(0, 0, 200),
            QColor(200, 140, 0),   QColor(160, 0, 160), QColor(0, 150, 150),
        };
        return colors[column % 6];
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(0, 4 + m_column * 3, width(), 2), colorFor(m_column));
    }

private:
    int m_column = 0;
};

class StripeCellAdapter : public CellWidgetAdapter
{
public:
    QWidget *createCellWidget(WidgetType, QWidget *parent) override
    {
        return new StripeCell(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<StripeCell *>(widget)->setColumn(index.column());
    }
};

/// Lightweight huge table model (no per-cell storage).
class HugeTableModel : public QAbstractTableModel
{
public:
    HugeTableModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(rows)
        , m_columns(columns)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("r%1c%2").arg(index.row()).arg(index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("Column %1").arg(section)
                                             : QString::number(section + 1);
    }

    /// QAbstractItemModel::dataChanged() is protected in Qt 5, so the test needs
    /// a helper to emit it.
    void touchCell(const QModelIndex &index)
    {
        emit dataChanged(index, index, {Qt::DisplayRole});
    }

private:
    int m_rows = 0;
    int m_columns = 0;
};
} // namespace

class TestTableCellMode : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void materializesVisibleCellsOnly();
    void cellWidgetsFollowBothAxes();
    void scrollingIsAllocationFree();
    void dataChangedRebindsAffectedCellsOnly();
    void hiddenColumnDropsItsCells();
    void columnResizeUpdatesCellGeometry();
    void switchingModeReleasesTheOtherWidgets();
    void focusedCellIsNotRecycled();
    void frozenColumnsStayMaterializedAndOnTop();
    void frozenCellsCoverTheScrolledOnes();

private:
    HugeTableModel *m_model = nullptr;
    CellTestAdapter *m_adapter = nullptr;
    VirtualTableView *m_view = nullptr;
};

void TestTableCellMode::init()
{
    m_model = new HugeTableModel(kRows, kColumns, this);
    m_adapter = new CellTestAdapter();
    m_view = new VirtualTableView();
    m_view->setCellAdapter(m_adapter);
    m_view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    m_view->setUniformItemHeight(kRowHeight);
    m_view->setDefaultColumnWidth(kColumnWidth);
    m_view->setModel(m_model);
    showView(m_view, QSize(kViewWidth, kViewHeight));
}

void TestTableCellMode::cleanup()
{
    delete m_view;
    m_view = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestTableCellMode::materializesVisibleCellsOnly()
{
    QCOMPARE(m_view->columnCount(), kColumns);
    QCOMPARE(m_view->materializationMode(), VirtualTableView::MaterializationMode::CellWidgets);
    QVERIFY(!m_view->usesItemWidgets());

    const VisibleRange rows = m_view->visibleRows();
    const QVector<int> columns = m_view->visibleColumnLogicalIndexes();
    QVERIFY(rows.isValid());
    QVERIFY(!columns.isEmpty());

    // 2D virtualization: visibleRows x visibleColumns, never rows x all columns.
    const qsizetype visibleCells = rows.count() * columns.size();
    QVERIFY(m_view->materializedCellCount() >= visibleCells);
    // The materialization window is the visible range widened by the overscan
    // (2 rows before/after, 1 column on each side).
    QVERIFY(m_view->materializedCellCount() <= (rows.count() + 4) * (columns.size() + 2));
    QVERIFY(m_view->materializedCellCount() < 200);
    QCOMPARE(m_view->stats().materializedItems, m_view->materializedCellCount());
    QVERIFY(m_view->materializedItems().isEmpty()); // no row widgets at all

    // Every materialized cell has a widget and vice versa.
    for (const QModelIndex &index : m_view->materializedCellIndexes()) {
        QWidget *widget = m_view->cellWidget(index);
        QVERIFY(widget != nullptr);
        QCOMPARE(m_view->cellIndexForWidget(widget), index);
    }
}

void TestTableCellMode::cellWidgetsFollowBothAxes()
{
    const QModelIndex firstCell = m_view->materializedCellIndexes().value(0);
    QVERIFY(firstCell.isValid());
    QWidget *cell = m_view->cellWidget(firstCell);
    QVERIFY(cell != nullptr);
    QCOMPARE(cell->y(), int(firstCell.row()) * kRowHeight - int(m_view->verticalOffset()));

    // Vertical scroll: the cells move with their rows. A cell of a row that
    // scrolled out of the materialized window is no longer materialized (and its
    // widget is recycled), so this walks the cells that exist after the scroll
    // instead of holding on to a stale index.
    m_view->verticalScrollBar()->setValue(kRowHeight * 4);
    m_view->flushPendingRelayout();
    const QList<QModelIndex> scrolledCells = m_view->materializedCellIndexes();
    QVERIFY(!scrolledCells.isEmpty());
    for (const QModelIndex &index : scrolledCells) {
        QWidget *widget = m_view->cellWidget(index);
        QVERIFY(widget != nullptr);
        QCOMPARE(widget->y(), int(index.row()) * kRowHeight - int(m_view->verticalOffset()));
    }

    // Horizontal scroll: every materialized cell follows its column, and the
    // header agrees with it.
    auto *header = qobject_cast<QHeaderView *>(m_view->horizontalHeader()->headerWidget());
    QVERIFY(header != nullptr);
    m_view->setHorizontalOffset(kColumnWidth * 2);
    const QList<QModelIndex> cells = m_view->materializedCellIndexes();
    QVERIFY(!cells.isEmpty());
    for (const QModelIndex &index : cells) {
        QWidget *widget = m_view->cellWidget(index);
        QVERIFY(widget != nullptr);
        const ColumnGeometry column = m_view->columnGeometry(index.column());
        QCOMPARE(widget->x(), column.viewportX);
        QCOMPARE(widget->width(), column.width);
        QCOMPARE(header->sectionViewportPosition(index.column()), column.viewportX);
    }
}

void TestTableCellMode::scrollingIsAllocationFree()
{
    const int createdAfterFirstPass = m_adapter->created;
    QVERIFY(createdAfterFirstPass > 0);

    for (int step = 1; step <= 40; ++step) {
        m_view->verticalScrollBar()->setValue(step * kRowHeight);
        m_view->flushPendingRelayout();
        QCOMPARE(qsizetype(m_adapter->created), m_view->materializedCellCount() + m_view->pooledWidgetCount());
    }
    QCOMPARE(m_adapter->created, createdAfterFirstPass);
    QCOMPARE(m_view->destroyedWidgetCount(), qsizetype(0));

    for (int step = 1; step <= 10; ++step) {
        m_view->scrollByHorizontalPixels(kColumnWidth);
        m_view->flushPendingRelayout();
    }
    QCOMPARE(m_adapter->created, createdAfterFirstPass);
}

void TestTableCellMode::dataChangedRebindsAffectedCellsOnly()
{
    const QModelIndex index = m_model->index(1, 1);
    QWidget *cell = m_view->cellWidget(index);
    QVERIFY(cell != nullptr);
    const int bindsBefore = m_adapter->bound;

    m_model->touchCell(index);
    m_view->flushPendingRelayout();

    QCOMPARE(m_adapter->bound, bindsBefore + 1);
    QCOMPARE(cell->findChild<QLabel *>(QStringLiteral("cellLabel"))->text(),
             m_model->data(index).toString());
}

void TestTableCellMode::hiddenColumnDropsItsCells()
{
    const int column = m_view->visibleColumnLogicalIndexes().value(1);
    const QModelIndex cell = m_model->index(0, column);
    QVERIFY(m_view->cellWidget(cell) != nullptr);

    m_view->setColumnHidden(column, true);
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->cellWidget(cell), nullptr);
    QCOMPARE(m_view->columnWidth(column), 0);

    m_view->setColumnHidden(column, false);
    m_view->flushPendingRelayout();
    QVERIFY(m_view->cellWidget(cell) != nullptr);
}

void TestTableCellMode::columnResizeUpdatesCellGeometry()
{
    const QModelIndex cell = m_view->materializedCellIndexes().value(0);
    QVERIFY(cell.isValid());
    const int createdBefore = m_adapter->created;

    m_view->setColumnWidth(cell.column(), 200);

    QCOMPARE(m_adapter->created, createdBefore);
    QCOMPARE(m_view->cellWidget(cell)->width(), 200);
    QCOMPARE(m_view->columnGeometry(cell.column()).width, 200);
}

void TestTableCellMode::switchingModeReleasesTheOtherWidgets()
{
    const qsizetype cells = m_view->materializedCellCount();
    QVERIFY(cells > 0);

    m_view->setMaterializationMode(VirtualTableView::MaterializationMode::RowWidgets);
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->materializedCellCount(), qsizetype(0));
    QCOMPARE(m_view->viewport()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).size(),
             int(m_view->materializedItemCount() + m_view->pooledWidgetCount()));

    m_view->setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->materializedCellCount(), cells);
    QCOMPARE(m_view->destroyedWidgetCount(), qsizetype(0));
}

void TestTableCellMode::focusedCellIsNotRecycled()
{
    // Column 3 uses the editor cell type; give it the focus and scroll away.
    const QModelIndex cell = m_model->index(0, 3);
    QWidget *widget = m_view->cellWidget(cell);
    QVERIFY(widget != nullptr);
    auto *editor = widget->findChild<QLineEdit *>(QStringLiteral("cellEditor"));
    QVERIFY(editor != nullptr);

    m_view->activateWindow();
    editor->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    if (QApplication::focusWidget() != editor)
        QSKIP("The platform does not deliver real focus, cell pinning cannot be verified.");

    m_view->verticalScrollBar()->setValue(kRowHeight * 200);
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->cellWidget(cell), widget);
    QCOMPARE(m_view->stats().pinnedWidgets, qsizetype(1));

    editor->clearFocus();
    QCoreApplication::processEvents();
    m_view->verticalScrollBar()->setValue(kRowHeight * 400);
    m_view->flushPendingRelayout();
    QCOMPARE(m_view->cellWidget(cell), nullptr);
    QCOMPARE(m_view->stats().pinnedWidgets, qsizetype(0));
}

void TestTableCellMode::frozenColumnsStayMaterializedAndOnTop()
{
    // Freeze the first two columns and scroll far into the scrollable pane.
    m_view->setFrozenColumns(QVector<int>({0, 1}));
    m_view->setHorizontalOffset(40 * kColumnWidth);
    m_view->flushPendingRelayout();

    QVERIFY(m_view->isColumnFrozen(0));
    QVERIFY(m_view->isColumnFrozen(1));
    QVERIFY(!m_view->isColumnFrozen(20));
    QCOMPARE(m_view->panes().at(0).viewportRect.width(), 2 * kColumnWidth);

    // Frozen cells keep their pane position even though the pane scrolled.
    QWidget *frozen = nullptr;
    QWidget *scrolled = nullptr;
    for (int row = 0; row < 3; ++row) {
        const QModelIndex first = m_model->index(row, 0);
        const QModelIndex second = m_model->index(row, 1);
        QWidget *cell0 = m_view->cellWidget(first);
        QWidget *cell1 = m_view->cellWidget(second);
        QVERIFY(cell0 != nullptr);
        QVERIFY(cell1 != nullptr);
        QVERIFY(cell0->isVisible());
        QVERIFY(cell1->isVisible());
        QCOMPARE(cell0->x(), 0);
        QCOMPARE(cell1->x(), kColumnWidth);
        if (row == 0)
            frozen = cell0;
    }
    // The materialized set is unordered, so pick a cell that is provably not
    // frozen instead of taking the last one.
    for (const QModelIndex &index : m_view->materializedCellIndexes()) {
        if (!m_view->isColumnFrozen(index.column())) {
            scrolled = m_view->cellWidget(index);
            break;
        }
    }

    // Frozen cells are raised above the scrollable ones (they cover the
    // columns that scroll under them, §31). Widget stacking is the child order.
    QVERIFY(frozen != nullptr);
    QVERIFY(scrolled != nullptr);
    const QList<QObject *> children = m_view->viewport()->children();
    QVERIFY(children.indexOf(frozen) > children.indexOf(scrolled));
}

void TestTableCellMode::frozenCellsCoverTheScrolledOnes()
{
    // A cell paints only a thin stripe, so a covering widget has to fill its own
    // background: otherwise the scrolled columns stay visible inside the frozen
    // pane while the pane scrolls (§31).
    HugeTableModel model(100, 6);
    StripeCellAdapter adapter;
    VirtualTableView view;
    view.setCellAdapter(&adapter);
    view.setMaterializationMode(VirtualTableView::MaterializationMode::CellWidgets);
    view.setUniformItemHeight(kRowHeight);
    view.setDefaultColumnWidth(kColumnWidth);
    view.setModel(&model);
    showView(&view, QSize(kViewWidth, kViewHeight));

    // Reproduce a translucent table background: the cover has to be opaque even
    // then, otherwise the scrollable columns shine through (the Windows 11 style
    // of Qt 6.8 hands out a translucent QPalette::Base).
    QPalette translucent = view.palette();
    translucent.setColor(QPalette::Base, QColor(255, 255, 255, 180));
    view.setPalette(translucent);

    view.setFrozenColumns(QVector<int>({0}));
    view.setHorizontalOffset(kColumnWidth * 7 / 2); // 3.5 columns: partially scrolled cells
    view.flushPendingRelayout();

    QImage image(view.viewport()->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    view.viewport()->render(&image);

    // Inside the frozen pane only the frozen column may be visible.
    int foreignPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < kColumnWidth; ++x) {
            const QColor pixel(image.pixel(x, y));
            for (int column = 1; column < 6; ++column) {
                if (pixel == StripeCell::colorFor(column))
                    ++foreignPixels;
            }
        }
    }
    QCOMPARE(foreignPixels, 0);
}

QTEST_MAIN(TestTableCellMode)

#include "tst_tablecellmode.moc"
