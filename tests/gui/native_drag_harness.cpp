#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSize>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTimer>
#include <QTextStream>
#include <QVBoxLayout>
#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef interface
#endif

namespace {

class HarnessRow : public QWidget
{
public:
    explicit HarnessRow(QWidget *parent = nullptr) : QWidget(parent)
    {
        for (int column = 0; column < 3; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("nativeDragCell%1").arg(column));
            layout->addWidget(label);
            m_labels[column] = label;
        }
    }

    void bind(const QModelIndex &index)
    {
        for (int column = 0; column < 3; ++column)
            m_labels[column]->setText(index.siblingAtColumn(column).data().toString());
    }

private:
    QLabel *m_labels[3] = {nullptr, nullptr, nullptr};
};

class HarnessAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new HarnessRow(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<HarnessRow *>(widget)->bind(index);
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 32); }
};

class HarnessCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        label->setContentsMargins(8, 0, 8, 0);
        return label;
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

QList<QStandardItem *> makeRow(const QString &name, const QString &kind,
                               const QString &state)
{
    return {new QStandardItem(name), new QStandardItem(kind), new QStandardItem(state)};
}

void enableDrag(QStandardItem *item)
{
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("VirtualTreeTableView - Native Drag Harness"));
    auto *rootLayout = new QVBoxLayout(&window);
    auto *controls = new QHBoxLayout;
    rootLayout->addLayout(controls);

    auto *mode = new QComboBox(&window);
    mode->addItem(QStringLiteral("Row widgets"));
    mode->addItem(QStringLiteral("Cell widgets"));
    controls->addWidget(mode);

    auto *pin = new QComboBox(&window);
    pin->addItem(QStringLiteral("Temporary pin"), false);
    pin->addItem(QStringLiteral("Explicit pin"), true);
    controls->addWidget(pin);

    auto *start = new QPushButton(QStringLiteral("Start native drag"), &window);
    controls->addWidget(start);
    controls->addStretch();

    auto *status = new QLabel(&window);
    status->setWordWrap(true);
    status->setText(QStringLiteral("Select a mode and pin policy, then drag the Start button. "
                                  "The native drag cancels after scroll/collapse checks."));
    rootLayout->addWidget(status);

    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Node"), QStringLiteral("Kind"),
                                     QStringLiteral("State")});
    auto parentRow = makeRow(QStringLiteral("Parent"), QStringLiteral("branch"),
                             QStringLiteral("expanded"));
    QStandardItem *parentItem = parentRow.first();
    enableDrag(parentItem);
    for (int row = 0; row < 120; ++row) {
        auto childRow = makeRow(QStringLiteral("child-%1").arg(row), QStringLiteral("leaf"),
                                QStringLiteral("ready"));
        enableDrag(childRow.first());
        parentItem->appendRow(childRow);
    }
    model.appendRow(parentRow);

    auto *view = new viv::VirtualTreeTableView(&window);
    view->setModel(&model);
    view->setTableAdapter(new HarnessAdapter, true);
    view->setCellAdapter(new HarnessCellAdapter, true);
    view->setUniformItemHeight(32);
    view->setColumnWidth(0, 260);
    view->setColumnWidth(1, 140);
    view->setColumnWidth(2, 140);
    view->setDragEnabled(true);
    view->setDragDropActions(Qt::MoveAction);
    view->setDefaultDropAction(Qt::MoveAction);
    view->setMoveRemovesSourceRows(true);
    view->setOverscan(0, 0);
    view->expand(model.index(0, 0));
    rootLayout->addWidget(view, 1);
    QSet<int> acceptedCases;
    bool failedAttempt = false;

    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), view,
                     [view](int index) {
                         using Mode = viv::VirtualTableView::MaterializationMode;
                         using Selection = viv::VirtualTableView::SelectionBehavior;
                         view->setMaterializationMode(index == 0 ? Mode::RowWidgets
                                                                  : Mode::CellWidgets);
                         view->setSelectionBehavior(index == 0 ? Selection::SelectRows
                                                               : Selection::SelectItems);
                         view->flushPendingRelayout();
                     });

    QObject::connect(start, &QPushButton::pressed, &window,
                     [&, view, mode, pin, start, status]() {
                         const QModelIndex parent = model.index(0, 0);
                         const QPersistentModelIndex source(
                             mode->currentIndex() == 0 ? model.index(0, 0, parent)
                                                       : model.index(0, 1, parent));
                         if (!source.isValid()) {
                             status->setText(QStringLiteral("ERROR: source index is invalid."));
                             return;
                         }

                         view->expand(parent);
                         view->scrollTo(source, viv::VirtualItemView::PositionAtTop);
                         view->flushPendingRelayout();
                         QPointer<QWidget> sourceWidget(
                             mode->currentIndex() == 0 ? view->widgetForIndex(source)
                                                       : view->cellWidget(source));
                         if (!sourceWidget) {
                             status->setText(
                                 QStringLiteral("ERROR: source widget was not materialized."));
                             return;
                         }

                         const bool explicitPin = pin->currentData().toBool();
                         view->setItemPinned(source, explicitPin);
                         view->flushPendingRelayout();
                         const int rowsBefore = parentRow.first()->rowCount();
                         const QString modeName = mode->currentText();
                         const QString pinName = pin->currentText();
                         status->setText(QStringLiteral("Native drag active: %1, %2.")
                                             .arg(modeName, pinName));
                         start->setEnabled(false);
                         mode->setEnabled(false);
                         pin->setEnabled(false);

                         bool transitionObserved = false;
                         bool sourceKeptDuringTransition = false;
                         bool activeDragSeen = false;
                         bool sourcePinnedDuring = false;
                         bool scrollObserved = false;
                         bool collapseObserved = false;
                         QTimer transitionTimer;
                         transitionTimer.setSingleShot(true);
                         const auto transition = [&, source, sourceWidget, mode, view, status]() {
                             if (transitionObserved)
                                 return;
                             transitionObserved = true;
                             QDrag *drag = view->findChild<QDrag *>();
                             activeDragSeen = drag && drag->mimeData()
                                 && !drag->mimeData()->formats().isEmpty();
                             sourcePinnedDuring = view->isItemPinned(source);
                             if (!sourceWidget)
                                 return;
                             const QModelIndex transitionParent = model.index(0, 0);
                             view->scrollTo(model.index(80, 0, transitionParent),
                                            viv::VirtualItemView::PositionAtTop);
                             view->flushPendingRelayout();
                             scrollObserved = view->verticalOffset() > 0;
                             QWidget *afterScroll = mode->currentIndex() == 0
                                 ? view->widgetForIndex(source) : view->cellWidget(source);
                             view->collapse(transitionParent);
                             view->flushPendingRelayout();
                             collapseObserved = !view->isExpanded(transitionParent)
                                 && view->visibleRowCount() == 1;
                             QWidget *afterCollapse = mode->currentIndex() == 0
                                 ? view->widgetForIndex(source) : view->cellWidget(source);
                             sourceKeptDuringTransition = afterScroll == sourceWidget.data()
                                 && afterCollapse == sourceWidget.data();
                             status->setText(QStringLiteral(
                                 "Native drag active: source survived scroll+collapse=%1.")
                                                 .arg(sourceKeptDuringTransition
                                                          ? QStringLiteral("true")
                                                          : QStringLiteral("false")));
                             QDrag::cancel();
                         };
                         QObject::connect(&transitionTimer, &QTimer::timeout, view, transition);

#ifdef Q_OS_WIN
                         // OLE dispatches native timers while Qt timers may be suspended.
                         static std::function<void()> nativeTransition;
                         nativeTransition = transition;
                         const HWND nativeWindow = reinterpret_cast<HWND>(view->winId());
                         const UINT_PTR nativeTimer = SetTimer(nativeWindow,
                             reinterpret_cast<UINT_PTR>(&nativeTransition), 10,
                             [](HWND owner, UINT, UINT_PTR timer, DWORD) {
                                 KillTimer(owner, timer);
                                 if (nativeTransition)
                                     nativeTransition();
                             });
#endif
                         transitionTimer.start(350);
                         const bool qtLeftButtonDown = QApplication::mouseButtons() & Qt::LeftButton;
#ifdef Q_OS_WIN
                         const bool nativeLeftButtonDown = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
#endif
                         QElapsedTimer dragElapsed;
                         dragElapsed.start();
                         const Qt::DropAction result = view->startDrag(source);
                         const qint64 dragElapsedMs = dragElapsed.elapsed();
                         transitionTimer.stop();
#ifdef Q_OS_WIN
                         KillTimer(nativeWindow, nativeTimer);
                         nativeTransition = {};
#endif
                         view->flushPendingRelayout();
                         QWidget *afterWidget = mode->currentIndex() == 0
                             ? view->widgetForIndex(source) : view->cellWidget(source);
                         const bool sameWidget = sourceWidget && afterWidget == sourceWidget.data();
                         const bool rowsPreserved = parentRow.first()->rowCount() == rowsBefore;
                         const bool pinPreserved = view->isItemPinned(source) == explicitPin;
                         const bool temporaryPinReleased = !explicitPin && !view->isItemPinned(source);
                         const bool sourceLifecycleOk = explicitPin ? sameWidget
                             : temporaryPinReleased && !afterWidget;
                         status->setText(QStringLiteral(
                             "RESULT: action=%1; rowsPreserved=%2; sourceLifecycleOk=%3; "
                             "pinPreserved=%4; temporaryPinReleased=%5; sourceMaterializedAfter=%6; "
                             "transitionObserved=%7; transitionKept=%8; rows=%9.")
                                             .arg(int(result))
                                             .arg(rowsPreserved ? QStringLiteral("true")
                                                                 : QStringLiteral("false"))
                                             .arg(sourceLifecycleOk ? QStringLiteral("true")
                                                                    : QStringLiteral("false"))
                                             .arg(pinPreserved ? QStringLiteral("true")
                                                               : QStringLiteral("false"))
                                             .arg(temporaryPinReleased ? QStringLiteral("true")
                                                                       : QStringLiteral("false"))
                                             .arg(afterWidget ? QStringLiteral("true")
                                                              : QStringLiteral("false"))
                                             .arg(transitionObserved ? QStringLiteral("true")
                                                                      : QStringLiteral("false"))
                                             .arg(sourceKeptDuringTransition
                                                      ? QStringLiteral("true")
                                                      : QStringLiteral("false"))
                                             .arg(parentRow.first()->rowCount()));
                         view->setItemPinned(source, false);
                         view->flushPendingRelayout();
                         const bool cleanupOk = view->stats().pinnedWidgets == 0
                             && !(mode->currentIndex() == 0 ? view->widgetForIndex(source)
                                                           : view->cellWidget(source));
                         const bool passed = result == Qt::IgnoreAction && rowsPreserved
                             && sourceLifecycleOk && pinPreserved && transitionObserved
                             && sourceKeptDuringTransition && activeDragSeen && sourcePinnedDuring
                             && scrollObserved && collapseObserved && cleanupOk;
                         if (passed)
                             acceptedCases.insert(mode->currentIndex() * 2 + int(explicitPin));
                         else
                             failedAttempt = true;
                         QJsonObject record{
                             {QStringLiteral("platform"), QApplication::platformName()},
                             {QStringLiteral("mode"), modeName},
                             {QStringLiteral("pin"), pinName},
                             {QStringLiteral("passed"), passed},
                             {QStringLiteral("action"), int(result)},
                             {QStringLiteral("activeDragSeen"), activeDragSeen},
                             {QStringLiteral("sourcePinnedDuring"), sourcePinnedDuring},
                             {QStringLiteral("scrollObserved"), scrollObserved},
                             {QStringLiteral("collapseObserved"), collapseObserved},
                             {QStringLiteral("transitionKept"), sourceKeptDuringTransition},
                             {QStringLiteral("rowsPreserved"), rowsPreserved},
                             {QStringLiteral("sourceLifecycleOk"), sourceLifecycleOk},
                             {QStringLiteral("pinPreserved"), pinPreserved},
                             {QStringLiteral("cleanupOk"), cleanupOk},
                             {QStringLiteral("qtLeftButtonDown"), qtLeftButtonDown},
                             {QStringLiteral("dragElapsedMs"), double(dragElapsedMs)},
                             {QStringLiteral("rows"), parentRow.first()->rowCount()}
                         };
#ifdef Q_OS_WIN
                         record.insert(QStringLiteral("nativeLeftButtonDown"), nativeLeftButtonDown);
                         record.insert(QStringLiteral("nativeTimerArmed"), nativeTimer != 0);
#endif
                         QTextStream output(stdout);
                         output << "NATIVE_DRAG_RESULT "
                             << QJsonDocument(record).toJson(QJsonDocument::Compact) << '\n';
                         output.flush();
                         status->setText(QStringLiteral("%1: %2, %3. %4")
                             .arg(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                                  modeName, pinName, status->text()));
                         start->setEnabled(true);
                         mode->setEnabled(true);
                         pin->setEnabled(true);
                     });

    window.resize(760, 520);
    window.show();
    view->setFocus();
    const int result = app.exec();
    view->setModel(nullptr);
    const bool passed = result == 0 && acceptedCases.size() == 4 && !failedAttempt;
    QTextStream output(stdout);
    output << "NATIVE_DRAG_SUMMARY " << QJsonDocument(QJsonObject{
        {QStringLiteral("passed"), passed},
        {QStringLiteral("acceptedCases"), acceptedCases.size()},
        {QStringLiteral("expectedCases"), 4},
        {QStringLiteral("failedAttempt"), failedAttempt}
    }).toJson(QJsonDocument::Compact) << '\n';
    output.flush();
    return result != 0 ? result : (passed ? 0 : 1);
}
