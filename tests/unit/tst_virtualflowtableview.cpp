#include <virtualitemviews/virtualflowtableview.h>
#include "vivtestfixtures.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QLineEdit>
#include <QPainter>
#include <QtMath>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QCursor>
#include <QtTest>

namespace {

class FlowDropModel : public QStandardItemModel
{
public:
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        return accepts && data && data->hasText() && action == Qt::CopyAction && column == -1
            && parent == expectedParent && row >= 0 && row <= rowCount(parent);
    }
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        ++calls;
        lastRow = row;
        lastColumn = column;
        lastParent = parent;
        if (!canDropMimeData(data, action, row, column, parent) || !submitSucceeds)
            return false;
        auto *item = new QStandardItem(data->text());
        if (parent.isValid())
            itemFromIndex(parent)->insertRow(row, item);
        else
            insertRow(row, item);
        return true;
    }
    QPersistentModelIndex expectedParent;
    QPersistentModelIndex lastParent;
    int calls = 0;
    int lastRow = -1;
    int lastColumn = -2;
    bool accepts = true;
    bool submitSucceeds = true;
};

class FlowMovingModel : public QAbstractItemModel
{
public:
    FlowMovingModel()
    {
        for (int root = 0; root < 2; ++root) {
            Node *parent = append(&m_root, QStringLiteral("root-%1").arg(root));
            for (int row = 0; row < 4; ++row)
                append(parent, QStringLiteral("%1-%2").arg(root).arg(row));
        }
    }
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() && parent.column() != 0)
            return {};
        Node *owner = node(parent);
        return row >= 0 && row < owner->children.size() && column >= 0 && column < owner->columns
            ? createIndex(row, column, owner->children.at(row)) : QModelIndex();
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return {};
        Node *owner = node(index)->parent;
        return owner == &m_root ? QModelIndex()
            : createIndex(owner->parent->children.indexOf(owner), 0, owner);
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() && parent.column() != 0 ? 0 : node(parent)->children.size();
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override { return node(parent)->columns; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        return index.isValid() && role == Qt::DisplayRole ? QVariant(node(index)->text.at(index.column())) : QVariant();
    }
    bool moveRows(const QModelIndex &source, int first, int count, const QModelIndex &destination, int row) override
    {
        if (count != 1 || source == destination || first < 0 || first >= rowCount(source)
            || row < 0 || row > rowCount(destination) || !beginMoveRows(source, first, first, destination, row))
            return false;
        Node *moving = node(source)->children.takeAt(first);
        moving->parent = node(destination);
        node(destination)->children.insert(row, moving);
        endMoveRows();
        return true;
    }
    bool moveColumns(const QModelIndex &source, int first, int count, const QModelIndex &destination, int column) override
    {
        if (source != destination || first != 0 || count != 1 || column != 2
            || !beginMoveColumns(source, first, first, destination, column))
            return false;
        for (Node *child : node(source)->children)
            child->text.move(0, 1);
        endMoveColumns();
        return true;
    }
    bool removeColumns(int first, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (first != 0 || count != 1 || columnCount(parent) < 1)
            return false;
        beginRemoveColumns(parent, first, first);
        --node(parent)->columns;
        for (Node *child : node(parent)->children)
            child->text.removeFirst();
        endRemoveColumns();
        return true;
    }
private:
    struct Node
    {
        QStringList text;
        Node *parent = nullptr;
        QVector<Node *> children;
        int columns = 2;
        ~Node() { qDeleteAll(children); }
    };
    Node *node(const QModelIndex &index) const
    {
        return index.isValid() ? static_cast<Node *>(index.internalPointer()) : const_cast<Node *>(&m_root);
    }
    Node *append(Node *parent, const QString &text)
    {
        auto *child = new Node;
        child->text = QStringList{text, text + QStringLiteral("-second")};
        child->parent = parent;
        parent->children.append(child);
        return child;
    }
    Node m_root;
};

class EditorAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new QLineEdit(parent); }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(100, 32); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        if (bindings.value(widget) != index)
            static_cast<QLineEdit *>(widget)->setText(index.data().toString());
        bindings.insert(widget, QPersistentModelIndex(index));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override { bindings.remove(widget); }
    QHash<QWidget *, QPersistentModelIndex> bindings;
};

class ExposedFlowView : public viv::VirtualFlowTableView
{
public:
    using VirtualFlowTableView::resolveDropTarget;
    using VirtualFlowTableView::resolveDropIndicatorRect;
    using VirtualFlowTableView::paintVisualStateBackgrounds;
    using VirtualFlowTableView::paintGridLines;
};

class PaintedFlowView : public ExposedFlowView
{
public:
    VisualState visualState(const QModelIndex &index) const override
    {
        return {index == hovered, index == selected, index == hovered ? hoverProgress : 0.0,
                index == selected ? selectedProgress : 0.0};
    }
    QPersistentModelIndex hovered;
    QPersistentModelIndex selected;
    qreal hoverProgress = 1.0;
    qreal selectedProgress = 1.0;
};

class OpaqueAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *widget = new QWidget(parent);
        widget->setAutoFillBackground(true);
        widget->setBackgroundRole(QPalette::Window);
        QPalette palette = widget->palette();
        palette.setColor(QPalette::Window, Qt::green);
        widget->setPalette(palette);
        return widget;
    }
    void bindWidget(viv::WidgetType, QWidget *, const QModelIndex &) override {}
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(100, 30); }
};

class ReentrantAdapter : public vivtest::TestAdapter
{
public:
    void bindWidget(viv::WidgetType type, QWidget *widget, const QModelIndex &index) override
    {
        TestAdapter::bindWidget(type, widget, index);
        if (callback) {
            auto action = std::move(callback);
            callback = {};
            action();
        }
    }
    std::function<void()> callback;
};

class CountingModel : public QAbstractListModel
{
public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 1000000;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        ++queries;
        return index.isValid() && role == Qt::DisplayRole ? QVariant(index.row()) : QVariant();
    }
    mutable int queries = 0;
};

} // namespace

class TestVirtualFlowTableView : public QObject
{
    Q_OBJECT
private slots:
    void spacingSeparatorsHaveBothEdges()
    {
        QStandardItemModel model(10, 1);
        vivtest::TestAdapter adapter;
        ExposedFlowView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(31);
        view.setOverscan(0, 0);
        view.setVerticalGridLineColor(Qt::red);
        view.setHorizontalGridLineColor(Qt::blue);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 160));
        for (int gap : {0, 1, 9}) {
            view.setRowSpacing(gap);
            view.setColumnSpacing(gap);
            view.flushPendingRelayout();
            for (qint64 offset : {qint64(0), qint64(31 + gap)}) {
                view.setVerticalOffset(offset);
                view.flushPendingRelayout();
                const int columns = view.columnCount();
                const int row = offset ? 1 : 0;
                const QRect nextColumn = view.visualRect(model.index(row * columns + 1, 0));
                const QRect nextRow = view.visualRect(model.index((row + 1) * columns, 0));
                QVERIFY(view.widgetForIndex(model.index((row + 1) * columns, 0)));
                for (int thickness : {1, 2, 3}) {
                    view.setVerticalGridLineWidth(thickness);
                    view.setHorizontalGridLineWidth(thickness);
                    for (bool through : {false, true}) {
                        view.setVerticalSpacingLineThroughRowSpacing(through);
                        view.setHorizontalSpacingLineThroughColumnSpacing(through);
                        for (qreal scale : {1.0, 1.25, 1.5, 1.75}) {
                            QImage image(qCeil(view.viewport()->width() * scale),
                                         qCeil(view.viewport()->height() * scale), QImage::Format_ARGB32_Premultiplied);
                            image.setDevicePixelRatio(scale);
                            image.fill(Qt::white);
                            QPainter painter(&image);
                            view.paintGridLines(&painter);
                            painter.end();
                            const int left = qRound(nextColumn.left() * scale);
                            const int top = qRound(nextRow.top() * scale);
                            const int sampleY = qRound((nextColumn.top() + 10) * scale);
                            const int sampleX = qRound((nextRow.left() + 10) * scale);
                            for (int pixel = 0; pixel <= thickness; ++pixel) {
                                QCOMPARE(image.pixelColor(left + pixel, sampleY),
                                    QColor(gap > 0 && pixel < thickness ? Qt::red : Qt::white));
                                QCOMPARE(image.pixelColor(sampleX, top + pixel),
                                    QColor(gap > 0 && pixel < thickness ? Qt::blue : Qt::white));
                            }
                            if (gap == 9) {
                                const int middleX = qRound((nextColumn.left() - gap / 2) * scale);
                                const int middleY = qRound((nextRow.top() - gap / 2) * scale);
                                QCOMPARE(image.pixelColor(middleX, sampleY), QColor(Qt::white));
                                QCOMPARE(image.pixelColor(sampleX, middleY), QColor(Qt::white));
                                QCOMPARE(image.pixelColor(middleX, top), QColor(through ? Qt::blue : Qt::white));
                                QCOMPARE(image.pixelColor(left, middleY), QColor(through ? Qt::red : Qt::white));
                            }
                            const QRect last = view.visualRect(model.index(9, 0));
                            if (9 % columns + 1 < columns) {
                                const QPoint absent(view.columnViewportX(9 % columns + 1), last.center().y());
                                if (view.viewport()->rect().contains(absent))
                                    QCOMPARE(image.pixelColor(qRound(absent.x() * scale), qRound(absent.y() * scale)),
                                             QColor(Qt::white));
                            }
                        }
                    }
                }
            }
        }
    }

    void modelDestroyedDuringBinding()
    {
        ReentrantAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        vivtest::showView(&view, QSize(401, 200));
        QPointer<QStandardItemModel> doomed(new QStandardItemModel(100, 1));
        adapter.callback = [&]() { delete doomed.data(); };
        view.setModel(doomed);
        vivtest::settle();
        QVERIFY(!doomed);
        QVERIFY(!view.model());
        QVERIFY(!view.rootIndex().isValid());
        QVERIFY(!view.currentIndex().isValid());
        QCOMPARE(view.stats().logicalItems, qsizetype(0));
        QCOMPARE(view.stats().materializedItems, qsizetype(0));
        QCOMPARE(view.pinnedItemCount(), qsizetype(0));
        QCOMPARE(view.maximumVerticalOffset(), qint64(0));
        QStandardItemModel replacement(7, 1);
        view.setModel(&replacement);
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(7));
        for (int row = 0; row < 7; ++row) {
            const QModelIndex index = replacement.index(row, 0);
            QWidget *widget = view.widgetForIndex(index);
            QVERIFY(widget);
            QCOMPARE(adapter.indexOf(widget), QPersistentModelIndex(index));
        }
    }

    void timedVisualStatesSurviveReflow()
    {
        QStandardItemModel model(50, 1);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 200));
        const QModelIndex target = model.index(0, 0);
        const auto move = [&](const QPoint &point) {
            const QPoint global = view.viewport()->mapToGlobal(point);
            QCursor::setPos(global);
            QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(global),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(view.viewport(), &event);
        };
        view.setVisualStateAnimationDuration(0);
        move(QPoint(-1, -1));
        QCOMPARE(view.visualState(target).selectedProgress, qreal(0));
        QCOMPARE(view.visualState(target).hoverProgress, qreal(0));
        view.setVisualStateAnimationDuration(600);
        QCOMPARE(view.visualState(target).selectedProgress, qreal(0));
        QCOMPARE(view.visualState(target).hoverProgress, qreal(0));
        move(view.visualRect(target).center());
        view.setCurrentIndex(target);
        QVERIFY(view.visualState(target).hovered);
        QVERIFY(view.visualState(target).selected);
        QTRY_VERIFY(view.visualState(target).selectedProgress > 0
                    && view.visualState(target).selectedProgress < 1);
        QTRY_VERIFY(view.visualState(target).hoverProgress > 0
                    && view.visualState(target).hoverProgress < 1);
        const int oldColumns = view.columnCount();
        const qreal progress = view.visualState(target).selectedProgress;
        view.resize(350, 200);
        view.flushPendingRelayout();
        QVERIFY(view.columnCount() != oldColumns);
        QCOMPARE(view.currentIndex(), target);
        QVERIFY(view.visualState(target).selectedProgress >= progress);
        QTRY_COMPARE(view.visualState(target).selectedProgress, qreal(1));
        QTRY_COMPARE(view.visualState(target).hoverProgress, qreal(1));
        view.selectionModel()->clearSelection();
        move(QPoint(-1, -1));
        QVERIFY(!view.visualState(target).selected);
        QVERIFY(!view.visualState(target).hovered);
        QTRY_VERIFY(view.visualState(target).selectedProgress > 0
                    && view.visualState(target).selectedProgress < 1);
        QTRY_COMPARE(view.visualState(target).selectedProgress, qreal(0));
        QTRY_COMPARE(view.visualState(target).hoverProgress, qreal(0));
        view.setVisualStateAnimationDuration(0);
        view.setCurrentIndex(target);
        QCOMPARE(view.visualState(target).selectedProgress, qreal(1));
    }

    void proportionalScrolledGridLines()
    {
        QStandardItemModel model(50, 1);
        vivtest::TestAdapter adapter;
        ExposedFlowView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
        view.setCellAspectRatio(1.7);
        view.setVerticalGridLineColor(Qt::red);
        view.setHorizontalGridLineColor(Qt::blue);
        view.setModel(&model);
        vivtest::showView(&view, QSize(619, 211));
        for (int gap : {0, 1, 9}) {
            view.setRowSpacing(gap);
            view.setColumnSpacing(gap);
            view.setVerticalOffset(31);
            view.flushPendingRelayout();
            const QRect first = view.visualRect(model.index(0, 0));
            QVERIFY(first.top() < 0 && first.bottom() > 10);
            const int nextX = view.columnViewportX(1);
            for (bool through : {false, true}) {
                view.setVerticalSpacingLineThroughRowSpacing(through);
                view.setHorizontalSpacingLineThroughColumnSpacing(through);
                for (int thickness : {1, 2, 3}) {
                    view.setVerticalGridLineWidth(thickness);
                    view.setHorizontalGridLineWidth(thickness);
                    for (qreal scale : {1.0, 1.25, 1.5, 1.75}) {
                        QImage image(qCeil(view.viewport()->width() * scale),
                                     qCeil(view.viewport()->height() * scale), QImage::Format_ARGB32_Premultiplied);
                        image.setDevicePixelRatio(scale);
                        image.fill(Qt::white);
                        QPainter painter(&image);
                        view.paintGridLines(&painter);
                        painter.end();
                        const int right = qRound((first.x() + first.width()) * scale);
                        const int bottom = qRound((first.y() + first.height()) * scale);
                        for (int x = right - thickness - 1; x <= right; ++x)
                            QCOMPARE(image.pixelColor(x, qRound(10 * scale)),
                                QColor(x >= right - thickness && x < right ? Qt::red : Qt::white));
                        for (int y = bottom - thickness - 1; y <= bottom; ++y)
                            QCOMPARE(image.pixelColor(qRound(10 * scale), y),
                                QColor(y >= bottom - thickness && y < bottom ? Qt::blue : Qt::white));
                        if (gap == 9) {
                            const int columnGapX = qRound((nextX - gap / 2) * scale);
                            QCOMPARE(image.pixelColor(columnGapX, bottom - 1), QColor(through ? Qt::blue : Qt::white));
                            const int rowGapY = qRound((first.y() + first.height() + gap / 2) * scale);
                            QCOMPARE(image.pixelColor(right - 1, rowGapY), QColor(through ? Qt::red : Qt::white));
                        }
                    }
                }
            }
        }
    }

    void selectionGesturesAcrossReflow()
    {
        QStandardItemModel model(30, 2);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 300));
        const auto click = [&](int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            const QModelIndex index = model.index(row, 0);
            view.scrollTo(index);
            view.flushPendingRelayout();
            const QPoint point = view.visualRect(index).center();
            QVERIFY(view.viewport()->rect().contains(point));
            QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, point);
            vivtest::settle();
        };
        const auto selectedRows = [&]() {
            QSet<int> rows;
            for (const QModelIndex &index : view.selectionModel()->selectedIndexes()) {
                if (index.column() != 0 || index.parent().isValid()) return QSet<int>{-1};
                rows.insert(index.row());
            }
            return rows;
        };
        click(1);
        click(7, Qt::ShiftModifier);
        QSet<int> expected;
        for (int row = 1; row <= 7; ++row) expected.insert(row);
        QCOMPARE(selectedRows(), expected);
        QCOMPARE(view.currentIndex(), model.index(7, 0));
        const int oldColumns = view.columnCount();
        view.resize(350, 300);
        vivtest::settle();
        QVERIFY(view.columnCount() != oldColumns);
        QCOMPARE(selectedRows(), expected);
        QCOMPARE(view.currentIndex(), model.index(7, 0));
        const int target = 7 + view.columnCount();
        QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
        for (int row = 8; row <= target; ++row) expected.insert(row);
        QCOMPARE(view.currentIndex(), model.index(target, 0));
        QCOMPARE(selectedRows(), expected);
        QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(view.currentIndex(), model.index(target + 1, 0));
        QCOMPARE(selectedRows(), expected);
        click(15, Qt::ControlModifier);
        expected.insert(15);
        QCOMPARE(selectedRows(), expected);
        click(15, Qt::ControlModifier);
        expected.remove(15);
        QCOMPARE(selectedRows(), expected);
        view.setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        click(2);
        click(3, Qt::ControlModifier);
        QCOMPARE(selectedRows(), QSet<int>{3});
        view.setSelectionMode(viv::VirtualItemView::SelectionMode::MultiSelection);
        view.selectionModel()->clearSelection();
        click(2);
        click(3);
        QCOMPARE(selectedRows(), (QSet<int>{2, 3}));
        click(2);
        QCOMPARE(selectedRows(), QSet<int>{3});
        QTest::keyClick(&view, Qt::Key_Space);
        QCOMPARE(selectedRows(), (QSet<int>{2, 3}));
        view.setSelectionMode(viv::VirtualItemView::SelectionMode::NoSelection);
        view.selectionModel()->clearSelection();
        click(5);
        QTest::keyClick(&view, Qt::Key_Space);
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(view.currentIndex(), model.index(5 + view.columnCount(), 0));
        QVERIFY(selectedRows().isEmpty());
    }

    void filteringAndSubrootResetClearStaleState()
    {
        QStandardItemModel source;
        for (int row = 0; row < 24; ++row)
            source.appendRow(new QStandardItem(QStringLiteral("%1-%2")
                .arg(row % 2 ? QStringLiteral("hide") : QStringLiteral("keep")).arg(row)));
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setOverscan(0, 0);
        view.setModel(&proxy);
        vivtest::showView(&view, QSize(601, 150));
        const QPersistentModelIndex retained(proxy.index(2, 0));
        const QPersistentModelIndex removed(proxy.index(5, 0));
        QWidget *widget = view.widgetForIndex(retained);
        QVERIFY(widget);
        view.setCurrentIndex(retained);
        view.setItemPinned(retained);
        view.setItemPinned(removed);
        view.selectionModel()->select(removed, QItemSelectionModel::Select);
        proxy.setFilterFixedString(QStringLiteral("keep"));
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(12));
        QVERIFY(retained.isValid());
        QVERIFY(!removed.isValid());
        QCOMPARE(view.widgetForIndex(retained), widget);
        QCOMPARE(adapter.indexOf(widget), retained);
        QCOMPARE(view.currentIndex(), QModelIndex(retained));
        QCOMPARE(view.selectionModel()->selectedIndexes(), QModelIndexList{retained});
        QCOMPARE(view.pinnedItemCount(), qsizetype(1));
        proxy.setFilterFixedString(QStringLiteral("no-match"));
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(0));
        QCOMPARE(view.stats().materializedItems, qsizetype(0));
        QCOMPARE(view.pinnedItemCount(), qsizetype(0));
        QCOMPARE(view.verticalOffset(), qint64(0));
        QVERIFY(!view.currentIndex().isValid());
        QVERIFY(!view.selectionModel()->hasSelection());
        proxy.setFilterFixedString(QString());
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(24));
        QVERIFY(!view.selectionModel()->hasSelection());
        QCOMPARE(view.pinnedItemCount(), qsizetype(0));

        auto *root = new QStandardItem(QStringLiteral("root"));
        for (int row = 0; row < 40; ++row)
            root->appendRow(new QStandardItem(QStringLiteral("child-%1").arg(row)));
        source.appendRow(root);
        view.setModel(&source);
        view.setRootIndex(root->index());
        vivtest::settle();
        const QPersistentModelIndex oldRoot(root->index());
        const QPersistentModelIndex oldChild(source.index(2, 0, root->index()));
        view.setCurrentIndex(oldChild);
        view.setItemPinned(oldChild);
        view.setVerticalOffset(200);
        view.flushPendingRelayout();
        source.clear();
        for (int row = 0; row < 7; ++row)
            source.appendRow(new QStandardItem(QStringLiteral("replacement-%1").arg(row)));
        vivtest::settle();
        QVERIFY(!oldRoot.isValid());
        QVERIFY(!oldChild.isValid());
        QVERIFY(!view.rootIndex().isValid());
        QVERIFY(!view.currentIndex().isValid());
        QVERIFY(!view.selectionModel()->hasSelection());
        QCOMPARE(view.pinnedItemCount(), qsizetype(0));
        QCOMPARE(view.verticalOffset(), qint64(0));
        QCOMPARE(view.stats().logicalItems, qsizetype(7));
        QVERIFY(view.visualRect(oldChild).isEmpty());
        for (int row = 0; row < 7; ++row) {
            const QModelIndex index = source.index(row, 0);
            QWidget *replacement = view.widgetForIndex(index);
            QVERIFY(replacement);
            QCOMPARE(adapter.indexOf(replacement), QPersistentModelIndex(index));
        }
    }

    void modelDropSubmissionAndRejection()
    {
        for (bool nested : {false, true}) {
            FlowDropModel model;
            QStandardItem *root = nested ? new QStandardItem(QStringLiteral("root")) : model.invisibleRootItem();
            if (nested)
                model.appendRow(root);
            for (int row = 0; row < 10; ++row)
                root->appendRow(new QStandardItem(QString::number(row)));
            model.expectedParent = nested ? root->index() : QModelIndex();
            vivtest::TestAdapter adapter;
            viv::VirtualFlowTableView view;
            view.setFrameShape(QFrame::NoFrame);
            view.setAdapter(&adapter);
            view.setFixedCellHeight(30);
            view.setRowSpacing(8);
            view.setColumnSpacing(10);
            view.setModel(&model);
            view.setRootIndex(model.expectedParent);
            view.setDragEnabled(true);
            vivtest::showView(&view, QSize(601, 240));
            QMimeData data;
            data.setText(QStringLiteral("dropped"));
            const auto hover = [&](const QPoint &pos) {
                QDragEnterEvent enter(pos, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view.viewport(), &enter);
                QDragMoveEvent move(pos, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view.viewport(), &move);
                return move.isAccepted();
            };
            const auto drop = [&](const QPoint &pos) {
                QDropEvent event(QPointF(pos), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view.viewport(), &event);
                return event.isAccepted();
            };
            const QRect first = view.visualRect(model.index(0, 0, model.expectedParent));
            const QPoint gap(first.right() + 3, first.center().y());
            QSignalSpy submitted(&view, &viv::VirtualItemView::itemDropped);
            QVERIFY(hover(gap));
            QWidget *indicator = view.viewport()->findChild<QWidget *>(QStringLiteral("vivDropIndicator"));
            QVERIFY(indicator && indicator->isVisible());
            QVERIFY(drop(gap));
            vivtest::settle();
            QCOMPARE(model.lastRow, 1);
            QCOMPARE(model.lastColumn, -1);
            QCOMPARE(model.lastParent, model.expectedParent);
            QCOMPARE(model.index(1, 0, model.expectedParent).data().toString(), QStringLiteral("dropped"));
            QCOMPARE(submitted.count(), 1);
            QVERIFY(!indicator->isVisible());
            model.submitSucceeds = false;
            QVERIFY(hover(gap));
            QVERIFY(!drop(gap));
            QVERIFY(!indicator->isVisible());
            QCOMPARE(submitted.count(), 1);
            const int calls = model.calls;
            model.accepts = false;
            QVERIFY(!hover(gap));
            QVERIFY(!drop(gap));
            QCOMPARE(model.calls, calls);
            QCOMPARE(view.stats().logicalItems, qsizetype(11));
            model.accepts = true;
            model.submitSucceeds = true;
            root->removeRows(0, root->rowCount());
            vivtest::settle();
            QVERIFY(hover(QPoint(2, 2)));
            QVERIFY(drop(QPoint(2, 2)));
            vivtest::settle();
            QCOMPARE(model.lastRow, 0);
            QCOMPARE(view.stats().logicalItems, qsizetype(1));
        }
    }

    void crossParentMovesAndRootColumns()
    {
        FlowMovingModel model;
        EditorAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setModel(&model);
        const QPersistentModelIndex root = model.index(0, 0);
        const QPersistentModelIndex other = model.index(1, 0);
        view.setRootIndex(root);
        vivtest::showView(&view);
        const QPersistentModelIndex retained = model.index(0, 0, root);
        auto *editor = qobject_cast<QLineEdit *>(view.widgetForIndex(retained));
        QVERIFY(editor);
        editor->setFocus();
        editor->setText(QStringLiteral("editing"));
        const QPersistentModelIndex moving = model.index(1, 0, root);
        view.setItemPinned(moving);
        QVERIFY(model.moveRows(root, 1, 1, other, 0));
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(3));
        QCOMPARE(view.widgetForIndex(retained), static_cast<QWidget *>(editor));
        QVERIFY(editor->hasFocus());
        QCOMPARE(editor->text(), QStringLiteral("editing"));
        QVERIFY(!view.isItemPinned(moving));
        QVERIFY(model.moveRows(other, 0, 1, root, 1));
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(4));
        QCOMPARE(adapter.bindings.value(view.widgetForIndex(moving)), moving);
        editor->clearFocus();
        view.setFocus();
        view.setItemPinned(retained);
        QVERIFY(model.moveColumns(root, 0, 1, root, 2));
        vivtest::settle();
        const QModelIndex canonical = model.index(0, 0, root);
        QVERIFY(view.isItemPinned(canonical));
        QCOMPARE(canonical.data().toString(), QStringLiteral("0-0-second"));
        QCOMPARE(adapter.bindings.value(view.widgetForIndex(canonical)), QPersistentModelIndex(canonical));
        QVERIFY(model.removeColumns(0, 1, root));
        vivtest::settle();
        const QModelIndex replacement = model.index(0, 0, root);
        QCOMPARE(replacement.data().toString(), QStringLiteral("0-0"));
        QVERIFY(view.isItemPinned(replacement));
        QCOMPARE(adapter.bindings.value(view.widgetForIndex(replacement)), QPersistentModelIndex(replacement));
    }

    void spacingBackgroundsAtFractionalScales()
    {
        QStandardItemModel model(10, 1);
        vivtest::TestAdapter adapter;
        PaintedFlowView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(31);
        view.setHoverBackgroundColor(Qt::red);
        view.setSelectedBackgroundColor(Qt::red);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 240));
        for (int gap : {0, 1, 5, 9}) {
            view.setRowSpacing(gap);
            view.setColumnSpacing(gap);
            vivtest::settle();
            for (bool selected : {false, true}) {
                view.hovered = selected ? QModelIndex() : model.index(0, 0);
                view.selected = selected ? model.index(0, 0) : QModelIndex();
                for (bool throughRows : {false, true}) {
                    for (bool throughColumns : {false, true}) {
                        view.setHoverBackgroundThroughRowSpacing(throughRows);
                        view.setSelectedBackgroundThroughRowSpacing(throughRows);
                        view.setHoverBackgroundThroughColumnSpacing(throughColumns);
                        view.setSelectedBackgroundThroughColumnSpacing(throughColumns);
                        const QRect cell = view.visualRect(model.index(0, 0));
                        const QRect expected = cell.adjusted(0, 0, throughColumns ? gap : 0,
                                                            throughRows ? gap : 0);
                        for (int radius : {0, 9}) {
                            view.setStateBackgroundCornerRadius(radius);
                            for (qreal scale : {1.0, 1.25, 1.5, 1.75}) {
                                QImage image(qCeil(601 * scale), qCeil(240 * scale), QImage::Format_ARGB32_Premultiplied);
                                image.setDevicePixelRatio(scale);
                                image.fill(Qt::white);
                                QPainter painter(&image);
                                view.paintVisualStateBackgrounds(&painter, view.viewport()->rect());
                                painter.end();
                                const QRect pixels(qRound(expected.x() * scale), qRound(expected.y() * scale),
                                    qRound((expected.x() + expected.width()) * scale) - qRound(expected.x() * scale),
                                    qRound((expected.y() + expected.height()) * scale) - qRound(expected.y() * scale));
                                int colored = 0;
                                for (int y = 0; y < image.height(); ++y) {
                                    for (int x = 0; x < image.width(); ++x) {
                                        if (image.pixelColor(x, y) != QColor(Qt::white)) {
                                            QVERIFY2(pixels.contains(x, y), "Background leaked beyond its exclusive device edges");
                                            ++colored;
                                        }
                                    }
                                }
                                QVERIFY(colored > 0);
                                QCOMPARE(image.pixelColor(pixels.center()), QColor(Qt::red));
                                if (radius == 0)
                                    QCOMPARE(colored, pixels.width() * pixels.height());
                            }
                        }
                    }
                }
            }
        }
        view.hovered = QModelIndex();
        view.selected = model.index(9, 0);
        view.setStateBackgroundCornerRadius(0);
        view.setSelectedBackgroundThroughColumnSpacing(true);
        QImage image(601, 240, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        view.paintVisualStateBackgrounds(&painter, view.viewport()->rect());
        painter.end();
        const QRect last = view.visualRect(view.selected);
        QCOMPARE(image.pixelColor(last.center()), QColor(Qt::red));
        QCOMPARE(image.pixelColor(QPoint(view.columnViewportX(2) + 2, last.center().y())), QColor(Qt::white));
    }

    void proportionalScrolledAnimatedBackgrounds()
    {
        QStandardItemModel model(50, 1);
        vivtest::TestAdapter adapter;
        PaintedFlowView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
        view.setHoverBackgroundColor(Qt::red);
        view.setSelectedBackgroundColor(Qt::blue);
        view.hoverProgress = 0.35;
        view.selectedProgress = 0.6;
        view.setModel(&model);
        vivtest::showView(&view, QSize(619, 211));
        bool unequalHeightsSeen = false;
        const auto deviceRect = [](const QRect &rect, qreal scale) {
            const int left = qRound(rect.x() * scale);
            const int top = qRound(rect.y() * scale);
            return QRect(left, top, qRound((rect.x() + rect.width()) * scale) - left,
                         qRound((rect.y() + rect.height()) * scale) - top);
        };
        for (qreal ratio : {1.0, 1.7}) {
            view.setCellAspectRatio(ratio);
            for (int gap : {1, 9}) {
                view.setRowSpacing(gap);
                view.setColumnSpacing(gap);
                view.flushPendingRelayout();
                const int columns = view.columnCount();
                QVERIFY(columns >= 3);
                const int rowHeight = view.visualRect(model.index(0, 0)).height();
                unequalHeightsSeen = unequalHeightsSeen
                    || rowHeight != view.visualRect(model.index(columns - 1, 0)).height();
                for (qint64 offset : {qint64(1), qint64(31)}) {
                    view.setVerticalOffset(offset);
                    view.flushPendingRelayout();
                    QCOMPARE(view.verticalOffset(), offset);
                    for (int row : {0, 1}) {
                        const QModelIndex target = model.index(row * columns + columns - 2, 0);
                        QVERIFY(view.widgetForIndex(target));
                        view.hovered = target;
                        view.selected = target;
                        const QRect cell = view.visualRect(target);
                        for (bool throughRows : {false, true}) {
                            for (bool throughColumns : {false, true}) {
                                view.setHoverBackgroundThroughRowSpacing(throughRows);
                                view.setSelectedBackgroundThroughRowSpacing(throughRows);
                                view.setHoverBackgroundThroughColumnSpacing(throughColumns);
                                view.setSelectedBackgroundThroughColumnSpacing(throughColumns);
                                const QRect shape = cell.adjusted(0, 0, throughColumns ? gap : 0,
                                    throughRows ? rowHeight - cell.height() + gap : 0);
                                for (int radius : {0, 9}) {
                                    view.setStateBackgroundCornerRadius(radius);
                                    for (qreal scale : {1.0, 1.25, 1.5, 1.75}) {
                                        const QSize size(qCeil(view.viewport()->width() * scale),
                                                         qCeil(view.viewport()->height() * scale));
                                        QImage full(size, QImage::Format_ARGB32_Premultiplied);
                                        full.setDevicePixelRatio(scale);
                                        full.fill(Qt::white);
                                        QPainter painter(&full);
                                        view.paintVisualStateBackgrounds(&painter, view.viewport()->rect());
                                        painter.end();
                                        const QRect expected = deviceRect(shape.intersected(view.viewport()->rect()), scale);
                                        int colored = 0;
                                        for (int y = 0; y < full.height(); ++y) {
                                            const auto *pixels = reinterpret_cast<const QRgb *>(full.constScanLine(y));
                                            for (int x = 0; x < full.width(); ++x) {
                                                if (pixels[x] != qRgb(255, 255, 255)) {
                                                    QVERIFY2(expected.contains(x, y),
                                                        "Animated proportional background leaked into another item or gap");
                                                    ++colored;
                                                }
                                            }
                                        }
                                        QVERIFY(colored > 0);
                                        if (radius == 0) QCOMPARE(colored, expected.width() * expected.height());
                                        const QColor mixed = full.pixelColor(expected.center());
                                        QVERIFY(mixed.red() > mixed.green());
                                        QVERIFY(mixed.blue() > mixed.red());

                                        const QRect dirty(0, 17, view.viewport()->width(), 101);
                                        QImage partial(size, QImage::Format_ARGB32_Premultiplied);
                                        partial.setDevicePixelRatio(scale);
                                        partial.fill(Qt::white);
                                        QPainter partialPainter(&partial);
                                        view.paintVisualStateBackgrounds(&partialPainter, dirty);
                                        partialPainter.end();
                                        const QRect dirtyPixels = deviceRect(dirty, scale);
                                        for (int y = 0; y < partial.height(); ++y) {
                                            const auto *actual = reinterpret_cast<const QRgb *>(partial.constScanLine(y));
                                            const auto *reference = reinterpret_cast<const QRgb *>(full.constScanLine(y));
                                            for (int x = 0; x < partial.width(); ++x) {
                                                const QRgb expectedPixel = dirtyPixels.contains(x, y)
                                                    ? reference[x] : qRgb(255, 255, 255);
                                                if (actual[x] != expectedPixel) QCOMPARE(actual[x], expectedPixel);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        QVERIFY2(unequalHeightsSeen, "The test must exercise proportional heights with column-width remainder");
    }

    void gridLinesKeepPhysicalThickness()
    {
        QStandardItemModel model(10, 1);
        vivtest::TestAdapter adapter;
        ExposedFlowView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(31);
        view.setModel(&model);
        view.setVerticalGridLineColor(Qt::red);
        view.setHorizontalGridLineColor(Qt::blue);
        vivtest::showView(&view, QSize(601, 240));
        for (int gap : {0, 1, 5, 9}) {
            view.setRowSpacing(gap);
            view.setColumnSpacing(gap);
            vivtest::settle();
            const QRect first = view.visualRect(model.index(0, 0));
            for (int thickness : {1, 2, 3}) {
                view.setVerticalGridLineWidth(thickness);
                view.setHorizontalGridLineWidth(thickness);
                for (qreal scale : {1.0, 1.25, 1.5, 1.75}) {
                    QImage image(qCeil(601 * scale), qCeil(240 * scale), QImage::Format_ARGB32_Premultiplied);
                    image.setDevicePixelRatio(scale);
                    image.fill(Qt::white);
                    QPainter painter(&image);
                    view.paintGridLines(&painter);
                    painter.end();
                    const int right = qRound((first.x() + first.width()) * scale);
                    const int bottom = qRound((first.y() + first.height()) * scale);
                    for (int x = right - thickness - 1; x <= right; ++x)
                        QCOMPARE(image.pixelColor(x, qRound(10 * scale)),
                                 QColor(x >= right - thickness && x < right ? Qt::red : Qt::white));
                    for (int y = bottom - thickness - 1; y <= bottom; ++y)
                        QCOMPARE(image.pixelColor(qRound(10 * scale), y),
                                 QColor(y >= bottom - thickness && y < bottom ? Qt::blue : Qt::white));
                }
            }
        }
    }

    void gridOverlayAboveOpaqueWidgets()
    {
        QStandardItemModel model(10, 1);
        OpaqueAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setVerticalGridLineColor(Qt::red);
        view.setHorizontalGridLineColor(Qt::blue);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 240));
        const QRect first = view.visualRect(model.index(0, 0));
        const QImage image = view.viewport()->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        QCOMPARE(image.pixelColor(qRound((first.x() + first.width()) * scale) - 1, qRound(10 * scale)), QColor(Qt::red));
        QCOMPARE(image.pixelColor(qRound(10 * scale), qRound((first.y() + first.height()) * scale) - 1), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(qRound(10 * scale), qRound(10 * scale)), QColor(Qt::green));
        view.setVerticalGridLinesVisible(false);
        view.setHorizontalGridLinesVisible(false);
        vivtest::settle();
        const QImage hidden = view.viewport()->grab().toImage();
        QCOMPARE(hidden.pixelColor(qRound((first.x() + first.width()) * scale) - 1, qRound(10 * scale)), QColor(Qt::green));
    }

    void editorFocusSurvivesReflowAndSorting()
    {
        QStringList names;
        for (int row = 0; row < 200; ++row)
            names.append(QString::number(row));
        vivtest::StringListModel model(names);
        EditorAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(32);
        view.setOverscan(0, 0);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 240));
        const QPersistentModelIndex identity = model.index(2, 0);
        auto *editor = qobject_cast<QLineEdit *>(view.widgetForIndex(identity));
        QVERIFY(editor);
        editor->setFocus();
        editor->setText(QStringLiteral("uncommitted input"));
        editor->setCursorPosition(5);
        vivtest::settle();
        QVERIFY(editor->hasFocus());
        view.resize(401, 240);
        vivtest::settle();
        QCOMPARE(view.widgetForIndex(identity), static_cast<QWidget *>(editor));
        QVERIFY(editor->hasFocus());
        QCOMPARE(editor->text(), QStringLiteral("uncommitted input"));
        QCOMPARE(editor->cursorPosition(), 5);
        model.reverseKeepingPersistentIndexes();
        vivtest::settle();
        QCOMPARE(view.widgetForIndex(identity), static_cast<QWidget *>(editor));
        QCOMPARE(adapter.bindings.value(editor), identity);
        QVERIFY(editor->hasFocus());
        view.setVerticalOffset(0);
        vivtest::settle();
        QVERIFY(view.widgetForIndex(identity));
        QCOMPARE(editor->text(), QStringLiteral("uncommitted input"));
        editor->clearFocus();
        view.setFocus();
        vivtest::settle();
        QVERIFY(!view.widgetForIndex(identity));
    }

    void heightConfigurationAndModelDestruction()
    {
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        auto *model = new QStandardItemModel(100, 1);
        view.setModel(model);
        view.setFixedCellHeight(41);
        vivtest::showView(&view);
        viv::VirtualItemView *base = &view;
        QTest::ignoreMessage(QtWarningMsg, "VirtualItemViews: use this view's cell height configuration");
        base->setUniformItemHeight(77);
        QTest::ignoreMessage(QtWarningMsg, "VirtualItemViews: use this view's cell height configuration");
        base->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
        QTest::ignoreMessage(QtWarningMsg, "VirtualItemViews: use this view's cell height configuration");
        base->setEstimatedItemHeight(90);
        QTest::ignoreMessage(QtWarningMsg, "VirtualItemViews: this layout does not support automatic height measurement");
        base->setAutoMeasureItemHeight(true);
        vivtest::settle();
        QVERIFY(!base->autoMeasureItemHeight());
        QCOMPARE(view.visualRect(model->index(0, 0)).height(), 41);
        view.setItemPinned(model->index(0, 0));
        delete model;
        vivtest::settle();
        QVERIFY(!view.model());
        QCOMPARE(view.stats().logicalItems, qsizetype(0));
        QCOMPARE(view.stats().materializedItems, qsizetype(0));
        QCOMPARE(view.maximumVerticalOffset(), qint64(0));
        QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    }

    void pagingSelectionAndScrollHints()
    {
        QStandardItemModel model(100, 1);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setRowSpacing(10);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 160));
        const int columns = view.columnCount();
        view.setCurrentIndex(model.index(1, 0));
        QTest::keyClick(&view, Qt::Key_PageDown);
        QCOMPARE(view.currentIndex().row(), 1 + 4 * columns);
        QTest::keyClick(&view, Qt::Key_PageUp);
        QCOMPARE(view.currentIndex().row(), 1);
        view.setSelectionMode(viv::VirtualItemView::SelectionMode::NoSelection);
        view.selectionModel()->clearSelection();
        QTest::keyClick(&view, Qt::Key_Down);
        QVERIFY(view.selectionModel()->selectedIndexes().isEmpty());
        QCOMPARE(view.currentIndex().row(), 1 + columns);
        view.setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        view.selectionModel()->select(view.currentIndex(), QItemSelectionModel::ClearAndSelect);
        QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(view.selectionModel()->selectedIndexes().size(), 1);
        const QModelIndex target = model.index(50, 0);
        view.scrollTo(target, viv::VirtualItemView::PositionAtTop);
        vivtest::settle();
        QCOMPARE(view.visualRect(target).top(), 0);
        view.scrollTo(target, viv::VirtualItemView::PositionAtBottom);
        vivtest::settle();
        QCOMPARE(view.visualRect(target).bottom(), view.viewport()->height() - 1);
        view.scrollTo(target, viv::VirtualItemView::PositionAtCenter);
        vivtest::settle();
        QVERIFY(qAbs(view.visualRect(target).center().y() - view.viewport()->rect().center().y()) <= 1);
    }

    void zeroWidthAndReentrantBinding()
    {
        QStandardItemModel first(100, 1);
        QStandardItemModel second(13, 1);
        ReentrantAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.show();
        view.resize(0, 200);
        view.setModel(&first);
        vivtest::settle();
        QCOMPARE(view.viewport()->width(), 0);
        QCOMPARE(view.stats().materializedItems, qsizetype(0));
        adapter.callback = [&]() {
            view.setModel(&second);
            view.setCellWidthRange(80, 150);
        };
        vivtest::showView(&view, QSize(601, 200));
        QCOMPARE(view.model(), static_cast<QAbstractItemModel *>(&second));
        QCOMPARE(view.stats().logicalItems, qsizetype(13));
        for (int row = 0; row < second.rowCount(); ++row) {
            QWidget *widget = view.widgetForIndex(second.index(row, 0));
            if (widget)
                QCOMPARE(adapter.indexOf(widget), QPersistentModelIndex(second.index(row, 0)));
        }
        view.hide();
        view.resize(400, 200);
        view.show();
        vivtest::settle();
        QVERIFY(adapter.createdCount() < 100);
    }

    void mutationsSortingAndPins()
    {
        QStringList names;
        for (int row = 0; row < 200; ++row)
            names.append(QStringLiteral("item-%1").arg(row, 3, 10, QLatin1Char('0')));
        vivtest::StringListModel model(names);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setOverscan(0, 0);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 180));
        const QPersistentModelIndex identity = model.index(2, 0);
        QWidget *widget = view.widgetForIndex(identity);
        QVERIFY(widget);
        view.setItemPinned(identity);
        view.setCurrentIndex(identity);
        view.selectionModel()->select(identity, QItemSelectionModel::ClearAndSelect);
        model.reverseKeepingPersistentIndexes();
        vivtest::settle();
        QCOMPARE(view.widgetForIndex(identity), widget);
        QVERIFY(view.isItemPinned(identity));
        QCOMPARE(adapter.indexOf(widget), identity);
        QCOMPARE(view.currentIndex(), QModelIndex(identity));
        QVERIFY(view.selectionModel()->isSelected(identity));
        model.insertRowsAt(0, {QStringLiteral("new")});
        QVERIFY(model.moveRow(identity.row(), 1));
        vivtest::settle();
        QCOMPARE(view.widgetForIndex(identity), widget);
        QVERIFY(view.isItemPinned(identity));
        QCOMPARE(adapter.indexOf(widget), identity);
        view.setVerticalOffset(600);
        vivtest::settle();
        const int first = int(view.visibleItemRange().first);
        const QPersistentModelIndex next = model.index(first + view.columnCount(), 0);
        const int oldY = view.visualRect(model.index(first, 0)).y();
        QVERIFY(model.removeRowsAt(first, view.columnCount()));
        vivtest::settle();
        QCOMPARE(view.visualRect(next).y(), oldY);
        view.setItemPinned(identity, false);
        vivtest::settle();
        QVERIFY(!view.widgetForIndex(identity));
        model.replaceAll({QStringLiteral("replacement")});
        vivtest::settle();
        QCOMPARE(view.stats().logicalItems, qsizetype(1));
        QVERIFY(!identity.isValid());
        QCOMPARE(view.pinnedItemCount(), qsizetype(0));
    }

    void proxyAndRepeatedModelRoot()
    {
        QStandardItemModel source;
        auto *root = new QStandardItem(QStringLiteral("root"));
        root->appendRow(new QStandardItem(QStringLiteral("b")));
        root->appendRow(new QStandardItem(QStringLiteral("a")));
        source.appendRow(root);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setModel(&source);
        view.setRootIndex(root->index());
        view.setModel(&source);
        vivtest::showView(&view);
        QCOMPARE(view.rootIndex(), QModelIndex(root->index()));
        QCOMPARE(view.stats().logicalItems, qsizetype(2));
        root->insertColumn(0, {new QStandardItem(QStringLiteral("c")), new QStandardItem(QStringLiteral("d"))});
        vivtest::settle();
        for (int row = 0; row < 2; ++row) {
            const QModelIndex index = source.index(row, 0, root->index());
            QCOMPARE(adapter.indexOf(view.widgetForIndex(index)), QPersistentModelIndex(index));
        }
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);
        view.setModel(&proxy);
        view.setRootIndex(proxy.mapFromSource(root->index()));
        const QPersistentModelIndex identity = proxy.index(0, 0, view.rootIndex());
        view.setCurrentIndex(identity);
        proxy.sort(0, Qt::DescendingOrder);
        vivtest::settle();
        QCOMPARE(view.currentIndex(), QModelIndex(identity));
        proxy.setFilterFixedString(QStringLiteral("no-match"));
        vivtest::settle();
        QCOMPARE(view.stats().materializedItems, qsizetype(0));
    }

    void gridNavigationAndDropPositions()
    {
        QStandardItemModel model(10, 1);
        vivtest::TestAdapter adapter;
        ExposedFlowView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(30);
        view.setRowSpacing(8);
        view.setColumnSpacing(10);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 240));
        QCOMPARE(view.columnCount(), 3);
        view.setCurrentIndex(model.index(2, 0));
        QTest::keyClick(&view, Qt::Key_Up);
        QCOMPARE(view.currentIndex().row(), 2);
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(view.currentIndex().row(), 5);
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(view.currentIndex().row(), 8);
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(view.currentIndex().row(), 9);
        view.setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        view.setCurrentIndex(model.index(0, 0));
        view.selectionModel()->select(view.currentIndex(), QItemSelectionModel::ClearAndSelect);
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(view.selectionModel()->selectedIndexes().size(), 2);
        const QRect first = view.visualRect(model.index(0, 0));
        QCOMPARE(view.resolveDropTarget(first.topLeft() + QPoint(2, 2)).row, 0);
        QCOMPARE(view.resolveDropTarget(first.topRight() + QPoint(-2, 2)).row, 1);
        QCOMPARE(view.resolveDropTarget(QPoint(first.right() + 3, first.center().y())).row, 1);
        QCOMPARE(view.resolveDropTarget(QPoint(2, first.bottom() + 3)).row, 3);
        const QRect last = view.visualRect(model.index(9, 0));
        const auto append = view.resolveDropTarget(QPoint(view.columnViewportX(2) + 2, last.center().y()));
        QCOMPARE(append.row, 10);
        QVERIFY(append.trailing);
        QVERIFY(!view.resolveDropIndicatorRect(append).isEmpty());
        QVERIFY(!view.resolveDropTarget(QPoint(-1, 0)).isValid());
        model.removeRows(0, 10);
        vivtest::settle();
        const auto empty = view.resolveDropTarget(QPoint(2, 2));
        QCOMPARE(empty.row, 0);
        QVERIFY(!view.resolveDropIndicatorRect(empty).isEmpty());
    }

    void geometryAndLastRow()
    {
        QStandardItemModel model(10, 1);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setFrameShape(QFrame::NoFrame);
        view.setAdapter(&adapter);
        view.setFixedCellHeight(25);
        view.setModel(&model);
        vivtest::showView(&view, QSize(601, 240));
        QCOMPARE(view.columnCount(), 4);
        QCOMPARE(view.displayRowCount(), qsizetype(3));
        int width = 0;
        for (int column = 0; column < view.columnCount(); ++column) {
            QCOMPARE(view.columnViewportX(column), width);
            width += view.columnWidth(column);
        }
        QCOMPARE(width, view.viewport()->width());
        for (int item = 0; item < model.rowCount(); ++item) {
            const QModelIndex index = model.index(item, 0);
            const QRect rect = view.visualRect(index);
            QCOMPARE(view.indexAt(rect.center()), index);
            QVERIFY(view.widgetForIndex(index));
            QCOMPARE(view.widgetForIndex(index)->geometry(), rect);
            QCOMPARE(rect.height(), 25);
        }
        const QRect last = view.visualRect(model.index(9, 0));
        QCOMPARE(last.width(), view.columnWidth(1));
        QVERIFY(!view.indexAt(QPoint(view.columnViewportX(2) + 2, last.center().y())).isValid());
        viv::VirtualItemView *base = &view;
        base->setRowSpacing(11);
        vivtest::settle();
        QCOMPARE(view.visualRect(model.index(4, 0)).y(), 36);
        QVERIFY(!view.indexAt(QPoint(2, 26)).isValid());
    }

    void boundedVirtualizationAndRecycling()
    {
        CountingModel model;
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(26);
        view.setOverscan(0, 0);
        view.setModel(&model);
        vivtest::showView(&view, QSize(650, 180));
        const auto verifyVisible = [&]() {
            const auto range = view.visibleItemRange();
            QVERIFY(range.isValid());
            for (qsizetype item = range.first; item <= range.last; ++item) {
                const QModelIndex index = model.index(int(item), 0);
                QVERIFY(view.widgetForIndex(index));
                QCOMPARE(adapter.indexOf(view.widgetForIndex(index)), QPersistentModelIndex(index));
            }
            QCOMPARE(view.stats().materializedItems, range.count());
        };
        verifyVisible();
        QVERIFY(adapter.createdCount() < 100);
        view.setVerticalOffset(400000);
        vivtest::settle();
        verifyVisible();
        QVERIFY(adapter.unbindCount() > 0);
        QVERIFY(adapter.createdCount() < 100);
        model.queries = 0;
        view.resize(830, 220);
        vivtest::settle();
        verifyVisible();
        QVERIFY(model.queries < 1000);
        QVERIFY(adapter.createdCount() < 150);
        view.setVerticalOffset(view.maximumVerticalOffset());
        vivtest::settle();
        verifyVisible();
        QVERIFY(view.widgetForIndex(model.index(999999, 0)));
    }

    void reflowPreservesAnchorAndSelection()
    {
        QStandardItemModel model(1000, 1);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setFixedCellHeight(26);
        view.setModel(&model);
        vivtest::showView(&view, QSize(400, 230));
        view.setVerticalOffset(951);
        const auto first = view.visibleItemRange().first;
        const QPersistentModelIndex anchor = model.index(int(first), 0);
        const int oldY = view.visualRect(anchor).y();
        const QPersistentModelIndex selected = model.index(int(first + 1), 0);
        view.setCurrentIndex(selected);
        view.selectionModel()->select(selected, QItemSelectionModel::ClearAndSelect);
        const int columnsBefore = view.columnCount();
        view.resize(820, 230);
        vivtest::settle();
        QVERIFY(view.columnCount() > columnsBefore);
        QCOMPARE(view.visualRect(anchor).y(), oldY);
        QCOMPARE(view.currentIndex(), QModelIndex(selected));
        QVERIFY(view.selectionModel()->isSelected(selected));
        view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
        view.setCellAspectRatio(2);
        vivtest::settle();
        QCOMPARE(view.visualRect(anchor).y(), oldY);
        const QRect rect = view.visualRect(selected);
        QCOMPARE(rect.height(), qRound(rect.width() / 2.0));
    }

    void scrollbarAndRootAreStable()
    {
        QStandardItemModel model;
        auto *root = new QStandardItem(QStringLiteral("root"));
        for (int row = 0; row < 9; ++row)
            root->appendRow(new QStandardItem(QString::number(row)));
        model.appendRow(root);
        vivtest::TestAdapter adapter;
        viv::VirtualFlowTableView view;
        view.setAdapter(&adapter);
        view.setModel(&model);
        view.setRootIndex(root->index());
        view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
        QSignalSpy spy(&view, &viv::VirtualFlowTableView::flowLayoutChanged);
        vivtest::showView(&view, QSize(601, 440));
        for (int width : {601, 599, 401, 399, 201, 199, 100, 800}) {
            view.resize(width, 440);
            vivtest::settle(6);
            const int columns = view.columnCount();
            const int events = spy.count();
            vivtest::settle(6);
            QCOMPARE(view.columnCount(), columns);
            QCOMPARE(spy.count(), events);
            QCOMPARE(view.stats().logicalItems, qsizetype(9));
        }
        QTest::ignoreMessage(QtWarningMsg, "VirtualItemViews: this layout does not support frozen rows");
        view.setFrozenRows(1);
        QCOMPARE(view.frozenRows(), 0);
        QTest::ignoreMessage(QtWarningMsg, "VirtualItemViews: this layout does not support frozen rows");
        view.setFrozenBottomRows(1);
        QCOMPARE(view.frozenBottomRows(), 0);
        root->removeRows(0, 9);
        vivtest::settle();
        QCOMPARE(view.stats().materializedItems, qsizetype(0));
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QStandardItemModel other(7, 1);
        view.setModel(&other);
        vivtest::settle();
        QVERIFY(!view.rootIndex().isValid());
        QCOMPARE(view.stats().logicalItems, qsizetype(7));
    }
};

QTEST_MAIN(TestVirtualFlowTableView)
#include "tst_virtualflowtableview.moc"
