#include "example.h"
#include "styled_headers.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <virtualitemviews/headerwidgetadapter.h>
#include <QPointer>

namespace table_custom_header {

class HeaderSection : public QWidget
{
public:
    explicit HeaderSection(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *title = nullptr;
    QPushButton *sort = nullptr;
    int logicalColumn = -1;
private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 0, 6, 0);
        title = new QLabel(this);
        sort = new QPushButton(QStringLiteral("排序"), this);
        sort->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(title, 1);
        layout->addWidget(sort);
    }
};
class HeaderAdapter : public viv::HeaderWidgetAdapter
{
public:
    explicit HeaderAdapter(viv::VirtualTableView &view) : m_view(view) {}
    void setLabelModel(QAbstractItemModel *model) override { m_model = model; }
    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        auto *section = new HeaderSection(parent);
        QObject::connect(section->sort, &QPushButton::clicked, section, [this, section] {
            // Header widgets are also reused; read the logical column saved by the latest bind.
            if (section->logicalColumn >= 0)
                m_view.sortByColumn(section->logicalColumn, Qt::AscendingOrder);
        });
        return section;
    }
    void bindSection(QWidget *widget, int logicalIndex) override
    {
        auto *section = static_cast<HeaderSection *>(widget);
        section->logicalColumn = logicalIndex;
        section->title->setText(m_model ? m_model->headerData(logicalIndex, Qt::Horizontal).toString() : QString());
    }
    void unbindSection(QWidget *widget, int) override
    {
        auto *section = static_cast<HeaderSection *>(widget);
        section->logicalColumn = -1;
        section->title->clear();
    }
private:
    viv::VirtualTableView &m_view;
    QPointer<QAbstractItemModel> m_model;
};

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
        auto *description = new QLabel(QStringLiteral("19 自定义表头控件：每个可见列头是包含标题和排序按钮的 QWidget，支持冻结区复用"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        m_customCorner = new QCheckBox(QStringLiteral("左上角使用全选按钮"), this);
        layout->addWidget(m_customCorner);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
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
        // Replace the default header's section adapter directly; true transfers ownership.
        // The header supplies the current model and geometry and rebuilds frozen pane clones.
        m_view->horizontalHeader()->setAdapter(new HeaderAdapter(*m_view), true);
        m_view->setHeaderHeight(48);
        m_view->setColumnDragEnabled(true);
        m_view->setSortingEnabled(true);
        m_view->setFrozenColumns({0});

        // The default corner shares the built-in header style, without a separate adapter.
        connect(m_customCorner, &QCheckBox::toggled, this, [this](bool enabled) {
            if (!enabled) {
                // nullptr restores the default; the view releases the old custom widget.
                m_view->setHeaderCornerWidget(nullptr);
                return;
            }
            auto *button = new QPushButton(QStringLiteral("全选"));
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QPushButton::clicked, m_view, [this] {
                // Corner events belong to the application; select the model's full range.
                const QItemSelection all(m_model->index(0, 0),
                    m_model->index(m_model->rowCount() - 1, m_model->columnCount() - 1));
                m_view->selectionModel()->select(all, QItemSelectionModel::ClearAndSelect);
            });
            // The view owns the widget and tracks both header sizes and visibility.
            m_view->setHeaderCornerWidget(button);
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *m_customCorner = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    auto *tabs = new QTabWidget(parent);
    tabs->addTab(table_header_states::createExample(tabs), QStringLiteral("表头样式与绘制"));
    tabs->addTab(new Example(tabs), QStringLiteral("组合控件与 corner"));
    return tabs;
}

} // namespace table_custom_header
