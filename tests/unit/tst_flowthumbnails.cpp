#include "../../examples/flow_table_view/thumbnailcache.h"

#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class FlowThumbnailsTest : public QObject
{
    Q_OBJECT
private slots:
    void sharedCacheAvoidsRereading();
    void cancelledRequestsDoNotReachReusedReceiver();
    void folderResetDiscardsOldResults();
    void destroyedReceiverAndFailedImage();
    void suppliedFolderRemainsResponsive();
};

void FlowThumbnailsTest::sharedCacheAvoidsRereading()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("image.png"));
    QImage image(1600, 800, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    flowdemo::ThumbnailCache cache;
    QObject first, second;
    int delivered = 0;
    QSize size;
    const auto receive = [&](const flowdemo::Thumbnail &thumbnail) {
        size = thumbnail.image.size();
        if (!thumbnail.image.isNull())
            ++delivered;
    };
    cache.request(path, &first, receive);
    cache.request(path, &second, receive);
    QCOMPARE(delivered, 0);
    QTRY_COMPARE(delivered, 2);
    QCOMPARE(size, QSize(512, 256));
    QVERIFY(QFile::remove(path));
    cache.request(path, &first, receive);
    QCOMPARE(delivered, 3);
}

void FlowThumbnailsTest::cancelledRequestsDoNotReachReusedReceiver()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::blue);
    flowdemo::ThumbnailCache cache;
    QObject receiver;
    int obsolete = 0;
    for (int i = 0; i < 20; ++i) {
        const QString path = directory.filePath(QStringLiteral("%1.png").arg(i));
        QVERIFY(image.save(path));
        cache.request(path, &receiver, [&](const flowdemo::Thumbnail &) { ++obsolete; });
    }
    cache.cancel(&receiver);
    int current = 0;
    cache.request(directory.filePath(QStringLiteral("19.png")), &receiver,
        [&](const flowdemo::Thumbnail &thumbnail) { if (!thumbnail.image.isNull()) ++current; });
    QTRY_COMPARE(current, 1);
    QCOMPARE(obsolete, 0);
}

void FlowThumbnailsTest::folderResetDiscardsOldResults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("image.png"));
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(path));
    flowdemo::ThumbnailCache cache;
    QObject receiver;
    int obsolete = 0, current = 0;
    cache.request(path, &receiver, [&](const flowdemo::Thumbnail &) { ++obsolete; });
    cache.reset();
    cache.request(path, &receiver,
        [&](const flowdemo::Thumbnail &thumbnail) { if (!thumbnail.image.isNull()) ++current; });
    QTRY_COMPARE(current, 1);
    QCOMPARE(obsolete, 0);
}

void FlowThumbnailsTest::destroyedReceiverAndFailedImage()
{
    flowdemo::ThumbnailCache cache;
    int obsolete = 0;
    auto *receiver = new QObject;
    cache.request(QStringLiteral("missing-image.png"), receiver,
        [&](const flowdemo::Thumbnail &) { ++obsolete; });
    delete receiver;
    QObject liveReceiver;
    bool delivered = false, failed = false;
    cache.request(QStringLiteral("missing-image.png"), &liveReceiver,
        [&](const flowdemo::Thumbnail &thumbnail) {
            failed = thumbnail.image.isNull() && !thumbnail.error.isEmpty();
            delivered = true;
        });
    QTRY_VERIFY(delivered);
    QVERIFY(failed);
    QCOMPARE(obsolete, 0);
}

void FlowThumbnailsTest::suppliedFolderRemainsResponsive()
{
    const QString folder = qEnvironmentVariable("VIV_FLOW_IMAGE_TEST_FOLDER");
    if (folder.isEmpty())
        QSKIP("Set VIV_FLOW_IMAGE_TEST_FOLDER to measure a real image folder.");
    QDir directory(folder);
    QVERIFY(directory.exists());
    QStringList filters;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        filters.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    const auto files = directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
    QVERIFY(!files.isEmpty());
    flowdemo::ThumbnailCache cache;
    QObject receiver;
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 lastTick = 0, longestTick = 0;
    int ticks = 0, delivered = 0, failed = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &receiver, [&]() {
        const qint64 now = elapsed.elapsed();
        longestTick = qMax(longestTick, now - lastTick);
        lastTick = now;
        ++ticks;
    });
    heartbeat.start(10);
    for (const auto &file : files) {
        cache.request(file.absoluteFilePath(), &receiver, [&](const flowdemo::Thumbnail &thumbnail) {
            ++delivered;
            if (thumbnail.image.isNull())
                ++failed;
        });
    }
    const qint64 enqueueMs = elapsed.elapsed();
    QTRY_COMPARE_WITH_TIMEOUT(delivered, int(files.size()), 60000);
    QVERIFY(ticks > 0);
    QCOMPARE(failed, 0);
    qInfo().nospace() << "FLOW_THUMBNAILS files=" << files.size() << " enqueueMs=" << enqueueMs
        << " totalMs=" << elapsed.elapsed() << " heartbeatTicks=" << ticks
        << " longestHeartbeatMs=" << longestTick;
}

QTEST_MAIN(FlowThumbnailsTest)
#include "tst_flowthumbnails.moc"
