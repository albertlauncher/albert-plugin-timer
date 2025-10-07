// Copyright (c) 2024-2025 Manuel Schneider

#include "plugin.h"
#include <QDateTime>
#include <QLocale>
#include <albert/albert.h>
#include <albert/iconutil.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/standarditem.h>
ALBERT_LOGGING_CATEGORY("timer")
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace std;

namespace
{
static unique_ptr<Icon> makeIcon() { return makeGraphemeIcon(u"⏲️"_s); }
}

// QString albert::util::humanDurationString(uint64_t sec)
// {
//     if (sec == 0)
//         return {};

//     const auto &[h, modm] = div(sec, 3600);
//     const auto &[m, s] = div(modm, 60);

//     QStringList parts;
//     if (h > 0)
//         parts.append(QCoreApplication::translate("strings", "%n hour(s)", nullptr, h));
//     if (m > 0)
//         parts.append(QCoreApplication::translate("strings", "%n minute(s)", nullptr, m));
//     if (s > 0)
//         parts.append(QCoreApplication::translate("strings", "%n second(s)", nullptr, s));

//     parts[0][0] = parts[0][0].toUpper();

//     QString string = parts.join(" ");
//     return string;
// }

static QString digitalDurationString(uint64_t sec)
{
    const auto &[h, modm] = div(sec, 3600);
    const auto &[m, s] = div(modm, 60);
    return u"%1:%2:%3"_s
        .arg(h, 2, 10, '0'_L1)
        .arg(m, 2, 10, '0'_L1)
        .arg(s, 2, 10, '0'_L1);
}

static uint64_t durFromMatch(QRegularExpressionMatch m)
{
    uint64_t dur = 0;
    if (m.capturedLength(1)) dur += m.captured(1).toInt() * 60 * 60;  // hours
    if (m.capturedLength(2)) dur += m.captured(2).toInt() * 60;       // minutes
    if (m.capturedLength(3)) dur += m.captured(3).toInt();            // seconds
    return dur;
}

static uint64_t parseNaturalDurationString(const QString &s)
{
    static QRegularExpression re(uR"(^(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?$)"_s);
    if (auto m = re.match(s); m.hasMatch() && m.capturedLength())  // required because all optional matches empty string
        return durFromMatch(m);
    return 0;
}

static uint64_t parseDigitalDurationString(const QString &s)
{
    static QRegularExpression re(uR"(^(?|(\d+):(\d*):(\d*)|()(\d+):(\d*)|()()(\d+))$)"_s);
    if (auto m = re.match(s); m.hasMatch())
        return durFromMatch(m);
    return 0;
}


Timer::Timer(Plugin &plugin, const QString &name, int _interval):
    plugin_(plugin),
    interval(_interval),
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
{ return u"%1: %2"_s.arg(Plugin::tr("Timer"), objectName()); }

QString Timer::expiryString() const
{
    return (isActive() ? Plugin::tr("Expires at %1") : Plugin::tr("Expired at %1"))
        .arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(end).time(), u"hh:mm:ss"_s));
}

QString Timer::id() const { return u"timer"_s; }

QString Timer::text() const { return titleString(); }

QString Timer::subtext() const
{
    if (isActive())
        return u"%1 | %2"_s.arg(digitalDurationString(left), expiryString());
    else
        return expiryString();
}

unique_ptr<Icon> Timer::icon() const { return ::makeIcon(); }

vector<Action> Timer::actions() const
{
    return {
        {
            u"rem"_s, Plugin::tr("Remove", "Action verb form"),
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
            name = u'#' + QString::number(timer_counter_);

        r.emplace_back(
            StandardItem::make(
                u"set"_s,
                u"%1: %2"_s.arg(tr("Set timer"), name),
                digitalDurationString(duration),
                makeIcon,
                {{
                    u"set"_s, tr("Start", "Action verb form"),
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
