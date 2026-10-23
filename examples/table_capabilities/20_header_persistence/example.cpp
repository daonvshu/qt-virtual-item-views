#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace table_header_persistence {

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
        auto *description = new QLabel(QStringLiteral("20 列布局保存与恢复：保存列宽、顺序、隐藏、冻结设置，恢复前检查结果"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_save = new QPushButton(QStringLiteral("保存布局"), this);
        controls->addWidget(m_save);
        m_change = new QPushButton(QStringLiteral("改变布局"), this);
        controls->addWidget(m_change);
        m_restore = new QPushButton(QStringLiteral("恢复布局"), this);
        controls->addWidget(m_restore);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
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
        // Applications may persist this initial snapshot; this example keeps it in memory.
        m_saved = m_view->saveHeaderState();
        m_view->setColumnDragEnabled(true);
        // Allow manual column widths to be included in the saved header layout.
        m_view->setColumnResizeEnabled(true);

        connect(m_save, &QPushButton::clicked, this, [this] {
            m_saved = m_view->saveHeaderState();
            m_status->setText(QStringLiteral("已保存 %1 字节").arg(m_saved.size()));
        });
        connect(m_change, &QPushButton::clicked, this, [this] {
            m_view->setColumnWidth(0, 240);
            m_view->setColumnHidden(2, true);
            m_view->moveColumn(3, 0);
            m_view->setFrozenColumns({0});
            m_view->setFrozenRows(1);
        });
        connect(m_restore, &QPushButton::clicked, this, [this] {
            const bool restored = m_view->restoreHeaderState(m_saved);
            m_status->setText(restored ? QStringLiteral("布局恢复成功") : QStringLiteral("布局数据无效，保留当前布局"));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_change = nullptr;
    QPushButton *m_restore = nullptr;
    QByteArray m_saved;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_header_persistence
