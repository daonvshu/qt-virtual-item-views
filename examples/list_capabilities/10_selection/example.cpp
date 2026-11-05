#include "ui_selection.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QStringListModel>

namespace list_selection {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListSelectionPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListSelectionPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->right->setObjectName(QStringLiteral("vivItemView"));
        ui->left->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        m_model = new QStringListModel(records, this);
        m_selection = new QItemSelectionModel(m_model, this);
        for (auto *view : {ui->left, ui->right}) {
            // Both views use the same model and selection model, so selecting in
            // either one updates the other without duplicating selection state.
            // The adapter only binds text; the framework paints hover/selection
            // backgrounds below the transparent labels, including transitions.
            view->setVisualStateBackgroundVisible(true);
            view->setVisualStateCornerRadius(8);
            view->setAdapter(new TextAdapter, true);
            view->setUniformItemHeight(36);
            view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
            view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
            view->setModel(m_model);
            // Install the shared selection model after setModel(); it must
            // refer to the exact model used by both views.
            view->setSelectionModel(m_selection);
        }
        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            using Mode = viv::VirtualItemView::SelectionMode;
            const Mode modes[] = {Mode::NoSelection, Mode::SingleSelection, Mode::MultiSelection, Mode::ExtendedSelection};
            // SelectionMode controls mouse/keyboard gestures; it does not prevent
            // direct programmatic changes through QItemSelectionModel.
            ui->left->setSelectionMode(modes[mode]);
            ui->right->setSelectionMode(modes[mode]);
        });
        connect(ui->select, &QPushButton::clicked, this, [this] {
            const QItemSelection range(m_model->index(1, 0), m_model->index(4, 0));
            // Replace the previous selection with this inclusive range.
            m_selection->select(range, QItemSelectionModel::ClearAndSelect);
            // Update the shared current index without changing the selected range.
            // The view's setCurrentIndex() would replace it in selecting modes.
            m_selection->setCurrentIndex(m_model->index(4, 0), QItemSelectionModel::NoUpdate);
            // scrollTo uses the default EnsureVisible hint here.
            ui->left->scrollTo(m_model->index(4, 0));
        });
        connect(ui->clear, &QPushButton::clicked, m_selection, &QItemSelectionModel::clearSelection);
        connect(m_selection, &QItemSelectionModel::selectionChanged, this, [this] { updateStatus(); });
        connect(m_selection, &QItemSelectionModel::currentChanged, this, [this] { updateStatus(); });
        ui->mode->setCurrentIndex(3);
        updateStatus();
    }

    void updateStatus()
    {
        ui->status->setText(QStringLiteral("当前行：%1；已选择：%2 行。选择模式约束用户手势，程序仍可直接操作选择模型。")
            .arg(m_selection->currentIndex().isValid() ? m_selection->currentIndex().row() + 1 : 0)
            .arg(m_selection->selectedIndexes().size()));
    }

    QStringListModel *m_model = nullptr;
    QItemSelectionModel *m_selection = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_selection
