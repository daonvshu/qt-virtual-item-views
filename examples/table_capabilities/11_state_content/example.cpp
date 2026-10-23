#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace table_state_content {

class ActionCell : public QWidget
{
public:
    explicit ActionCell(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *title = nullptr;
    QPushButton *action = nullptr;
private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        title = new QLabel(this);
        action = new QPushButton(QStringLiteral("标记"), this);
        // Keep arrow-key navigation focus on the table when an inline action is clicked.
        action->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(title, 1);
        layout->addWidget(action);
    }
};
class CellAdapter : public viv::CellWidgetAdapter
{
public:
    explicit CellAdapter(viv::VirtualTableView &view) : m_view(view) {}
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *cell = new ActionCell(parent);
        QObject::connect(cell->action, &QPushButton::clicked, cell, [this, cell] {
            // Query the current binding when the event occurs; do not capture the first bound index.
            const QModelIndex index = m_view.cellIndexForWidget(cell);
            if (index.isValid())
                m_view.model()->setData(index, index.data().toString() + QStringLiteral(" ✓"));
        });
        return cell;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *cell = static_cast<ActionCell *>(widget);
        // Reserve the button slot only in the action column to keep its title stable on hover.
        // Reset on every bind because pooled cells can be reused in another column.
        auto policy = cell->action->sizePolicy();
        policy.setRetainSizeWhenHidden(index.column() == 2);
        cell->action->setSizePolicy(policy);
        cell->title->setText(index.data().toString());
    }
    void visualStateChanged(QWidget *widget, const QModelIndex &index) override
    {
        // Only update content visibility here; leave state backgrounds and model check values unchanged.
        const auto state = m_view.visualState(index);
        static_cast<ActionCell *>(widget)->action->setVisible(index.column() == 2 && (state.hovered || state.selected));
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        auto *cell = static_cast<ActionCell *>(widget);
        cell->title->clear();
        cell->action->hide();
    }
private:
    viv::VirtualTableView &m_view;
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
        auto *description = new QLabel(QStringLiteral("11 状态关联控件：鼠标悬停或选中行时显示第 3 列操作按钮，业务数据与视觉状态分开"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // The view owns this adapter so it remains available while widgets are being unbound.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter(*m_view), true);
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
        m_view->setVisualStateCornerRadius(6);
        m_view->setColumnWidth(2, 240);
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_state_content
