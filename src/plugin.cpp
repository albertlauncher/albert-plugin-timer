// Copyright (c) 2024-2025 Manuel Schneider

#include "plugin.h"
#include <QDateTime>
#include <QLocale>
#include <albert/albert.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
ALBERT_LOGGING_CATEGORY("timer")
using namespace albert;
using namespace std;

const QStringList Plugin::icon_urls = {"gen:?text=⏲️"};

static QString durationString(uint sec)
{
    if (sec == 0)
        return {};

    const auto &[h, modm] = div(sec, 3600);
    const auto &[m, s] = div(modm, 60);

    QStringList parts;
    if (h > 0) parts.append(Plugin::tr("%n hour(s)", nullptr, h));
    if (m > 0) parts.append(Plugin::tr("%n minute(s)", nullptr, m));
    if (s > 0) parts.append(Plugin::tr("%n second(s)", nullptr, s));

    parts[0][0] = parts[0][0].toUpper();

    QString string = parts.join(" ");
    return string;
}

Timer::Timer(const QString &name, int interval):
    end(QDateTime::currentSecsSinceEpoch() + interval)
{
    setObjectName(name);
    setSingleShot(true);
    start(interval * 1000);
    connect(this, &Timer::timeout, this, &Timer::onTimeout);

    INFO << QStringLiteral("Timer started: '%1' %2 seconds").arg(objectName(), interval);

    notification.setTitle(titleString());
    notification.setText(timeoutString());
    notification.send();
}

Timer::~Timer()
{
    INFO << QStringLiteral("Timer removed: '%1' %2 seconds").arg(objectName(), interval());
}

QString Timer::titleString() const
{ return QStringLiteral("%1: %2").arg(Plugin::tr("Timer"), objectName()); }

QString Timer::durationString() const { return ::durationString(interval() / 1000); }

QString Timer::timeoutString() const
{
    QString s = isActive() ? Plugin::tr("Times out at %1") : Plugin::tr("Timed out at %1");
    return s.arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(end).time(), "hh:mm:ss"));
}

void Timer::onTimeout()
{
    INFO << QStringLiteral("Timer timed out: '%1' %2 seconds").arg(objectName(), interval());

    notification.setTitle(titleString());
    notification.setText(timeoutString());
    notification.dismiss();
    notification.send();
}

QString Plugin::defaultTrigger() const { return tr("timer ", "The trigger. Lowercase."); }

QString Plugin::synopsis(const QString &) const { return tr("<duration> [name]"); }

vector<RankItem> Plugin::handleGlobalQuery(const Query &query)
{
    if (!query.isValid())
        return {};

    Matcher matcher(query);
    vector<RankItem> r;

    // List matching timers
    for (auto &t: timers_)
        if(auto m = matcher.match(t.objectName()); m)
            r.emplace_back(
                StandardItem::make(
                    QStringLiteral("timer"),
                    t.titleString(),
                    QStringLiteral("%1 | %2").arg(t.durationString(), t.timeoutString()),
                    icon_urls,
                    {
                        {
                            QStringLiteral("rem"), tr("Remove", "Action verb form"),
                            [t=&t, this] { removeTimer(t); }
                        }
                    }
                    ),
                m
            );

    // Add new timer item
    QString name;
    uint dur = 0;
    auto s = query.string().trimmed();

    static QRegularExpression re_nat(R"(^(?:(\d+)h\ *)?(?:(\d+)m\ *)?(?:(\d+)s)?)");
    static QRegularExpression re_dig(R"(^(?|(\d+):(\d*):(\d*)|()(\d+):(\d*)|()()(\d+)))");

    if (auto m = re_nat.match(s); m.hasMatch() && m.capturedLength())  // required because all optional matches empty string
    {
        if (m.capturedLength(1)) dur += m.captured(1).toInt() * 60 * 60;  // hours
        if (m.capturedLength(2)) dur += m.captured(2).toInt() * 60;       // minutes
        if (m.capturedLength(3)) dur += m.captured(3).toInt();            // seconds
        name = query.string().mid(m.capturedLength(0)).trimmed();
    }
    else if (m = re_dig.match(s); m.hasMatch())
    {
        // hasCaptured is 6.3
        if (m.capturedLength(1)) dur += m.captured(1).toInt() * 60 * 60;  // hours
        if (m.capturedLength(2)) dur += m.captured(2).toInt() * 60;       // minutes
        if (m.capturedLength(3)) dur += m.captured(3).toInt();            // seconds
        name = query.string().mid(m.capturedLength(0)).trimmed();
    }

    if (dur > 0)
    {
        if (name.isEmpty())
            name = QString("#%1").arg(timer_counter_);
        r.emplace_back(
            StandardItem::make(
                QStringLiteral("set"),
                QStringLiteral("%1: %2").arg(tr("Set timer"), name),
                durationString(dur),
                icon_urls,
                {{
                    QStringLiteral("set"), tr("Start", "Action verb form"),
                    [=, this]{ startTimer(name, dur); }

                }}
            ),
            1.0
        );
    }

    return r;
}

vector<shared_ptr<Item>> Plugin::handleEmptyQuery()
{
    vector<shared_ptr<Item>> results;
    for (auto &t: timers_)
        results.emplace_back(
            StandardItem::make(
                QStringLiteral("timer"),
                t.titleString(),
                QStringLiteral("%1 | %2").arg(t.durationString(), t.timeoutString()),
                icon_urls,
                {
                    {
                        QStringLiteral("rem"), tr("Remove", "Action verb form"),
                        [t=&t, this] { removeTimer(t); show(""); }, false  // rerun to remove
                    }
                }
            )
        );
    return results;
}

void Plugin::startTimer(const QString &name, uint seconds)
{
    ++timer_counter_;
    auto &timer = timers_.emplace_front(name, seconds);
    QObject::connect(&timer.notification, &Notification::activated,
                     &timer.notification, [t=&timer, this]{ removeTimer(t); });
}

void Plugin::removeTimer(Timer *t)
{
    if (auto it = find_if(timers_.begin(), timers_.end(), [&](const auto& o) {return t == &o;});
        it != timers_.end())
        timers_.erase(it);
}
