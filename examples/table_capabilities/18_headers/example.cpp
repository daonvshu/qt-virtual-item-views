#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QVariant>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <virtualitemviews/virtualheaderview.h>

namespace table_headers {

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
        auto *description = new QLabel(QStringLiteral("18 表头与换序动画：显示隐藏表头、调整高度和行号宽度，列拖动时主体跟随动画"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_horizontal = new QCheckBox(QStringLiteral("列头"), this);
        m_horizontal->setChecked(true);
        controls->addWidget(m_horizontal);
        m_vertical = new QCheckBox(QStringLiteral("行号"), this);
        m_vertical->setChecked(true);
        controls->addWidget(m_vertical);
        m_height = new QSpinBox(this);
        m_height->setRange(24, 80);
        m_height->setValue(32);
        m_height->setPrefix(QStringLiteral("表头高度 "));
        controls->addWidget(m_height);
        m_width = new QSpinBox(this);
        m_width->setRange(30, 120);
        m_width->setValue(50);
        m_width->setPrefix(QStringLiteral("行号宽 "));
        controls->addWidget(m_width);
        m_follow = new QCheckBox(QStringLiteral("主体跟随列动画"), this);
        m_follow->setChecked(true);
        controls->addWidget(m_follow);
        m_move = new QPushButton(QStringLiteral("第 3 列移到首列"), this);
        controls->addWidget(m_move);
        controls->addStretch();
        layout->addLayout(controls);
        auto *styleControls = new QHBoxLayout;
        m_styles = new QCheckBox(QStringLiteral("自定义表头文字样式"), this);
        styleControls->addWidget(m_styles);
        m_alignment = new QComboBox(this);
        m_alignment->addItems({QStringLiteral("居左"), QStringLiteral("居中"), QStringLiteral("居右")});
        styleControls->addWidget(m_alignment);
        m_padding = new QSpinBox(this);
        m_padding->setRange(0, 24);
        m_padding->setValue(8);
        m_padding->setPrefix(QStringLiteral("左右留白 "));
        styleControls->addWidget(m_padding);
        m_fontSize = new QSpinBox(this);
        m_fontSize->setRange(8, 24);
        m_fontSize->setValue(11);
        m_fontSize->setPrefix(QStringLiteral("字号 pt "));
        styleControls->addWidget(m_fontSize);
        m_bold = new QCheckBox(QStringLiteral("加粗"), this);
        m_bold->setChecked(true);
        styleControls->addWidget(m_bold);
        styleControls->addStretch();
        layout->addLayout(styleControls);
        auto *modelControls = new QHBoxLayout;
        m_modelStyle = new QCheckBox(QStringLiteral("第 2 列表头使用模型样式"), this);
        modelControls->addWidget(m_modelStyle);
        m_freeze = new QCheckBox(QStringLiteral("冻结首行首列，观察样式同步"), this);
        modelControls->addWidget(m_freeze);
        modelControls->addStretch();
        layout->addLayout(modelControls);
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
        // The default label header uses QWidgets and supports column dragging and animation.
        m_view->setHorizontalHeaderVisible(true);
        m_view->setVerticalHeaderVisible(true);
        m_view->setHeaderHeight(32);
        m_view->setVerticalHeaderWidth(50);
        m_view->setColumnDragEnabled(true);
        m_view->setColumnResizeEnabled(true);
        m_view->setVerticalHeaderResizeEnabled(true);
        m_view->setHeaderAnimationEnabled(true);
        m_view->setHeaderAnimationDuration(300);
        m_view->setColumnFollowsHeaderVisual(true);

        connect(m_horizontal, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setHorizontalHeaderVisible(enabled);
        });
        connect(m_vertical, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setVerticalHeaderVisible(enabled);
        });
        connect(m_height, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setHeaderHeight(value);
        });
        connect(m_width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setVerticalHeaderWidth(value);
        });
        connect(m_follow, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setColumnFollowsHeaderVisual(enabled);
        });
        connect(m_move, &QPushButton::clicked, this, [this] {
            m_view->moveColumn(2, 0, viv::VirtualTableView::MoveAnimation::Animate);
        });
        const auto updateStyles = [this] { applyHeaderStyles(); };
        connect(m_styles, &QCheckBox::toggled, this, updateStyles);
        connect(m_alignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateStyles);
        connect(m_padding, QOverload<int>::of(&QSpinBox::valueChanged), this, updateStyles);
        connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, updateStyles);
        connect(m_bold, &QCheckBox::toggled, this, updateStyles);
        connect(m_modelStyle, &QCheckBox::toggled, this, [this](bool enabled) {
            // Model roles override header defaults for this logical section only.
            QFont font = m_view->font();
            font.setPointSize(12);
            font.setBold(true);
            font.setItalic(true);
            m_model->setHeaderData(1, Qt::Horizontal, enabled ? QVariant(font) : QVariant(), Qt::FontRole);
            m_model->setHeaderData(1, Qt::Horizontal,
                enabled ? QVariant(QColor(QStringLiteral("#b91c1c"))) : QVariant(), Qt::ForegroundRole);
            m_model->setHeaderData(1, Qt::Horizontal,
                enabled ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter)) : QVariant(), Qt::TextAlignmentRole);
        });
        connect(m_freeze, &QCheckBox::toggled, this, [this](bool enabled) {
            // Clones inherit the corresponding header's appearance, including later changes.
            m_view->setFrozenColumns(enabled ? QVector<int>{0} : QVector<int>{});
            m_view->setFrozenRows(enabled ? 1 : 0);
        });
    }

    void applyHeaderStyles()
    {
        const bool enabled = m_styles->isChecked();
        const Qt::Alignment alignments[] = {
            Qt::AlignLeft | Qt::AlignVCenter, Qt::AlignCenter, Qt::AlignRight | Qt::AlignVCenter
        };
        QFont font = m_view->font();
        font.setPointSize(m_fontSize->value());
        font.setBold(m_bold->isChecked());
        // Access the public header interface directly: no cast, adapter or subclass is needed.
        for (auto *header : {m_view->horizontalHeader(), m_view->verticalHeader()}) {
            header->setTextAlignment(enabled ? alignments[m_alignment->currentIndex()]
                                             : Qt::Alignment(Qt::AlignCenter));
            header->setContentPadding(enabled ? QMargins(m_padding->value(), 2, m_padding->value(), 2)
                                               : QMargins());
            header->setFont(enabled ? font : QFont());
        }
        // Each axis owns independent colors; the default corner follows the horizontal header.
        m_view->horizontalHeader()->setBackgroundColor(enabled ? QColor(QStringLiteral("#eef6ff")) : QColor());
        m_view->horizontalHeader()->setForegroundColor(enabled ? QColor(QStringLiteral("#334155")) : QColor());
        m_view->verticalHeader()->setBackgroundColor(enabled ? QColor(QStringLiteral("#f5f0ff")) : QColor());
        m_view->verticalHeader()->setForegroundColor(enabled ? QColor(QStringLiteral("#7c3aed")) : QColor());
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *m_horizontal = nullptr;
    QCheckBox *m_vertical = nullptr;
    QSpinBox *m_height = nullptr;
    QSpinBox *m_width = nullptr;
    QCheckBox *m_follow = nullptr;
    QPushButton *m_move = nullptr;
    QCheckBox *m_styles = nullptr;
    QComboBox *m_alignment = nullptr;
    QSpinBox *m_padding = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QCheckBox *m_bold = nullptr;
    QCheckBox *m_modelStyle = nullptr;
    QCheckBox *m_freeze = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_headers
