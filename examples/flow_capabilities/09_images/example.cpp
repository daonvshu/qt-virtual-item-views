#include "example.h"
#include "ui_images.h"
#include "imagebackend.h"
#include <virtualitemviews/widgetadapter.h>
#include <QStandardItemModel>
#include <QDir>
#include <QFileDialog>
#include <QImageReader>
#include <QVBoxLayout>

namespace flow_images {

class ImageCard : public QWidget
{
public:
    ImageCard(flow_images_backend::ImageBackend *backend, QWidget *parent)
        : QWidget(parent), m_backend(backend)
    {
        setupUi();
    }
    void bind(const QModelIndex &index)
    {
        m_title->setText(index.data().toString());
        const QString path = index.data(Qt::UserRole).toString();
        setToolTip(path);
        // ImageContent guards asynchronous results with the current binding identity.
        m_image->setSource(path, {}, [this](const QString &error) { setToolTip(error); });
    }
    void unbind()
    {
        // Cancel old requests before this widget is rebound to a different file.
        m_image->clear();
        m_title->clear();
        setToolTip(QString());
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        m_image = new flow_images_backend::ImageContent(m_backend, this);
        m_title = new QLabel(this);
        m_title->setAlignment(Qt::AlignCenter);
        m_title->setMinimumWidth(0);
        m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(m_image, 1);
        layout->addWidget(m_title);
    }
    flow_images_backend::ImageBackend *m_backend;
    flow_images_backend::ImageContent *m_image = nullptr;
    QLabel *m_title = nullptr;
};

class ImageAdapter : public viv::WidgetAdapter
{
public:
    explicit ImageAdapter(flow_images_backend::ImageBackend *backend) : m_backend(backend) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new ImageCard(m_backend, parent);
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ImageCard *>(widget)->bind(index);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<ImageCard *>(widget)->unbind();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(180, 160); }
private:
    flow_images_backend::ImageBackend *m_backend;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }
    ~Example() override
    {
        // Destroy/unbind cards before their shared image backend is destroyed.
        delete m_ui.view;
    }

private:
    void setupUi()
    {
        m_ui.setupUi(this);
    }
    void setupExample()
    {
        m_model = new QStandardItemModel(0, 1, m_ui.view);
        // The adapter binds file paths; decoding and caching stay in this chapter.
        m_ui.view->setAdapter(new ImageAdapter(&m_backend), true);
        m_ui.view->setCellWidthRange(120, 220);
        m_ui.view->setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::Fixed);
        m_ui.view->setFixedCellHeight(180);
        m_ui.view->setColumnSpacing(8);
        m_ui.view->setRowSpacing(8);
        m_ui.view->setStateBackgroundCornerRadius(8);
        m_ui.view->setHorizontalGridLinesVisible(false);
        m_ui.view->setVerticalGridLinesVisible(false);
        m_ui.view->setModel(m_model);
        connect(m_ui.load, &QPushButton::clicked, this, [this]() { loadFolder(); });
        connect(m_ui.clear, &QPushButton::clicked, this, [this]() {
            m_model->removeRows(0, m_model->rowCount());
            m_backend.reset();
            m_ui.status->setText(QStringLiteral("已清空图片。"));
        });
        m_ui.status->setText(QStringLiteral("请选择图片文件夹。图片加载只发生在本章节。"));
    }
    void loadFolder()
    {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("选择图片文件夹"));
        if (folder.isEmpty())
            return;
        QStringList filters;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            filters.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
        const QFileInfoList files = QDir(folder).entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
        // Removing rows unbinds old cards before the cache generation is reset.
        m_model->removeRows(0, m_model->rowCount());
        m_backend.reset();
        QList<QStandardItem *> items;
        for (const QFileInfo &file : files) {
            auto *item = new QStandardItem(file.fileName());
            item->setData(file.absoluteFilePath(), Qt::UserRole);
            items.append(item);
        }
        m_model->invisibleRootItem()->appendRows(items);
        m_ui.status->setText(QStringLiteral("已加载 %1 个图片文件；滚动观察控件复用。").arg(files.size()));
    }

    Ui::FlowImages m_ui;
    flow_images_backend::ImageBackend m_backend;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace flow_images
