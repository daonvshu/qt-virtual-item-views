#include <virtualitemviews/virtualitemview.h>
#include <virtualitemviews/widgetadapter.h>
#include "transparenthost_p.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QChildEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QScopeGuard>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QTimer>

namespace viv {

// A separate delegate per session also isolates Qt's queued Return-key commit.
class ItemEditorSession final : public QStyledItemDelegate
{
public:
    ItemEditorSession(VirtualItemView *owner, const QModelIndex &target,
                      ItemEditorAdapter *ownerAdapter)
        : QStyledItemDelegate(owner), view(owner), index(target), adapter(ownerAdapter)
    {
        connect(this, &QAbstractItemDelegate::commitData, this, [this](QWidget *widget) {
            if (widget == editor && active())
                commit();
        });
        connect(this, &QAbstractItemDelegate::closeEditor, this,
                [this](QWidget *widget, QAbstractItemDelegate::EndEditHint hint) {
            if (widget != editor || !active())
                return;
            if (hint != RevertModelCache && rejected) {
                // Keep a rejected draft, but do not steal focus back from another window.
                if (editor && editor->window()->isActiveWindow())
                    editor->setFocus(Qt::OtherFocusReason);
                return;
            }
            const QPointer<VirtualItemView> guardedView(view);
            const QPersistentModelIndex previous(index);
            const bool navigate = hint == EditNextItem || hint == EditPreviousItem;
            const bool forward = hint == EditNextItem;
            QWidget *focus = QApplication::focusWidget();
            const bool hadFocus = editor && (focus == editor || editor->isAncestorOf(focus));
            guardedView->closeEditor();
            if (!guardedView)
                return;
            if (hadFocus)
                guardedView->setFocus(Qt::OtherFocusReason);
            if (!guardedView || !navigate || !previous.isValid())
                return;
            QModelIndex candidate = guardedView->adjacentEditorIndex(previous, forward);
            while (guardedView && candidate.isValid()) {
                const QPersistentModelIndex current(candidate);
                if (guardedView->edit(candidate))
                    return;
                if (!guardedView || !current.isValid() || guardedView->editorWidget())
                    return;
                candidate = guardedView->adjacentEditorIndex(current, forward);
            }
        });
        auto cancel = [this] { if (active()) view->closeEditor(); };
        // Structural changes cancel the draft before indexes/parents can disappear.
        connect(owner->model(), &QAbstractItemModel::modelAboutToBeReset, this, cancel);
        connect(owner->model(), &QAbstractItemModel::layoutAboutToBeChanged, this, cancel);
        connect(owner->model(), &QAbstractItemModel::rowsAboutToBeRemoved, this, cancel);
        connect(owner->model(), &QAbstractItemModel::columnsAboutToBeRemoved, this, cancel);
        connect(owner->model(), &QAbstractItemModel::rowsAboutToBeMoved, this, cancel);
        connect(owner->model(), &QAbstractItemModel::columnsAboutToBeMoved, this, cancel);
        connect(owner->model(), &QObject::destroyed, this, cancel);
        connect(owner->model(), &QAbstractItemModel::rowsAboutToBeInserted, this,
                [this, cancel] { if (customRequest) cancel(); });
        connect(owner->model(), &QAbstractItemModel::columnsAboutToBeInserted, this,
                [this, cancel] { if (customRequest) cancel(); });
        connect(owner->model(), &QAbstractItemModel::dataChanged, this,
                [this] { if (active() && customRequest) updateGeometry(); });
        if (owner->selectionModel()) {
            connect(owner->selectionModel(), &QItemSelectionModel::currentChanged, this,
                    [this, cancel](const QModelIndex &current) {
                if (customRequest && current != index)
                    cancel();
            });
        }
        connect(owner->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this] {
            if (!active())
                return;
            if (customRequest)
                view->closeEditor();
            else
                updateGeometry();
        });
        connect(owner->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this] {
            if (!active())
                return;
            if (customRequest)
                view->closeEditor();
            else
                updateGeometry();
        });
    }

    bool active() const { return view && view->m_editorSession == this; }

    void prepareRequest(EditRequestContext &request)
    {
        customRequest = true;
        lastContext = request.binding;
        requestRect = request.globalRect;
        const QPointer<ItemEditorSession> self(this);
        request.finish = [self] {
            if (!self || !self->active() || !self->customRequest)
                return;
            // Detach without notifying cancellation after normal business completion.
            self->customRequest = false;
            self->view->closeEditor();
        };
        qApp->installEventFilter(this);
    }

    bool start()
    {
        host = new QWidget(view->viewport());
        host->setObjectName(QStringLiteral("vivEditorHost"));
        detail::prepareTransparentHost(host);
        host->setFocusPolicy(Qt::NoFocus);
        host->hide();
        const QPointer<ItemEditorSession> self(this);
        const ItemBindingContext context = view->bindingContext(index);
        if (!self || !active() || !index.isValid())
            return false;
        lastContext = context;
        const QPointer<QWidget> created(adapter->createEditor(host, context));
        if (!self || !active()) {
            if (created)
                created->deleteLater();
            return false;
        }
        editor = created;
        if (!editor || !index.isValid()) {
            view->closeEditor();
            return false;
        }
        if (editor->parentWidget() != host)
            editor->setParent(host);
        connect(editor, &QObject::destroyed, this, [this] {
            editor = nullptr;
            if (active())
                view->closeEditor();
        });
        const ItemBindingContext currentContext = view->bindingContext(index);
        if (!self || !active() || !editor || !index.isValid())
            return false;
        adapter->setEditorData(editor, currentContext);
        if (!self || !active() || !editor)
            return false;
        updateGeometry();
        if (!self || !active() || !editor)
            return false;
        installFilters(editor);
        editor->show();
        if (!self || !active() || !editor)
            return false;
        host->show();
        if (!self || !active() || !editor || !host)
            return false;
        host->raise();
        if (!self || !active() || !editor)
            return false;
        editor->setFocus(Qt::OtherFocusReason);
        return self && active() && editor;
    }

    bool commit()
    {
        if (!active() || !editor || !index.isValid() || committing)
            return false;
        const QPointer<ItemEditorSession> self(this);
        const Qt::ItemFlags flags = index.flags();
        if (!self || !active() || !editor || !index.isValid())
            return false;
        if (!(flags & Qt::ItemIsEnabled) || !(flags & Qt::ItemIsEditable)) {
            rejected = true;
            return false;
        }
        committing = true;
        const ItemBindingContext context = view->bindingContext(index);
        if (!self || !active() || !editor || !index.isValid())
            return false;
        const bool accepted = adapter->commitEditorData(editor, view->model(), context);
        if (!self)
            return accepted;
        committing = false;
        rejected = !accepted;
        return accepted;
    }

    void updateGeometry()
    {
        if (!active() || updating)
            return;
        const QPointer<ItemEditorSession> guardedSession(this);
        updating = true;
        const auto updateGuard = qScopeGuard([guardedSession] {
            if (guardedSession)
                guardedSession->updating = false;
        });
        if (customRequest) {
            const QPointer<ItemEditorSession> self(this);
            if (!index.isValid() || view->viewItemForIndex(index) < 0) {
                view->closeEditor();
                return;
            }
            const Qt::ItemFlags flags = index.flags();
            if (!self || !active())
                return;
            const QRect rect = view->editorRect(index);
            if (!self || !active())
                return;
            const QRect globalRect(view->viewport()->mapToGlobal(rect.topLeft()), rect.size());
            const ItemBindingContext context = view->bindingContext(index);
            if (!self || !active())
                return;
            if (!(flags & Qt::ItemIsEditable) || !(flags & Qt::ItemIsEnabled)
                || rect.isEmpty() || globalRect != requestRect
                || context.indentationWidth != lastContext.indentationWidth
                || context.branchWidth != lastContext.branchWidth)
                view->closeEditor();
            return;
        }
        if (!editor)
            return;
        if (!index.isValid() || view->viewItemForIndex(index) < 0) {
            view->closeEditor();
            return;
        }
        const QPointer<ItemEditorSession> self(this);
        const QRect rect = view->editorRect(index);
        if (!self || !active() || !editor || !index.isValid())
            return;
        if (rect.isEmpty()) {
            view->closeEditor();
            return;
        }
        const QRect clip = rect.intersected(view->editorClipRect(index));
        if (!self || !active() || !editor)
            return;
        // Never hide the editor during scrolling: Hide/FocusOut could commit a draft.
        // A zero-sized host clips it until it returns to the visible pane.
        const QRect hostRect = clip.isEmpty() ? QRect(rect.topLeft(), QSize(0, 0)) : clip;
        const QRect localRect(rect.topLeft() - hostRect.topLeft(), rect.size());
        const ItemBindingContext context = view->bindingContext(index);
        if (!self || !active() || !editor || !index.isValid())
            return;
        if (geometrySet && lastRect == localRect && host->geometry() == hostRect
            && lastIndentationWidth == context.indentationWidth
            && lastBranchWidth == context.branchWidth) {
            host->raise();
            return;
        }
        host->setGeometry(hostRect);
        if (!self || !active() || !editor)
            return;
        lastRect = localRect;
        geometrySet = true;
        lastIndentationWidth = context.indentationWidth;
        lastBranchWidth = context.branchWidth;
        adapter->updateEditorGeometry(editor, localRect, context);
        if (!self)
            return;
        if (active() && host)
            host->raise();
    }

    void release()
    {
        const QPointer<ItemEditorSession> self(this);
        const QPointer<QWidget> oldEditor(editor);
        const QPointer<QWidget> oldHost(host);
        // Teardown must not query model data: model destruction can already be in progress.
        ItemBindingContext context = lastContext;
        context.index = index;
        qApp->removeEventFilter(this);
        if (customRequest) {
            customRequest = false;
            adapter->cancelEditRequest(context);
        }
        if (oldEditor) {
            oldEditor->removeEventFilter(this);
            for (QWidget *child : oldEditor->findChildren<QWidget *>())
                child->removeEventFilter(this);
            adapter->destroyEditor(oldEditor, context);
            if (oldEditor)
                oldEditor->hide();
        }
        if (oldHost) {
            oldHost->hide();
            oldHost->deleteLater();
        }
        if (self)
            deleteLater();
    }

    QPointer<VirtualItemView> view;
    QPersistentModelIndex index;
    QPointer<QWidget> editor;
    bool customRequest = false;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!active())
            return false;
        if (customRequest) {
            auto *widget = qobject_cast<QWidget *>(watched);
            const bool ownerOrAncestor = widget
                && (widget == view || widget == view->viewport() || widget->isAncestorOf(view));
            const auto type = event->type();
            const bool unavailable = ownerOrAncestor
                && (type == QEvent::Hide || type == QEvent::Close
                    || type == QEvent::ParentAboutToChange || type == QEvent::Move
                    || type == QEvent::Resize
                    || (type == QEvent::EnabledChange && !view->isEnabled()));
            const QPointer<QObject> receiver(watched);
            if (unavailable || type == QEvent::ApplicationDeactivate)
                view->closeEditor();
            // A cancellation callback may also destroy the event receiver.
            return !receiver;
        }
        if (!editor)
            return false;
        if (event->type() == QEvent::ChildAdded) {
            const QPointer<QObject> child(static_cast<QChildEvent *>(event)->child());
            QTimer::singleShot(0, this, [this, child] {
                auto *widget = qobject_cast<QWidget *>(child.data());
                if (active() && editor && widget && editor->isAncestorOf(widget))
                    installFilters(widget);
            });
        }
        auto *receiver = qobject_cast<QWidget *>(watched);
        if (receiver && receiver->window() != editor->window())
            return false;
        // Route composite editor input through the root, so delegate signals always
        // refer to the session editor. Multiline inputs retain their native keys.
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if ((qobject_cast<QTextEdit *>(watched) || qobject_cast<QPlainTextEdit *>(watched))
                && (key == Qt::Key_Tab || key == Qt::Key_Backtab
                    || key == Qt::Key_Return || key == Qt::Key_Enter))
                return false;
        }
        if (watched != editor && event->type() == QEvent::Hide)
            return false;
        if (committing)
            return false;
        return QStyledItemDelegate::eventFilter(editor, event);
    }

private:
    void installFilters(QWidget *widget)
    {
        widget->installEventFilter(this);
        for (QWidget *child : widget->findChildren<QWidget *>())
            child->installEventFilter(this);
    }

    ItemBindingContext lastContext;
    ItemEditorAdapter *adapter;
    QPointer<QWidget> host;
    QRect lastRect;
    QRect requestRect;
    int lastIndentationWidth = 0;
    int lastBranchWidth = 0;
    bool geometrySet = false;
    bool updating = false;
    bool committing = false;
    bool rejected = false;
};

bool VirtualItemView::edit(const QModelIndex &index)
{
    if (m_destroying || m_closingEditor || m_editRequestActive || !isEnabled() || !index.isValid() || index.model() != model()
        || viewItemForIndex(index) < 0 || !editorAdapter())
        return false;
    const QPointer<VirtualItemView> self(this);
    const Qt::ItemFlags flags = index.flags();
    if (!self)
        return false;
    if (!(flags & Qt::ItemIsEditable) || !(flags & Qt::ItemIsEnabled))
        return false;
    if (m_editorSession && m_editorSession->index == index) {
        if (editorWidget())
            editorWidget()->setFocus(Qt::OtherFocusReason);
        return self && m_editorSession != nullptr;
    }
    const QPersistentModelIndex target(index);
    if (m_editorSession && m_editorSession->customRequest)
        closeEditor();
    else if (!commitEditor())
        return false;
    if (!self || !target.isValid() || target.model() != model())
        return false;
    ensureEditorVisible(target);
    if (!self || !target.isValid() || target.model() != model() || !editorAdapter()
        || m_editorSession || viewItemForIndex(target) < 0)
        return false;
    if (selectionModel())
        selectionModel()->setCurrentIndex(target, QItemSelectionModel::NoUpdate);
    if (!self || !target.isValid() || target.model() != model() || !editorAdapter()
        || m_editorSession)
        return false;
    const Qt::ItemFlags currentFlags = target.flags();
    if (!self || !target.isValid() || target.model() != model() || m_editorSession
        || !(currentFlags & Qt::ItemIsEditable) || !(currentFlags & Qt::ItemIsEnabled))
        return false;
    ItemEditorAdapter *const activeAdapter = editorAdapter();
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    if (!activeAdapter)
        return false;
    m_editRequestActive = true;
    m_editRequestCancelled = false;
    const auto requestGuard = qScopeGuard([self] {
        if (self)
            self->m_editRequestActive = false;
    });
    const auto requestIsCurrent = [self, target, activeAdapter, modelSerial, mappingSerial]
            (ItemEditorSession *expectedSession = nullptr) {
        return self && !self->m_destroying && !self->m_editRequestCancelled
            && self->isEnabled() && target.isValid() && target.model() == self->model()
            && self->modelChangeSerial() == modelSerial
            && self->viewMappingSerial() == mappingSerial
            && self->editorAdapter() == activeAdapter && self->m_editorSession == expectedSession;
    };
    EditRequestContext request;
    request.binding = bindingContext(target);
    if (!requestIsCurrent())
        return false;
    request.viewportRect = editorRect(target);
    if (!requestIsCurrent() || request.viewportRect.isEmpty())
        return false;
    request.view = this;
    request.viewport = viewport();
    request.viewRect = QRect(viewport()->mapTo(this, request.viewportRect.topLeft()),
                             request.viewportRect.size());
    request.globalRect = QRect(viewport()->mapToGlobal(request.viewportRect.topLeft()),
                               request.viewportRect.size());
    const QPointer<ItemEditorSession> requestedSession(
        new ItemEditorSession(this, target, activeAdapter));
    m_editorSession = requestedSession;
    requestedSession->prepareRequest(request);
    const bool handled = activeAdapter->editRequest(request);
    if (!self || !requestedSession || m_editorSession != requestedSession)
        return handled;
    if (handled) {
        if (!requestIsCurrent(requestedSession))
            closeEditor();
        return true;
    }
    // A declined request leaves no cancellation callback attached to the overlay.
    requestedSession->customRequest = false;
    qApp->removeEventFilter(requestedSession);
    if (!requestIsCurrent(requestedSession)) {
        closeEditor();
        return false;
    }
    const Qt::ItemFlags fallbackFlags = target.flags();
    if (!requestIsCurrent(requestedSession)) {
        if (self && m_editorSession == requestedSession)
            closeEditor();
        return false;
    }
    if (!(fallbackFlags & Qt::ItemIsEditable) || !(fallbackFlags & Qt::ItemIsEnabled)
        || viewItemForIndex(target) < 0) {
        closeEditor();
        return false;
    }
    return requestedSession->start();
}

bool VirtualItemView::commitEditor()
{
    if (!m_editorSession)
        return true;
    const QPointer<VirtualItemView> self(this);
    ItemEditorSession *const session = m_editorSession;
    const bool accepted = session->commit();
    if (self && accepted && m_editorSession == session)
        closeEditor();
    return accepted;
}

void VirtualItemView::closeEditor()
{
    if (m_editRequestActive)
        m_editRequestCancelled = true;
    if (!m_editorSession)
        return;
    const QPointer<VirtualItemView> self(this);
    ItemEditorSession *const session = m_editorSession;
    m_editorSession = nullptr;
    m_closingEditor = true;
    session->release();
    if (self)
        m_closingEditor = false;
}

QModelIndex VirtualItemView::editingIndex() const
{
    return m_editorSession ? QModelIndex(m_editorSession->index) : QModelIndex();
}

QWidget *VirtualItemView::editorWidget() const
{
    return m_editorSession ? m_editorSession->editor.data() : nullptr;
}

bool VirtualItemView::triggerEdit(const QModelIndex &index, EditTrigger trigger)
{
    if (!index.isValid() || index.model() != model() || !editorAdapter())
        return false;
    const QPointer<VirtualItemView> self(this);
    const QPersistentModelIndex target(index);
    ItemEditorAdapter *const adapter = editorAdapter();
    const EditTriggers triggers = adapter->editTriggers(target, m_editTriggers);
    return self && target.isValid() && editorAdapter() == adapter
        && triggers.testFlag(trigger) && edit(target);
}

ItemEditorAdapter *VirtualItemView::editorAdapter() const
{
    return adapter();
}

void VirtualItemView::ensureEditorVisible(const QModelIndex &index)
{
    scrollTo(index);
}

QRect VirtualItemView::editorRect(const QModelIndex &index) const
{
    return visualRect(index);
}

QRect VirtualItemView::editorClipRect(const QModelIndex &index) const
{
    return itemPaneRect(itemPaneForRow(viewItemForIndex(index))).intersected(viewport()->rect());
}

QModelIndex VirtualItemView::adjacentEditorIndex(const QModelIndex &index, bool forward) const
{
    const qsizetype row = viewItemForIndex(index) + (forward ? 1 : -1);
    return row >= 0 && row < viewItemCount() ? viewIndex(row) : QModelIndex();
}

void VirtualItemView::updateEditorGeometry()
{
    if (m_editorSession)
        m_editorSession->updateGeometry();
}

} // namespace viv
