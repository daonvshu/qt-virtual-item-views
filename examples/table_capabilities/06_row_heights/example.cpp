#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("06 固定与动态行高：内容换行后自动测量，显式行高可优先于测量，支持清除覆盖"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_mode = new QComboBox(this);
        m_mode->addItems({QStringLiteral("动态高度"), QStringLiteral("固定 40px")});
        controls->addWidget(m_mode);
        m_explicit = new QPushButton(QStringLiteral("首行设为 100px"), this);
        controls->addWidget(m_explicit);
        m_clear = new QPushButton(QStringLiteral("清除首行显式高度"), this);
        controls->addWidget(m_clear);
        m_text = new QPushButton(QStringLiteral("追加首行文本"), this);
        controls->addWidget(m_text);
        m_policy = new QComboBox(this);
        m_policy->addItems({QStringLiteral("显式优先"), QStringLiteral("测量优先")});
        controls->addWidget(m_policy);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // Use one QWidget per row and transfer adapter ownership to the view.
        m_view->setTableAdapter(new RowAdapter, true);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        // Resize columns to demonstrate wrapping; row resizing stays disabled for automatic heights.
        m_view->setColumnResizeEnabled(true);
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
        // Variable allows different row heights; estimatedSize supplies estimates until measurement.
        m_view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
        m_view->setEstimatedItemHeight(40);
        // Column resizing also schedules measurement; sizeHint() reads the current host widths.
        m_view->setAutoMeasureItemHeight(true);
        // Explicit heights take precedence by default; MeasuredWins gives content measurement priority.
        m_view->setRowSizePolicy(viv::VirtualTableView::RowSizePolicy::ExplicitWins);
        for (int r = 0; r < 120; ++r)
            m_model->setData(m_model->index(r, 0), QStringLiteral("动态高度内容，文本会随列宽换行。 ").repeated(1 + r % 5));

        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            m_view->setAutoMeasureItemHeight(mode == 0);
            if (mode == 0)
                m_view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
            else
                m_view->setUniformItemHeight(40);
        });
        connect(m_explicit, &QPushButton::clicked, this, [this] {
            m_view->setRowHeight(0, 100);
        });
        connect(m_clear, &QPushButton::clicked, this, [this] {
            m_view->clearRowHeight(0);
        });
        connect(m_text, &QPushButton::clicked, this, [this] {
            const auto index = m_model->index(0, 0);
            m_model->setData(index, index.data().toString() + QStringLiteral("追加文本 ").repeated(12));
        });
        connect(m_policy, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setRowSizePolicy(value == 0 ? viv::VirtualTableView::RowSizePolicy::ExplicitWins : viv::VirtualTableView::RowSizePolicy::MeasuredWins);
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *m_explicit = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_text = nullptr;
    QComboBox *m_policy = nullptr;
    QComboBox *m_mode = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_row_heights
