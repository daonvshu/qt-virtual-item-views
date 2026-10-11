#include <virtualitemviews/virtualflowtableview.h>
#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/accessibility.h>

#include "../common/demoimage.h"

#include <QAbstractListModel>
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

namespace {

class ItemModel : public QAbstractListModel
{
public:
    explicit ItemModel(int count) : m_count(count) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_count;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_count)
            return {};
        if (role == Qt::DisplayRole)
            return m_imageMode ? QFileInfo(m_images.at(index.row())).fileName()
                               : QStringLiteral("项目 %1").arg(index.row());
        if (role == Qt::UserRole && m_imageMode)
            return m_images.at(index.row());
        return {};
    }
    void setImages(const QStringList &images)
    {
        beginResetModel();
        m_images = images;
        m_imageMode = true;
        m_count = m_images.size();
        endResetModel();
    }
private:
    int m_count;
    QStringList m_images;
    bool m_imageMode = false;
};

class ItemWidget : public QWidget
{
public:
    explicit ItemWidget(vivdemo::ImageBackend *images, QWidget *parent)
        : QWidget(parent), m_label(new QLabel(this)),
          m_content(new vivdemo::ImageContent(images, this))
    {
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void bind(const QModelIndex &index)
    {
        m_label->setText(index.data().toString());
        const QString path = index.data(Qt::UserRole).toString();
        if (path == m_path) {
            layoutLabel();
            update();
            return;
        }
        m_path = path;
        m_hasImage = !path.isEmpty();
        setToolTip(path);
        m_content->setVisible(m_hasImage);
        layoutLabel();
        m_content->setSource(path, [this](const QSize &) {
            layoutLabel();
            update();
        }, [this](const QString &error) {
            setToolTip(m_path + QStringLiteral("\n无法读取图片：") + error);
            layoutLabel();
        });
        layoutLabel();
        update();
    }
    void unbind()
    {
        m_content->clear();
        m_content->hide();
        m_path.clear();
        m_hasImage = false;
    }
protected:
    void resizeEvent(QResizeEvent *) override { layoutLabel(); }
private:
    void layoutLabel()
    {
        const QRect area = rect().adjusted(6, 6, -6, -6);
        m_content->setGeometry(rect().adjusted(6, 6, -6, -m_label->sizeHint().height() - 12));
        if (m_hasImage && m_content->hasImage()) {
            const int height = qMin(qMax(0, area.height()), m_label->sizeHint().height());
            m_label->setGeometry(area.x(), area.bottom() - height + 1, qMax(0, area.width()), height);
        } else {
            m_label->setGeometry(area);
        }
    }
    QLabel *m_label;
    vivdemo::ImageContent *m_content;
    QString m_path;
    bool m_hasImage = false;
};

class Adapter : public viv::WidgetAdapter
{
public:
    explicit Adapter(vivdemo::ImageBackend *thumbnails) : m_thumbnails(thumbnails) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new ItemWidget(m_thumbnails, parent); }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ItemWidget *>(widget)->bind(index);
    }
    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<ItemWidget *>(widget)->unbind();
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(150, 100); }
private:
    vivdemo::ImageBackend *m_thumbnails;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    viv::installAccessibilityFactory();
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("accessibility-trace"), QStringLiteral("输出辅助焦点重排诊断日志")});
    parser.addOption({QStringLiteral("items"), QStringLiteral("项目数量"), QStringLiteral("count"), QStringLiteral("10000")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("accessibility-trace")))
        qputenv("VIV_FLOW_ACCESSIBILITY_TRACE", "1");
    ItemModel model(qMax(0, parser.value(QStringLiteral("items")).toInt()));
    vivdemo::ImageBackend thumbnails;
    Adapter adapter(&thumbnails);
    QMainWindow window;
    auto *view = new viv::VirtualFlowTableView(&window);
    view->setAccessibleName(QStringLiteral("流式表格"));
    view->setAdapter(&adapter);
    view->setModel(&model);
    view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
    window.setCentralWidget(view);
    window.setWindowTitle(QStringLiteral("VirtualFlowTableView — 拖动窗口调整列数"));
    auto *toolbar = window.addToolBar(QStringLiteral("布局"));
    auto *loadImages = toolbar->addAction(QStringLiteral("加载图片文件夹"));
    QObject::connect(loadImages, &QAction::triggered, &window, [&window, &model, &thumbnails]() {
        const QString folder = QFileDialog::getExistingDirectory(&window, QStringLiteral("选择图片文件夹"));
        if (folder.isEmpty())
            return;
        QStringList filters;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            filters.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
        const QDir directory(folder);
        const QFileInfoList files = directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
        QStringList images;
        for (const QFileInfo &file : files)
            images.append(file.absoluteFilePath());
        thumbnails.reset();
        model.setImages(images);
        window.statusBar()->showMessage(QStringLiteral("已加载 %1 张图片：%2").arg(images.size()).arg(folder));
    });
    const auto spin = [&window, &toolbar](const QString &label, int value, int minimum, int maximum) {
        toolbar->addWidget(new QLabel(label, &window));
        auto *control = new QSpinBox(&window);
        control->setRange(minimum, maximum);
        control->setValue(value);
        toolbar->addWidget(control);
        return control;
    };
    auto *minWidth = spin(QStringLiteral("最小宽度"), 100, 1, 1000);
    auto *maxWidth = spin(QStringLiteral("最大宽度"), 200, 1, 2000);
    const auto widths = [view, minWidth, maxWidth]() {
        view->setCellWidthRange(minWidth->value(), maxWidth->value());
    };
    QObject::connect(minWidth, QOverload<int>::of(&QSpinBox::valueChanged), view, [widths](int) { widths(); });
    QObject::connect(maxWidth, QOverload<int>::of(&QSpinBox::valueChanged), view, [widths](int) { widths(); });
    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("高度与间距"));
    auto *mode = new QComboBox(&window);
    mode->addItems({QStringLiteral("固定高度"), QStringLiteral("宽高比")});
    toolbar->addWidget(mode);
    auto *height = spin(QStringLiteral("固定高度"), 100, 1, 1000);
    toolbar->addWidget(new QLabel(QStringLiteral("宽 / 高"), &window));
    auto *ratio = new QDoubleSpinBox(&window);
    ratio->setRange(0.1, 10);
    ratio->setSingleStep(0.1);
    ratio->setValue(1);
    toolbar->addWidget(ratio);
    auto *rowGap = spin(QStringLiteral("行间距"), 0, 0, 100);
    auto *columnGap = spin(QStringLiteral("列间距"), 0, 0, 100);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), view, [view](int modeIndex) {
        view->setCellHeightMode(modeIndex == 0 ? viv::VirtualFlowTableView::CellHeightMode::Fixed
                                             : viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
    });
    QObject::connect(height, QOverload<int>::of(&QSpinBox::valueChanged), view, &viv::VirtualFlowTableView::setFixedCellHeight);
    QObject::connect(ratio, QOverload<double>::of(&QDoubleSpinBox::valueChanged), view, &viv::VirtualFlowTableView::setCellAspectRatio);
    QObject::connect(rowGap, QOverload<int>::of(&QSpinBox::valueChanged), view, &viv::VirtualFlowTableView::setRowSpacing);
    QObject::connect(columnGap, QOverload<int>::of(&QSpinBox::valueChanged), view, &viv::VirtualFlowTableView::setColumnSpacing);
    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("背景"));
    const auto toggle = [&window, &toolbar](const QString &label, bool value) {
        auto *control = new QCheckBox(label, &window);
        control->setChecked(value);
        toolbar->addWidget(control);
        return control;
    };
    QObject::connect(toggle(QStringLiteral("状态背景"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setVisualStateBackgroundVisible);
    QObject::connect(toggle(QStringLiteral("hover 行隙"), false), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setHoverBackgroundThroughRowSpacing);
    QObject::connect(toggle(QStringLiteral("select 行隙"), false), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setSelectedBackgroundThroughRowSpacing);
    QObject::connect(toggle(QStringLiteral("hover 列隙"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setHoverBackgroundThroughColumnSpacing);
    QObject::connect(toggle(QStringLiteral("select 列隙"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setSelectedBackgroundThroughColumnSpacing);
    QObject::connect(spin(QStringLiteral("圆角"), 0, 0, 100), QOverload<int>::of(&QSpinBox::valueChanged),
        view, &viv::VirtualFlowTableView::setStateBackgroundCornerRadius);
    QObject::connect(spin(QStringLiteral("动画 ms"), view->visualStateAnimationDuration(), 0, 2000),
        QOverload<int>::of(&QSpinBox::valueChanged), view, &viv::VirtualItemView::setVisualStateAnimationDuration);
    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("分割线"));
    QObject::connect(toggle(QStringLiteral("横线"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setHorizontalGridLinesVisible);
    QObject::connect(toggle(QStringLiteral("竖线"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setVerticalGridLinesVisible);
    QObject::connect(toggle(QStringLiteral("横线穿列隙"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setHorizontalSpacingLineThroughColumnSpacing);
    QObject::connect(toggle(QStringLiteral("竖线穿行隙"), true), &QCheckBox::toggled,
        view, &viv::VirtualFlowTableView::setVerticalSpacingLineThroughRowSpacing);
    QObject::connect(spin(QStringLiteral("横线 px"), 1, 1, 8), QOverload<int>::of(&QSpinBox::valueChanged),
        view, &viv::VirtualFlowTableView::setHorizontalGridLineWidth);
    QObject::connect(spin(QStringLiteral("竖线 px"), 1, 1, 8), QOverload<int>::of(&QSpinBox::valueChanged),
        view, &viv::VirtualFlowTableView::setVerticalGridLineWidth);
    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);
    QObject::connect(view, &viv::VirtualItemView::virtualizationUpdated, status, [view, status]() {
        const auto stats = view->stats();
        status->setText(QStringLiteral("列 %1 | 首列宽 %2 | 行 %3 | 控件 %4 / 项目 %5")
            .arg(view->columnCount()).arg(view->columnWidth(0)).arg(view->displayRowCount())
            .arg(stats.materializedItems).arg(stats.logicalItems));
    });
    window.resize(840, 600);
    window.show();
    return app.exec();
}
