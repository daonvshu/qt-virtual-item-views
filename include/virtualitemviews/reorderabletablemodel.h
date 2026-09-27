#pragma once

#include <virtualitemviews/global.h>

#include <QAbstractTableModel>
#include <QVector>

namespace viv {

/// Base class for a model whose row order the view is allowed to rewrite - the model half
/// of "drag the row-number strip to sort the rows".
///
/// `QAbstractItemModel::moveRows()` has a default implementation that simply returns
/// `false`, so a plain model refuses every strip drag: the preview snaps back and the rows
/// stay where they were. Deriving from this class provides the missing half - a recorded
/// "view row -> source row" order plus a working `moveRows()` - and nothing else: the data
/// stays yours.
///
/// ```cpp
/// class OrderModel : public viv::ReorderableTableModel
/// {
/// public:
///     OrderModel(int rows, QObject *parent = nullptr)
///         : viv::ReorderableTableModel(rows, 4, parent) {}
///
///     QVariant data(const QModelIndex &index, int role) const override
///     {
///         const int row = sourceRow(index.row());   // stable identity, not the position
///         return m_orders.at(row).value(index.column());
///     }
/// };
///
/// table->setModel(&orderModel);
/// table->setVerticalHeaderDragEnabled(true);   // needs a model that records the order
/// ```
///
/// Take `sourceRow(viewRow)` as the row's identity in `data()` / `headerData()` (the demo's
/// row-number column shows it). Reading the *position* instead makes a moved row look
/// renumbered in place - the content would appear to stay put while only the number
/// changed. The class never moves data: it only records which data row sits at which view
/// row, and reports the change with the usual `rowsMoved()`.
///
/// `VirtualTableView::setVerticalHeaderDragEnabled()` instantiates this class itself when
/// it is asked for the gesture and the view has no model yet (see there).
class VIRTUALITEMVIEWS_EXPORT ReorderableTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ReorderableTableModel(QObject *parent = nullptr);
    ReorderableTableModel(int rows, int columns, QObject *parent = nullptr);
    ~ReorderableTableModel() override;

    // -- structure -----------------------------------------------------------
    /// The recorded order *is* the model's structure: one entry per row.
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /// Columns the model reports. A subclass with a fixed schema sets it in its
    /// constructor; the default is 0 columns.
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    void setColumnCount(int columns);

    /// The base owns no data: the default returns an invalid QVariant, and a subclass
    /// supplies the content by reading through sourceRow(). It is *implemented* rather than
    /// left pure so this class can be instantiated directly - which is what
    /// VirtualTableView::setVerticalHeaderDragEnabled() does when the view has no model.
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /// Labels are the subclass' business: the base reports none, rather than letting the
    /// QAbstractItemModel default synthesise numbers that would be a second, unrelated
    /// source of row identities next to sourceRow().
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Grows/shrinks the row set at the *end* (identity rows). Use insertRows() /
    /// removeRows() when the change happens somewhere else in the data set.
    void setRowCount(int rows);
    /// Inserts \a count identity rows before view row \a row; the new rows get fresh source
    /// ids, so a later move cannot confuse them with the ones already there.
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // -- recorded order ------------------------------------------------------
    /// Stable identity of the row shown at \a viewRow: its index in the data set. -1 when
    /// the view row is out of range.
    int sourceRow(int viewRow) const;
    /// View row that currently shows the data row \a sourceRow, or -1 when it is not part
    /// of the model (any more).
    int viewRow(int sourceRow) const;
    /// Puts the order back to "view row == source row", i.e. the order of the data set.
    void resetRowOrder();
    /// True while the recorded order is still the identity - view row i shows data row i.
    /// A cheap "did anything move?" check for tests and diagnostics (an insert/remove can
    /// make it true or false on its own; it only tracks the *order*).
    bool isIdentityOrder() const;

    /// Moves one row, or a run of rows, before \a destinationChild - Qt's
    /// "insert before" convention, which is also what the strip drag ends up calling.
    ///
    /// Reimplement it when the order has to be pushed somewhere else as well (a backend,
    /// a proxy chain): call the base implementation for the recording, or return false to
    /// refuse the move (the strip then keeps the committed order).
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override;

signals:
    /// The recorded order changed: save it, or push it to the backend. The view learns
    /// about the same change through the model's own `rowsMoved()` signal.
    void rowOrderChanged();

private:
    /// view row -> source row
    QVector<int> m_order;
    int m_columns = 0;
    /// Identity of the next row insertRows() creates; source ids are never reused, so a
    /// removed row cannot be confused with a later one.
    int m_nextSourceId = 0;
};

} // namespace viv
