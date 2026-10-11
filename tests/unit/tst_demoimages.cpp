#include "../../examples/common/demoimage.h"
#include "../../examples/masonry_view/cards.h"
#include <virtualitemviews/virtualmasonryview.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DemoImagesTest : public QObject
{
    Q_OBJECT
private slots:
    void catalogReadsOriginalDimensionsAndCancelsOldFolders()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(600, 200, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(directory.filePath(QStringLiteral("00-wide.png"))));
        image = QImage(90, 30, QImage::Format_RGB32);
        image.fill(Qt::blue);
        const QString rotated = directory.filePath(QStringLiteral("01-rotated.jpg"));
        QVERIFY(image.save(rotated));
        QFile jpeg(rotated);
        QVERIFY(jpeg.open(QIODevice::ReadOnly));
        QByteArray bytes = jpeg.readAll();
        jpeg.close();
        bytes.insert(2, QByteArray::fromHex(
            "ffe1002245786966000049492a0008000000010012010300010000000600000000000000"));
        QVERIFY(jpeg.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(jpeg.write(bytes), qint64(bytes.size()));
        jpeg.close();
        QFile broken(directory.filePath(QStringLiteral("02-broken.png")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("invalid image");
        broken.close();
        vivdemo::ImageCatalog catalog;
        int stale = 0;
        int completed = 0;
        QVector<vivdemo::ImageEntry> entries;
        catalog.loadFolder(directory.path(), [&](const auto &) { ++stale; });
        catalog.cancel();
        catalog.loadFolder(directory.path(), [&](const auto &result) {
            ++completed;
            entries = result;
            QCOMPARE(QThread::currentThread(), thread());
        });
        QTRY_COMPARE(completed, 1);
        QCOMPARE(stale, 0);
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).size, QSize(600, 200));
        QCOMPARE(entries.at(1).size, QSize(30, 90));
        QCOMPARE(entries.at(2).size, QSize(150, 120));
        catalog.loadFolder(directory.path(), [&](const auto &) { ++stale; });
        catalog.cancel();
        QTest::qWait(100);
        QCOMPARE(stale, 0);
    }

    void masonryImageCompletionDoesNotReflowLaterCards()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (int row = 0; row < 12; ++row) {
            QImage image(300, 100 + (row % 4) * 130, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(row * 25, 180, 220));
            QVERIFY(image.save(directory.filePath(
                QStringLiteral("%1.png").arg(row, 2, 10, QLatin1Char('0')))));
        }
        vivdemo::ImageCatalog catalog;
        QVector<vivdemo::ImageEntry> entries;
        catalog.loadFolder(directory.path(), [&](const auto &result) { entries = result; });
        QTRY_COMPARE(entries.size(), 12);
        masonrydemo::MasonryModel model(0);
        model.setImages(entries);
        vivdemo::ImageBackend backend;
        masonrydemo::MasonryAdapter adapter(&model, &backend);
        viv::VirtualMasonryView view;
        view.resize(420, 1200);
        view.setColumnCount(3);
        view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        view.setRowSpacing(12);
        view.setColumnSpacing(12);
        view.setAdapter(&adapter);
        view.setModel(&model);
        view.show();
        view.flushPendingRelayout();
        QCOMPARE(view.columnCount(), 3);
        QVector<QRect> before;
        QVector<int> columns;
        for (int row = 0; row < entries.size(); ++row) {
            before.append(view.visualRect(model.index(row, 0)));
            columns.append(view.itemColumn(model.index(row, 0)));
        }
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        const auto allLoaded = [&]() {
            for (int row = 0; row < entries.size(); ++row) {
                QWidget *widget = view.widgetForIndex(model.index(row, 0));
                if (!widget)
                    return false;
                bool loaded = false;
                for (QObject *child : widget->children()) {
                    if (auto *content = dynamic_cast<vivdemo::ImageContent *>(child))
                        loaded = content->hasImage();
                }
                if (!loaded)
                    return false;
            }
            return true;
        };
        QTRY_VERIFY(allLoaded());
        QTest::qWait(250);
        QCOMPARE(changes.count(), 0);
        for (int row = 0; row < entries.size(); ++row) {
            const QModelIndex index = model.index(row, 0);
            QCOMPARE(model.estimatedSize(index), entries.at(row).size);
            QCOMPARE(view.visualRect(index), before.at(row));
            QCOMPARE(view.itemColumn(index), columns.at(row));
        }
    }

    void loadsWithAspectAndKeepsBindingOnResize()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(600, 200, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString path = directory.filePath(QStringLiteral("wide.png"));
        QVERIFY(image.save(path));
        vivdemo::ImageBackend backend;
        vivdemo::ImageContent content(&backend, nullptr);
        content.resize(180, 100);
        content.show();
        int loaded = 0;
        int failed = 0;
        QSize size;
        content.setSource(path, [&](const QSize &value) { ++loaded; size = value; },
                          [&](const QString &) { ++failed; });
        QTRY_COMPARE(loaded, 1);
        QCOMPARE(failed, 0);
        QVERIFY(content.hasImage());
        QVERIFY(qAbs(qreal(size.width()) / size.height() - 3.0) < 0.05);
        content.resize(120, 240);
#ifdef VIV_DEMO_USE_ASTER
        auto *box = content.findChild<aster::gui::ImageBox *>();
        QVERIFY(box);
        QTRY_COMPARE(box->state(), aster::gui::ImageBoxState::Ready);
#endif
        content.setSource(path, [&](const QSize &) { ++loaded; });
        QTest::qWait(150);
        QCOMPARE(loaded, 1);
        content.clear();
        QVERIFY(!content.hasImage());
    }

    void recycleCancelsOldResultsAndReportsErrors()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(600, 200, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString wide = directory.filePath(QStringLiteral("wide.png"));
        QVERIFY(image.save(wide));
        image = QImage(100, 300, QImage::Format_RGB32);
        image.fill(Qt::blue);
        const QString tall = directory.filePath(QStringLiteral("tall.png"));
        QVERIFY(image.save(tall));
        vivdemo::ImageBackend backend;
        vivdemo::ImageContent content(&backend, nullptr);
        content.resize(120, 180);
        content.show();
        int stale = 0;
        int loaded = 0;
        QSize size;
        content.setSource(wide, [&](const QSize &) { ++stale; });
        content.clear();
        content.setSource(tall, [&](const QSize &value) { ++loaded; size = value; });
        QTRY_COMPARE(loaded, 1);
        QCOMPARE(stale, 0);
        QVERIFY(qAbs(qreal(size.width()) / size.height() - 1.0 / 3.0) < 0.02);
        int failed = 0;
        content.setSource(directory.filePath(QStringLiteral("missing.png")), {},
                          [&](const QString &error) { if (!error.isEmpty()) ++failed; });
        QTRY_COMPARE(failed, 1);
        QVERIFY(!content.hasImage());
        const QString uncached = directory.filePath(QStringLiteral("uncached.png"));
        QVERIFY(image.save(uncached));
        content.setSource(uncached, [&](const QSize &) { ++stale; });
        content.clear();
        QTest::qWait(150);
        QCOMPARE(stale, 0);
        QVERIFY(!content.hasImage());
    }
};

QTEST_MAIN(DemoImagesTest)
#include "tst_demoimages.moc"
