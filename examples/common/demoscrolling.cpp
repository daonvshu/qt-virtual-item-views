#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
#include <smoothscroll/smoothscrollcontroller.h>

#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QEvent>

namespace {

class DemoScrolling : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show) {
            auto *area = qobject_cast<QAbstractScrollArea *>(object);
            if (area && !area->findChild<sscroll::SmoothScrollController *>() ) {
                auto *controller = new sscroll::SmoothScrollController(area, area->viewport());
                auto settings = controller->settings();
                settings.minimumWheelStep = 1;
                controller->setSettings(settings);
            }
        }
        return false;
    }
};

void installDemoScrolling()
{
    auto *app = QCoreApplication::instance();
    app->installEventFilter(new DemoScrolling(app));
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installDemoScrolling)
#endif
