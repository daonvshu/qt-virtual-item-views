#include <virtualitemviews/accessibility.h>
#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

namespace {

void record(const QJsonObject &value)
{
    QTextStream output(stdout);
    output << "NATIVE_PLATFORM_EVENT "
           << QJsonDocument(value).toJson(QJsonDocument::Compact) << '\n';
    output.flush();
}

class PlatformView : public viv::VirtualTreeTableView
{
public:
    using viv::VirtualTreeTableView::VirtualTreeTableView;
    bool traceKeyboard = false;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        const QPersistentModelIndex before(currentIndex());
        viv::VirtualTreeTableView::keyPressEvent(event);
        if (traceKeyboard) {
            const QModelIndex after = currentIndex();
            record({{QStringLiteral("case"), QStringLiteral("accessibility-key")},
                    {QStringLiteral("key"), event->key()},
                    {QStringLiteral("autoRepeat"), event->isAutoRepeat()},
                    {QStringLiteral("before"), before.data().toString()},
                    {QStringLiteral("after"), after.data().toString()},
                    {QStringLiteral("afterColumn"), after.column()},
                    {QStringLiteral("viewHasFocus"), hasFocus()},
                    {QStringLiteral("afterParent"), after.parent().data().toString()},
                    {QStringLiteral("beforeExpanded"), isExpanded(before)},
                    {QStringLiteral("visibleRows"), int(visibleRowCount())}});
        }
    }
};

class PlatformRow : public QWidget
{
public:
    explicit PlatformRow(QWidget *parent) : QWidget(parent)
    {
        for (int column = 0; column < 4; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(4, 2, 4, 2);
            if (column == 2) {
                editor = new QComboBox(host);
                editor->addItems({QStringLiteral("draft"), QStringLiteral("accepted")});
                layout->addWidget(editor);
            } else {
                labels[column] = new QLabel(host);
                layout->addWidget(labels[column]);
            }
        }
    }
    QComboBox *editor = nullptr;
    QLabel *labels[4] = {};
};

class PlatformRows : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new PlatformRow(parent);
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<PlatformRow *>(widget);
        for (int column : {0, 1, 3})
            row->labels[column]->setText(index.siblingAtColumn(column).data().toString());
        row->editor->setCurrentIndex(row->editor->findText(index.siblingAtColumn(2).data().toString()));
        row->editor->setProperty("boundIndex", QVariant::fromValue(QPersistentModelIndex(index.siblingAtColumn(2))));
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 36); }
};

class PlatformCells : public viv::CellWidgetAdapter
{
public:
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() == 2 ? 1 : 0;
    }
    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *editor = new QComboBox(parent);
            editor->addItems({QStringLiteral("draft"), QStringLiteral("accepted")});
            return editor;
        }
        return new QLabel(parent);
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (index.column() == 2) {
            auto *editor = static_cast<QComboBox *>(widget);
            editor->setCurrentIndex(editor->findText(index.data().toString()));
            editor->setProperty("boundIndex", QVariant::fromValue(QPersistentModelIndex(index)));
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }
};

class HeaderObserver : public QObject
{
public:
    viv::VirtualTreeTableView *view = nullptr;
    std::function<void(bool)> complete;
    bool active = false;
    int mode = 0;
    QPointer<viv::VirtualHeaderView> pressed;
    int source = -1;
    int oldVisual = -1;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (!active || !event->spontaneous() || (event->type() != QEvent::MouseButtonPress
                                    && event->type() != QEvent::MouseButtonRelease))
            return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        Q_UNUSED(object);
        const QPoint global = mouse->globalPos();
        if (event->type() == QEvent::MouseButtonPress && !pressed) {
            for (auto *header : view->findChildren<viv::VirtualHeaderView *>()) {
                if (header->orientation() != Qt::Horizontal || !header->isVisible()
                    || !header->rect().contains(header->mapFromGlobal(global)))
                    continue;
                for (int logical : header->materializedSections()) {
                    QWidget *section = header->sectionWidget(logical);
                    if (section && section->rect().contains(section->mapFromGlobal(global))) {
                        pressed = header;
                        source = logical;
                        oldVisual = view->horizontalHeaderGeometry()->visualIndex(logical);
                        break;
                    }
                }
                if (pressed)
                    break;
            }
        } else if (event->type() == QEvent::MouseButtonRelease && pressed) {
            bool crossed = false;
            int target = -1;
            for (auto *other : view->findChildren<viv::VirtualHeaderView *>()) {
                if (other != pressed && other->orientation() == Qt::Horizontal
                    && other->isVisible() && other->rect().contains(other->mapFromGlobal(global))) {
                    crossed = true;
                    for (int logical : other->materializedSections()) {
                        QWidget *section = other->sectionWidget(logical);
                        if (section && section->rect().contains(section->mapFromGlobal(global)))
                            target = logical;
                    }
                }
            }
            const int logical = source;
            const int from = oldVisual;
            pressed.clear();
            active = false;
            QTimer::singleShot(300, view, [this, logical, from, crossed, target, global]() {
                const auto panes = view->paneSpecs();
                const bool passed = crossed && logical == 1 && from == 1
                    && view->horizontalHeaderGeometry()->visualIndex(1) == 2
                    && view->horizontalHeaderGeometry()->visualIndex(2) == 1
                    && panes.size() == 3 && panes[1].logicalColumns == QVector<int>({1, 2})
                    && panes[2].logicalColumns == QVector<int>({3})
                    && !QWidget::mouseGrabber();
                record({{QStringLiteral("case"), QStringLiteral("cross-header")},
                        {QStringLiteral("mode"), mode},
                        {QStringLiteral("spontaneousMouse"), true},
                        {QStringLiteral("crossedIndependentHeader"), crossed},
                        {QStringLiteral("releaseColumn"), target},
                        {QStringLiteral("releaseGlobalX"), global.x()},
                        {QStringLiteral("releaseGlobalY"), global.y()},
                        {QStringLiteral("finalVisualIndex"), view->horizontalHeaderGeometry()->visualIndex(1)},
                        {QStringLiteral("source"), logical}, {QStringLiteral("passed"), passed}});
                if (complete)
                    complete(passed);
            });
        }
        return false;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const bool accessibilityOnly = app.arguments().contains(QStringLiteral("--accessibility"));
    viv::installAccessibilityFactory();
    QWidget window;
    window.setWindowTitle(QStringLiteral("VirtualTreeTableView - Native Platform Acceptance"));
    auto *layout = new QVBoxLayout(&window);
    auto *controls = new QHBoxLayout;
    auto *mode = new QComboBox(&window);
    mode->addItems({QStringLiteral("Row widgets"), QStringLiteral("Cell widgets")});
    auto *test = new QComboBox(&window);
    test->addItems({QStringLiteral("Cross-header"), QStringLiteral("Combo popup"), QStringLiteral("Menu popup")});
    auto *prepare = new QPushButton(QStringLiteral("Prepare"), &window);
    controls->addWidget(mode);
    controls->addWidget(test);
    controls->addWidget(prepare);
    controls->addStretch();
    layout->addLayout(controls);
    auto *status = new QLabel(QStringLiteral("Pending"), &window);
    layout->addWidget(status);
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Node"), QStringLiteral("Group A"),
                                     QStringLiteral("Choice"), QStringLiteral("Group B")});
    const auto row = [](const QString &name) {
        return QList<QStandardItem *>{new QStandardItem(name), new QStandardItem(QStringLiteral("alpha")),
                                     new QStandardItem(QStringLiteral("draft")), new QStandardItem(QStringLiteral("beta"))};
    };
    auto parentRow = row(QStringLiteral("parent"));
    parentRow[0]->appendRow(row(QStringLiteral("edited")));
    model.appendRow(parentRow);
    for (int i = 0; i < 120; ++i)
        model.appendRow(row(QStringLiteral("tail-%1").arg(i)));
    auto *view = new PlatformView(&window);
    view->traceKeyboard = accessibilityOnly;
    view->setAccessibleName(QStringLiteral("Acceptance tree table"));
    layout->addWidget(view, 1);
    view->setModel(&model);
    view->setTableAdapter(new PlatformRows, true);
    view->setCellAdapter(new PlatformCells, true);
    view->setUniformItemHeight(36);
    view->setDefaultColumnWidth(180);
    view->setOverscan(0, 0);
    view->setColumnOverscan(0);
    view->setColumnDragEnabled(true);
    const QPersistentModelIndex parent(model.index(0, 0));
    const QPersistentModelIndex source(model.index(0, 2, parent));
    QPointer<QComboBox> editor;
    QPointer<QWidget> popup;
    QPointer<QMenu> menu;
    QSet<QString> accepted;
    bool failed = false;
    bool watching = false;
    bool migrated = false;
    bool migrationCompleted = false;
    bool kept = false;
    int preparedMode = 0;
    int preparedTest = 0;
    quint64 request = 0;
    const auto panes = [](bool moveEditor) {
        const auto pane = [](QVector<int> columns, viv::PaneScroll scroll, int group) {
            viv::TablePaneSpec spec;
            spec.logicalColumns = columns;
            spec.scroll = scroll;
            spec.scrollGroup = group;
            return spec;
        };
        return QVector<viv::TablePaneSpec>{pane({0}, viv::PaneScroll::Frozen, 0),
            pane(moveEditor ? QVector<int>{1} : QVector<int>{1, 2}, viv::PaneScroll::Scrollable, 0),
            pane(moveEditor ? QVector<int>{2, 3} : QVector<int>{3}, viv::PaneScroll::Scrollable, 1)};
    };
    const auto sourceEditor = [&]() -> QComboBox * {
        QWidget *widget = preparedMode == 0 ? view->widgetForIndex(QModelIndex(source).siblingAtColumn(0))
                                           : view->cellWidget(source);
        return preparedMode == 0 && widget ? static_cast<PlatformRow *>(widget)->editor
                                          : static_cast<QComboBox *>(widget);
    };
    const auto finish = [&](bool passed) {
        watching = false;
        const QString key = QStringLiteral("%1:%2").arg(preparedMode).arg(preparedTest);
        if (passed)
            accepted.insert(key);
        else
            failed = true;
        status->setText(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
        mode->setEnabled(true);
        test->setEnabled(true);
        prepare->setEnabled(true);
    };
    HeaderObserver observer;
    observer.view = view;
    observer.complete = [&](bool passed) { if (watching && preparedTest == 0) finish(passed); };
    app.installEventFilter(&observer);
    const auto committed = [&]() {
        if (!watching || preparedTest == 0)
            return;
        if (!migrationCompleted) {
            record({{QStringLiteral("case"), preparedTest == 1 ? QStringLiteral("combo") : QStringLiteral("menu")},
                    {QStringLiteral("mode"), preparedMode},
                    {QStringLiteral("incomplete"), QStringLiteral("selection-before-migration")},
                    {QStringLiteral("popupObserved"), migrated}});
            watching = false;
            status->setText(QStringLiteral("INCOMPLETE: Prepare again; wait for Popup migration kept before selecting"));
            mode->setEnabled(true);
            test->setEnabled(true);
            prepare->setEnabled(true);
            return;
        }
        const quint64 current = request;
        const bool submitted = migrated && kept && editor && sourceEditor() == editor
            && editor->property("boundIndex").value<QPersistentModelIndex>() == source;
        QTimer::singleShot(100, view, [&, current, submitted]() {
            if (current != request)
                return;
            if (editor)
                editor->clearFocus();
            view->setFocus();
            view->scrollTo(model.index(100, 0), viv::VirtualItemView::PositionAtTop);
            view->flushPendingRelayout();
            const bool cleanup = !QApplication::activePopupWidget() && !sourceEditor()
                && view->stats().pinnedWidgets == 0;
            const bool passed = submitted && cleanup;
            record({{QStringLiteral("case"), preparedTest == 1 ? QStringLiteral("combo") : QStringLiteral("menu")},
                    {QStringLiteral("mode"), preparedMode}, {QStringLiteral("migrationKept"), kept},
                    {QStringLiteral("submittedToSource"), submitted}, {QStringLiteral("cleanupOk"), cleanup},
                    {QStringLiteral("passed"), passed}});
            finish(passed);
        });
    };
    QTimer watch;
    watch.setInterval(100);
    QObject::connect(&watch, &QTimer::timeout, view, [&]() {
        if (!watching || preparedTest == 0 || migrated || !editor)
            return;
        QWidget *candidate = QApplication::activePopupWidget();
        bool owned = false;
        // Popup windows cross the window boundary that isAncestorOf() stops at.
        for (QWidget *owner = candidate; owner; owner = owner->parentWidget()) {
            if (owner == editor) {
                owned = true;
                break;
            }
        }
        if (!owned)
            return;
        popup = candidate;
        migrated = true;
        record({{QStringLiteral("mode"), preparedMode}, {QStringLiteral("popupObserved"), true},
                {QStringLiteral("migrationDelayMs"), 1000}});
        const quint64 current = request;
        QTimer::singleShot(1000, view, [&, current]() {
            if (current != request || !watching)
                return;
            const QPointer<QComboBox> before(editor);
            view->setPanes(panes(true));
            view->scrollTo(model.index(80, 0), viv::VirtualItemView::PositionAtTop);
            view->collapse(parent);
            view->flushPendingRelayout();
            kept = before && before == editor && sourceEditor() == editor && popup
                && popup == QApplication::activePopupWidget() && popup->isVisible()
                && view->stats().pinnedWidgets == 1;
            view->expand(parent);
            view->scrollTo(source);
            view->flushPendingRelayout();
            kept = kept && popup && popup == QApplication::activePopupWidget()
                && sourceEditor() == editor && editor->currentIndex() == 0;
            migrationCompleted = true;
            record({{QStringLiteral("mode"), preparedMode}, {QStringLiteral("popupKept"), kept}});
            status->setText(kept ? QStringLiteral("Popup migration kept") : QStringLiteral("Popup migration failed"));
        });
    });
    QObject::connect(prepare, &QPushButton::clicked, view, [&]() {
        ++request;
        watching = false;
        migrated = migrationCompleted = kept = false;
        if (menu)
            menu->deleteLater();
        popup.clear();
        preparedMode = mode->currentIndex();
        preparedTest = test->currentIndex();
        view->setMaterializationMode(preparedMode == 0 ? viv::VirtualTableView::MaterializationMode::RowWidgets
                                                      : viv::VirtualTableView::MaterializationMode::CellWidgets);
        view->horizontalHeaderGeometry()->moveSection(view->horizontalHeaderGeometry()->visualIndex(1), 1);
        view->setPanes(panes(false));
        view->expand(parent);
        view->scrollTo(source);
        view->flushPendingRelayout();
        editor = sourceEditor();
        if (!editor) {
            finish(false);
            return;
        }
        editor->setCurrentIndex(0);
        QObject::disconnect(editor, nullptr, view, nullptr);
        QObject::connect(editor, QOverload<int>::of(&QComboBox::activated), view, [&](int choice) {
            if (choice == 1)
                committed();
            else
                finish(false);
        });
        mode->setEnabled(false);
        test->setEnabled(false);
        prepare->setEnabled(false);
        watching = true;
        observer.active = preparedTest == 0;
        observer.pressed.clear();
        observer.mode = preparedMode;
        status->setText(QStringLiteral("Ready"));
        if (preparedTest == 1) {
            editor->setFocus();
            editor->showPopup();
        } else if (preparedTest == 2) {
            menu = new QMenu(editor);
            menu->addAction(QStringLiteral("Discard"));
            auto *commit = menu->addAction(QStringLiteral("Commit draft"));
            QObject::connect(commit, &QAction::triggered, view, committed);
            menu->popup(editor->mapToGlobal(QPoint(0, editor->height())));
        }
    });
    if (accessibilityOnly) {
        test->hide();
        prepare->hide();
        QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, view, [&]() {
            for (auto *combo : view->findChildren<QComboBox *>()) {
                if (combo->property("accessibilityCommitConnected").toBool())
                    continue;
                combo->setProperty("accessibilityCommitConnected", true);
                QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), view, [&, combo](int choice) {
                    const QPersistentModelIndex bound = combo->property("boundIndex").value<QPersistentModelIndex>();
                    const QString value = combo->itemText(choice);
                    if (!bound.isValid())
                        return;
                    const bool committed = model.setData(bound, value, Qt::EditRole);
                    record({{QStringLiteral("case"), QStringLiteral("accessibility-value")},
                            {QStringLiteral("node"), QModelIndex(bound).siblingAtColumn(0).data().toString()},
                            {QStringLiteral("modelValue"), bound.data().toString()},
                            {QStringLiteral("committed"), committed}});
                });
            }
        });
        const auto resetAccessibility = [&, view](int selectedMode) {
            view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
            view->setMaterializationMode(selectedMode == 0
                ? viv::VirtualTableView::MaterializationMode::RowWidgets
                : viv::VirtualTableView::MaterializationMode::CellWidgets);
            view->setPanes(panes(false));
            view->expand(parent);
            view->scrollTo(parent);
            view->flushPendingRelayout();
            view->setCurrentIndex(parent);
            view->setFocus();
            status->setText(selectedMode == 0 ? QStringLiteral("Row widgets") : QStringLiteral("Cell widgets"));
            record({{QStringLiteral("case"), QStringLiteral("accessibility-ready")},
                    {QStringLiteral("mode"), selectedMode},
                    {QStringLiteral("parentExpanded"), view->isExpanded(parent)},
                    {QStringLiteral("visibleRows"), int(view->visibleRowCount())},
                    {QStringLiteral("current"), view->currentIndex().data().toString()}});
        };
        QObject::connect(mode, QOverload<int>::of(&QComboBox::activated), view,
                         [view, resetAccessibility](int selectedMode) {
            QTimer::singleShot(0, view, [resetAccessibility, selectedMode]() { resetAccessibility(selectedMode); });
        });
        QTimer::singleShot(0, view, [resetAccessibility]() { resetAccessibility(0); });
    } else {
        watch.start();
    }
    window.resize(940, 560);
    window.show();
    const int result = app.exec();
    if (accessibilityOnly) {
        app.removeEventFilter(&observer);
        view->setModel(nullptr);
        return result;
    }
    const bool passed = accepted.size() == 6 && !failed;
    record({{QStringLiteral("acceptedCases"), accepted.size()}, {QStringLiteral("expectedCases"), 6},
            {QStringLiteral("failedAttempt"), failed}, {QStringLiteral("summaryPassed"), passed}});
    app.removeEventFilter(&observer);
    view->setModel(nullptr);
    return result != 0 ? result : passed ? 0 : 1;
}
