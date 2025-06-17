// Copyright (c) 2024-2025 Manuel Schneider

#pragma once
#include <QTimer>
#include <albert/extensionplugin.h>
#include <albert/globalqueryhandler.h>
#include <albert/notification.h>
#include <list>
#include <set>
class Plugin;

class Timer : public QTimer, public albert::Item
{
public:

    Timer(Plugin&, const QString &name, int interval);
    ~Timer();

    QString id() const override;
    QString text() const override;
    QString subtext() const override;
    QStringList iconUrls() const override;
    std::vector<albert::Action> actions() const override;
    void addObserver(Observer *observer) override;
    void removeObserver(Observer *observer) override;

    QString titleString() const;
    QString expiryString() const;

    Plugin &plugin_;
    const uint64_t interval;
    uint64_t left;
    uint64_t end;
    albert::util::Notification notification;
    std::set<Observer*> observers;

};


class Plugin : public albert::util::ExtensionPlugin,
               public albert::GlobalQueryHandler
{
    ALBERT_PLUGIN

public:

    QString defaultTrigger() const override;
    QString synopsis(const QString &) const override;
    std::vector<albert::RankItem> handleGlobalQuery(const albert::Query &) override;
    std::vector<std::shared_ptr<albert::Item>> handleEmptyQuery() override;

    void startTimer(const QString &name, uint seconds);
    void removeTimer(const Timer *);

    std::list<std::shared_ptr<Timer>> timers_;
    uint timer_counter_ = 0;

};
