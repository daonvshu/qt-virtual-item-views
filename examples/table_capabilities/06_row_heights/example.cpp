#include "ui_row_heights.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>

namespace table_row_heights {

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent) : QWidget(parent) { setupUi(); }
    QVector<QLabel *> labels;
    QSize sizeHint() const override
    {
        int height = 40;
        for (QLabel *label : labels) {
            const int width = qMax(40, label->parentWidget()->width() - 16);
            height = qMax(height, label->fontMetrics().boundingRect(
                QRect(0, 0, width, 100000), Qt::TextWordWrap, label->text()).height() + 16);
        }
        return QSize(0, height);
    }
private:
    void setupUi()
    {
        for (int column = 0; column < 8; ++column) {
            // The framework manages ColumnHost position, width, visibility and clipping in frozen panes.
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            layout->addWidget(label);
            label->setWordWrap(true);
            labels.append(label);
        }
    }
};
class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new RowWidget(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<RowWidget *>(widget);
        for (int column = 0; column < row->labels.size(); ++column)
            row->labels[column]->setText(index.siblingAtColumn(column).data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        for (QLabel *label : static_cast<RowWidget *>(widget)->labels)
            label->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(0, 40); }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableRowHeightsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableRowHeightsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Use one QWidget per row and transfer adapter ownership to the view.
        ui->view->setTableAdapter(new RowAdapter, true);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        // Resize columns to demonstrate wrapping; row resizing stays disabled for automatic heights.
        ui->view->setColumnResizeEnabled(true);
        m_model = new QStandardItemModel(120, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Variable allows different row heights; estimatedSize supplies estimates until measurement.
        ui->view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
        ui->view->setEstimatedItemHeight(40);
        // Column resizing also schedules measurement; sizeHint() reads the current host widths.
        ui->view->setAutoMeasureItemHeight(true);
        // Explicit heights take precedence by default; MeasuredWins gives content measurement priority.
        ui->view->setRowSizePolicy(viv::VirtualTableView::RowSizePolicy::ExplicitWins);
        for (int r = 0; r < 120; ++r)
            m_model->setData(m_model->index(r, 0), QStringLiteral("动态高度内容，文本会随列宽换行。 ").repeated(1 + r % 5));

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setAutoMeasureItemHeight(mode == 0);
            if (mode == 0)
                ui->view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
            else
                ui->view->setUniformItemHeight(40);
        });
        connect(ui->explicitHeight, &QPushButton::clicked, this, [this] {
            ui->view->setRowHeight(0, 100);
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            ui->view->clearRowHeight(0);
        });
        connect(ui->text, &QPushButton::clicked, this, [this] {
            const auto index = m_model->index(0, 0);
            m_model->setData(index, index.data().toString() + QStringLiteral("追加文本 ").repeated(12));
        });
        connect(ui->policy, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setRowSizePolicy(value == 0 ? viv::VirtualTableView::RowSizePolicy::ExplicitWins : viv::VirtualTableView::RowSizePolicy::MeasuredWins);
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_row_heights
