#pragma once

#include <QColor>
#include <QPainterPath>
#include <QWidget>

namespace viv { class VirtualTableView; }

namespace table_borders {

struct BorderOptions
{
    int radius = 20;
    int width = 2;
    int stateRadius = 6;
    QColor color = QColor(QStringLiteral("#4079ad"));
    bool headers = true;
    bool gaps = false;
    bool frozen = false;
    bool scrollBars = true;
};

class BorderDemo : public QWidget
{
public:
    explicit BorderDemo(QWidget *parent);
    viv::VirtualTableView *view() const { return m_view; }
    const BorderOptions &options() const { return m_options; }
    void setOptions(const BorderOptions &options);
    QPainterPath outerOutline() const;
    QPainterPath contentOutline(const QWidget *target) const;
    qreal contentRadius() const;

protected:
    void attachView(viv::VirtualTableView *view);
    virtual bool coversCorners() const { return false; }
    virtual void updateTechnique() {}
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutContent();
    viv::VirtualTableView *m_view = nullptr;
    QWidget *m_border = nullptr;
    BorderOptions m_options;
    friend class BorderOverlay;
};

QColor stageColor();
QPainterPath roundedOutline(const QRectF &rect, qreal radius);
BorderDemo *createCornerCover(QWidget *parent);
BorderDemo *createAlphaFrame(QWidget *parent);

} // namespace table_borders
