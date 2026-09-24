#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/virtualheaderview.h>

#include <QtTest>

#include <QLabel>
#include <QStandardItemModel>

using namespace viv;

namespace {
constexpr int kSectionWidth = 80;
constexpr int kHeaderHeight = 30;

/// Section widget of the test: one label, so bind/unbind are observable.
class SectionWidget : public QWidget
{
public:
    explicit SectionWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("sectionLabel"));
        m_label->setGeometry(0, 0, kSectionWidth, kHeaderHeight);
    }

    void setText(const QString &text) { m_label->setText(text); }
    QString text() const { return m_label->text(); }

private:
    QLabel *m_label = nullptr;
};

class SectionAdapter : public HeaderWidgetAdapter
{
public:
    QWidget *createSection(WidgetType, QWidget *parent) override
    {
        ++created;
        return new SectionWidget(parent);
    }

    void bindSection(QWidget *widget, int logicalIndex) override
    {
        ++bound;
        static_cast<SectionWidget *>(widget)->setText(QStringLiteral("s%1").arg(logicalIndex));
    }

    void unbindSection(QWidget *widget, int logicalIndex) override
    {
        ++unbound;
        static_cast<SectionWidget *>(widget)->setText(QString());
    }

    int created = 0;
    int bound = 0;
    int unbound = 0;
};
} // namespace

/// VirtualHeaderView materializes only the sections of its window (§17-§19).
class TestVirtualHeaderView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void materializesOnlyTheVisibleSections();
    void recyclesSectionsThatLeaveTheWindow();
    void sectionGeometryComesFromHeaderGeometry();
    void dragOnASectionEdgeResizesIt();
    void clickSetsTheSortIndicator();
    void paneFilterLimitsTheSections();

private:
    QStandardItemModel *m_model = nullptr;
    HeaderGeometry *m_geometry = nullptr;
    VirtualHeaderView *m_header = nullptr;
    SectionAdapter *m_adapter = nullptr;
};

void TestVirtualHeaderView::init()
{
    m_model = new QStandardItemModel(10, 40, this);
    QStringList labels;
    for (int column = 0; column < 40; ++column)
        labels << QStringLiteral("c%1").arg(column);
    m_model->setHorizontalHeaderLabels(labels);
    m_geometry = new HeaderGeometry(Qt::Horizontal, this);
    m_geometry->setSectionCount(m_model->columnCount());
    m_geometry->setDefaultSectionSize(kSectionWidth);
    m_adapter = new SectionAdapter;
    m_header = new VirtualHeaderView(Qt::Horizontal);
    m_header->resize(400, kHeaderHeight);
    m_header->setAdapter(m_adapter);
    m_header->setGeometryModel(m_geometry);
    m_header->setLabelModel(m_model);
    m_header->setSortInteractionEnabled(true);
    m_header->show();
    QApplication::processEvents();
}

void TestVirtualHeaderView::cleanup()
{
    delete m_header;
    m_header = nullptr;
    delete m_adapter;
    m_adapter = nullptr;
    delete m_geometry;
    m_geometry = nullptr;
    delete m_model;
    m_model = nullptr;
}

void TestVirtualHeaderView::materializesOnlyTheVisibleSections()
{
    // 400 px wide, 80 px sections, overscan 1: far fewer widgets than columns.
    const QList<int> sections = m_header->materializedSections();
    QVERIFY(!sections.isEmpty());
    QVERIFY(sections.size() <= 8);
    QVERIFY(m_header->materializedSectionCount() < m_model->columnCount());
    QCOMPARE(m_adapter->created, int(m_header->materializedSectionCount()));
    // The widget count follows the viewport, not the column count.
    m_geometry->setSectionCount(400);
    m_header->resize(200, kHeaderHeight);
    QApplication::processEvents();
    QVERIFY(m_header->materializedSectionCount() <= 8);
}

void TestVirtualHeaderView::recyclesSectionsThatLeaveTheWindow()
{
    const int createdAfterFirstPass = m_adapter->created;
    // Scroll ten sections: the first ones leave the window, the pool serves the
    // newly visible ones.
    m_geometry->setViewportOffset(10 * kSectionWidth);
    QApplication::processEvents();

    // Pooled widgets are reused: at most one extra widget for the slightly larger
    // window, and never one per column.
    QVERIFY(m_adapter->created <= createdAfterFirstPass + 1);
    QVERIFY(m_adapter->created <= 10);
    QVERIFY(m_adapter->unbound > 0);
    const QList<int> sections = m_header->materializedSections();
    QVERIFY(!sections.isEmpty());
    for (int logical : sections) {
        QWidget *widget = m_header->sectionWidget(logical);
        QVERIFY(widget != nullptr);
        QCOMPARE(static_cast<SectionWidget *>(widget)->text(), QStringLiteral("s%1").arg(logical));
    }
}

void TestVirtualHeaderView::sectionGeometryComesFromHeaderGeometry()
{
    m_geometry->resizeSection(0, 150);
    m_geometry->setSectionHidden(2, true);
    QApplication::processEvents();
    QWidget *first = m_header->sectionWidget(0);
    QVERIFY(first != nullptr);
    QCOMPARE(first->width(), 150);
    QCOMPARE(first->x(), 0);

    // A hidden section owns no widget; the following one closes the gap.
    QVERIFY(m_header->sectionWidget(2) == nullptr);
    QWidget *third = m_header->sectionWidget(3);
    QVERIFY(third != nullptr);
    QCOMPARE(third->x(), 150 + kSectionWidth);
}

void TestVirtualHeaderView::dragOnASectionEdgeResizesIt()
{
    const int edgeX = kSectionWidth - 1;
    QTest::mousePress(m_header, Qt::LeftButton, Qt::NoModifier, QPoint(edgeX, kHeaderHeight / 2));
    // QTest::mouseMove does not carry the pressed button in Qt 5, so the move is
    // sent explicitly for both Qt versions.
    QMouseEvent move(QEvent::MouseMove, QPointF(edgeX + 40, kHeaderHeight / 2), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_header, &move);
    QTest::mouseRelease(m_header, Qt::LeftButton, Qt::NoModifier,
                        QPoint(edgeX + 40, kHeaderHeight / 2));
    QCOMPARE(m_geometry->storedSectionSize(0), kSectionWidth + 40);
    QCOMPARE(m_header->sectionWidget(0)->width(), kSectionWidth + 40);
}

void TestVirtualHeaderView::clickSetsTheSortIndicator()
{
    const QPoint center(kSectionWidth / 2, kHeaderHeight / 2);
    QTest::mouseClick(m_header, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(m_geometry->sortIndicatorSection(), 0);
    QCOMPARE(m_geometry->sortIndicatorOrder(), Qt::AscendingOrder);
    QTest::mouseClick(m_header, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(m_geometry->sortIndicatorOrder(), Qt::DescendingOrder);
}

void TestVirtualHeaderView::paneFilterLimitsTheSections()
{
    m_header->setPaneFilter(QVector<int>({1, 3}), true);
    QApplication::processEvents();
    QCOMPARE(m_header->materializedSections(), QList<int>({1, 3}));
    QVERIFY(m_header->sectionWidget(0) == nullptr);
    QVERIFY(m_header->sectionWidget(1) != nullptr);
    QVERIFY(m_header->sectionWidget(2) == nullptr);
    QVERIFY(m_header->sectionWidget(3) != nullptr);

    m_header->clearPaneFilter();
    QApplication::processEvents();
    QVERIFY(m_header->materializedSections().size() > 2);
}

QTEST_MAIN(TestVirtualHeaderView)

#include "tst_virtualheaderview.moc"
