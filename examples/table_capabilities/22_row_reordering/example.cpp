#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <virtualitemviews/reorderabletablemodel.h>

namespace table_row_reordering {

class Model : public viv::ReorderableTableModel
{
public:
    explicit Model(QObject *parent) : viv::ReorderableTableModel(100, 8, parent) {}
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        // sourceRow identifies the data record; index.row() is its current position.
        return QStringLiteral("记录 %1 / 列 %2").arg(sourceRow(index.row()) + 1).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1) : QString::number(sourceRow(section) + 1);
    }
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
        auto *description = new QLabel(QStringLiteral("22 行号拖动排序：拖动左侧行号，模型通过 sourceRow 保存数据身份，主体跟随移动"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_move = new QPushButton(QStringLiteral("首行移到第 5 行之后"), this);
        controls->addWidget(m_move);
        m_reset = new QPushButton(QStringLiteral("恢复原始顺序"), this);
        controls->addWidget(m_reset);
        m_follow = new QCheckBox(QStringLiteral("主体跟随行动画"), this);
        m_follow->setChecked(true);
        controls->addWidget(m_follow);
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
        m_model = new Model(m_view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // The model must implement moveRows; the default QAbstractItemModel implementation rejects moves.
        m_view->setVerticalHeaderDragEnabled(true);
        m_view->setRowFollowsHeaderVisual(true);
        connect(m_view, &viv::VirtualTableView::rowMoveRequested, this, [this](int from, int to) {
            m_status->setText(QStringLiteral("行号拖动 from=%1 to=%2").arg(from).arg(to));
        });

        connect(m_move, &QPushButton::clicked, this, [this] {
            m_model->moveRows({}, 0, 1, {}, 5);
        });
        connect(m_reset, &QPushButton::clicked, this, [this] {
            m_model->resetRowOrder();
        });
        connect(m_follow, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setRowFollowsHeaderVisual(enabled);
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    Model *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_move = nullptr;
    QPushButton *m_reset = nullptr;
    QCheckBox *m_follow = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_row_reordering
