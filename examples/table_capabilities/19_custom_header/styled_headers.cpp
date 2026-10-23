#include "styled_headers.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/labelheaderview.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLinearGradient>
#include <QPointer>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionHeader>

namespace table_header_states {

class GradientHeaderSection : public viv::LabelHeaderSection
{
public:
    GradientHeaderSection(QCheckBox *enabled, QWidget *parent)
        : viv::LabelHeaderSection(parent), m_enabled(enabled) {}

protected:
    QBrush visualStateBackgroundBrush(bool selected, const QColor &defaultColor) const override
    {
        // Delegate hover and the disabled example option to the built-in renderer.
        if (!selected || !m_enabled || !m_enabled->isChecked())
            return viv::StyledHeaderSection::visualStateBackgroundBrush(selected, defaultColor);

        // Section-local coordinates keep the gradient aligned after scrolling or reuse.
        // The base renderer applies selectedProgress and the configured corner geometry.
        QLinearGradient gradient(0, 0, width(), height());
        gradient.setColorAt(0.0, QColor(QStringLiteral("#7dd3fc")));
        gradient.setColorAt(0.35, QColor(QStringLiteral("#c4b5fd")));
        gradient.setColorAt(0.7, QColor(QStringLiteral("#f9a8d4")));
        gradient.setColorAt(1.0, QColor(QStringLiteral("#fde68a")));
        return QBrush(gradient);
    }

private:
    QPointer<QCheckBox> m_enabled;
};

class SelectedIconHeaderSection : public GradientHeaderSection
{
public:
    SelectedIconHeaderSection(QCheckBox *gradientEnabled, QCheckBox *iconEnabled, QWidget *parent)
        : GradientHeaderSection(gradientEnabled, parent), m_iconEnabled(iconEnabled)
    {
        // Use a built-in icon so the copied example needs no external image resources.
        m_selectedIcon = style()->standardIcon(QStyle::SP_DialogApplyButton, nullptr, this);
    }

protected:
    void paintContent(QPainter *painter) override
    {
        const int iconSize = 16;
        const int margin = 4;
        const int iconSlot = iconSize + 2 * margin;
        QStyleOptionHeader option;
        initContentStyleOption(&option);
        if (!m_iconEnabled || !m_iconEnabled->isChecked()
            || option.rect.width() <= iconSlot + 8 || option.rect.height() < iconSlot) {
            // Narrow sections and the disabled option keep the built-in text layout.
            GradientHeaderSection::paintContent(painter);
            return;
        }

        // Keep the slot reserved while enabled, including when not selected or fading out.
        // This avoids moving the label on every animation frame.
        const QRect iconRect(option.rect.left() + margin,
                             option.rect.top() + (option.rect.height() - iconSize) / 2,
                             iconSize, iconSize);
        const auto state = visualState();
        if (state.selectedProgress > 0) {
            painter->save();
            painter->setOpacity(state.selectedProgress);
            m_selectedIcon.paint(painter, iconRect, Qt::AlignCenter,
                                 isEnabled() ? QIcon::Normal : QIcon::Disabled);
            painter->restore();
        }

        // Lay out the native label and sort arrow in the remaining content area only.
        // The content helper applies padding without changing the background geometry.
        option.rect.adjust(iconSlot, 0, 0, 0);
        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        QStyleOptionHeader labelOption = option;
        labelOption.rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
        if (labelOption.rect.isValid())
            style()->drawControl(QStyle::CE_HeaderLabel, &labelOption, painter, this);
        if (option.sortIndicator != QStyleOptionHeader::None) {
            QStyleOptionHeader arrowOption = option;
            arrowOption.rect = style()->subElementRect(QStyle::SE_HeaderArrow, &option, this);
            style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &arrowOption, painter, this);
        }
        painter->restore();
    }

private:
    QPointer<QCheckBox> m_iconEnabled;
    QIcon m_selectedIcon;
};

class StateHeaderAdapter : public viv::LabelHeaderAdapter
{
public:
    StateHeaderAdapter(QCheckBox *gradientEnabled, QCheckBox *iconEnabled)
        : m_gradientEnabled(gradientEnabled), m_iconEnabled(iconEnabled) {}

    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        // Inherited bind/unbind still manage labels, sort indicators and recycled identity.
        return new SelectedIconHeaderSection(m_gradientEnabled.data(), m_iconEnabled.data(), parent);
    }

private:
    QPointer<QCheckBox> m_gradientEnabled;
    QPointer<QCheckBox> m_iconEnabled;
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
        auto *description = new QLabel(QStringLiteral("19 表头样式与绘制：基础背景、透明、圆角、彩色渐变和选中图标，支持横纵表头与冻结区"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_behavior = new QComboBox(this);
        m_behavior->addItems({QStringLiteral("按行选择与高亮"), QStringLiteral("按格选择与高亮")});
        controls->addWidget(m_behavior);
        m_radius = new QSpinBox(this);
        m_radius->setRange(0, 24);
        m_radius->setValue(8);
        m_radius->setPrefix(QStringLiteral("圆角 "));
        controls->addWidget(m_radius);
        m_duration = new QSpinBox(this);
        m_duration->setRange(0, 800);
        m_duration->setValue(180);
        m_duration->setPrefix(QStringLiteral("过渡 ms "));
        controls->addWidget(m_duration);
        m_curve = new QComboBox(this);
        m_curve->addItems({QStringLiteral("OutCubic"), QStringLiteral("InOutCubic"), QStringLiteral("Linear")});
        controls->addWidget(m_curve);
        controls->addStretch();
        layout->addLayout(controls);
        auto *headerControls = new QHBoxLayout;
        m_headerCorners = new QCheckBox(QStringLiteral("圆角扩展到表头"), this);
        m_headerCorners->setChecked(true);
        headerControls->addWidget(m_headerCorners);
        m_columnSelection = new QCheckBox(QStringLiteral("点击列头选整列"), this);
        headerControls->addWidget(m_columnSelection);
        m_headerColor = new QCheckBox(QStringLiteral("自定义表头底色"), this);
        headerControls->addWidget(m_headerColor);
        headerControls->addStretch();
        layout->addLayout(headerControls);
        auto *appearanceControls = new QHBoxLayout;
        m_gradientHeader = new QCheckBox(QStringLiteral("选中表头使用彩色渐变"), this);
        appearanceControls->addWidget(m_gradientHeader);
        m_transparentHeader = new QCheckBox(QStringLiteral("透明表头背景"), this);
        appearanceControls->addWidget(m_transparentHeader);
        m_headerIcon = new QCheckBox(QStringLiteral("选中表头显示图标"), this);
        appearanceControls->addWidget(m_headerIcon);
        appearanceControls->addStretch();
        layout->addLayout(appearanceControls);
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
        // Frozen header clones share the primary adapter and its appearance.
        m_view->setFrozenColumns({0});
        // Replace only the section factory; retain the built-in label binding and painting.
        // The table owns both headers; each header owns its adapter, also used by pane clones.
        m_view->horizontalHeader()->setAdapter(new StateHeaderAdapter(m_gradientHeader, m_headerIcon), true);
        m_view->verticalHeader()->setAdapter(new StateHeaderAdapter(m_gradientHeader, m_headerIcon), true);
        // Backgrounds are enabled by default; keep content widgets transparent without custom painting.
        m_view->setVisualStateBackgroundVisible(true);
        m_view->setVisualStateCornerRadius(8);
        // Enabled by default: row headers round the left edge, row content the right edge.
        // Selected column headers keep all four corners; cell backgrounds stay independent.
        m_view->setVisualStateCornerRadiusIncludesHeaders(true);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Hover and selected backgrounds both follow the selection behavior.
        m_view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        connect(m_behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->selectionModel()->clearSelection();
            m_view->setSelectionBehavior(value == 0 ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                                   : viv::VirtualItemView::SelectionBehavior::SelectItems);
        });
        connect(m_radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setVisualStateCornerRadius(value);
        });
        connect(m_headerCorners, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setVisualStateCornerRadiusIncludesHeaders(enabled);
        });
        connect(m_columnSelection, &QCheckBox::toggled, this, [this](bool enabled) {
            // Header selection is opt-in; use SelectItems above to see each selected cell.
            m_view->setHorizontalHeaderSelectionEnabled(enabled);
        });
        const auto repaintHeaders = [this](bool) {
            // Repaint existing sections without replacing headers or resetting selection/animation.
            for (auto *section : m_view->findChildren<viv::HeaderSection *>())
                section->update();
        };
        connect(m_gradientHeader, &QCheckBox::toggled, this, repaintHeaders);
        connect(m_headerIcon, &QCheckBox::toggled, this, repaintHeaders);
        connect(m_headerColor, &QCheckBox::toggled, this, [this](bool enabled) {
            // This works with the default headers too; no custom section or adapter is needed.
            // Invalid QColor restores native panels without changing hover/selection settings.
            const QColor color = enabled ? QColor(QStringLiteral("#fff3d6")) : QColor();
            m_view->horizontalHeader()->setBackgroundColor(color);
            m_view->verticalHeader()->setBackgroundColor(color);
        });
        connect(m_transparentHeader, &QCheckBox::toggled, this, [this](bool transparent) {
            // Hide base fills in both headers and the default corner, keeping state animations.
            m_view->horizontalHeader()->setBackgroundVisible(!transparent);
            m_view->verticalHeader()->setBackgroundVisible(!transparent);
        });
        connect(m_duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setVisualStateAnimationDuration(value);
        });
        connect(m_curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::InOutCubic, QEasingCurve::Linear};
            m_view->setVisualStateAnimationEasingCurve(QEasingCurve(curves[value]));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *m_behavior = nullptr;
    QSpinBox *m_radius = nullptr;
    QSpinBox *m_duration = nullptr;
    QComboBox *m_curve = nullptr;
    QCheckBox *m_headerCorners = nullptr;
    QCheckBox *m_columnSelection = nullptr;
    QCheckBox *m_gradientHeader = nullptr;
    QCheckBox *m_transparentHeader = nullptr;
    QCheckBox *m_headerIcon = nullptr;
    QCheckBox *m_headerColor = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_header_states
