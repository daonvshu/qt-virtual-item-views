#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
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
class Editor : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    std::function<void(const QString &, const QString &)> input;
protected:
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        const QString preedit = event->preeditString();
        const QString commit = event->commitString();
        QLineEdit::inputMethodEvent(event);
        if (input) input(preedit, commit);
    }
};

class Card : public QWidget
{
public:
    explicit Card(QWidget *parent) : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        name = new QLabel(this);
        editor = new Editor(this);
        combo = new QComboBox(this);
        combo->addItems({QStringLiteral("draft"), QStringLiteral("accepted")});
        menuButton = new QPushButton(QStringLiteral("菜单"), this);
        dragButton = new QPushButton(QStringLiteral("按住此按钮测试拖放"), this);
        layout->addWidget(name);
        layout->addWidget(editor);
        layout->addWidget(combo);
        layout->addWidget(menuButton);
        layout->addWidget(dragButton);
    }
    QPersistentModelIndex bound;
    QLabel *name;
    Editor *editor;
    QComboBox *combo;
    QPushButton *menuButton;
    QPushButton *dragButton;
};

class Adapter : public viv::WidgetAdapter
{
public:
    std::function<void(Card *)> created;
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *card = new Card(parent);
        if (created) created(card);
        return card;
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *card = static_cast<Card *>(widget);
        card->bound = index;
        card->name->setText(index.data().toString());
        card->editor->setText(index.data(Qt::UserRole).toString());
        card->combo->setCurrentIndex(index.data(Qt::UserRole + 1).toInt());
    }
    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<Card *>(widget)->bound = QPersistentModelIndex();
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(150, 190); }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    for (int row = 0; row < 120; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("项目 %1").arg(row)));
    Adapter adapter;
    QWidget window;
    window.setWindowTitle(QStringLiteral("Masonry 原生重排验收"));
    auto *layout = new QVBoxLayout(&window);
    auto *test = new QComboBox(&window);
    test->addItems({QStringLiteral("输入法"), QStringLiteral("下拉框"),
                    QStringLiteral("菜单"), QStringLiteral("原生拖放")});
    auto *prepare = new QPushButton(QStringLiteral("准备所选测试"), &window);
    auto *status = new QLabel(QStringLiteral("选择测试后点准备。下拉框/菜单会自动打开。"), &window);
    status->setWordWrap(true);
    layout->addWidget(test);
    layout->addWidget(prepare);
    layout->addWidget(status);
    auto *view = new viv::VirtualMasonryView(&window);
    view->setCellWidthRange(100, 200);
    view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view->setColumnSpacing(12);
    view->setRowSpacing(12);
    view->setOverscan(0, 0);
    view->setDragEnabled(true);
    layout->addWidget(view, 1, Qt::AlignLeft);
    const QPersistentModelIndex source(model.index(4, 0));
    QPointer<Card> active;
    QPointer<QWidget> popup;
    QSet<int> accepted;
    quint64 request = 0;
    int prepared = -1;
    int beforeColumns = 0;
    bool pending = false;
    bool migrated = false;
    bool kept = false;
    bool preeditObserved = false;
    bool failedAttempt = false;
    const auto record = [](const QJsonObject &value) {
        QTextStream output(stdout);
        output << "NATIVE_MASONRY_EVENT " << QJsonDocument(value).toJson(QJsonDocument::Compact) << '\n';
        output.flush();
    };
    const auto sameSource = [&]() {
        return active && active->bound == source && view->widgetForIndex(source) == active.data();
    };
    const auto migrate = [&]() {
        view->setFixedWidth(360);
        view->flushPendingRelayout();
        migrated = beforeColumns != view->columnCount();
        kept = migrated && sameSource();
        if (prepared == 0) kept = kept && active->editor->hasFocus();
        if (prepared == 1 || prepared == 2)
            kept = kept && popup && popup->isVisible() && QApplication::activePopupWidget() == popup;
        record({{"case", prepared}, {"columnsBefore", beforeColumns},
                {"columnsAfter", view->columnCount()}, {"migrated", migrated}, {"sourceKept", kept}});
        status->setText(kept ? QStringLiteral("重排保持成功。现在选词提交 / 选择 accepted。")
                            : QStringLiteral("重排保持失败，请反馈日志。"));
    };
    const auto finish = [&](bool submitted) {
        if (!pending) return;
        pending = false;
        const quint64 current = request;
        const int finishedCase = prepared;
        const bool lifecycle = migrated && kept && sameSource();
        QTimer::singleShot(150, view, [&, current, finishedCase, submitted, lifecycle]() {
            if (request != current) return;
            if (active) {
                active->editor->clearFocus();
                active->combo->clearFocus();
                active->menuButton->clearFocus();
                active->dragButton->clearFocus();
            }
            view->setFocus();
            view->scrollTo(model.index(100, 0), viv::VirtualItemView::PositionAtTop);
            view->flushPendingRelayout();
            const bool cleanup = !QApplication::activePopupWidget()
                && !view->widgetForIndex(source) && !view->isItemPinned(source)
                && view->stats().pinnedWidgets == 0;
            const bool passed = lifecycle && submitted && cleanup;
            if (passed) accepted.insert(finishedCase);
            else failedAttempt = true;
            record({{"case", finishedCase}, {"migrationKept", lifecycle},
                    {"submittedToSource", submitted}, {"cleanupOk", cleanup}, {"passed", passed}});
            record({{"acceptedCases", accepted.size()}, {"expectedCases", 4},
                    {"failedAttempt", failedAttempt}, {"summaryPassed", accepted.size() == 4 && !failedAttempt}});
            status->setText(passed ? QStringLiteral("本项通过。可选择下一项并点准备。")
                                  : QStringLiteral("本项失败，请反馈日志。可点准备重试。"));
            test->setEnabled(true);
            prepare->setEnabled(true);
        });
    };
    adapter.created = [&](Card *card) {
        card->editor->input = [&, card](const QString &preedit, const QString &commit) {
            if (!pending || prepared != 0 || card != active) return;
            preeditObserved = preeditObserved || !preedit.isEmpty();
            record({{"case", 0}, {"preedit", preedit}, {"commit", commit},
                    {"boundToSource", card->bound == source}, {"preeditObserved", preeditObserved}});
            if (!commit.isEmpty()) {
                const bool submitted = sameSource() && migrated && kept;
                if (submitted) model.setData(source, card->editor->text(), Qt::UserRole);
                finish(submitted && source.data(Qt::UserRole).toString() == card->editor->text());
            }
        };
        QObject::connect(card->combo, QOverload<int>::of(&QComboBox::activated), view, [&, card](int choice) {
            if (!pending || prepared != 1 || card != active) return;
            const bool submitted = sameSource() && migrated && kept && choice == 1;
            if (submitted) model.setData(source, choice, Qt::UserRole + 1);
            finish(submitted && source.data(Qt::UserRole + 1).toInt() == 1);
        });
        QObject::connect(card->dragButton, &QPushButton::pressed, view, [&, card]() {
            if (!pending || prepared != 3 || card != active) return;
            prepare->setEnabled(false);
            test->setEnabled(false);
            const bool realButton = QApplication::mouseButtons().testFlag(Qt::LeftButton);
            bool transitioned = false;
            bool activeDrag = false;
            bool pinnedDuring = false;
            const auto transition = [&]() {
                if (transitioned) return;
                transitioned = true;
                auto *drag = view->findChild<QDrag *>();
                activeDrag = drag && drag->mimeData() && !drag->mimeData()->formats().isEmpty();
                pinnedDuring = view->isItemPinned(source);
                migrate();
                view->scrollTo(model.index(100, 0), viv::VirtualItemView::PositionAtTop);
                view->flushPendingRelayout();
                kept = kept && sameSource() && view->verticalOffset() > 0;
                QDrag::cancel();
            };
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, view, transition);
#ifdef Q_OS_WIN
            // OLE dispatches native timers while Qt timers may be suspended.
            static std::function<void()> nativeTransition;
            nativeTransition = transition;
            const HWND owner = reinterpret_cast<HWND>(view->winId());
            const UINT_PTR nativeTimer = SetTimer(owner, reinterpret_cast<UINT_PTR>(&nativeTransition), 350,
                [](HWND windowHandle, UINT, UINT_PTR id, DWORD) {
                    KillTimer(windowHandle, id);
                    if (nativeTransition) nativeTransition();
                });
#endif
            timer.start(500);
            const Qt::DropAction action = view->startDrag(source);
            timer.stop();
#ifdef Q_OS_WIN
            if (nativeTimer) KillTimer(owner, nativeTimer);
            nativeTransition = {};
#endif
            record({{"case", 3}, {"nativeMouseDown", realButton}, {"activeDragSeen", activeDrag},
                    {"pinnedDuring", pinnedDuring}, {"action", int(action)}});
            finish(realButton && transitioned && activeDrag && pinnedDuring
                   && action == Qt::IgnoreAction && model.rowCount() == 120);
        });
    };
    QTimer popupWatch;
    popupWatch.setInterval(50);
    QObject::connect(&popupWatch, &QTimer::timeout, view, [&]() {
        if (!pending || (prepared != 1 && prepared != 2) || popup || !active) return;
        QWidget *candidate = QApplication::activePopupWidget();
        bool owned = false;
        for (QWidget *owner = candidate; owner; owner = owner->parentWidget())
            if (owner == active) owned = true;
        if (!owned) return;
        popup = candidate;
        const quint64 current = request;
        record({{"case", prepared}, {"popupObserved", true}, {"migrationDelayMs", 1000}});
        QTimer::singleShot(1000, view, [&, current]() {
            if (pending && current == request) migrate();
        });
    });
    popupWatch.start();
    QObject::connect(prepare, &QPushButton::clicked, view, [&]() {
        ++request;
        prepared = test->currentIndex();
        pending = false;
        migrated = kept = preeditObserved = false;
        popup.clear();
        view->setFixedWidth(760);
        view->scrollTo(source, viv::VirtualItemView::PositionAtTop);
        view->flushPendingRelayout();
        active = static_cast<Card *>(view->widgetForIndex(source));
        beforeColumns = view->columnCount();
        if (!sameSource()) {
            status->setText(QStringLiteral("准备失败：源控件未物化。"));
            pending = true;
            finish(false);
            return;
        }
        view->setCurrentIndex(source);
        active->editor->clear();
        active->combo->setCurrentIndex(0);
        pending = true;
        const quint64 current = request;
        if (prepared == 0) {
            status->setText(QStringLiteral("5 秒后自动重排：现在输入拼音，保持候选窗，等重排成功后再选词。"));
            active->editor->setFocus();
            QTimer::singleShot(5000, view, [&, current]() {
                if (pending && request == current) migrate();
            });
        } else if (prepared == 1) {
            status->setText(QStringLiteral("下拉框自动打开：等 1 秒重排成功后，选择 accepted。"));
            active->combo->setFocus();
            active->combo->showPopup();
        } else if (prepared == 2) {
            status->setText(QStringLiteral("菜单自动打开：等 1 秒重排成功后，选择 accepted。"));
            auto *menu = new QMenu(active);
            menu->addAction(QStringLiteral("draft"));
            auto *action = menu->addAction(QStringLiteral("accepted"));
            QObject::connect(action, &QAction::triggered, view, [&, current]() {
                if (!pending || request != current) return;
                const bool submitted = migrated && kept && sameSource();
                if (submitted) model.setData(source, QStringLiteral("accepted"), Qt::UserRole + 2);
                finish(submitted && source.data(Qt::UserRole + 2).toString() == QStringLiteral("accepted"));
            });
            QObject::connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
            menu->popup(active->menuButton->mapToGlobal(QPoint(0, active->menuButton->height())));
        } else {
            status->setText(QStringLiteral("找到项目 4，按住它的拖放按钮；约半秒后自动重排、滚动并取消拖放。"));
        }
    });
    // Install callbacks before binding: setModel may materialize synchronously.
    view->setAdapter(&adapter);
    view->setModel(&model);
    view->setFixedWidth(760);
    window.resize(820, 760);
    window.show();
    return app.exec();
}


