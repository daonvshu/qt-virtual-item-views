#include <virtualitemviews/accessibility.h>
#include <virtualitemviews/virtualtreetableview.h>

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <functional>

namespace {

class InputEditor : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    QPersistentModelIndex boundIndex;
    std::function<void(InputEditor *, const QString &, const QString &)> report;

protected:
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        const QString preedit = event->preeditString();
        const QString commit = event->commitString();
        QLineEdit::inputMethodEvent(event);
        if (report)
            report(this, preedit, commit);
    }
};

class InputRow : public QWidget
{
public:
    explicit InputRow(QWidget *parent) : QWidget(parent)
    {
        for (int column = 0; column < 3; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(2, 2, 2, 2);
            if (column == 1) {
                editor = new InputEditor(host);
                layout->addWidget(editor);
            } else {
                labels[column] = new QLabel(host);
                layout->addWidget(labels[column]);
            }
        }
    }
    InputEditor *editor = nullptr;
    QLabel *labels[3] = {};
};

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    std::function<void(InputEditor *, const QString &, const QString &)> report;
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *widget = new InputRow(parent);
        widget->editor->report = report;
        return widget;
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<InputRow *>(widget);
        row->labels[0]->setText(index.data().toString());
        row->labels[2]->setText(index.siblingAtColumn(2).data().toString());
        row->editor->boundIndex = index.siblingAtColumn(1);
        row->editor->setText(row->editor->boundIndex.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(0, 36); }
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    std::function<void(InputEditor *, const QString &, const QString &)> report;
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() == 1 ? 1 : 0;
    }
    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *editor = new InputEditor(parent);
            editor->report = report;
            return editor;
        }
        return new QLabel(parent);
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (index.column() == 1) {
            auto *editor = static_cast<InputEditor *>(widget);
            editor->boundIndex = index;
            editor->setText(index.data().toString());
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const bool preparationSelfTest = app.arguments().contains(QStringLiteral("--prepare-self-test"));
    bool preparationPassed = false;
    viv::installAccessibilityFactory();
    QWidget window;
    window.setWindowTitle(QStringLiteral("VirtualTreeTableView - Native Input Acceptance"));
    auto *layout = new QVBoxLayout(&window);
    auto *controls = new QHBoxLayout;
    auto *mode = new QComboBox(&window);
    mode->addItems({QStringLiteral("Row widgets"), QStringLiteral("Cell widgets")});
    auto *prepare = new QPushButton(QStringLiteral("Prepare editor"), &window);
    auto *migrationTrigger = new QComboBox(&window);
    migrationTrigger->addItems({QStringLiteral("Preedit (750 ms)"), QStringLiteral("Timed (5 s)")});
    controls->addWidget(mode);
    controls->addWidget(migrationTrigger);
    controls->addWidget(prepare);
    controls->addStretch();
    layout->addLayout(controls);
    auto *status = new QLabel(QStringLiteral("Pending native input"), &window);
    status->setWordWrap(true);
    layout->addWidget(status);
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Node"), QStringLiteral("Draft"),
                                     QStringLiteral("State")});
    auto *parent = new QStandardItem(QStringLiteral("parent"));
    for (int child = 0; child < 100; ++child)
        parent->appendRow({new QStandardItem(QStringLiteral("child-%1").arg(child)),
                          new QStandardItem(QStringLiteral("draft")),
                          new QStandardItem(QStringLiteral("ready"))});
    model.appendRow({parent, new QStandardItem(QStringLiteral("draft")),
                     new QStandardItem(QStringLiteral("expanded"))});
    auto *view = new viv::VirtualTreeTableView(&window);
    layout->addWidget(view, 1);
    view->setModel(&model);
    view->setUniformItemHeight(36);
    view->setColumnWidth(0, 220);
    view->setColumnWidth(1, 260);
    view->setColumnWidth(2, 160);
    view->setFrozenColumns({0});
    view->setFrozenRows(1);
    view->setOverscan(0, 0);
    view->expand(model.index(0, 0));
    const QPersistentModelIndex source(model.index(1, 1, model.index(0, 0)));
    QPointer<InputEditor> active;
    QSet<int> accepted;
    bool sawPreedit = false;
    bool composing = false;
    bool migrated = false;
    bool migrationKept = false;
    bool failedAttempt = false;
    quint64 preparation = 0;
    int preparedMode = 0;
    bool timedMigration = false;
    const auto sourceEditor = [&]() -> InputEditor * {
        QWidget *widget = preparedMode == 0 ? view->widgetForIndex(QModelIndex(source).siblingAtColumn(0))
                                           : view->cellWidget(source);
        return preparedMode == 0 && widget ? static_cast<InputRow *>(widget)->editor
                                          : static_cast<InputEditor *>(widget);
    };
    const auto record = [&](const QJsonObject &value) {
        QTextStream output(stdout);
        output << "NATIVE_INPUT_EVENT "
               << QJsonDocument(value).toJson(QJsonDocument::Compact) << '\n';
        output.flush();
    };
    const auto migrateEditor = [&]() {
        const QPointer<InputEditor> before(active);
        const QString draft = active ? active->text() : QString();
        view->setFrozenColumns({0, 1});
        view->flushPendingRelayout();
        migrated = true;
        migrationKept = before && before == active && active->boundIndex == source
            && active->text() == draft && QApplication::focusWidget() == active
            && sourceEditor() == active;
        status->setText(QStringLiteral("Editor migration: %1")
            .arg(migrationKept ? QStringLiteral("kept") : QStringLiteral("failed")));
        record({{QStringLiteral("migrationKept"), migrationKept},
                {QStringLiteral("mode"), preparedMode},
                {QStringLiteral("migrationTrigger"), timedMigration ? QStringLiteral("timer")
                                                                     : QStringLiteral("preedit")},
                {QStringLiteral("preeditObserved"), sawPreedit}});
    };
    const auto report = [&](InputEditor *editor, const QString &preedit, const QString &commit) {
        if (editor != active)
            return;
        composing = !preedit.isEmpty();
        record({{QStringLiteral("mode"), preparedMode},
                {QStringLiteral("preedit"), preedit}, {QStringLiteral("commit"), commit},
                {QStringLiteral("boundToSource"), editor->boundIndex == source},
                {QStringLiteral("focused"), QApplication::focusWidget() == editor}});
        if (!preedit.isEmpty() && !sawPreedit) {
            sawPreedit = true;
            if (!timedMigration) {
                const quint64 request = preparation;
                QTimer::singleShot(750, view, [&, request]() {
                    if (preparation != request || !active || !composing)
                        return;
                    migrateEditor();
                });
            }
        }
        if (!commit.isEmpty()) {
            const bool passed = (sawPreedit || timedMigration) && migrated && migrationKept && active
                && editor->boundIndex == source && QApplication::focusWidget() == editor
                && editor->text().contains(commit);
            if (passed)
                accepted.insert(preparedMode);
            else
                failedAttempt = true;
            status->setText(passed
                ? (timedMigration ? QStringLiteral("PASS: commit after timed migration")
                                  : QStringLiteral("PASS: native input and editor identity"))
                : (!migrated ? QStringLiteral("INCOMPLETE: migration not triggered before commit")
                             : QStringLiteral("FAIL: editor identity, focus or commit")));
            record({{QStringLiteral("passed"), passed}, {QStringLiteral("mode"), preparedMode},
                    {QStringLiteral("preeditObserved"), sawPreedit},
                    {QStringLiteral("migrated"), migrated},
                    {QStringLiteral("migrationKept"), migrationKept},
                    {QStringLiteral("migrationTrigger"), timedMigration ? QStringLiteral("timer")
                                                                         : QStringLiteral("preedit")},
                    {QStringLiteral("text"), editor->text()}});
        }
    };
    auto *rows = new RowAdapter;
    auto *cells = new CellAdapter;
    rows->report = report;
    cells->report = report;
    view->setTableAdapter(rows, true);
    view->setCellAdapter(cells, true);
    QObject::connect(prepare, &QPushButton::clicked, view, [&]() {
        ++preparation;
        active.clear();
        sawPreedit = composing = migrated = migrationKept = false;
        preparedMode = mode->currentIndex();
        timedMigration = migrationTrigger->currentIndex() == 1;
        view->setMaterializationMode(preparedMode == 0
            ? viv::VirtualTableView::MaterializationMode::RowWidgets
            : viv::VirtualTableView::MaterializationMode::CellWidgets);
        view->setFrozenColumns({0});
        view->scrollTo(source);
        view->flushPendingRelayout();
        active = sourceEditor();
        if (!active) {
            failedAttempt = true;
            status->setText(QStringLiteral("FAIL: editor missing"));
            return;
        }
        active->clear();
        active->setFocus(Qt::OtherFocusReason);
        view->setCurrentIndex(source);
        status->setText(timedMigration ? QStringLiteral("Editor ready; migration in 5 s")
                                      : QStringLiteral("Editor ready"));
        if (timedMigration) {
            const quint64 request = preparation;
            QTimer::singleShot(5000, Qt::PreciseTimer, view, [&, request]() {
                if (preparation == request && active)
                    migrateEditor();
            });
        }
    });
    window.resize(820, 540);
    window.show();
    QTimer preparationCheckTimer;
    int selfTestMode = 0;
    QPointer<InputEditor> preparedEditor;
    if (preparationSelfTest) {
        preparationCheckTimer.setInterval(5200);
        preparationCheckTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&preparationCheckTimer, &QTimer::timeout, view, [&]() {
            const bool kept = migrated && migrationKept && preparedEditor
                && preparedEditor == active && active->boundIndex == source
                && sourceEditor() == active && QApplication::focusWidget() == active;
            preparationPassed = preparationPassed && kept && !failedAttempt;
            record({{QStringLiteral("editorKeptAfterMigration"), kept},
                    {QStringLiteral("mode"), selfTestMode}});
            if (++selfTestMode == 2) {
                preparationCheckTimer.stop();
                app.exit(preparationPassed ? 0 : 1);
                return;
            }
            mode->setCurrentIndex(selfTestMode);
            prepare->click();
            preparedEditor = active;
        });
        QTimer::singleShot(0, view, [&]() {
            preparationPassed = true;
            migrationTrigger->setCurrentIndex(1);
            prepare->click();
            preparedEditor = active;
            preparationCheckTimer.start();
        });
    }
    const int result = app.exec();
    if (preparationSelfTest) {
        record({{QStringLiteral("preparationSummaryPassed"), preparationPassed}});
        view->setModel(nullptr);
        return result != 0 ? result : preparationPassed ? 0 : 1;
    }
    const bool passed = accepted.size() == 2 && !failedAttempt;
    record({{QStringLiteral("summaryPassed"), passed},
            {QStringLiteral("acceptedModes"), accepted.size()},
            {QStringLiteral("failedAttempt"), failedAttempt}});
    view->setModel(nullptr);
    return result != 0 ? result : passed ? 0 : 1;
}
