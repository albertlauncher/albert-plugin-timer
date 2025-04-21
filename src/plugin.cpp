// Copyright (c) 2024-2025 Manuel Schneider

#include "plugin.h"
#include <QDateTime>
#include <QLocale>
#include <albert/albert.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
#include <albert/timestrings.h>
ALBERT_LOGGING_CATEGORY("timer")
using namespace albert::util::strings;
using namespace albert;
using namespace std;

static const QStringList icon_urls = {"gen:?text=⏲️"};

Timer::Timer(Plugin &plugin, const QString &name, int interval):
    plugin_(plugin),
    interval(interval),
    left(interval),
    end(QDateTime::currentSecsSinceEpoch() + interval)
{
    setObjectName(name);
    start(1s);
    connect(this, &QTimer::timeout, this, [this]
    {
        if (--left == 0)
        {
            INFO << "Timer expired:" << objectName();
            stop();
            notification.setTitle(titleString());
            notification.setText(expiryString());
            notification.dismiss();
            notification.send();
        }
        for (auto obs : observers)
            obs->notify(this);
    });

    notification.setTitle(titleString());
    notification.setText(expiryString());
    notification.send();

    INFO << "Timer started:" << objectName();
}

Timer::~Timer()
{
    INFO << "Timer removed:" << objectName();
}

QString Timer::titleString() const
{ return QStringLiteral("%1: %2").arg(Plugin::tr("Timer"), objectName()); }

QString Timer::expiryString() const
{
    return (isActive() ? Plugin::tr("Expires at %1") : Plugin::tr("Expired at %1"))
        .arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(end).time(), "hh:mm:ss"));
}

QString Timer::id() const { return QStringLiteral("timer"); }

QString Timer::text() const { return titleString(); }

QString Timer::subtext() const
{
    if (isActive())
        return QStringLiteral("%1 | %2").arg(digitalDurationString(left), expiryString());
    else
        return expiryString();
}

QStringList Timer::iconUrls() const { return icon_urls; }

vector<Action> Timer::actions() const
{
    return {
        {
            QStringLiteral("rem"), Plugin::tr("Remove", "Action verb form"),
            [this] { plugin_.removeTimer(this); }
        }
    };
}

void Timer::addObserver(Observer *obs) { observers.emplace(obs); }

void Timer::removeObserver(Observer *obs) { observers.erase(obs); }

// -----------------------------------------------------------------------------------------------

QString Plugin::defaultTrigger() const { return tr("timer ", "The trigger. Lowercase."); }

QString Plugin::synopsis(const QString &) const { return tr("<duration> [name]"); }

vector<RankItem> Plugin::handleGlobalQuery(const Query &query)
{
    if (!query.isValid())
        return {};

    Matcher matcher(query);
    vector<RankItem> r;

    // List matching timers
    for (auto &t : timers_)
        if(auto m = matcher.match(t->objectName()); m)
            r.emplace_back(t, m);

    // Add new timer item
    auto s = query.string().trimmed();
    auto duration_string = s.section(QChar::Space, 0, 0, QString::SectionSkipEmpty);

    uint64_t duration = 0;
    if (duration = parseNaturalDurationString(duration_string); duration == 0)
        duration = parseDigitalDurationString(duration_string);

    if (duration > 0)
    {
        QString name = s.section(QChar::Space, 1).trimmed();
        if (name.isEmpty())
            name = QString("#%1").arg(timer_counter_);

        r.emplace_back(
            StandardItem::make(
                QStringLiteral("set"),
                QStringLiteral("%1: %2").arg(tr("Set timer"), name),
                digitalDurationString(duration),
                icon_urls,
                {{
                    QStringLiteral("set"), tr("Start", "Action verb form"),
                    [=, this]{ startTimer(name, duration); }

                }}
                ),
            1.0
            );
    }

    return r;
}

vector<shared_ptr<Item>> Plugin::handleEmptyQuery()
{ return vector<shared_ptr<Item>>(timers_.begin(), timers_.end()); }

void Plugin::startTimer(const QString &name, uint seconds)
{
    ++timer_counter_;
    auto &timer = timers_.emplace_front(make_shared<Timer>(*this, name, seconds));
    QObject::connect(&timer->notification, &Notification::activated,
                     &timer->notification, [t=&timer, this]{ removeTimer(t->get()); });
}

void Plugin::removeTimer(const Timer *timer)
{
    if (auto it = find_if(timers_.begin(), timers_.end(),
                          [&](const auto& t) { return timer == t.get(); });
        it != timers_.end())
        timers_.erase(it);
}
