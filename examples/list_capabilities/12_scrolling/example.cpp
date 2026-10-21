#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QAbstractListModel>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace list_scrolling {

class LogicalModel : public QAbstractListModel
{
public:
    explicit LogicalModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : m_count; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= m_count || role != Qt::DisplayRole)
            return {};
        return QStringLiteral("逻辑条目 %1 · 数据按需生成").arg(index.row() + 1);
    }
    void setLarge(bool large)
    {
        beginResetModel();
        m_count = large ? 100000000 : 1000000;
        endResetModel();
    }

private:
    int m_count = 1000000;
};

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral("12 滚动：默认百万行；开启一亿行后总高度 36 亿像素，超过 32 位有符号范围。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *wheel = new QHBoxLayout;
        m_wheelMode = new QComboBox(this);
        m_wheelMode->addItems({QStringLiteral("滚轮按像素"), QStringLiteral("滚轮按项")});
        m_step = new QSpinBox(this);
        m_step->setRange(1, 200);
        m_step->setValue(48);
        m_large = new QCheckBox(QStringLiteral("一亿行 / 64 位滚动范围"), this);
        wheel->addWidget(m_wheelMode);
        wheel->addWidget(m_step);
        wheel->addWidget(m_large);
        layout->addLayout(wheel);
        auto *jump = new QHBoxLayout;
        m_row = new QSpinBox(this);
        m_row->setRange(1, 1000000);
        m_row->setPrefix(QStringLiteral("第 "));
        m_row->setSuffix(QStringLiteral(" 行"));
        m_hint = new QComboBox(this);
        m_hint->addItems({QStringLiteral("确保可见"), QStringLiteral("顶部"), QStringLiteral("底部"), QStringLiteral("居中")});
        m_jump = new QPushButton(QStringLiteral("定位"), this);
        jump->addWidget(m_row);
        jump->addWidget(m_hint);
        jump->addWidget(m_jump);
        layout->addLayout(jump);
        auto *pixels = new QHBoxLayout;
        m_offset = new QLineEdit(QStringLiteral("0"), this);
        m_offset->setPlaceholderText(QStringLiteral("输入非负 64 位像素偏移"));
        m_setOffset = new QPushButton(QStringLiteral("设置偏移"), this);
        m_up = new QPushButton(QStringLiteral("上移 24 px"), this);
        m_down = new QPushButton(QStringLiteral("下移 24 px"), this);
        m_autoScroll = new QCheckBox(QStringLiteral("像素自动滚动"), this);
        pixels->addWidget(m_offset, 1);
        pixels->addWidget(m_setOffset);
        pixels->addWidget(m_up);
        pixels->addWidget(m_down);
        pixels->addWidget(m_autoScroll);
        layout->addLayout(pixels);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        m_model = new LogicalModel(m_view);
        // Fixed row height allows a large logical model without measuring each row.
        // The adapter is view-owned; the model generates text only when queried.
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        m_view->setRowGridLinesVisible(false);
        m_view->setModel(m_model);
        connect(m_wheelMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applyWheel(); });
        connect(m_step, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyWheel(); });
        connect(m_large, &QCheckBox::toggled, this, [this](bool large) {
            m_model->setLarge(large);
            m_row->setMaximum(m_model->rowCount());
        });
        connect(m_jump, &QPushButton::clicked, this, [this] {
            using Hint = viv::VirtualItemView::ScrollHint;
            const Hint hints[] = {Hint::EnsureVisible, Hint::PositionAtTop, Hint::PositionAtBottom, Hint::PositionAtCenter};
            // scrollTo translates a model index into a pixel offset according to
            // the selected positioning hint.
            m_view->scrollTo(m_model->index(m_row->value() - 1, 0), hints[m_hint->currentIndex()]);
        });
        connect(m_setOffset, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const qint64 offset = m_offset->text().toLongLong(&ok);
            if (!ok || offset < 0) {
                m_offset->setToolTip(QStringLiteral("请输入有效的非负 64 位整数"));
                m_offset->selectAll();
                m_offset->setFocus();
                return;
            }
            m_offset->setToolTip(QString());
            // The logical scroll axis is qint64, so large content stays addressable.
            m_view->setVerticalOffset(offset);
            // The view clamps to its valid range; display the actual resulting offset.
            m_offset->setText(QString::number(m_view->verticalOffset()));
        });
        // Programmatic pixel movement is independent of the selected wheel mode.
        // Negative moves upward, positive downward; both clamp at the content edges.
        connect(m_up, &QPushButton::clicked, this, [this] { m_view->scrollByPixels(-24); });
        connect(m_down, &QPushButton::clicked, this, [this] { m_view->scrollByPixels(24); });
        auto *autoScroll = new QTimer(this);
        autoScroll->setInterval(16);
        connect(autoScroll, &QTimer::timeout, this, [this] {
            if (!isVisible())
                return;
            // Repeated pixel movement uses the same 64-bit offset as manual
            // positioning; reaching the bottom restarts at the first row.
            if (m_view->verticalOffset() >= m_view->maximumVerticalOffset())
                m_view->setVerticalOffset(0);
            else
                m_view->scrollByPixels(24);
        });
        connect(m_autoScroll, &QCheckBox::toggled, autoScroll, [autoScroll](bool enabled) {
            if (enabled)
                autoScroll->start();
            else
                autoScroll->stop();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // Extent is total content height; maximum offset accounts for viewport height.
            const auto stats = m_view->stats();
            m_status->setText(QStringLiteral("offset=%1 / maximum=%2 / extent=%3\n逻辑行=%4 / 物化=%5 / 池内=%6 / 累计创建=%7")
                .arg(m_view->verticalOffset()).arg(m_view->maximumVerticalOffset()).arg(m_view->contentExtent())
                .arg(stats.logicalItems).arg(stats.materializedItems).arg(stats.pooledWidgets).arg(stats.createCount));
        });
        timer->start(200);
        applyWheel();
    }

    void applyWheel()
    {
        if (m_wheelMode->currentIndex() == 0) {
            // Pixels keeps rows from snapping; Items makes each wheel notch row-aligned.
            m_view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
            // Set pixels per wheel notch; this setter also selects Pixels mode.
            m_view->setWheelScrollPixels(m_step->value());
        } else {
            // Set items per notch and select Items mode in one call.
            m_view->setWheelScrollItems(m_step->value());
        }
    }

    viv::VirtualListView *m_view = nullptr;
    LogicalModel *m_model = nullptr;
    QComboBox *m_wheelMode = nullptr;
    QSpinBox *m_step = nullptr;
    QCheckBox *m_large = nullptr;
    QSpinBox *m_row = nullptr;
    QComboBox *m_hint = nullptr;
    QPushButton *m_jump = nullptr;
    QLineEdit *m_offset = nullptr;
    QPushButton *m_setOffset = nullptr;
    QPushButton *m_up = nullptr;
    QPushButton *m_down = nullptr;
    QCheckBox *m_autoScroll = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_scrolling
