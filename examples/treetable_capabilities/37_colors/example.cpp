#include "example.h"
#include <memory>
#include "ui_colors.h"

#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPalette>
#include <QStandardItemModel>
#include <QTimer>

namespace treetable_colors {

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setObjectName(QStringLiteral("colorItem"));
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableColors)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableColors> ui;

    void setupUi()
    {
        ui->setupUi(this);
        ui->pageLayout->setStretch(ui->pageLayout->count() - 1, 1);
        // Parent color makes body transparency and unoccupied space easy to distinguish.
        QPalette palette = ui->previewHost->palette();
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#dde4ec")));
        ui->previewHost->setPalette(palette);
        ui->previewHost->setAutoFillBackground(true);
        for (QComboBox *combo : {ui->bodyColor, ui->itemColor, ui->hoverColor, ui->selectedColor,
                                ui->gapColor, ui->lineColor, ui->frameColor, ui->dividerColor,
                                ui->branchColor, ui->horizontalColor, ui->verticalColor, ui->headerGapColor,
                                ui->headerTextColor, ui->horizontalSectionColor, ui->verticalSectionColor}) {
            combo->addItem(QStringLiteral("默认 / 未设置"), QColor());
            combo->addItem(QStringLiteral("透明"), QColor(Qt::transparent));
            combo->addItem(QStringLiteral("暖黄 #fff0c2"), QColor(QStringLiteral("#fff0c2")));
            combo->addItem(QStringLiteral("浅蓝 #b9dfff"), QColor(QStringLiteral("#b9dfff")));
            combo->addItem(QStringLiteral("紫色 #7654ad"), QColor(QStringLiteral("#7654ad")));
            combo->addItem(QStringLiteral("半透明蓝"), QColor(60, 145, 240, 90));
        }
    }

    void setupExample()
    {
        m_view = new viv::VirtualTreeTableView(ui->previewHost);
        // View internals already have stable object names for descendant QSS selectors.
        ui->previewLayout->addWidget(m_view);
        // Use real cells so a broad QWidget selector visibly affects business children.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        m_view->setDefaultColumnWidth(125);
        m_view->setUniformItemHeight(42);
        m_view->setVisualStateCornerRadius(6);
        // A flat frame keeps this page focused on colors rather than alpha clipping.
        m_view->setFrameBorderWidth(1);
        m_defaultHover = m_view->hoverBackgroundColor();
        m_defaultSelected = m_view->selectedBackgroundColor();
        m_model = new QStandardItemModel(m_view);
        m_view->setModel(m_model);
        m_view->setExpansionAnimationEnabled(true);
        populateModel();
        for (QComboBox *combo : {ui->bodyColor, ui->itemColor, ui->hoverColor, ui->selectedColor,
                                ui->gapColor, ui->lineColor, ui->frameColor, ui->dividerColor,
                                ui->branchColor, ui->horizontalColor, ui->verticalColor, ui->headerGapColor,
                                ui->headerTextColor, ui->horizontalSectionColor, ui->verticalSectionColor})
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyColors(); });
        for (QCheckBox *check : {ui->bodyVisible, ui->statesVisible, ui->gaps, ui->headersVisible, ui->headerStates, ui->frozen})
            connect(check, &QCheckBox::toggled, this, [this](bool) { applyColors(); });
        connect(ui->manyRows, &QCheckBox::toggled, this, [this](bool) { populateModel(); });
        connect(ui->customCorner, &QCheckBox::toggled, this, [this](bool custom) {
            // The default corner follows the horizontal header. Custom widgets own their fill.
            if (custom) {
                auto *corner = new QLabel(QStringLiteral("ALL"));
                corner->setObjectName(QStringLiteral("colorCorner"));
                corner->setAlignment(Qt::AlignCenter);
                QPalette palette = corner->palette();
                palette.setColor(QPalette::Window, QColor(QStringLiteral("#f8b4d0")));
                corner->setPalette(palette);
                corner->setAutoFillBackground(true);
                m_view->setHeaderCornerWidget(corner);
            } else {
                m_view->setHeaderCornerWidget(nullptr);
            }
        });
        connect(ui->preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyPreset(); });
        connect(ui->applyQss, &QPushButton::clicked, this, [this]() { applyStyleSheet(); });
        connect(ui->clearQss, &QPushButton::clicked, this, [this]() {
            if (ui->preset->currentIndex() == 0)
                applyPreset();
            else
                ui->preset->setCurrentIndex(0);
        });
        applyPreset();
    }

    QColor color(QComboBox *combo) const { return combo->currentData().value<QColor>(); }

    void applyColors()
    {
        // Paint the overall view base first; a viewport QSS background is layered above it.
        // Visibility controls both layers. Invalid color follows the view's QPalette::Base.
        m_view->setBackgroundVisible(ui->bodyVisible->isChecked());
        m_view->setBackgroundColor(color(ui->bodyColor));
        // Item color affects actual rows only; invalid/transparent disables this layer.
        m_view->setItemBackgroundColor(color(ui->itemColor));
        // State backgrounds are independent of the normal item base.
        m_view->setVisualStateBackgroundVisible(ui->statesVisible->isChecked());
        m_view->setHoverBackgroundColor(color(ui->hoverColor).isValid() ? color(ui->hoverColor) : m_defaultHover);
        m_view->setSelectedBackgroundColor(color(ui->selectedColor).isValid() ? color(ui->selectedColor) : m_defaultSelected);
        // Explicit gap colors remain visible even when the body base is hidden.
        m_view->setRowSpacing(ui->gaps->isChecked() ? 8 : 0);
        m_view->setSpacingBackgroundColor(color(ui->gapColor));
        m_view->setFrameBorderColor(color(ui->frameColor));
        // Frozen-pane dividers have a separate pen from ordinary row/column grid lines.
        viv::PaneSeparatorStyle divider;
        divider.color = color(ui->dividerColor);
        m_view->setPaneSeparatorStyle(divider);
        m_view->setColumnSpacing(ui->gaps->isChecked() ? 8 : 0);
        m_view->setHorizontalGridLinesVisible(true);
        m_view->setVerticalGridLinesVisible(true);
        m_view->setHorizontalGridLineColor(color(ui->lineColor));
        m_view->setVerticalGridLineColor(color(ui->lineColor));
        // Header base, text, gaps and state layers are configured independently.
        auto *horizontal = m_view->horizontalHeader();
        auto *vertical = m_view->verticalHeader();
        // The container base fills empty space and sections without their own override.
        horizontal->setBackgroundColor(color(ui->horizontalColor));
        vertical->setBackgroundColor(color(ui->verticalColor));
        // Section-only colors leave gaps and the default corner on the container base.
        // QColor() clears the override; transparent shows the underlying container.
        horizontal->setSectionBackgroundColor(color(ui->horizontalSectionColor));
        vertical->setSectionBackgroundColor(color(ui->verticalSectionColor));
        for (auto *header : {horizontal, vertical}) {
            header->setBackgroundVisible(ui->headersVisible->isChecked());
            header->setSpacingBackgroundColor(color(ui->headerGapColor));
            header->setForegroundColor(color(ui->headerTextColor));
        }
        m_view->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, ui->headerStates->isChecked());
        m_view->setHeaderVisualStateBackgroundVisible(Qt::Vertical, ui->headerStates->isChecked());
        m_view->setFrozenColumns(ui->frozen->isChecked() ? QVector<int>{0} : QVector<int>{});
        m_view->setFrozenRightColumns(ui->frozen->isChecked() ? QVector<int>{3} : QVector<int>{});
        // Branches have no separate panel background; only the icon color changes.
        m_view->setBranchIconColor(color(ui->branchColor));
        updateStatus();
    }

    void populateModel()
    {
        m_model->clear();
        m_model->setColumnCount(4);
        m_model->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("状态"), QStringLiteral("备注")});
        const int count = ui->manyRows->isChecked() ? 60 : 2;
        for (int row = 0; row < count; ++row) {
            auto *first = new QStandardItem(QStringLiteral("条目 %1").arg(row + 1));
            m_model->appendRow({first, new QStandardItem(QStringLiteral("普通")), new QStandardItem(QStringLiteral("就绪")), new QStandardItem(QStringLiteral("说明"))});
            first->appendRow({new QStandardItem(QStringLiteral("子节点")), new QStandardItem(QStringLiteral("明细")), new QStandardItem(QStringLiteral("就绪")), new QStandardItem(QStringLiteral("层级缩进"))});
        }
        for (int row = 0; row < m_model->rowCount(); ++row)
            m_view->expandRecursively(m_model->index(row, 0));
        applyColors();
        // Keep one selected row to compare normal, hovered and selected layers.
        m_view->selectionModel()->select(m_model->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    void applyPreset()
    {
        QString text;
        QString hint;
        switch (ui->preset->currentIndex()) {
        case 1:
            text = QStringLiteral("#vivItemView { background-color: #c4edda; }");
            hint = QStringLiteral("仅匹配 View。主体颜色为默认时读取该 View 的 Base 调色板；显式 setBackgroundColor 优先。项、状态和间距的显式颜色独立；本选择器不命中表头。");
            break;
        case 2:
            text = QStringLiteral("#vivItemView { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #c4edda,stop:1 #b5ceff); }");
            hint = QStringLiteral("主体默认路径读取画刷，可观察渐变；这不代表完整支持 QSS 背景图片、定位或圆角。圆角应使用框架接口。");
            break;
        case 3:
            text = QStringLiteral("#vivItemView #vivViewport { background-color: #ffd6e8; }");
            hint = QStringLiteral("先绘制整体 View 底色，再在内容区域叠加粉色 viewport 背景。表头和滚动条不使用该 viewport 颜色；项、状态和显式间距仍在上层。关闭主体底色会同时关闭这两层。");
            break;
        case 4:
            text = QStringLiteral("QLabel#colorItem { background-color: #ffca96; color: #402820; }");
            hint = QStringLiteral("只给业务行 / 单元格 QLabel 填色。它们绘制在框架项和状态背景上面，会遮住相应区域的 hover / selected；滚动后复用控件仍匹配同一选择器。");
            break;
        case 5:
            text = QStringLiteral("QWidget { background-color: #a9d0ff; }");
            hint = QStringLiteral("宽泛选择器匹配 View 及其子控件，业务 QLabel 也会铺色，可能遮挡框架状态；它不会把框架每一个绘制层自动转换为相同颜色。建议使用 objectName 限定范围。");
            break;
        case 6:
            text = QStringLiteral("#vivItemView QScrollBar#vivVerticalScrollBar:vertical { background: #eadcf5; width: 14px; }\n#vivItemView QScrollBar#vivVerticalScrollBar::handle:vertical { background: #7654ad; min-height: 24px; }");
            hint = QStringLiteral("滚动条是独立 Qt 控件，其背景由 QSS 样式负责。勾选大量数据后观察滚动条；主体和项底色接口不会设置滚动条底色。");
            break;
        case 7:
            text = QStringLiteral("QHeaderView::section { background-color: #ff9e5e; }");
            hint = QStringLiteral("本框架使用 VirtualHeaderView + section widgets，QHeaderView::section 不匹配；请用 header 颜色接口。对于透明或显式间距颜色，请在颜色页配置。");
            break;
        case 8:
            text = QStringLiteral("#vivItemView #vivHorizontalHeader, #vivItemView #vivVerticalHeader { background-color: #c4edda; }");
            hint = QStringLiteral("背景覆盖整条表头，包括 section、空白和默认间距；普通和冻结表头都会匹配，默认 corner 跟随水平表头。显式区域、section 或间距颜色优先。");
            break;
        case 9:
            // Descendant selectors also match sections in frozen or temporary clipping hosts.
            text = QStringLiteral("#vivHorizontalHeader #vivHeaderSection { color: #7654ad; font-weight: bold; }\n#vivVerticalHeader #vivHeaderSection { color: #16704a; }");
            hint = QStringLiteral("仅设置文字，保留原生表头面板。勾选冻结列可检查副本；显式 header 文字颜色或 model 的 ForegroundRole 优先。状态与分割线仍通过各自的接口配置。");
            break;
        case 10:
            // Each section may override the container; the corner has a separate selector.
            text = QStringLiteral("#vivHorizontalHeader #vivHeaderSection { background-color: #c4edda; }\n#vivVerticalHeader #vivHeaderSection { background-color: #ffd6e8; }\n#vivHeaderCorner { background-color: #ffca96; }");
            hint = QStringLiteral("按默认名称分别设置列头、行号和 corner 背景；间距保持原有颜色。Hover / selected 动画和分割线绘制在背景上面；显式区域和 section 底色优先。");
            break;
        case 11:
            // Paint the gradient once per header container so spacing does not restart it.
            text = QStringLiteral("#vivHorizontalHeader, #vivVerticalHeader { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #c4edda,stop:1 #b5ceff); }");
            hint = QStringLiteral("渐变在各表头容器内连续绘制，默认 section 和间距不再重复填色。冻结副本分别按自己的矩形绘制；显式间距颜色仍可覆盖间距。");
            break;
        case 12:
            text = QStringLiteral("#vivHorizontalHeader, #vivVerticalHeader, #vivHeaderCorner { background-color: transparent; }");
            hint = QStringLiteral("透明背景替代原生面板；Hover / selected、文字与分割线保持独立。若仍有底色，请将区域、section 和表头间距的显式颜色恢复默认，并取消自定义 corner。");
            break;
        case 13:
            // A translucent viewport composites over the view base, not the outer parent.
            text = QStringLiteral("#vivItemView { background-color: #c4edda; }\n#vivItemView #vivViewport { background-color: rgba(255, 80, 160, 100); }");
            hint = QStringLiteral("绿色 View 底色上叠加半透明粉色 viewport。可将 viewport 改成 transparent，观察绿色底色；清除项和间距的显式颜色后检查滚动中是否一致。");
            break;
        case 14:
            // Use the full viewport bounds for the gradient during partial repaints.
            text = QStringLiteral("#vivItemView { background-color: #c4edda; }\n#vivItemView #vivViewport { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #ffd6e8,stop:1 #b5ceff); }");
            hint = QStringLiteral("渐变以完整 viewport 为范围，鼠标悬停或局部重绘不会重新起算；行列间距复原底色使用同一坐标。背景图片与 QSS 圆角不在该画刷路径支持范围内。");
            break;
        default:
            hint = QStringLiteral("无 QSS：主体默认 Base，项默认透明，父背景为灰蓝。先设置项底色再关闭 Hover / 选中，可确认普通项底色独立；选择 QSS 预设后可以继续切换颜色接口对比优先级。");
            break;
        }
        ui->qssEditor->setPlainText(text);
        ui->qssHint->setText(hint);
        applyStyleSheet();
    }

    void applyStyleSheet()
    {
        // Keep the stylesheet scoped to this preview, not the navigation or option controls.
        // Object selectors intentionally distinguish the view, viewport and business widgets.
        m_view->setStyleSheet(ui->qssEditor->toPlainText());
        m_view->ensurePolished();
        m_view->viewport()->ensurePolished();
        // Read palette values after style propagation; this is diagnostic text, not a repaint loop.
        QTimer::singleShot(0, this, [this]() { updateStatus(); });
    }

    void updateStatus()
    {
        const QColor item = m_view->itemBackgroundColor();
        ui->statusLabel->setText(QStringLiteral("View Base=%1 / Viewport Base=%2 / 主体画刷 style=%3 / 项底色=%4。点击行选中，鼠标划过比较。")
            .arg(m_view->palette().color(QPalette::Base).name(QColor::HexArgb))
            .arg(m_view->viewport()->palette().color(QPalette::Base).name(QColor::HexArgb))
            .arg(int(m_view->backgroundBrush().style()))
            .arg(item.isValid() ? item.name(QColor::HexArgb) : QStringLiteral("不绘制")));
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QColor m_defaultHover;
    QColor m_defaultSelected;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace treetable_colors
