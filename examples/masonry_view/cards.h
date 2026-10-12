#pragma once

#include <virtualitemviews/widgetadapter.h>
#include "../common/demoimage.h"
#include "../common/imagecatalog.h"

#include <QAbstractListModel>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QResizeEvent>

namespace masonrydemo {
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

    void setImages(const QVector<vivdemo::ImageEntry> &entries)
    {
        beginResetModel();
        m_paths.clear();
        m_sizes.clear();
        m_paths.reserve(entries.size());
        m_sizes.reserve(entries.size());
        for (const auto &entry : entries) {
            m_paths.append(entry.path);
            m_sizes.append(entry.size.isEmpty() ? QSize(150, 120) : entry.size);
        }
        m_imageMode = true;
        endResetModel();
    }

    void setSyntheticItems(int count)
    {
        beginResetModel();
        m_imageMode = false;
        m_paths.clear();
        m_sizes.resize(count);
        for (int row = 0; row < m_sizes.size(); ++row)
            m_sizes[row] = QSize(150, 80 + (row * 47) % 220);
        endResetModel();
    }

private:
    QVector<QSize> m_sizes;
    QStringList m_paths;
    bool m_imageMode = false;
};

class ImageWidget : public QWidget
{
public:
    ImageWidget(vivdemo::ImageBackend *images, QWidget *parent)
        : QWidget(parent), m_content(new vivdemo::ImageContent(images, this))
    {}

    void bind(const QModelIndex &index)
    {
        m_path = index.data(Qt::UserRole).toString();
        m_title = index.data().toString();
        setToolTip(m_path);
        layoutImage();
        m_content->setSource(m_path, [this](const QSize &) { update(); },
            [this](const QString &error) {
                setToolTip(m_path + QStringLiteral("\n无法读取图片：") + error);
            });
        update();
    }

    void unbind()
    {
        m_content->clear();
        m_content->hide();
        m_path.clear();
        m_title.clear();
    }

protected:
    void resizeEvent(QResizeEvent *) override { layoutImage(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);

        const QFontMetrics metrics(font());
        const int titleHeight = metrics.height() + 12;
        const QRect imageArea = rect().adjusted(7, 7, -7, -titleHeight);
        if (!m_content->hasImage()) {
            painter.setPen(QColor(QStringLiteral("#64748b")));
            painter.drawText(imageArea, Qt::AlignCenter, m_path.isEmpty()
                ? m_title : QStringLiteral("加载中…"));
        }

        painter.setPen(QColor(QStringLiteral("#1e3a8a")));
        painter.drawText(rect().adjusted(8, rect().height() - titleHeight, -8, -5),
                         Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(
                             m_title, Qt::ElideMiddle, qMax(0, rect().width() - 16)));
    }

private:
    void layoutImage()
    {
        const int titleHeight = QFontMetrics(font()).height() + 12;
        m_content->setGeometry(rect().adjusted(7, 7, -7, -titleHeight));
        m_content->setVisible(!m_path.isEmpty());
    }

    vivdemo::ImageContent *m_content;
    QString m_path;
    QString m_title;
};

class MasonryAdapter : public viv::WidgetAdapter
{
public:
    MasonryAdapter(MasonryModel *model, vivdemo::ImageBackend *thumbnails)
        : m_model(model), m_thumbnails(thumbnails) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new ImageWidget(m_thumbnails, parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ImageWidget *>(widget)->bind(index);
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<ImageWidget *>(widget)->unbind();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return m_model->estimatedSize(index);
    }

private:
    MasonryModel *m_model = nullptr;
    vivdemo::ImageBackend *m_thumbnails = nullptr;
};

} // namespace masonrydemo
