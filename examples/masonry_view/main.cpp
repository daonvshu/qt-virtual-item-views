#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>

#include "../flow_table_view/thumbnailcache.h"

#include <QAbstractListModel>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMainWindow>
#include <QMimeData>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

namespace {

const QString kMasonryRowFormat = QStringLiteral("application/x-viv-masonry-row");

class MasonryModel : public QAbstractListModel
{
public:
    explicit MasonryModel(int count) : m_sizes(count)
    {
        for (int row = 0; row < m_sizes.size(); ++row)
            m_sizes[row] = QSize(150, 80 + (row * 47) % 220);
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_sizes.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_sizes.size())
            return {};
        if (role == Qt::DisplayRole)
            return m_imageMode ? QFileInfo(m_paths.at(index.row())).fileName()
                               : QStringLiteral("项目 %1\n高度 %2")
                                     .arg(index.row()).arg(m_sizes.at(index.row()).height());
        if (role == Qt::UserRole)
            return m_imageMode ? m_paths.at(index.row()) : QVariant();
        if (role == Qt::UserRole + 1)
            return m_sizes.at(index.row());
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::ItemIsDropEnabled;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled;
    }

    QStringList mimeTypes() const override { return {kMasonryRowFormat}; }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        for (const QModelIndex &index : indexes) {
            if (!index.isValid() || index.column() != 0)
                continue;
            auto *mime = new QMimeData;
            mime->setData(kMasonryRowFormat, QByteArray::number(index.row()));
            return mime;
        }
        return nullptr;
    }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override
    {
        if (!data || action != Qt::MoveAction || parent.isValid() || column > 0
            || !data->hasFormat(kMasonryRowFormat))
            return false;
        return row >= 0 && row <= rowCount();
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent))
            return false;
        const int source = data->data(kMasonryRowFormat).toInt();
        if (source < 0 || source >= rowCount() || row == source || row == source + 1)
            return false;
        if (!beginMoveRows(QModelIndex(), source, source, QModelIndex(), row))
            return false;
        m_sizes.move(source, row > source ? row - 1 : row);
        if (m_imageMode)
            m_paths.move(source, row > source ? row - 1 : row);
        endMoveRows();
        return true;
    }

    QSize estimatedSize(const QModelIndex &index) const
    {
        return index.isValid() && index.row() >= 0 && index.row() < m_sizes.size()
            ? m_sizes.at(index.row()) : QSize(150, 120);
    }

    void setImages(const QStringList &paths)
    {
        beginResetModel();
        m_paths = paths;
        m_sizes.fill(QSize(150, 120), paths.size());
        m_imageMode = true;
        endResetModel();
    }

    void setImageSize(const QModelIndex &index, const QSize &size)
    {
        if (!m_imageMode || !index.isValid() || index.row() < 0 || index.row() >= m_sizes.size()
            || size.width() <= 0 || size.height() <= 0 || m_sizes.at(index.row()) == size)
            return;
        m_sizes[index.row()] = size;
        emit dataChanged(index, index, {Qt::UserRole + 1});
    }

private:
    QVector<QSize> m_sizes;
    QStringList m_paths;
    bool m_imageMode = false;
};

class ImageWidget : public QWidget
{
public:
    ImageWidget(MasonryModel *model, flowdemo::ThumbnailCache *thumbnails, QWidget *parent)
        : QWidget(parent), m_model(model), m_thumbnails(thumbnails)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void bind(const QModelIndex &index)
    {
        ++m_binding;
        m_thumbnails->cancel(this);
        m_index = QPersistentModelIndex(index);
        m_path = index.data(Qt::UserRole).toString();
        m_title = index.data().toString();
        m_image = QPixmap();
        setToolTip(m_path);
        const quint64 binding = m_binding;
        if (!m_path.isEmpty()) {
            m_thumbnails->request(m_path, this,
                [this, path = m_path, binding](const flowdemo::Thumbnail &thumbnail) {
                    if (binding != m_binding || path != m_path)
                        return;
                    m_image = thumbnail.image;
                    if (!m_image.isNull()) {
                        m_model->setImageSize(QModelIndex(m_index), m_image.size());
                        setToolTip(path);
                    } else {
                        setToolTip(path + QStringLiteral("\n无法读取图片：") + thumbnail.error);
                    }
                    update();
                });
        }
        update();
    }

    void unbind()
    {
        ++m_binding;
        m_thumbnails->cancel(this);
        m_index = QPersistentModelIndex();
        m_path.clear();
        m_title.clear();
        m_image = QPixmap();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#eff6ff")));
        painter.setPen(QColor(QStringLiteral("#60a5fa")));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const QFontMetrics metrics(font());
        const int titleHeight = metrics.height() + 12;
        const QRect imageArea = rect().adjusted(7, 7, -7, -titleHeight);
        if (!m_image.isNull() && !imageArea.isEmpty()) {
            const QSize size = m_image.size().scaled(imageArea.size(), Qt::KeepAspectRatio);
            const QRect target(imageArea.x() + (imageArea.width() - size.width()) / 2,
                               imageArea.y() + (imageArea.height() - size.height()) / 2,
                               size.width(), size.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawPixmap(target, m_image);
        } else {
            painter.setPen(QColor(QStringLiteral("#64748b")));
            painter.drawText(imageArea, Qt::AlignCenter, m_path.isEmpty()
                ? QStringLiteral("等待图片") : QStringLiteral("加载中…"));
        }

        painter.setPen(QColor(QStringLiteral("#1e3a8a")));
        painter.drawText(rect().adjusted(8, rect().height() - titleHeight, -8, -5),
                         Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(
                             m_title, Qt::ElideMiddle, qMax(0, rect().width() - 16)));
    }

private:
    MasonryModel *m_model = nullptr;
    flowdemo::ThumbnailCache *m_thumbnails = nullptr;
    QPersistentModelIndex m_index;
    QPixmap m_image;
    QString m_path;
    QString m_title;
    quint64 m_binding = 0;
};

class MasonryAdapter : public viv::WidgetAdapter
{
public:
    MasonryAdapter(MasonryModel *model, flowdemo::ThumbnailCache *thumbnails)
        : m_model(model), m_thumbnails(thumbnails) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new ImageWidget(m_model, m_thumbnails, parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ImageWidget *>(widget)->bind(index);
    }

    void unbindWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<ImageWidget *>(widget)->unbind();
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        return m_model->estimatedSize(index);
    }

private:
    MasonryModel *m_model = nullptr;
    flowdemo::ThumbnailCache *m_thumbnails = nullptr;
};

QStringList imageFilesInFolder(const QString &folder)
{
    QStringList filters;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        filters.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    const QFileInfoList files = QDir(folder).entryInfoList(
        filters, QDir::Files | QDir::Readable, QDir::Name);
    QStringList paths;
    paths.reserve(files.size());
    for (const QFileInfo &file : files)
        paths.append(file.absoluteFilePath());
    return paths;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MasonryModel model(1000);
    flowdemo::ThumbnailCache thumbnails;
    MasonryAdapter adapter(&model, &thumbnails);
    viv::VirtualMasonryView view;
    view.setAccessibleName(QStringLiteral("瀑布流视图"));
    view.setAdapter(&adapter);
    view.setModel(&model);
    view.setCellWidthRange(100, 200);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setColumnSpacing(12);
    view.setRowSpacing(12);
    view.setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
    view.setDragDropActions(Qt::MoveAction);
    view.setDefaultDropAction(Qt::MoveAction);

    QMainWindow window;
    window.setCentralWidget(&view);
    window.setWindowTitle(QStringLiteral("VirtualMasonryView — 最短列瀑布流"));
    auto *toolbar = window.addToolBar(QStringLiteral("瀑布流"));
    auto *loadImages = toolbar->addAction(QStringLiteral("加载图片文件夹"));
    QObject::connect(loadImages, &QAction::triggered, &window,
        [&window, &model, &thumbnails]() {
            const QString folder = QFileDialog::getExistingDirectory(
                &window, QStringLiteral("选择图片文件夹"));
            if (folder.isEmpty())
                return;
            const QStringList paths = imageFilesInFolder(folder);
            thumbnails.reset();
            model.setImages(paths);
            window.statusBar()->showMessage(
                QStringLiteral("已加载 %1 张图片：%2").arg(paths.size()).arg(folder));
        });

    auto *dragEnabled = new QCheckBox(QStringLiteral("允许拖放"), &window);
    dragEnabled->setChecked(true);
    toolbar->addWidget(dragEnabled);
    QObject::connect(dragEnabled, &QCheckBox::toggled, &view,
        &viv::VirtualItemView::setDragEnabled);
    view.setDragEnabled(true);
    auto *dropIndicator = new QCheckBox(QStringLiteral("拖放指示器"), &window);
    dropIndicator->setChecked(true);
    toolbar->addWidget(dropIndicator);
    QObject::connect(dropIndicator, &QCheckBox::toggled, &view,
        &viv::VirtualItemView::setDropIndicatorShown);
    view.setDropIndicatorShown(true);

    toolbar->addWidget(new QLabel(QStringLiteral("列数"), &window));
    auto *columns = new QSpinBox(&window);
    columns->setRange(0, 20);
    columns->setValue(0);
    toolbar->addWidget(columns);
    QObject::connect(columns, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setColumnCount);

    toolbar->addWidget(new QLabel(QStringLiteral("高度模式"), &window));
    auto *mode = new QComboBox(&window);
    mode->addItems({QStringLiteral("固定"), QStringLiteral("宽高比"), QStringLiteral("估算")});
    mode->setCurrentIndex(2);
    toolbar->addWidget(mode);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), &view,
        [&view](int index) {
            view.setItemHeightMode(static_cast<viv::VirtualMasonryView::ItemHeightMode>(index));
        });

    toolbar->addWidget(new QLabel(QStringLiteral("列间距"), &window));
    auto *columnSpacing = new QSpinBox(&window);
    columnSpacing->setRange(0, 100);
    columnSpacing->setValue(view.columnSpacing());
    toolbar->addWidget(columnSpacing);
    QObject::connect(columnSpacing, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setColumnSpacing);

    toolbar->addWidget(new QLabel(QStringLiteral("行间距"), &window));
    auto *rowSpacing = new QSpinBox(&window);
    rowSpacing->setRange(0, 100);
    rowSpacing->setValue(view.rowSpacing());
    toolbar->addWidget(rowSpacing);
    QObject::connect(rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setRowSpacing);

    auto *verticalLines = new QCheckBox(QStringLiteral("竖分割线"), &window);
    verticalLines->setChecked(view.verticalGridLinesVisible());
    toolbar->addWidget(verticalLines);
    QObject::connect(verticalLines, &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setVerticalGridLinesVisible);
    auto *horizontalLines = new QCheckBox(QStringLiteral("横分割线"), &window);
    horizontalLines->setChecked(view.horizontalGridLinesVisible());
    toolbar->addWidget(horizontalLines);
    QObject::connect(horizontalLines, &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setHorizontalGridLinesVisible);

    auto *heightAnimation = new QCheckBox(QStringLiteral("高度动画"), &window);
    heightAnimation->setChecked(view.itemHeightAnimationEnabled());
    toolbar->addWidget(heightAnimation);
    QObject::connect(heightAnimation, &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setItemHeightAnimationEnabled);
    toolbar->addWidget(new QLabel(QStringLiteral("动画 ms"), &window));
    auto *animationDuration = new QSpinBox(&window);
    animationDuration->setRange(0, 2000);
    animationDuration->setValue(view.itemHeightAnimationDuration());
    toolbar->addWidget(animationDuration);
    QObject::connect(animationDuration, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setItemHeightAnimationDuration);

    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);
    const auto updateStatus = [&view, &model, status]() {
        const auto stats = view.stats();
        status->setText(QStringLiteral("列 %1 | 项目 %2 | 已物化 %3")
            .arg(view.columnCount()).arg(model.rowCount()).arg(stats.materializedItems));
    };
    QObject::connect(&view, &viv::VirtualItemView::virtualizationUpdated, &window,
        updateStatus);
    QObject::connect(&view, &viv::VirtualMasonryView::masonryLayoutChanged, &window,
        updateStatus);
    updateStatus();

    window.resize(900, 700);
    window.show();
    return app.exec();
}
