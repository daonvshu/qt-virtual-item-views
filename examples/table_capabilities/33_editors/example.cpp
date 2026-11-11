#include "example.h"
#include "ui_editors.h"
#include "ui_popup.h"
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>
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
#include <QToolButton>
#include <memory>

namespace table_editors {

// Store the raw quantity independently of DisplayRole and EditRole.
constexpr int ValueRole = Qt::UserRole + 1;

class ButtonCell : public QWidget
{
public:
    explicit ButtonCell(QWidget *parent) : QWidget(parent) { setupUi(); }
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
        : QDialog(request.view, Qt::Popup), ui(new Ui::TableEditorPopup),
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

    std::unique_ptr<Ui::TableEditorPopup> ui;
    QPointer<viv::VirtualItemView> owner;
    QPointer<QAbstractItemModel> sourceModel;
    QPersistentModelIndex target;
};

class Adapter : public viv::CellWidgetAdapter
{
public:
    explicit Adapter(viv::VirtualTableView *owner) : view(owner) {}

    viv::WidgetType cellWidgetType(const QModelIndex &index) const override
    {
        return index.column() == 2 ? 1 : 0;
    }

    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *cell = new ButtonCell(parent);
            QObject::connect(cell->button, &QToolButton::clicked, view, [this, cell] {
                // Resolve the current binding at click time: the cell may have been recycled.
                // Explicit editing bypasses gesture flags, but still checks model permissions.
                view->edit(view->indexForWidget(cell));
            });
            return cell;
        }
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (index.column() == 2) {
            // Format only for display; keep the model value numeric.
            const int value = index.data(ValueRole).toInt();
            static_cast<ButtonCell *>(widget)->label->setText(
                QStringLiteral("数量：%1 件").arg(value));
        } else {
            static_cast<QLabel *>(widget)->setText(index.data().toString());
        }
    }

    viv::EditTriggers editTriggers(const QModelIndex &index, viv::EditTriggers defaults) const override
    {
        // Per-cell policy can also inspect roles, model flags, or business permissions.
        if (index.column() == 0)
            return viv::EditTrigger::SingleClicked | viv::EditTrigger::EditKeyPressed;
        if (index.column() == 1)
            return defaults;
        if (index.column() == 4)
            return viv::EditTrigger::SingleClicked;
        return viv::EditTrigger::NoEditTriggers;
    }

    bool editRequest(const viv::EditRequestContext &request) override
    {
        if (request.binding.logicalColumn != 4)
            return false; // Continue through createEditor() for all other columns.
        if (popup)
            popup->close();
        popup = new PopupEditor(request);
        // globalRect is already in Qt logical screen coordinates; no DPR scaling.
        popup->showAt(request.globalRect);
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
        if (context.logicalColumn == 2) {
            auto *input = new QSpinBox(parent);
            input->setRange(0, 1000000);
            return input;
        }
        return new QLineEdit(parent);
    }

    void setEditorData(QWidget *editor, const viv::ItemBindingContext &context) override
    {
        if (context.logicalColumn == 2) {
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
        // The default covers the entire cell. Column 2 demonstrates optional padding.
        viv::ItemEditorAdapter::updateEditorGeometry(
            editor, context.logicalColumn == 1 ? rect.adjusted(4, 3, -4, -3) : rect, context);
    }

    bool commitEditorData(QWidget *editor, QAbstractItemModel *model,
                          const viv::ItemBindingContext &context) override
    {
        if (context.logicalColumn == 2) {
            if (!model || !context.index.isValid())
                return false;
            auto *input = static_cast<QSpinBox *>(editor);
            // Apply pending typed text before reading value(), including explicit commits.
            input->interpretText();
            // QStandardItemModel emits dataChanged; bindCellWidget formats the new value.
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
    viv::VirtualTableView *view;
    QPointer<PopupEditor> popup;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableEditorsPage)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi() { ui->setupUi(this); }

    void setupExample()
    {
        auto *model = new QStandardItemModel(80, 5, ui->view);
        model->setHorizontalHeaderLabels({QStringLiteral("Single click"), QStringLiteral("Double click / F2"),
                                          QStringLiteral("Cell button"), QStringLiteral("External action"), QStringLiteral("Popup request")});
        for (int row = 0; row < model->rowCount(); ++row) {
            for (int column = 0; column < model->columnCount(); ++column) {
                if (column == 2) {
                    // This column intentionally has no DisplayRole/EditRole value.
                    model->setData(model->index(row, column), (row + 1) * 10, ValueRole);
                } else {
                    model->setData(model->index(row, column), QStringLiteral("Value %1:%2").arg(row + 1).arg(column + 1));
                }
                if (row == model->rowCount() - 1)
                    model->item(row, column)->setEditable(false);
            }
        }
        // CellWidgets supplies the editor callbacks through CellWidgetAdapter.
        // RowWidgets uses the same hooks inherited by TableWidgetAdapter.
        ui->view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new Adapter(ui->view), true);
        ui->view->setModel(model);
        ui->view->setUniformItemHeight(40);
        ui->view->setDefaultColumnWidth(210);
        ui->view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
        // These are also the framework defaults; the adapter overrides columns 1, 3, 4 and 5.
        ui->view->setEditTriggers(viv::EditTrigger::DoubleClicked | viv::EditTrigger::EditKeyPressed);
        ui->view->selectionModel()->setCurrentIndex(model->index(0, 3), QItemSelectionModel::NoUpdate);

        connect(ui->editCurrent, &QPushButton::clicked, this, [this] {
            // External actions use the same lifecycle as automatic and child-button editing.
            const bool opened = ui->view->edit(ui->view->currentIndex());
            ui->status->setText(opened ? QStringLiteral("Edit request handled") : QStringLiteral("Cannot edit this cell"));
        });
        connect(ui->commit, &QPushButton::clicked, this, [this] {
            ui->status->setText(ui->view->commitEditor() ? QStringLiteral("Committed") : QStringLiteral("Rejected: enter non-empty text"));
        });
        connect(ui->cancel, &QPushButton::clicked, this, [this] {
            ui->view->closeEditor();
            ui->status->setText(QStringLiteral("Cancelled"));
        });
        connect(model, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &index) {
            const int role = index.column() == 2 ? ValueRole : Qt::EditRole;
            ui->status->setText(QStringLiteral("Committed %1:%2 = %3")
                                   .arg(index.row() + 1).arg(index.column() + 1).arg(index.data(role).toString()));
        });
    }

    std::unique_ptr<Ui::TableEditorsPage> ui;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace table_editors
