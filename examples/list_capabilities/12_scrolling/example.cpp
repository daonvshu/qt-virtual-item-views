#include "ui_scrolling.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QAbstractListModel>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListScrollingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListScrollingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        m_model = new LogicalModel(ui->view);
        // Fixed row height allows a large logical model without measuring each row.
        // The adapter is view-owned; the model generates text only when queried.
        ui->view->setAdapter(new TextAdapter, true);
        ui->view->setUniformItemHeight(36);
        ui->view->setRowGridLinesVisible(false);
        ui->view->setModel(m_model);
        connect(ui->wheelMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applyWheel(); });
        connect(ui->step, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyWheel(); });
        connect(ui->large, &QCheckBox::toggled, this, [this](bool large) {
            m_model->setLarge(large);
            ui->row->setMaximum(m_model->rowCount());
        });
        connect(ui->jump, &QPushButton::clicked, this, [this] {
            using Hint = viv::VirtualItemView::ScrollHint;
            const Hint hints[] = {Hint::EnsureVisible, Hint::PositionAtTop, Hint::PositionAtBottom, Hint::PositionAtCenter};
            // scrollTo translates a model index into a pixel offset according to
            // the selected positioning hint.
            ui->view->scrollTo(m_model->index(ui->row->value() - 1, 0), hints[ui->hint->currentIndex()]);
        });
        connect(ui->setOffset, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const qint64 offset = ui->offset->text().toLongLong(&ok);
            if (!ok || offset < 0) {
                ui->offset->setToolTip(QStringLiteral("请输入有效的非负 64 位整数"));
                ui->offset->selectAll();
                ui->offset->setFocus();
                return;
            }
            ui->offset->setToolTip(QString());
            // The logical scroll axis is qint64, so large content stays addressable.
            ui->view->setVerticalOffset(offset);
            // The view clamps to its valid range; display the actual resulting offset.
            ui->offset->setText(QString::number(ui->view->verticalOffset()));
        });
        // Programmatic pixel movement is independent of the selected wheel mode.
        // Negative moves upward, positive downward; both clamp at the content edges.
        connect(ui->up, &QPushButton::clicked, this, [this] { ui->view->scrollByPixels(-24); });
        connect(ui->down, &QPushButton::clicked, this, [this] { ui->view->scrollByPixels(24); });
        auto *autoScroll = new QTimer(this);
        autoScroll->setInterval(16);
        connect(autoScroll, &QTimer::timeout, this, [this] {
            if (!isVisible())
                return;
            // Repeated pixel movement uses the same 64-bit offset as manual
            // positioning; reaching the bottom restarts at the first row.
            if (ui->view->verticalOffset() >= ui->view->maximumVerticalOffset())
                ui->view->setVerticalOffset(0);
            else
                ui->view->scrollByPixels(24);
        });
        connect(ui->autoScroll, &QCheckBox::toggled, autoScroll, [autoScroll](bool enabled) {
            if (enabled)
                autoScroll->start();
            else
                autoScroll->stop();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // Extent is total content height; maximum offset accounts for viewport height.
            const auto stats = ui->view->stats();
            ui->status->setText(QStringLiteral("offset=%1 / maximum=%2 / extent=%3\n逻辑行=%4 / 物化=%5 / 池内=%6 / 累计创建=%7")
                .arg(ui->view->verticalOffset()).arg(ui->view->maximumVerticalOffset()).arg(ui->view->contentExtent())
                .arg(stats.logicalItems).arg(stats.materializedItems).arg(stats.pooledWidgets).arg(stats.createCount));
        });
        timer->start(200);
        applyWheel();
    }

    void applyWheel()
    {
        if (ui->wheelMode->currentIndex() == 0) {
            // Pixels keeps rows from snapping; Items makes each wheel notch row-aligned.
            ui->view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
            // Set pixels per wheel notch; this setter also selects Pixels mode.
            ui->view->setWheelScrollPixels(ui->step->value());
        } else {
            // Set items per notch and select Items mode in one call.
            ui->view->setWheelScrollItems(ui->step->value());
        }
    }

    LogicalModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_scrolling
