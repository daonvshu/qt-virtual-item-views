#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QItemSelectionModel>

namespace table_selection {

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        // Replace old content on every bind; a widget can be reused for a different row or column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("12 选择与当前项：行号默认选整行，列头可选整列；Ctrl 追加或取消，Shift 连选。上下视图共享选择"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_mode = new QComboBox(this);
        m_mode->addItems({QStringLiteral("扩展选择"), QStringLiteral("单选"), QStringLiteral("多选"), QStringLiteral("无选择")});
        controls->addWidget(m_mode);
        m_behavior = new QComboBox(this);
        m_behavior->addItems({QStringLiteral("按行"), QStringLiteral("按格")});
        controls->addWidget(m_behavior);
        m_select = new QPushButton(QStringLiteral("程序选择 2~5 行"), this);
        controls->addWidget(m_select);
        m_query = new QPushButton(QStringLiteral("查询选择"), this);
        controls->addWidget(m_query);
        controls->addStretch();
        layout->addLayout(controls);
        auto *headerControls = new QHBoxLayout;
        m_rowHeaderSelection = new QCheckBox(QStringLiteral("点击行号选整行"), this);
        m_rowHeaderSelection->setChecked(true);
        headerControls->addWidget(m_rowHeaderSelection);
        m_columnHeaderSelection = new QCheckBox(QStringLiteral("点击列头选整列"), this);
        headerControls->addWidget(m_columnHeaderSelection);
        headerControls->addStretch();
        layout->addLayout(headerControls);
        auto *backgroundControls = new QHBoxLayout;
        m_rowHeaderBackground = new QCheckBox(QStringLiteral("行号状态背景"), this);
        m_rowHeaderBackground->setChecked(true);
        backgroundControls->addWidget(m_rowHeaderBackground);
        m_columnHeaderBackground = new QCheckBox(QStringLiteral("列头状态背景"), this);
        m_columnHeaderBackground->setChecked(true);
        backgroundControls->addWidget(m_columnHeaderBackground);
        backgroundControls->addWidget(new QLabel(QStringLiteral("关闭两项后，仅 item 显示 hover/selected 背景"), this));
        backgroundControls->addStretch();
        layout->addLayout(backgroundControls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
        m_other = new viv::VirtualTableView(this);
        layout->addWidget(m_other, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(120, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        m_other->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_other->setCellAdapter(new CellAdapter, true);
        m_other->setUniformItemHeight(40);
        m_other->setDefaultColumnWidth(150);
        m_other->setModel(m_model);
        // Views of the same model can share selection; each view still maintains its own hover state.
        auto *selection = new QItemSelectionModel(m_model, this);
        m_view->setSelectionModel(selection);
        m_other->setSelectionModel(selection);
        m_other->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        m_other->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
        // Use Ctrl and Shift with ExtendedSelection; select programmatically through selectionModel.
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        m_view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        for (auto *view : {m_view, m_other}) {
            // Header click selection is independent of body click selection behavior.
            // Row-header selection is on by default; column-header selection is opt-in.
            view->setVerticalHeaderSelectionEnabled(true);
            view->setHorizontalHeaderSelectionEnabled(false);
            // Each header axis shows its state background by default.
            // Set both to false to highlight only body items; header clicks still select.
            view->setHeaderVisualStateBackgroundVisible(Qt::Vertical, true);
            view->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, true);
        }

        connect(m_rowHeaderSelection, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setVerticalHeaderSelectionEnabled(enabled);
            m_other->setVerticalHeaderSelectionEnabled(enabled);
        });
        connect(m_columnHeaderSelection, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setHorizontalHeaderSelectionEnabled(enabled);
            m_other->setHorizontalHeaderSelectionEnabled(enabled);
        });
        connect(m_rowHeaderBackground, &QCheckBox::toggled, this, [this](bool visible) {
            m_view->setHeaderVisualStateBackgroundVisible(Qt::Vertical, visible);
            m_other->setHeaderVisualStateBackgroundVisible(Qt::Vertical, visible);
        });
        connect(m_columnHeaderBackground, &QCheckBox::toggled, this, [this](bool visible) {
            m_view->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, visible);
            m_other->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, visible);
        });

        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            using Mode = viv::VirtualItemView::SelectionMode;
            const Mode modes[] = {Mode::ExtendedSelection, Mode::SingleSelection, Mode::MultiSelection, Mode::NoSelection};
            m_view->setSelectionMode(modes[value]);
            m_other->setSelectionMode(modes[value]);
        });
        // Selection behavior also determines both hover and selected background scope.
        connect(m_behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            using Behavior = viv::VirtualItemView::SelectionBehavior;
            const Behavior behaviors[] = {Behavior::SelectRows, Behavior::SelectItems};
            m_view->selectionModel()->clearSelection();
            m_view->setSelectionBehavior(behaviors[value]);
            m_other->setSelectionBehavior(behaviors[value]);
        });
        connect(m_select, &QPushButton::clicked, this, [this] {
            const QItemSelection range(m_model->index(1, 0), m_model->index(4, m_model->columnCount() - 1));
            m_view->selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
            // Current and selected items are separate; NoUpdate preserves the selected range.
            m_view->selectionModel()->setCurrentIndex(m_model->index(4, 0), QItemSelectionModel::NoUpdate);
        });
        connect(m_query, &QPushButton::clicked, this, [this] {
            m_status->setText(QStringLiteral("当前 row=%1，选中单元格=%2").arg(m_view->currentIndex().row()).arg(m_view->selectionModel()->selectedIndexes().size()));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    viv::VirtualTableView *m_other = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_behavior = nullptr;
    QCheckBox *m_rowHeaderSelection = nullptr;
    QCheckBox *m_columnHeaderSelection = nullptr;
    QCheckBox *m_rowHeaderBackground = nullptr;
    QCheckBox *m_columnHeaderBackground = nullptr;
    QPushButton *m_select = nullptr;
    QPushButton *m_query = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_selection
