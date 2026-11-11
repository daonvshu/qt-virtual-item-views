#include "example.h"
#include "ui_editors.h"
#include "ui_popup.h"
#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtualtreeview.h>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringList>
#include <QToolButton>
#include <memory>

namespace tree_editors {

// Store the raw quantity independently of DisplayRole and EditRole.
constexpr int ValueRole = Qt::UserRole + 1;
// Keep the interaction mode in model data so it survives edits and row changes.
constexpr int ModeRole = Qt::UserRole + 2;

class ButtonRow : public QWidget
{
public:
    explicit ButtonRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *label = nullptr;
    QToolButton *button = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        label = new QLabel(this);
        button = new QToolButton(this);
        button->setText(QStringLiteral("Edit"));
        layout->addWidget(label, 1);
        layout->addWidget(button);
    }
};

class PopupEditor : public QDialog
{
public:
    explicit PopupEditor(const viv::EditRequestContext &request)
        : QDialog(request.view, Qt::Popup), ui(new Ui::TreeEditorPopup),
          owner(request.view), sourceModel(request.view->model()), target(request.binding.index)
    {
        setupUi();
        setAttribute(Qt::WA_DeleteOnClose);
        ui->input->setText(target.data(Qt::EditRole).toString());
        ui->input->selectAll();
        const auto rectText = [](const QRect &rect) {
            return QStringLiteral("(%1, %2) %3 x %4")
                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
        };
        ui->coordinates->setText(QStringLiteral("viewport: %1\nview: %2\nglobal: %3")
            .arg(rectText(request.viewportRect), rectText(request.viewRect), rectText(request.globalRect)));
        ui->coordinates->setText(ui->coordinates->text()
            + QStringLiteral("\nindentation: %1 px; branch cell: %2 px")
                .arg(request.binding.indentationWidth).arg(request.binding.branchWidth));
        connect(ui->buttons, &QDialogButtonBox::accepted, this, [this] { commitValue(); });
        connect(ui->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        // Confirmation, Escape and native popup dismissal all end this request.
        // The callback is session-specific, so late signals cannot close a newer edit.
        connect(this, &QDialog::finished, this, [finish = request.finish] { finish(); });
        connect(this, &QObject::destroyed, request.view,
                [finish = request.finish] { finish(); });
    }

    void showAt(const QRect &globalCellRect)
    {
        adjustSize();
        QPoint position(globalCellRect.left(), globalCellRect.bottom() + 1);
        QScreen *screen = QGuiApplication::screenAt(globalCellRect.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            setMaximumSize(available.size());
            resize(size().boundedTo(available.size()));
            if (position.y() + height() > available.bottom() + 1)
                position.setY(globalCellRect.top() - height());
            position.setX(qBound(available.left(), position.x(),
                                qMax(available.left(), available.right() - width() + 1)));
            position.setY(qBound(available.top(), position.y(),
                                qMax(available.top(), available.bottom() - height() + 1)));
        }
        move(position);
        show();
        ui->input->setFocus();
    }

private:
    void setupUi() { ui->setupUi(this); }

    void commitValue()
    {
        if (!owner || !sourceModel || !target.isValid() || owner->model() != sourceModel.data()) {
            reject();
            return;
        }
        if (ui->input->text().trimmed().isEmpty()) {
            ui->validation->setText(QStringLiteral("Text is required"));
            return;
        }
        const Qt::ItemFlags flags = target.flags();
        if (!(flags & Qt::ItemIsEditable) || !(flags & Qt::ItemIsEnabled)) {
            ui->validation->setText(QStringLiteral("This cell is no longer editable"));
            return;
        }
        const QPointer<PopupEditor> self(this);
        const bool saved = sourceModel->setData(target, ui->input->text(), Qt::EditRole);
        if (!self)
            return;
        if (saved)
            accept();
        else
            ui->validation->setText(QStringLiteral("The model rejected this value"));
    }

    std::unique_ptr<Ui::TreeEditorPopup> ui;
    QPointer<viv::VirtualItemView> owner;
    QPointer<QAbstractItemModel> sourceModel;
    QPersistentModelIndex target;
};

class Adapter : public viv::WidgetAdapter
{
public:
    explicit Adapter(viv::VirtualTreeView *owner, QComboBox *extent)
        : view(owner), editorExtent(extent) {}

    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.data(ModeRole).toInt() == 2 ? 1 : 0;
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        // Match the fixed row height used by this example.
        return QSize(480, 40);
    }

    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *rowWidget = new ButtonRow(parent);
            QObject::connect(rowWidget->button, &QToolButton::clicked, view, [this, rowWidget] {
                // Resolve the current binding at click time: the row may have been recycled.
                // Explicit editing bypasses gesture flags, but still checks model permissions.
                view->edit(view->indexForWidget(rowWidget));
            });
            return rowWidget;
        }
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        if (index.data(ModeRole).toInt() == 2) {
            // Format only for display; keep the model value numeric.
            const int value = index.data(ValueRole).toInt();
            static_cast<ButtonRow *>(widget)->label->setText(
                QStringLiteral("数量：%1 件").arg(value));
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }

    viv::EditTriggers editTriggers(const QModelIndex &index, viv::EditTriggers defaults) const override
    {
        // Per-item policy can also inspect roles, model flags, or business permissions.
        if (index.data(ModeRole).toInt() == 0)
            return viv::EditTrigger::SingleClicked | viv::EditTrigger::EditKeyPressed;
        if (index.data(ModeRole).toInt() == 1)
            return defaults;
        if (index.data(ModeRole).toInt() == 4)
            return viv::EditTrigger::SingleClicked;
        return viv::EditTrigger::NoEditTriggers;
    }

    bool editRequest(const viv::EditRequestContext &request) override
    {
        if (request.binding.index.data(ModeRole).toInt() != 4)
            return false; // Continue through createEditor() for all other items.
        if (popup)
            popup->close();
        popup = new PopupEditor(request);
        // globalRect is already in Qt logical screen coordinates; no DPR scaling.
        // The same widths can also align a popup to the chosen part of a tree cell.
        popup->showAt(editArea(request.globalRect, request.binding));
        return true; // Business submission, with framework-managed cancellation.
    }

    void cancelEditRequest(const viv::ItemBindingContext &) override
    {
        // The framework handles scroll, model, geometry and view lifecycle changes.
        // Clear first: reject() emits finished synchronously and ends the request.
        const QPointer<PopupEditor> closing = popup;
        popup = nullptr;
        if (closing)
            closing->reject();
    }

    QWidget *createEditor(QWidget *parent, const viv::ItemBindingContext &context) override
    {
        // The framework owns this editor. No stacked layout or manual pin is needed.
        if (context.index.data(ModeRole).toInt() == 2) {
            auto *input = new QSpinBox(parent);
            input->setRange(0, 1000000);
            return input;
        }
        return new QLineEdit(parent);
    }

    void setEditorData(QWidget *editor, const viv::ItemBindingContext &context) override
    {
        if (context.index.data(ModeRole).toInt() == 2) {
            // Load the raw quantity, not the formatted label text.
            auto *input = static_cast<QSpinBox *>(editor);
            const QSignalBlocker blocker(input);
            input->setValue(context.index.data(ValueRole).toInt());
            input->selectAll();
            return;
        }
        // The base implementation reads EditRole into QLineEdit's USER property.
        viv::ItemEditorAdapter::setEditorData(editor, context);
        static_cast<QLineEdit *>(editor)->selectAll();
    }

    void updateEditorGeometry(QWidget *editor, const QRect &rect,
                              const viv::ItemBindingContext &context) override
    {
        // rect is the full item/cell in editor-parent coordinates. Insets are logical
        // pixels and remain independent of scrolling, pane clipping and screen DPI.
        viv::ItemEditorAdapter::updateEditorGeometry(editor, editArea(rect, context), context);
    }

    bool commitEditorData(QWidget *editor, QAbstractItemModel *model,
                          const viv::ItemBindingContext &context) override
    {
        if (context.index.data(ModeRole).toInt() == 2) {
            if (!model || !context.index.isValid())
                return false;
            auto *input = static_cast<QSpinBox *>(editor);
            // Apply pending typed text before reading value(), including explicit commits.
            input->interpretText();
            // QStandardItemModel emits dataChanged; bindWidget formats the new value.
            return model->setData(context.index, input->value(), ValueRole);
        }
        auto *input = static_cast<QLineEdit *>(editor);
        if (input->text().trimmed().isEmpty()) {
            input->setPlaceholderText(QStringLiteral("Text is required"));
            return false;
        }
        // The default writes EditRole and returns model->setData()'s result.
        return viv::ItemEditorAdapter::commitEditorData(editor, model, context);
    }

    // destroyEditor() defaults to deleteLater(); override it only for custom cleanup.

private:
    QRect editArea(const QRect &rect, const viv::ItemBindingContext &context) const
    {
        const int extent = editorExtent ? editorExtent->currentIndex() : 0;
        int left = extent == 0 ? 0 : context.indentationWidth;
        if (extent == 2)
            left += context.branchWidth;
        // Deep nodes or narrow columns may leave no content width.
        left = qBound(0, left, qMax(0, rect.width()));
        return rect.adjusted(left, 0, 0, 0);
    }

    viv::VirtualTreeView *view;
    QPointer<QComboBox> editorExtent;
    QPointer<PopupEditor> popup;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeEditorsPage)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi() { ui->setupUi(this); }

    void setupExample()
    {
        // Plain model items store both the interaction policy and editable value.
        auto *model = new QStandardItemModel(ui->view);
        const auto makeItem = [](int row, const QString &prefix, bool readOnly) {
            const int mode = row % 5;
            const QStringList modes{QStringLiteral("Single click"), QStringLiteral("Double click / F2"),
                                    QStringLiteral("Row button"), QStringLiteral("External action"),
                                    QStringLiteral("Popup request")};
            auto *item = new QStandardItem;
            item->setData(mode, ModeRole);
            if (mode == 2)
                item->setData((row + 1) * 10, ValueRole);
            else
                item->setText(QStringLiteral("%1 / %2 / %3%4").arg(prefix).arg(row + 1)
                                  .arg(modes.at(mode), readOnly ? QStringLiteral(" (read only)") : QString()));
            item->setEditable(!readOnly);
            return item;
        };
        for (int group = 0; group < 4; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("Group %1").arg(group + 1));
            branch->setData(-1, ModeRole);
            // Group labels remain read-only; double-clicking them only toggles expansion.
            branch->setEditable(false);
            model->appendRow(branch);
            for (int row = 0; row < 11; ++row)
                branch->appendRow(makeItem(row, branch->text(), row == 10));
        }
        // The view owns the row adapter; editor hooks are inherited from ItemEditorAdapter.
        ui->view->setAdapter(new Adapter(ui->view, ui->editorExtent), true);
        // Fixed row height keeps this chapter focused on editing rather than measurement.
        ui->view->setUniformItemHeight(40);
        // QObject parenting owns the model; model signals rebind the visible row widgets.
        ui->view->setModel(model);
        // Default double-click/F2 gestures are overridden by each item's ModeRole.
        ui->view->setEditTriggers(viv::EditTrigger::DoubleClicked | viv::EditTrigger::EditKeyPressed);
        // Expand through the view so the visible mapping and branch controls stay in sync.
        ui->view->expand(model->index(0, 0));
        // Set a current item for the external button without opening an editor.
        ui->view->selectionModel()->setCurrentIndex(model->index(3, 0, model->index(0, 0)), QItemSelectionModel::NoUpdate);

        connect(ui->editorExtent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            // Choose the policy before opening the next editor or popup.
            ui->view->closeEditor();
            ui->status->setText(QStringLiteral("Coverage changed; open an item to edit"));
        });
        connect(ui->expand, &QPushButton::clicked, this, [this, model] {
            // Rebuild the visible hierarchy through the view, not through display widgets.
            for (int group = 0; group < model->rowCount(); ++group)
                ui->view->expand(model->index(group, 0));
        });
        connect(ui->collapse, &QPushButton::clicked, this, [this] {
            // The framework cancels editors whose target disappears from the visible mapping.
            ui->view->collapseAll();
        });

        connect(ui->editCurrent, &QPushButton::clicked, this, [this] {
            // External actions use the same lifecycle as automatic and child-button editing.
            const bool opened = ui->view->edit(ui->view->currentIndex());
            ui->status->setText(opened ? QStringLiteral("Edit request handled") : QStringLiteral("Cannot edit this item"));
        });
        connect(ui->commit, &QPushButton::clicked, this, [this] {
            ui->status->setText(ui->view->commitEditor() ? QStringLiteral("Committed") : QStringLiteral("Not committed: invalid input or a business Popup owns submission"));
        });
        connect(ui->cancel, &QPushButton::clicked, this, [this] {
            ui->view->closeEditor();
            ui->status->setText(QStringLiteral("Cancelled"));
        });
        connect(model, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &index) {
            const int role = index.data(ModeRole).toInt() == 2 ? ValueRole : Qt::EditRole;
            ui->status->setText(QStringLiteral("Committed [%1] row %2 = %3")
                .arg(index.parent().isValid() ? index.parent().data().toString() : QStringLiteral("root"))
                .arg(index.row() + 1).arg(index.data(role).toString()));
        });
    }

    std::unique_ptr<Ui::TreeEditorsPage> ui;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace tree_editors
