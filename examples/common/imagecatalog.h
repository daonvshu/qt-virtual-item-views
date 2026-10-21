#pragma once

#include <QDir>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QObject>
#include <QRunnable>
#include <QThreadPool>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace vivdemo {

struct ImageEntry
{
    QString path;
    QSize size;
};

class ImageCatalog : public QObject
{
public:
    using Completed = std::function<void(const QVector<ImageEntry> &)>;

    explicit ImageCatalog(QObject *parent = nullptr) : QObject(parent)
    {
        m_pool.setMaxThreadCount(1);
    }

    ~ImageCatalog() override
    {
        cancel();
        m_pool.waitForDone();
    }

    void cancel()
    {
        if (m_cancelled)
            m_cancelled->store(true);
        m_pool.clear();
    }

    void loadFolder(const QString &folder, Completed completed)
    {
        cancel();
        m_cancelled = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = m_cancelled;
        auto scanFolder = [this, folder, completed = std::move(completed), cancelled]() {
            QStringList filters;
            for (const QByteArray &format : QImageReader::supportedImageFormats())
                filters.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
            const auto files = QDir(folder).entryInfoList(
                filters, QDir::Files | QDir::Readable, QDir::Name);
            QVector<ImageEntry> entries;
            entries.reserve(files.size());
            for (const QFileInfo &file : files) {
                if (cancelled->load())
                    return;
                QImageReader reader(file.absoluteFilePath());
                reader.setAutoTransform(true);
                QSize size = reader.size();
                if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90))
                    size.transpose();
                if (size.isEmpty())
                    size = QSize(150, 120);
                entries.append({file.absoluteFilePath(), size});
            }
            QMetaObject::invokeMethod(this,
                [completed, entries = std::move(entries), cancelled]() {
                    if (!cancelled->load() && completed)
                        completed(entries);
                }, Qt::QueuedConnection);
        };
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        m_pool.start(QRunnable::create(std::move(scanFolder)));
#else
        m_pool.start(new FolderScanTask(std::move(scanFolder)));
#endif
    }

private:
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    class FolderScanTask : public QRunnable
    {
    public:
        explicit FolderScanTask(std::function<void()> scan) : m_scan(std::move(scan)) {}
        void run() override { m_scan(); }

    private:
        std::function<void()> m_scan;
    };
#endif

    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace vivdemo
