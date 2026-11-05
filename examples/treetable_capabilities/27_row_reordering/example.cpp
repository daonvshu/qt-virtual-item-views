#include "ui_row_reordering.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QAbstractItemModel>
#include <QtAlgorithms>
#include <algorithm>

namespace treetable_row_reordering {

// A tree needs stable node identities and sibling moves, not a flat row permutation.
class Model : public QAbstractItemModel
{
    struct Node
    {
        QString title;
        Node *parent = nullptr;
        QVector<Node *> children;
        ~Node() { qDeleteAll(children); }
    };
public:
    explicit Model(QObject *parent) : QAbstractItemModel(parent)
    {
        for (int group = 0; group < 6; ++group) {
            auto *root = new Node;
            root->title = QStringLiteral("分组 %1").arg(group + 1);
            root->parent = &m_root;
            m_root.children.append(root);
            for (int row = 0; row < 8; ++row) {
                auto *child = new Node;
                child->title = QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1);
                child->parent = root;
                root->children.append(child);
            }
        }
    }
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (!hasIndex(row, column, parent)) return {};
        return createIndex(row, column, node(parent)->children[row]);
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid()) return {};
        Node *parent = node(index)->parent;
        if (!parent || parent == &m_root) return {};
        return createIndex(parent->parent->children.indexOf(parent), 0, parent);
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() && parent.column() != 0 ? 0 : int(node(parent)->children.size());
    }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return 6; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        return index.column() == 0 ? node(index)->title
            : QStringLiteral("%1 / 列 %2").arg(node(index)->title).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1) : QString::number(section + 1);
    }
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if ((sourceParent.isValid() && sourceParent.model() != this)
            || (destinationParent.isValid() && destinationParent.model() != this)
            || sourceParent != destinationParent || sourceParent.column() > 0
            || sourceRow < 0 || count <= 0 || sourceRow > rowCount(sourceParent) - count
            || destinationChild < 0 || destinationChild > rowCount(destinationParent)
            || (destinationChild >= sourceRow && destinationChild <= sourceRow + count))
            return false;
        // begin/endMoveRows preserve selections, persistent indexes and expansion identities.
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent, destinationChild))
            return false;
        auto &children = node(sourceParent)->children;
        QVector<Node *> moving;
        for (int i = 0; i < count; ++i) moving.append(children.takeAt(sourceRow));
        const int insertion = destinationChild > sourceRow ? destinationChild - count : destinationChild;
        for (int i = 0; i < count; ++i) children.insert(insertion + i, moving[i]);
        endMoveRows();
        return true;
    }
    void resetRowOrder()
    {
        // A real reset intentionally clears view state; ordinary moves above do not.
        beginResetModel();
        std::sort(m_root.children.begin(), m_root.children.end(), less);
        for (Node *root : m_root.children)
            std::sort(root->children.begin(), root->children.end(), less);
        endResetModel();
    }
private:
    static bool less(const Node *left, const Node *right) { return left->title < right->title; }
    Node *node(const QModelIndex &index) const
    {
        return index.isValid() ? static_cast<Node *>(index.internalPointer()) : const_cast<Node *>(&m_root);
    }
    Node m_root;
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableRowReorderingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableRowReorderingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new Model(ui->view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        ui->view->setColumnWidth(0, 280);
        ui->view->expand(m_model->index(0, 0));
        ui->view->setCurrentIndex(m_model->index(0, 0, m_model->index(0, 0)));
        // The model must implement moveRows; the default QAbstractItemModel implementation rejects moves.
        ui->view->setVerticalHeaderDragEnabled(true);
        ui->view->setRowFollowsHeaderVisual(true);
        connect(ui->view, &viv::VirtualTreeTableView::rowMoveRequested, this, [this](int from, int to) {
            ui->status->setText(QStringLiteral("行号拖动 from=%1 to=%2").arg(from).arg(to));
        });

        connect(ui->move, &QPushButton::clicked, this, [this] {
            const QModelIndex current = ui->view->currentIndex();
            if (!current.isValid()) return;
            const QModelIndex parent = current.parent();
            // Model row numbers are sibling-local; the view's rowMoveRequested uses visible rows.
            m_model->moveRows(parent, current.row(), 1, parent, m_model->rowCount(parent));
        });
        connect(ui->reset, &QPushButton::clicked, this, [this] {
            m_model->resetRowOrder();
            ui->view->expand(m_model->index(0, 0));
        });
        connect(ui->follow, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setRowFollowsHeaderVisual(enabled);
        });
    }

    Model *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_row_reordering
