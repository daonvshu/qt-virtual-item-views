#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>
#include <QFlags>
#include <QRect>
#include <functional>

class QWidget;
class QAbstractItemModel;

namespace viv {

class VirtualItemView;

/// Snapshot for an adapter-owned edit request. Pointers and binding.index are
/// intended for this callback; use QPointer/QPersistentModelIndex for a popup.
struct EditRequestContext
{
    ItemBindingContext binding;
    VirtualItemView *view = nullptr;
    QWidget *viewport = nullptr;
    /// Full item/cell bounds in each coordinate system, not clipped to visibility.
    /// All rectangles use Qt logical pixels; do not multiply by devicePixelRatio.
    QRect viewportRect;
    QRect viewRect;
    QRect globalRect;
    /// Call after business confirmation or dismissal. Safe to copy and call later;
    /// an expired request cannot end a newer session. Does not commit model data.
    std::function<void()> finish;
};

/// Automatic gestures; explicit edit(index) is independent of these flags.
enum class EditTrigger {
    NoEditTriggers = 0,
    SingleClicked = 0x1,
    DoubleClicked = 0x2,
    EditKeyPressed = 0x4
};
Q_DECLARE_FLAGS(EditTriggers, EditTrigger)
Q_DECLARE_OPERATORS_FOR_FLAGS(EditTriggers)

/// Optional overlay editors shared by row and cell adapters. The model must
/// expose ItemIsEditable and ItemIsEnabled. Editors are not pooled.
class VIRTUALITEMVIEWS_EXPORT ItemEditorAdapter
{
public:
    virtual ~ItemEditorAdapter() = default;
    /// Overrides view defaults per index. NoEditTriggers permits explicit editing only.
    virtual EditTriggers editTriggers(const QModelIndex &index, EditTriggers defaults) const;
    /// Called after permissions and positioning, before createEditor(). Return true
    /// to handle the request (for example with a business popup), or false to use
    /// the normal overlay editor. Defaults to false. Handled requests participate
    /// in framework cancellation; call context.finish when business editing ends.
    virtual bool editRequest(const EditRequestContext &context);
    /// Cancels an adapter-owned request without committing. Close/reject the popup
    /// here; defer its deletion until the current event returns. May be called
    /// during editRequest() if a nested event invalidates the request. The index
    /// may be invalid during teardown. Defaults to no action.
    virtual void cancelEditRequest(const ItemBindingContext &context);
    /// Creates an editor under the supplied clipping host. Null disables editing.
    /// Composite editors should set a focus proxy to their input widget.
    virtual QWidget *createEditor(QWidget *parent, const ItemBindingContext &context);
    /// Loads EditRole into the editor's Qt USER property by default.
    virtual void setEditorData(QWidget *editor, const ItemBindingContext &context);
    /// Uses the entire cell in parent coordinates by default. Tree adapters can
    /// inset rect by context.indentationWidth and/or context.branchWidth to leave
    /// tree decoration uncovered; clamp the inset to rect.width().
    virtual void updateEditorGeometry(QWidget *editor, const QRect &rect,
                                      const ItemBindingContext &context);
    /// Writes the Qt USER property to EditRole. False keeps the editor open.
    virtual bool commitEditorData(QWidget *editor, QAbstractItemModel *model,
                                  const ItemBindingContext &context);
    /// Defaults to deleteLater(). The index may be invalid during model teardown.
    /// Do not commit data here. Overrides should also defer deletion until the
    /// current editor event has returned, as with QStyledItemDelegate.
    virtual void destroyEditor(QWidget *editor, const ItemBindingContext &context);
};

} // namespace viv
