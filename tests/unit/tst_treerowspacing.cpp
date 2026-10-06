#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/virtualheaderview.h>
#include "vivtestfixtures.h"
#include "../../src/core/pixelalignedlines_p.h"

#include <QtTest>
#include <QLabel>
#include <QPainter>
#include <QStandardItemModel>
#include <type_traits>

namespace {

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
        const QColor color = index.data(Qt::BackgroundRole).value<QColor>();
        widget->setStyleSheet(color.isValid()
            ? QStringLiteral("background-color: %1;").arg(color.name()) : QString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 24); }
};

class RoundedBackgroundTable : public viv::VirtualTableView
{
protected:
    void paintStateBackgroundLayer(QPainter *painter, const QRect &extended,
                                  const QRegion &clip, const QRegion &,
                                  const QColor &color) const override
    {
        paintRoundedStateBackgroundLayer(painter, extended, clip, color, 10);
    }
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
        const QColor color = index.data(Qt::BackgroundRole).value<QColor>();
        widget->setStyleSheet(color.isValid()
            ? QStringLiteral("background-color: %1;").arg(color.name()) : QString());
    }
};

class CenteredBranchRenderer : public viv::BranchIndicatorRenderer
{
public:
    mutable QHash<QModelIndex, QRect> rects;
    const QColor color = QColor(20, 180, 70);

    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &index, const QRect &rect) const override
    {
        if (!state.adjoinsItem)
            return;
        rects.insert(index, rect);
        painter->fillRect(QRect(rect.center() - QPoint(3, 3), QSize(8, 8)), color);
    }
};

template<typename View>
void exerciseSpacing(bool cells)
{
    QStandardItemModel model;
    auto *p = new QStandardItem(QStringLiteral("P"));
    auto *a = new QStandardItem(QStringLiteral("a"));
    auto *g = new QStandardItem(QStringLiteral("g"));
    auto *b = new QStandardItem(QStringLiteral("b"));
    auto *q = new QStandardItem(QStringLiteral("Q"));
    auto *childQ = new QStandardItem(QStringLiteral("q"));
    auto *r = new QStandardItem(QStringLiteral("R"));
    a->appendRow(g);
    p->appendRow(a);
    p->appendRow(b);
    q->appendRow(childQ);
    model.appendRow(p);
    model.appendRow(q);
    model.appendRow(r);
    p->setData(11, View::NodeRowSpacingRole);
    a->setData(2, View::NodeRowSpacingRole);
    g->setData(7, View::NodeRowSpacingRole);
    b->setData(0, View::NodeRowSpacingRole);
    q->setData(13, View::NodeRowSpacingRole);

    View view;
    view.setAdapter(new RowAdapter, true);
    if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
        view.setCellAdapter(new CellAdapter, true);
        if (cells)
            view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    }
    view.setUniformItemHeight(24);
    view.setModel(&model);
    view.setRowSpacing(4);
    vivtest::showView(&view, QSize(500, 500));

    const auto verify = [&](const QList<QStandardItem *> &nodes, const QVector<int> &gaps) {
        view.flushPendingRelayout();
        QCOMPARE(view.visibleRowCount(), qsizetype(nodes.size()));
        for (int i = 0; i < nodes.size(); ++i) {
            const QRect body = view.visualRect(nodes.at(i)->index());
            QVERIFY(body.isValid());
            if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
                if (i + 1 < nodes.size())
                    QCOMPARE(view.verticalHeaderGeometry()->sectionSpacingAfter(i), gaps.at(i));
                auto *header = dynamic_cast<viv::VirtualHeaderView *>(view.verticalHeader());
                QVERIFY(header);
                QWidget *section = header->sectionWidget(i);
                QVERIFY(section);
                QCOMPARE(header->y() + section->y(), view.viewport()->y() + body.y());
            }
            if (i + 1 < nodes.size()) {
                const int gap = gaps.at(i);
                QCOMPARE(view.visualRect(nodes.at(i + 1)->index()).top() - body.bottom() - 1, gap);
                if (gap > 0)
                    QCOMPARE(static_cast<const viv::VirtualItemView &>(view).indexAt(
                                 QPoint(body.center().x(), body.bottom() + 1)), QModelIndex());
            }
        }
        QCOMPARE(view.contentExtent(), qint64(view.visualRect(nodes.last()->index()).bottom() + 1));
    };

    verify({p, q, r}, {11, 13});
    p->setData(30, View::NodeRowSpacingAboveRole);
    q->setData(5, View::NodeRowSpacingAboveRole);
    r->setData(8, View::NodeRowSpacingAboveRole);
    verify({p, q, r}, {16, 21});
    QCOMPARE(view.visualRect(p->index()).top(), 0);
    view.expand(p->index());
    verify({p, a, b, q, r}, {11, 2, 5, 21});
    a->setData(3, View::NodeRowSpacingAboveRole);
    verify({p, a, b, q, r}, {14, 2, 5, 21});
    view.expand(a->index());
    g->setData(6, View::NodeRowSpacingAboveRole);
    verify({p, a, g, b, q, r}, {14, 8, 7, 5, 21});
    view.collapse(p->index());
    verify({p, q, r}, {16, 21});
    q->setData(-1, View::NodeRowSpacingAboveRole);
    verify({p, q, r}, {11, 21});
    for (QStandardItem *node : {p, a, g, q, r})
        node->setData(QVariant(), View::NodeRowSpacingAboveRole);
    view.collapse(a->index());
    verify({p, q, r}, {11, 13});
    view.expand(p->index());
    verify({p, a, b, q, r}, {11, 2, 0, 13});
    view.expand(a->index());
    verify({p, a, g, b, q, r}, {11, 2, 7, 0, 13});
    view.expand(q->index());
    verify({p, a, g, b, q, childQ, r}, {11, 2, 7, 0, 13, 4});
    view.collapse(p->index());
    verify({p, q, childQ, r}, {11, 13, 4});
    view.expand(p->index());
    verify({p, a, g, b, q, childQ, r}, {11, 2, 7, 0, 13, 4});
    a->setData(0, View::NodeRowSpacingRole);
    verify({p, a, g, b, q, childQ, r}, {11, 0, 7, 0, 13, 4});
    a->setData(-1, View::NodeRowSpacingRole);
    g->setData(QVariant(), View::NodeRowSpacingRole);
    verify({p, a, g, b, q, childQ, r}, {11, 4, 4, 0, 13, 4});
    view.collapse(a->index());
    verify({p, a, b, q, childQ, r}, {11, 4, 0, 13, 4});
    view.setRowSpacing(6);
    verify({p, a, b, q, childQ, r}, {11, 6, 0, 13, 6});
    view.setRootIndex(p->index());
    verify({a, b}, {6});
    view.expand(a->index());
    verify({a, g, b}, {6, 6});
    auto *added = new QStandardItem(QStringLiteral("added"));
    added->setData(15, View::NodeRowSpacingRole);
    added->setData(7, View::NodeRowSpacingAboveRole);
    p->insertRow(1, added);
    verify({a, g, added, b}, {6, 13, 15});
    p->removeRow(1);
    verify({a, g, b}, {6, 6});
    view.setRootIndex(QModelIndex());
    view.collapseAll();
    verify({p, q, r}, {11, 13});
    model.clear();
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
}

} // namespace

template<typename View>
void exerciseExpansionAnimation(bool cells)
{
    QStandardItemModel model;
    auto *parent = new QStandardItem(QStringLiteral("parent"));
    for (int i = 0; i < 8; ++i)
        parent->appendRow(new QStandardItem(QStringLiteral("child %1").arg(i)));
    const QColor childColor(20, 180, 70);
    const QColor tailColor(25, 90, 210);
    parent->child(0)->setData(childColor, Qt::BackgroundRole);
    parent->child(0)->setData(5, View::NodeRowSpacingAboveRole);
    parent->child(7)->setData(7, View::NodeRowSpacingBelowRole);
    model.appendRow(parent);
    auto *tail = new QStandardItem(QStringLiteral("tail"));
    tail->setData(tailColor, Qt::BackgroundRole);
    model.appendRow(tail);
    View view;
    view.setAdapter(new RowAdapter, true);
    if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
        view.setCellAdapter(new CellAdapter, true);
        if (cells)
            view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    }
    view.setUniformItemHeight(24);
    view.setModel(&model);
    view.setRowSpacing(3);
    vivtest::showView(&view, QSize(420, 340));
    QVERIFY(!view.expansionAnimationEnabled());
    QCOMPARE(view.expansionAnimationDuration(), 300);
    view.setExpansionAnimationEnabled(true);
    view.setExpansionAnimationDuration(400);
    view.setCurrentIndex(parent->index());
    const QImage before = view.grab().toImage();
    view.expand(parent->index());
    QCOMPARE(view.visibleRowCount(), qsizetype(10));
    QCOMPARE(view.currentIndex(), parent->index());
    auto *overlay = view.template findChild<QWidget *>(QStringLiteral("vivTreeExpansionTransition"));
    QVERIFY(overlay);
    QVERIFY(overlay->isVisible());
    QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
    QTest::qWait(100);
    const QImage middle = view.grab().toImage();
    QTRY_VERIFY(!overlay->isVisible());
    const QImage after = view.grab().toImage();
    const QString frames = QCoreApplication::applicationDirPath()
        + QStringLiteral("/expansion-%1-%2-").arg(
            QString::fromLatin1(view.metaObject()->className()).replace(QLatin1Char(':'), QLatin1Char('_')))
            .arg(cells);
    QVERIFY(before.save(frames + QStringLiteral("before.png")));
    QVERIFY(middle.save(frames + QStringLiteral("middle.png")));
    QVERIFY(after.save(frames + QStringLiteral("after.png")));
    QVERIFY(before != after);
    QVERIFY(middle != before);
    QVERIFY(middle != after);
    const auto colorRows = [](const QImage &image, const QColor &color) {
        int first = -1;
        int last = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == color) {
                    if (first < 0)
                        first = y;
                    last = y;
                    break;
                }
            }
        }
        return qMakePair(first, last);
    };
    const auto tailBefore = colorRows(before, tailColor);
    const auto tailMiddle = colorRows(middle, tailColor);
    const auto tailAfter = colorRows(after, tailColor);
    QVERIFY(tailBefore.first >= 0);
    QVERIFY(tailMiddle.first > tailBefore.first);
    QVERIFY(tailMiddle.first < tailAfter.first);
    const auto greenHeight = [](const QImage &image) {
        int rows = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.green() > color.red() + 30 && color.green() > color.blue() + 30) {
                    ++rows;
                    break;
                }
            }
        }
        return rows;
    };
    QVERIFY(greenHeight(middle) > 0);
    QVERIFY(greenHeight(middle) < greenHeight(after));
    view.collapse(parent->index());
    QTest::qWait(100);
    const QImage collapsing = view.grab().toImage();
    const auto tailCollapsing = colorRows(collapsing, tailColor);
    QVERIFY(tailCollapsing.first > tailBefore.first);
    QVERIFY(tailCollapsing.first < tailAfter.first);
    QVERIFY(greenHeight(collapsing) > 0);
    QVERIFY(greenHeight(collapsing) < greenHeight(after));
    QTRY_VERIFY(!overlay->isVisible());
    QCOMPARE(colorRows(view.grab().toImage(), tailColor), tailBefore);
    view.expand(parent->index());
    QTRY_VERIFY(!overlay->isVisible());
    view.collapse(parent->index());
    QVERIFY(overlay->isVisible());
    QCOMPARE(view.visibleRowCount(), qsizetype(2));
    view.expand(parent->index());
    QCOMPARE(view.visibleRowCount(), qsizetype(10));
    QVERIFY(overlay->isVisible());
    view.setExpansionAnimationEnabled(false);
    QVERIFY(!overlay->isVisible());
    view.collapse(parent->index());
    QVERIFY(!overlay->isVisible());
    view.setExpansionAnimationEnabled(true);
    view.setExpansionAnimationDuration(-1);
    QCOMPARE(view.expansionAnimationDuration(), 0);
    view.expand(parent->index());
    QVERIFY(!overlay->isVisible());
    view.setExpansionAnimationDuration(400);
    view.collapseAll();
    QVERIFY(overlay->isVisible());
    view.expandRecursively(parent->index());
    QVERIFY(overlay->isVisible());
    view.resize(430, 350);
    QVERIFY(!overlay->isVisible());
    view.collapse(parent->index());
    QVERIFY(overlay->isVisible());
    view.setRootIndex(parent->index());
    QVERIFY(!overlay->isVisible());
    view.setRootIndex(QModelIndex());
    view.expand(parent->index());
    QVERIFY(overlay->isVisible());
    model.clear();
    QVERIFY(!overlay->isVisible());
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
}

template<typename View>
void exerciseCollapseIncomingRows(bool cells, bool all)
{
    QStandardItemModel model;
    auto *parent = new QStandardItem(QStringLiteral("parent"));
    for (int i = 0; i < 4; ++i)
        parent->appendRow(new QStandardItem(QStringLiteral("child %1").arg(i)));
    model.appendRow(parent);
    auto *group = all ? nullptr : new QStandardItem(QStringLiteral("group"));
    if (!all)
        model.appendRow(group);
    const QColor firstColor(25, 90, 210);
    const QColor secondColor(180, 50, 150);
    for (int i = 0; i < 40; ++i) {
        auto *tail = new QStandardItem(QStringLiteral("tail %1").arg(i));
        if (i == 3)
            tail->setData(firstColor, Qt::BackgroundRole);
        if (i == 4)
            tail->setData(secondColor, Qt::BackgroundRole);
        if (all) {
            tail->appendRow(new QStandardItem(QStringLiteral("nested")));
            model.appendRow(tail);
        } else {
            group->appendRow(tail);
        }
    }
    View view;
    view.setAdapter(new RowAdapter, true);
    if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
        view.setCellAdapter(new CellAdapter, true);
        if (cells)
            view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    }
    view.setUniformItemHeight(24);
    view.setModel(&model);
    view.expand(parent->index());
    if (all) {
        for (int i = 1; i < model.rowCount(); ++i)
            view.expand(model.index(i, 0));
    } else {
        view.expand(group->index());
    }
    vivtest::showView(&view, QSize(420, 230));
    const auto colorRows = [](const QImage &image, const QColor &color) {
        int first = -1;
        int last = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == color) {
                    if (first < 0)
                        first = y;
                    last = y;
                    break;
                }
            }
        }
        return qMakePair(first, last);
    };
    const QImage before = view.grab().toImage();
    QCOMPARE(colorRows(before, secondColor).first, -1);
    view.setExpansionAnimationEnabled(true);
    view.setExpansionAnimationDuration(800);
    if (all)
        view.collapseAll();
    else
        view.collapse(parent->index());
    auto *overlay = view.template findChild<QWidget *>(QStringLiteral("vivTreeExpansionTransition"));
    QVERIFY(overlay && overlay->isVisible());
    QTest::qWait(400);
    const QImage middle = view.grab().toImage();
    QTRY_VERIFY(!overlay->isVisible());
    const QImage after = view.grab().toImage();
    const auto firstMiddle = colorRows(middle, firstColor);
    const auto secondMiddle = colorRows(middle, secondColor);
    const auto firstAfter = colorRows(after, firstColor);
    const auto secondAfter = colorRows(after, secondColor);
    QVERIFY(firstAfter.first >= 0 && secondAfter.first >= 0);
    QVERIFY(firstMiddle.first > firstAfter.first);
    QVERIFY(secondMiddle.first > secondAfter.first);
    QCOMPARE(firstMiddle.second - firstMiddle.first, firstAfter.second - firstAfter.first);
    QCOMPARE(secondMiddle.second - secondMiddle.first, secondAfter.second - secondAfter.first);
    if (!all)
        QCOMPARE(secondMiddle.first - firstMiddle.first, secondAfter.first - firstAfter.first);
}

template<typename View>
void exerciseExpansionOutgoingRows(bool cells)
{
    QStandardItemModel model;
    auto *parent = new QStandardItem(QStringLiteral("parent"));
    for (int i = 0; i < 4; ++i)
        parent->appendRow(new QStandardItem(QStringLiteral("child %1").arg(i)));
    model.appendRow(parent);
    const QColor color(25, 90, 210);
    for (int i = 0; i < 40; ++i) {
        auto *tail = new QStandardItem(QStringLiteral("tail %1").arg(i));
        if (i == 2)
            tail->setData(color, Qt::BackgroundRole);
        model.appendRow(tail);
    }
    View view;
    view.setAdapter(new RowAdapter, true);
    if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
        view.setCellAdapter(new CellAdapter, true);
        if (cells)
            view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    }
    view.setUniformItemHeight(24);
    view.setModel(&model);
    vivtest::showView(&view, QSize(420, 230));
    view.resize(view.width(), view.height() + 173 - view.viewport()->height());
    view.flushPendingRelayout();
    QCOMPARE(view.viewport()->height(), 173);
    const auto colorRows = [&](const QImage &image) {
        int first = -1;
        int last = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == color) {
                    if (first < 0)
                        first = y;
                    last = y;
                    break;
                }
            }
        }
        return qMakePair(first, last);
    };
    const auto before = colorRows(view.grab().toImage());
    QCOMPARE(view.visualRect(model.index(3, 0)).height(), 24);
    QVERIFY(before.first >= 0 && before.second > before.first);
    view.setExpansionAnimationEnabled(true);
    view.setExpansionAnimationDuration(800);
    view.expand(parent->index());
    auto *overlay = view.template findChild<QWidget *>(QStringLiteral("vivTreeExpansionTransition"));
    QVERIFY(overlay && overlay->isVisible());
    QCOMPARE(colorRows(view.grab().toImage()), before);
    QTest::qWait(50);
    const auto middle = colorRows(view.grab().toImage());
    QVERIFY(middle.first > before.first);
    QCOMPARE(middle.second - middle.first, before.second - before.first);
    QTRY_VERIFY(!overlay->isVisible());
    const auto after = colorRows(view.grab().toImage());
    QVERIFY(after.first > middle.first);
    const QImage viewportImage = view.viewport()->grab().toImage();
    const int rowTop = qRound(view.visualRect(model.index(3, 0)).top() * viewportImage.devicePixelRatio());
    QCOMPARE(after.second - after.first + 1, viewportImage.height() - rowTop);
}

template<typename View>
void exercisePartialBranch(bool cells)
{
    QStandardItemModel model;
    for (int i = 0; i < 20; ++i) {
        auto *node = new QStandardItem(QStringLiteral("node %1").arg(i));
        node->appendRow(new QStandardItem(QStringLiteral("child")));
        model.appendRow(node);
    }
    CenteredBranchRenderer renderer;
    View view;
    view.setAdapter(new RowAdapter, true);
    if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
        view.setCellAdapter(new CellAdapter, true);
        if (cells)
            view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    }
    view.setUniformItemHeight(40);
    view.setModel(&model);
    view.setBranchIndicatorRenderer(&renderer);
    vivtest::showView(&view, QSize(420, 230));
    view.resize(view.width(), view.height() + 5 - view.viewport()->height() % 40);
    view.flushPendingRelayout();
    QCOMPARE(view.viewport()->height() % 40, 5);
    const auto verify = [&](bool top) {
        renderer.rects.clear();
        const QImage image = view.viewport()->grab().toImage();
        const int row = top ? 0 : view.viewport()->height() / 40;
        const QModelIndex index = model.index(row, 0);
        QVERIFY(renderer.rects.contains(index));
        const QRect rect = renderer.rects.value(index);
        QCOMPARE(rect.height(), 40);
        QCOMPARE(rect.top(), view.visualRect(index).top());
        const QRect visible = rect.intersected(view.viewport()->rect());
        QCOMPARE(visible.height(), 5);
        const qreal dpr = image.devicePixelRatio();
        for (int y = visible.top(); y <= visible.bottom(); ++y)
            for (int x = visible.left(); x <= visible.right(); ++x)
                QVERIFY(image.pixelColor(int(x * dpr), int(y * dpr)) != renderer.color);
        const QRect full = renderer.rects.value(model.index(1, 0));
        QCOMPARE(image.pixelColor(int(full.center().x() * dpr), int(full.center().y() * dpr)),
                 renderer.color);
    };
    verify(false);
    view.setVerticalOffset(35);
    view.flushPendingRelayout();
    verify(true);
}

class TestTreeRowSpacing : public QObject
{
    Q_OBJECT
private slots:
    void pixelAlignedLines()
    {
        const QColor color(180, 20, 50, 128);
        QImage reference(1, 1, QImage::Format_ARGB32);
        reference.fill(Qt::white);
        { QPainter painter(&reference); painter.fillRect(reference.rect(), color); }
        const QColor expected = reference.pixelColor(0, 0);
        for (Qt::Orientation orientation : {Qt::Horizontal, Qt::Vertical})
        for (qreal scale : {1.0, 1.25, 1.5, 2.0}) {
            for (int width : {1, 2, 3}) {
                for (int offset = 0; offset < 24; ++offset) {
                    QImage image(160, 160, QImage::Format_ARGB32);
                    image.setDevicePixelRatio(scale);
                    image.fill(Qt::white);
                    {
                        QPainter painter(&image);
                        painter.translate(orientation == Qt::Vertical ? offset : 0,
                                          orientation == Qt::Horizontal ? offset : 0);
                        const QRect line = orientation == Qt::Horizontal
                            ? QRect(0, 10, 60, width) : QRect(10, 0, width, 60);
                        viv::fillPixelLines(&painter, {line, line}, width, color, orientation);
                    }
                    int painted = 0;
                    for (int y = 0; y < image.height(); ++y) {
                        const QColor pixel = orientation == Qt::Horizontal
                            ? image.pixelColor(10, y) : image.pixelColor(y, 10);
                        if (pixel != QColor(Qt::white)) {
                            QCOMPARE(pixel, expected);
                            ++painted;
                        }
                    }
                    QCOMPARE(painted, width);
                }
            }
        }
    }
    void spacingPixelLines_data()
    {
        relationshipSpacing_data();
        QTest::newRow("list") << 3;
        QTest::newRow("table-rows") << 4;
        QTest::newRow("table-cells") << 5;
    }
    void stateBackgroundSpacing_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("frozen");
        QTest::addColumn<int>("columnMask");
        for (int mode = 0; mode < 6; ++mode) {
            for (int columns = 0; columns < (mode == 0 || mode == 3 ? 1 : 4); ++columns) {
                QTest::newRow(qPrintable(QStringLiteral("mode-%1-columns-%2").arg(mode).arg(columns)))
                    << mode << false << columns;
                QTest::newRow(qPrintable(QStringLiteral("mode-%1-columns-%2-frozen").arg(mode).arg(columns)))
                    << mode << true << columns;
            }
        }
    }
    void stateBackgroundSpacing()
    {
        QFETCH(int, mode);
        QFETCH(bool, frozen);
        QFETCH(int, columnMask);
        const auto verify = [frozen, columnMask](auto &view, bool cells) {
            using View = std::decay_t<decltype(view)>;
            constexpr bool tree = std::is_same_v<View, viv::VirtualTreeView>
                || std::is_same_v<View, viv::VirtualTreeTableView>;
            QStandardItemModel model(2, 3);
            view.setAdapter(new RowAdapter, true);
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                view.setCellAdapter(new CellAdapter, true);
                if (cells) {
                    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
                    view.setVisualStateScope(viv::VirtualTableView::VisualStateScope::Cell);
                    view.setSelectionBehavior(viv::VirtualTableView::SelectionBehavior::SelectItems);
                }
                view.setColumnSpacing(14);
                view.setVerticalSpacingLineThroughRowSpacing(false);
            }
            view.setUniformItemHeight(24);
            view.setModel(&model);
            view.setRowSpacing(12);
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                for (int column = 0; column < 3; ++column)
                    view.setColumnWidth(column, 100);
                if (frozen)
                    view.setFrozenColumns({0});
            }
            if (frozen)
                view.setFrozenRows(1);
            const QColor hover(25, 180, 70);
            const QColor selected(190, 40, 90);
            if constexpr (std::is_same_v<View, viv::VirtualTableView>) {
                if (!cells)
                    model.setData(model.index(0, 0), selected, Qt::BackgroundRole);
            }
            view.setHoverBackgroundColor(hover);
            view.setSelectedBackgroundColor(selected);
            view.setVisualStateAnimationDuration(0);
            if constexpr (tree)
                view.setVisualStateBackgroundVisible(true);
            vivtest::showView(&view, QSize(420, 240));
            QVERIFY(!view.hoverBackgroundThroughRowSpacing());
            QVERIFY(!view.selectedBackgroundThroughRowSpacing());
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                QVERIFY(view.hoverBackgroundThroughColumnSpacing());
                QVERIFY(view.selectedBackgroundThroughColumnSpacing());
                view.setHoverBackgroundThroughColumnSpacing(columnMask & 1);
                view.setSelectedBackgroundThroughColumnSpacing(columnMask & 2);
            }
            const QModelIndex node = model.index(0, 0);
            const QRect rect = [&]() {
                if constexpr (std::is_base_of_v<viv::VirtualTableView, View>)
                    return view.spanRect(node);
                else
                    return view.visualRect(node);
            }();
            const auto check = [&](const QColor &color, bool through, bool throughColumns) {
                view.flushPendingRelayout();
                const QImage image = view.grab().toImage();
                const auto sample = [&](const QPoint &point) {
                    const QPoint p = point + view.viewport()->pos();
                    return image.pixelColor(qRound(p.x() * image.devicePixelRatio()),
                                            qRound(p.y() * image.devicePixelRatio()));
                };
                if constexpr (std::is_same_v<View, viv::VirtualTreeView>
                              || std::is_same_v<View, viv::VirtualTreeTableView>)
                    QCOMPARE(sample(rect.center()), color);
                QCOMPARE(sample(QPoint(rect.center().x(), rect.bottom() + 6)) == color, through);
                if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                    QCOMPARE(sample(QPoint(rect.right() + 7, rect.center().y())) == color, throughColumns);
                    QCOMPARE(sample(QPoint(rect.right() + 7, rect.bottom() + 6)) == color, through && throughColumns);
                }
            };
            for (bool hoverThrough : {false, true}) {
                for (bool selectedThrough : {false, true}) {
                    view.setHoverBackgroundThroughRowSpacing(hoverThrough);
                    view.setSelectedBackgroundThroughRowSpacing(selectedThrough);
                    QTest::mouseMove(view.viewport(), QPoint(300, 180));
                    view.selectionModel()->clearSelection();
                    QTest::mouseMove(view.viewport(), rect.center());
                    QTRY_COMPARE(view.hoveredIndex(), node);
                    check(hover, hoverThrough, columnMask & 1);
                    QTest::mouseMove(view.viewport(), QPoint(300, 180));
                    QTRY_VERIFY(!view.hoveredIndex().isValid());
                    view.selectionModel()->select(node, QItemSelectionModel::ClearAndSelect);
                    check(selected, selectedThrough, columnMask & 2);
                }
            }
            if constexpr (tree) {
                if (!frozen) {
                model.setData(node, 6, View::NodeRowSpacingBelowRole);
                const QModelIndex next = model.index(1, 0);
                model.setData(next, 10, View::NodeRowSpacingAboveRole);
                view.flushPendingRelayout();
                const QPoint nextCenter(rect.center().x(), view.visualRect(next).center().y());
                QTest::mouseMove(view.viewport(), nextCenter);
                QTRY_COMPARE(view.hoveredIndex(), next);
                view.selectionModel()->select(node, QItemSelectionModel::ClearAndSelect);
                for (bool hoverThrough : {false, true}) {
                    for (bool selectedThrough : {false, true}) {
                        view.setHoverBackgroundThroughRowSpacing(hoverThrough);
                        view.setSelectedBackgroundThroughRowSpacing(selectedThrough);
                        view.flushPendingRelayout();
                        const QImage image = view.grab().toImage();
                        const auto sample = [&](int offset) {
                            const QPoint point = view.viewport()->pos()
                                + QPoint(rect.center().x(), rect.bottom() + offset);
                            return image.pixelColor(qRound(point.x() * image.devicePixelRatio()),
                                                    qRound(point.y() * image.devicePixelRatio()));
                        };
                        QCOMPARE(sample(3) == selected, selectedThrough);
                        QCOMPARE(sample(11) == hover, hoverThrough);
                        QVERIFY(sample(3) != hover);
                        QVERIFY(sample(11) != selected);
                    }
                }
                }
                view.setVisualStateBackgroundVisible(false);
            }
            view.flushPendingRelayout();
        };
        if (mode == 0) {
            viv::VirtualTreeView view;
            verify(view, false);
        } else if (mode < 3) {
            viv::VirtualTreeTableView view;
            verify(view, mode == 2);
        } else if (mode == 3) {
            viv::VirtualListView view;
            verify(view, false);
        } else {
            viv::VirtualTableView view;
            verify(view, mode == 5);
        }
    }
    void stateBackgroundPixelBoundaries_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("cellScope");
        for (int mode = 0; mode < 6; ++mode) {
            for (int scope = 0; scope < (mode == 0 || mode == 3 ? 1 : 2); ++scope)
                QTest::newRow(qPrintable(QStringLiteral("mode-%1-scope-%2").arg(mode).arg(scope)))
                    << mode << bool(scope);
        }
    }
    void stateBackgroundPixelBoundaries()
    {
        QFETCH(int, mode);
        QFETCH(bool, cellScope);
        const auto verify = [mode, cellScope](auto &view) {
            using View = std::decay_t<decltype(view)>;
            QStandardItemModel model(16, 3);
            view.setAdapter(new RowAdapter, true);
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                view.setCellAdapter(new CellAdapter, true);
                if (mode == 2 || mode == 5)
                    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
                if (cellScope) {
                    view.setVisualStateScope(viv::VirtualTableView::VisualStateScope::Cell);
                    view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
                }
                view.setHorizontalGridLinesVisible(false);
                view.setVerticalGridLinesVisible(false);
            } else {
                view.setRowGridLinesVisible(false);
            }
            view.setUniformItemHeight(25);
            view.setModel(&model);
            if constexpr (std::is_same_v<View, viv::VirtualTreeView>
                          || std::is_same_v<View, viv::VirtualTreeTableView>) {
                view.setVisualStateBackgroundVisible(true);
                view.setBranchIndicatorsVisible(false);
            }
            view.setVisualStateAnimationDuration(0);
            const QColor hover(25, 180, 70);
            const QColor selected(190, 40, 90);
            view.setHoverBackgroundColor(hover);
            view.setSelectedBackgroundColor(selected);
            vivtest::showView(&view, QSize(420, 320));
            constexpr bool table = std::is_base_of_v<viv::VirtualTableView, View>;
            if constexpr (table) {
                for (int column = 0; column < 3; ++column)
                    view.setColumnWidth(column, 100);
            }
            const QModelIndex upper = model.index(3, table ? 1 : 0);
            const QModelIndex lower = model.index(4, table ? 1 : 0);
            for (int gap = 0; gap <= 8; ++gap) {
                view.setRowSpacing(gap);
                if constexpr (std::is_same_v<View, viv::VirtualTreeView>
                              || std::is_same_v<View, viv::VirtualTreeTableView>) {
                    for (int row = 0; row < model.rowCount(); ++row) {
                        model.setData(model.index(row, 0), (row + gap) % 5, View::NodeRowSpacingAboveRole);
                        model.setData(model.index(row, 0), (2 * row + gap) % 7, View::NodeRowSpacingBelowRole);
                    }
                }
                for (int offset : {0, 1, 3}) {
                    view.setVerticalOffset(offset);
                    view.flushPendingRelayout();
                    const auto rectFor = [&view](const QModelIndex &index) {
                        if constexpr (std::is_base_of_v<viv::VirtualTableView, View>)
                            return view.spanRect(index);
                        else
                            return view.visualRect(index);
                    };
                    const QRect upperRect = rectFor(upper);
                    const QRect lowerRect = rectFor(lower);
                    const QPoint hoverPoint(lowerRect.center());
                    QTest::mouseMove(view.viewport(), hoverPoint);
                    QTRY_COMPARE(view.hoveredIndex(), lower);
                    view.selectionModel()->select(upper, QItemSelectionModel::ClearAndSelect);
                    for (bool hoverThrough : {false, true}) {
                        for (bool selectedThrough : {false, true}) {
                            view.setHoverBackgroundThroughRowSpacing(hoverThrough);
                            view.setSelectedBackgroundThroughRowSpacing(selectedThrough);
                            view.flushPendingRelayout();
                            const QImage image = view.viewport()->grab().toImage();
                            const qreal dpr = image.devicePixelRatio();
                            const auto bounds = [dpr, gap](const QRect &rect, int row, bool through) {
                                if constexpr (std::is_same_v<View, viv::VirtualTreeView>
                                              || std::is_same_v<View, viv::VirtualTreeTableView>) {
                                    const int above = through ? (row + gap) % 5 : 0;
                                    const int below = through ? (2 * row + gap) % 7 : 0;
                                    return qMakePair(qRound((rect.top() - above) * dpr),
                                                     qRound((rect.bottom() + 1 + below) * dpr));
                                } else {
                                    return qMakePair(qRound((rect.bottom() + 1) * dpr),
                                                     qRound((rect.bottom() + 1 + (through ? gap : 0)) * dpr));
                                }
                            };
                            const auto selectedBounds = bounds(upperRect, 3, selectedThrough);
                            const auto hoverBounds = bounds(lowerRect, 4, hoverThrough);
                            const int x = qRound(hoverPoint.x() * dpr);
                            for (int y = 0; y < image.height(); ++y) {
                                const QColor actual = image.pixelColor(x, y);
                                const bool expectSelected = y >= selectedBounds.first && y < selectedBounds.second;
                                const bool expectHover = y >= hoverBounds.first && y < hoverBounds.second;
                                QVERIFY2((actual == selected) == expectSelected && (actual == hover) == expectHover,
                                    qPrintable(QStringLiteral("gap=%1 offset=%2 hover=%3 selected=%4 dpr=%5 y=%6 actual=%7 selected=[%8,%9) hover=[%10,%11)")
                                        .arg(gap).arg(offset).arg(hoverThrough).arg(selectedThrough).arg(dpr)
                                        .arg(y).arg(actual.name()).arg(selectedBounds.first).arg(selectedBounds.second)
                                        .arg(hoverBounds.first).arg(hoverBounds.second)));
                            }
                        }
                    }
                }
            }
        };
        if (mode == 0) {
            viv::VirtualTreeView view;
            verify(view);
        } else if (mode < 3) {
            viv::VirtualTreeTableView view;
            verify(view);
        } else if (mode == 3) {
            viv::VirtualListView view;
            verify(view);
        } else {
            viv::VirtualTableView view;
            verify(view);
        }
    }
    void roundedStatePixelBoundaries()
    {
        const QColor base(45, 45, 45);
        const QColor color(190, 220, 200);
        for (qreal dpr : {1.0, 1.25, 1.5, 1.75}) {
            for (int offset = 0; offset < 4; ++offset) {
                for (int height : {26, 38, 55, 56, 57}) {
                    for (int radius : {0, 8, 20}) {
                        QImage image(200, 180, QImage::Format_ARGB32_Premultiplied);
                        image.setDevicePixelRatio(dpr);
                        image.fill(base);
                        const QRect rect(5, 7, 86, height);
                        const QRegion clip = QRegion(rect) - QRegion(QRect(35, 7, 7, height));
                        QRegion ownedPixels;
                        {
                            QPainter painter(&image);
                            painter.translate(offset, offset + 3);
                            const QTransform transform = painter.deviceTransform();
                            for (const QRect &part : clip) {
                                const QRectF physical = transform.mapRect(QRectF(part));
                                ownedPixels += QRect(qRound(physical.left()), qRound(physical.top()),
                                    qRound(physical.right()) - qRound(physical.left()),
                                    qRound(physical.bottom()) - qRound(physical.top()));
                            }
                            painter.setOpacity(0.65);
                            viv::fillPixelAlignedRoundedRegion(&painter, rect, clip, radius, color);
                        }
                        int painted = 0;
                        for (int y = 0; y < image.height(); ++y) {
                            for (int x = 0; x < image.width(); ++x) {
                                if (image.pixelColor(x, y) == base)
                                    continue;
                                ++painted;
                                QVERIFY2(ownedPixels.contains(QPoint(x, y)),
                                    qPrintable(QStringLiteral("dpr=%1 offset=%2 height=%3 radius=%4 spill=(%5,%6)")
                                        .arg(dpr).arg(offset).arg(height).arg(radius).arg(x).arg(y)));
                            }
                        }
                        QVERIFY(painted > 0);
                    }
                }
            }
        }
    }
    void roundedStateSpacing()
    {
        QStandardItemModel model(8, 3);
        RoundedBackgroundTable view;
        view.setAdapter(new RowAdapter, true);
        view.setUniformItemHeight(26);
        view.setRowSpacing(12);
        view.setColumnSpacing(12);
        view.setHorizontalGridLinesVisible(false);
        view.setVerticalGridLinesVisible(false);
        view.setVisualStateAnimationDuration(0);
        const QColor hover(25, 180, 70);
        const QColor selected(190, 40, 90);
        view.setHoverBackgroundColor(hover);
        view.setSelectedBackgroundColor(selected);
        view.setModel(&model);
        vivtest::showView(&view, QSize(420, 380));
        for (int column = 0; column < 3; ++column)
            view.setColumnWidth(column, 100);
        const QModelIndex index = model.index(3, 1);
        for (bool cells : {false, true}) {
            view.setVisualStateScope(cells ? viv::VirtualTableView::VisualStateScope::Cell
                                          : viv::VirtualTableView::VisualStateScope::Row);
            view.setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectItems);
            for (bool select : {false, true}) {
                for (bool through : {false, true}) {
                    view.setHoverBackgroundThroughRowSpacing(!select && through);
                    view.setSelectedBackgroundThroughRowSpacing(select && through);
                    view.flushPendingRelayout();
                    const QRect cell = view.spanRect(index);
                    const QRect body = cells ? cell : QRect(0, cell.y(), view.viewport()->width(), cell.height());
                    view.selectionModel()->clearSelection();
                    QTest::mouseMove(view.viewport(), select ? QPoint(2, view.viewport()->height() - 2)
                                                            : cell.center());
                    if (select)
                        view.selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
                    else
                        QTRY_COMPARE(view.hoveredIndex(), index);
                    const QImage image = view.viewport()->grab().toImage();
                    const qreal dpr = image.devicePixelRatio();
                    const QColor color = select ? selected : hover;
                    const auto pixel = [&](int x, int y) {
                        return image.pixelColor(qRound(x * dpr), qRound(y * dpr));
                    };
                    const int x = body.left() + 3;
                    // Covering the gap moves the lower corner below the original row body.
                    QCOMPARE(pixel(x, body.bottom() - 2) == color, through);
                    QCOMPARE(pixel(body.center().x(), body.bottom() + 6) == color, through);
                    const int bottom = body.bottom() + (through ? 12 : 0);
                    QVERIFY(pixel(x, bottom - 1) != color);
                    QVERIFY(pixel(body.center().x(), bottom + 2) != color);
                    QCOMPARE(pixel(body.center().x(), body.center().y()), color);
                    const int physicalBottom = qRound((bottom + 1) * dpr);
                    const int sampleX = qRound(body.center().x() * dpr);
                    const QColor base = view.palette().color(QPalette::Base);
                    for (int y = physicalBottom; y < physicalBottom + 3; ++y)
                        QCOMPARE(image.pixelColor(sampleX, y), base);
                }
            }
        }
    }
    void treeChildStateGridBoundary()
    {
        QWidget window;
        window.resize(460, 480);
        QStandardItemModel model;
        auto *parent = new QStandardItem(QStringLiteral("parent"));
        for (int row = 0; row < 12; ++row)
            parent->appendRow(new QStandardItem(QStringLiteral("child %1").arg(row)));
        model.appendRow(parent);
        viv::VirtualTreeView view(&window);
        view.setAdapter(new RowAdapter, true);
        view.setUniformItemHeight(26);
        view.setVisualStateBackgroundVisible(true);
        view.setVisualStateAnimationDuration(0);
        const QColor base(45, 45, 45);
        const QColor grid(10, 15, 20);
        const QColor hover(25, 180, 70);
        const QColor selected(190, 40, 90);
        QPalette colors = view.palette();
        colors.setColor(QPalette::Base, base);
        view.setPalette(colors);
        view.setRowGridLineColor(grid);
        view.setHoverBackgroundColor(hover);
        view.setSelectedBackgroundColor(selected);
        view.setModel(&model);
        window.show();
        view.expand(parent->index());
        for (int offset = 0; offset < 4; ++offset) {
            view.setGeometry(7, 43 + offset, 420, 400);
            view.flushPendingRelayout();
            for (int row = 0; row < 8; ++row) {
                const QModelIndex index = parent->child(row)->index();
                const QRect rect = view.visualRect(index);
                QTest::mouseMove(view.viewport(), rect.center());
                QTRY_COMPARE(view.hoveredIndex(), index);
                for (bool select : {false, true}) {
                    view.selectionModel()->clearSelection();
                    if (select)
                        view.selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
                    const QImage image = window.grab().toImage();
                    const qreal dpr = image.devicePixelRatio();
                    const QPoint origin = view.viewport()->mapTo(&window, QPoint());
                    const int x = qRound((origin.x() + view.viewport()->width() - 24) * dpr);
                    const int bottom = qRound((origin.y() + rect.bottom() + 1) * dpr);
                    const QString context = QStringLiteral("child=%1 offset=%2 selected=%3 dpr=%4")
                        .arg(row).arg(offset).arg(select).arg(dpr);
                    QVERIFY2(image.pixelColor(x, bottom - 2) == (select ? selected : hover),
                             qPrintable(context + QStringLiteral(" body")));
                    QVERIFY2(image.pixelColor(x, bottom - 1) == grid,
                             qPrintable(context + QStringLiteral(" trailing separator")));
                    QVERIFY2(image.pixelColor(x, bottom) == base,
                             qPrintable(context + QStringLiteral(" next row")));
                }
            }
        }
    }
    void treeChildStateSpacing()
    {
        QStandardItemModel model;
        auto *parent = new QStandardItem(QStringLiteral("parent"));
        auto *child = new QStandardItem(QStringLiteral("child"));
        auto *sibling = new QStandardItem(QStringLiteral("sibling"));
        parent->setData(4, viv::VirtualTreeView::NodeRowSpacingBelowRole);
        child->setData(6, viv::VirtualTreeView::NodeRowSpacingBelowRole);
        child->setData(8, viv::VirtualTreeView::NodeRowSpacingAboveRole);
        sibling->setData(10, viv::VirtualTreeView::NodeRowSpacingAboveRole);
        parent->appendRow(child);
        parent->appendRow(sibling);
        model.appendRow(parent);
        viv::VirtualTreeView view;
        RowAdapter adapter;
        view.setAdapter(&adapter);
        view.setUniformItemHeight(24);
        view.setRowSpacing(12);
        view.setVisualStateBackgroundVisible(true);
        view.setVisualStateAnimationDuration(0);
        view.setHoverBackgroundColor(QColor(25, 180, 70));
        view.setSelectedBackgroundColor(QColor(190, 40, 90));
        view.setHoverBackgroundThroughRowSpacing(true);
        view.setSelectedBackgroundThroughRowSpacing(true);
        view.setModel(&model);
        vivtest::showView(&view, QSize(420, 260));
        view.expand(parent->index());
        view.flushPendingRelayout();
        const QModelIndex childIndex = child->index();
        const QRect childRect = view.visualRect(childIndex);
        const QRect siblingRect = view.visualRect(sibling->index());
        QTest::mouseMove(view.viewport(), childRect.center());
        QTRY_COMPARE(view.hoveredIndex(), childIndex);
        const QImage hoverImage = view.grab().toImage();
        const QPoint rowPoint(view.viewport()->width() - 24,
                              childRect.center().y() + view.viewport()->y());
        const auto pixelAt = [](const QImage &image, const QPoint &point) {
            const qreal dpr = image.devicePixelRatio();
            return image.pixelColor(qRound(point.x() * dpr), qRound(point.y() * dpr));
        };
        QCOMPARE(pixelAt(hoverImage, rowPoint), view.hoverBackgroundColor());
        const QPoint gapPoint(view.viewport()->width() - 24,
                              childRect.bottom() + 3 + view.viewport()->y());
        QCOMPARE(hoverImage.pixelColor(gapPoint.x() * hoverImage.devicePixelRatio(),
                                       gapPoint.y() * hoverImage.devicePixelRatio()),
                 view.hoverBackgroundColor());
        const int sampleX = qRound(gapPoint.x() * hoverImage.devicePixelRatio());
        const int nextRowTop = qRound((view.viewport()->y() + siblingRect.top())
                                      * hoverImage.devicePixelRatio());
        for (int y = nextRowTop; y < nextRowTop + 3; ++y)
            QVERIFY(hoverImage.pixelColor(sampleX, y) != view.hoverBackgroundColor());
        view.selectionModel()->select(childIndex, QItemSelectionModel::ClearAndSelect);
        const QImage selectedImage = view.grab().toImage();
        QCOMPARE(pixelAt(selectedImage, rowPoint), view.selectedBackgroundColor());
        QCOMPARE(selectedImage.pixelColor(gapPoint.x() * selectedImage.devicePixelRatio(),
                                          gapPoint.y() * selectedImage.devicePixelRatio()),
                 view.selectedBackgroundColor());
        for (int y = nextRowTop; y < nextRowTop + 3; ++y)
            QVERIFY(selectedImage.pixelColor(sampleX, y) != view.selectedBackgroundColor());
    }
    void spacingPixelLines()
    {
        QFETCH(int, mode);
        const auto verify = [](auto &view, bool cells) {
            using View = std::decay_t<decltype(view)>;
            QStandardItemModel model;
            for (int i = 0; i < 40; ++i)
                model.appendRow(new QStandardItem(QStringLiteral("row")));
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>)
                model.setColumnCount(3);
            const QColor color(220, 30, 90);
            const QColor paneColor(20, 170, 60);
            view.setAdapter(new RowAdapter, true);
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                view.setCellAdapter(new CellAdapter, true);
                if (cells)
                    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
                view.setHorizontalGridLineColor(color);
                view.setVerticalGridLineColor(color);
                viv::PaneSeparatorStyle separator;
                separator.width = 1;
                separator.color = paneColor;
                view.setPaneSeparatorStyle(separator);
            } else {
                view.setRowGridLineColor(color);
                viv::PaneSeparatorStyle separator;
                separator.width = 1;
                separator.color = paneColor;
                view.setItemPaneSeparatorStyle(separator);
            }
            view.setUniformItemHeight(25);
            view.setModel(&model);
            view.setFrozenRows(1);
            view.setFrozenBottomRows(1);
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>)
                view.setFrozenColumns({0});
            vivtest::showView(&view, QSize(420, 260));
            if constexpr (std::is_base_of_v<viv::VirtualTableView, View>) {
                for (int gap : {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
                    view.setColumnSpacing(gap);
                    view.setColumnWidth(0, 80 + gap);
                    view.setColumnWidth(1, 90 + gap);
                    view.setColumnWidth(2, 100);
                    view.flushPendingRelayout();
                    const QImage image = view.grab().toImage();
                    const qreal dpr = image.devicePixelRatio();
                    for (const QColor &lineColor : {color, paneColor})
                    for (int logicalY : {view.viewport()->geometry().y() - 10,
                                         view.viewport()->geometry().y() + 12}) {
                        const int y = qRound(logicalY * dpr);
                        int run = 0;
                        int lines = 0;
                        for (int x = 0; x < image.width(); ++x) {
                            if (image.pixelColor(x, y) == lineColor) {
                                ++run;
                                QVERIFY2(run == 1, qPrintable(QStringLiteral("column gap=%1, x=%2").arg(gap).arg(x)));
                                ++lines;
                            } else {
                                run = 0;
                            }
                        }
                        QVERIFY(lines > 0);
                    }
                }
            }
            for (int gap : {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
                view.setRowSpacing(gap);
                view.flushPendingRelayout();
                const QImage image = view.grab().toImage();
                const QRect body = view.viewport()->geometry();
                const int x = qRound((body.right() - 10) * image.devicePixelRatio());
                // A gap edge can adjoin a frozen boundary; check their colors independently.
                for (const QColor &lineColor : {color, paneColor}) {
                int run = 0;
                int lines = 0;
                for (int y = qRound(body.top() * image.devicePixelRatio());
                     y < qRound((body.bottom() + 1) * image.devicePixelRatio()); ++y) {
                    if (image.pixelColor(x, y) == lineColor) {
                        ++run;
                        QVERIFY2(run == 1, qPrintable(QStringLiteral("gap=%1, y=%2").arg(gap).arg(y)));
                        ++lines;
                    } else {
                        run = 0;
                    }
                }
                QVERIFY2(lines > 0, qPrintable(QStringLiteral("row gap=%1").arg(gap)));
                }
            }
        };
        if (mode == 0) {
            viv::VirtualTreeView view;
            verify(view, false);
        } else if (mode < 3) {
            viv::VirtualTreeTableView view;
            verify(view, mode == 2);
        } else if (mode == 3) {
            viv::VirtualListView view;
            verify(view, false);
        } else {
            viv::VirtualTableView view;
            verify(view, mode == 5);
        }
    }
    void expansionOutgoingRows_data() { relationshipSpacing_data(); }
    void expansionOutgoingRows()
    {
        QFETCH(int, mode);
        if (mode == 0)
            exerciseExpansionOutgoingRows<viv::VirtualTreeView>(false);
        else
            exerciseExpansionOutgoingRows<viv::VirtualTreeTableView>(mode == 2);
    }
    void partialBranch_data() { relationshipSpacing_data(); }
    void partialBranch()
    {
        QFETCH(int, mode);
        if (mode == 0)
            exercisePartialBranch<viv::VirtualTreeView>(false);
        else
            exercisePartialBranch<viv::VirtualTreeTableView>(mode == 2);
    }
    void collapseIncomingRows_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("all");
        for (int mode = 0; mode < 3; ++mode) {
            QTest::newRow(qPrintable(QStringLiteral("mode-%1-single").arg(mode))) << mode << false;
            QTest::newRow(qPrintable(QStringLiteral("mode-%1-all").arg(mode))) << mode << true;
        }
    }
    void collapseIncomingRows()
    {
        QFETCH(int, mode);
        QFETCH(bool, all);
        if (mode == 0)
            exerciseCollapseIncomingRows<viv::VirtualTreeView>(false, all);
        else
            exerciseCollapseIncomingRows<viv::VirtualTreeTableView>(mode == 2, all);
    }
    void expansionAnimation_data() { relationshipSpacing_data(); }
    void expansionStateHeight_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("hover");
        for (int mode = 0; mode < 3; ++mode) {
            QTest::newRow(qPrintable(QStringLiteral("mode-%1-hover").arg(mode))) << mode << true;
            QTest::newRow(qPrintable(QStringLiteral("mode-%1-selected").arg(mode))) << mode << false;
        }
    }
    void expansionStateHeight()
    {
        QFETCH(int, mode);
        QFETCH(bool, hover);
        const auto verify = [mode, hover](auto &view) {
            using View = std::decay_t<decltype(view)>;
            QStandardItemModel model;
            auto *parent = new QStandardItem;
            auto *child = new QStandardItem;
            child->setData(27, View::NodeRowSpacingAboveRole);
            parent->appendRow(child);
            model.appendRow(parent);
            auto *tail = new QStandardItem;
            tail->setData(1, View::NodeRowSpacingAboveRole);
            model.appendRow(tail);
            view.setAdapter(new RowAdapter, true);
            if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
                view.setCellAdapter(new CellAdapter, true);
                if (mode == 2)
                    view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
                view.setHorizontalGridLinesVisible(false);
                view.setVerticalGridLinesVisible(false);
            } else {
                view.setRowGridLinesVisible(false);
            }
            view.setUniformItemHeight(24);
            view.setModel(&model);
            view.setBranchIndicatorsVisible(false);
            view.setVisualStateBackgroundVisible(true);
            view.setVisualStateAnimationDuration(0);
            const QColor color(25, 180, 70);
            view.setHoverBackgroundColor(color);
            view.setSelectedBackgroundColor(color);
            vivtest::showView(&view, QSize(420, 340));
            if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>)
                view.setColumnWidth(0, 240);
            const QPoint point(100, view.visualRect(parent->index()).center().y());
            if (hover) {
                QTest::mouseMove(view.viewport(), point);
                QTRY_COMPARE(view.hoveredIndex(), parent->index());
            } else {
                QTest::mouseMove(view.viewport(), QPoint(300, 250));
                view.selectionModel()->select(parent->index(), QItemSelectionModel::ClearAndSelect);
            }
            const auto colorHeight = [&view, color]() {
                const QImage image = view.grab().toImage();
                const int x = qRound((view.viewport()->x() + 100) * image.devicePixelRatio());
                int count = 0;
                for (int y = 0; y < image.height(); ++y)
                    count += image.pixelColor(x, y) == color;
                return count;
            };
            const int expectedHeight = colorHeight();
            QVERIFY(expectedHeight > 0);
            view.setExpansionAnimationEnabled(true);
            view.setExpansionAnimationDuration(400);
            auto *overlay = view.template findChild<QWidget *>(QStringLiteral("vivTreeExpansionTransition"));
            QVERIFY(overlay);
            for (bool expanded : {true, false}) {
                view.setExpanded(parent->index(), expanded);
                QVERIFY(overlay->isVisible());
                QCOMPARE(colorHeight(), expectedHeight);
                for (int frame = 0; frame < 3; ++frame) {
                    QTest::qWait(70);
                    QVERIFY(overlay->isVisible());
                    QCOMPARE(colorHeight(), expectedHeight);
                }
                QTRY_VERIFY(!overlay->isVisible());
                QCOMPARE(colorHeight(), expectedHeight);
            }
        };
        if (mode == 0) {
            viv::VirtualTreeView view;
            verify(view);
        } else {
            viv::VirtualTreeTableView view;
            verify(view);
        }
    }
    void expansionAnimation()
    {
        QFETCH(int, mode);
        if (mode == 0)
            exerciseExpansionAnimation<viv::VirtualTreeView>(false);
        else
            exerciseExpansionAnimation<viv::VirtualTreeTableView>(mode == 2);
    }
    void relationshipSpacing_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("tree") << 0;
        QTest::newRow("tree-table-rows") << 1;
        QTest::newRow("tree-table-cells") << 2;
    }
    void relationshipSpacing()
    {
        QFETCH(int, mode);
        if (mode == 0)
            exerciseSpacing<viv::VirtualTreeView>(false);
        else
            exerciseSpacing<viv::VirtualTreeTableView>(mode == 2);
    }
};

QTEST_MAIN(TestTreeRowSpacing)
#include "tst_treerowspacing.moc"
