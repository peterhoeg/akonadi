/*
    Copyright (c) 2006 - 2007 Volker Krause <vkrause@kde.org>
    Copyright (c) 2010 Michael Jansen <kde@michael-jansen>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#include "notificationmanager.h"
#include "notificationsubscriber.h"
#include "storage/notificationcollector.h"
#include "tracer.h"

#include <shared/akdebug.h>
#include <private/standarddirs_p.h>
#include <private/xdgbasedirs_p.h>

#include <QLocalSocket>
#include <QSettings>
#include <QCoreApplication>
#include <QThreadPool>
#include <QPointer>

using namespace Akonadi;
using namespace Akonadi::Server;

NotificationManager::NotificationManager()
    : AkThread()
{
}

NotificationManager::~NotificationManager()
{
}

void NotificationManager::init()
{
    AkThread::init();

    const QString serverConfigFile = StandardDirs::serverConfigFile(XdgBaseDirs::ReadWrite);
    QSettings settings(serverConfigFile, QSettings::IniFormat);

    mTimer = new QTimer(this);
    mTimer->setInterval(settings.value(QStringLiteral("NotificationManager/Interval"), 50).toInt());
    mTimer->setSingleShot(true);
    connect(mTimer, &QTimer::timeout,
            this, &NotificationManager::emitPendingNotifications);

    mNotifyThreadPool = new QThreadPool(this);
    mNotifyThreadPool->setMaxThreadCount(5);
}

void NotificationManager::quit()
{
    AkThread::quit();

    qDeleteAll(mSubscribers);
}

void NotificationManager::registerConnection(quintptr socketDescriptor)
{
    Q_ASSERT(thread() == QThread::currentThread());

    NotificationSubscriber *subscriber = new NotificationSubscriber(this, socketDescriptor);
    qDebug() << "NotificationManager: new connection (registered as" << subscriber << ")";
    connect(subscriber, &QObject::destroyed,
            [this, subscriber]() {
                mSubscribers.removeOne(subscriber);
            });

    mSubscribers.push_back(subscriber);
}

void NotificationManager::connectNotificationCollector(NotificationCollector *collector)
{
    connect(collector, &NotificationCollector::notify,
            this, &NotificationManager::slotNotify);
}

void NotificationManager::slotNotify(const Protocol::ChangeNotification::List &msgs)
{
    Q_FOREACH (const Protocol::ChangeNotification &msg, msgs) {
        if (msg.type() == Protocol::Command::CollectionChangeNotification) {
            Protocol::CollectionChangeNotification::appendAndCompress(mNotifications, msg);
        } else {
            mNotifications.push_back(msg);
        }
    }

    if (!mTimer->isActive()) {
        mTimer->start();
    }
}

class NotifyRunnable : public QRunnable
{
public:
    explicit NotifyRunnable(NotificationSubscriber *subscriber,
                            const Protocol::ChangeNotification::List &notifications)
        : mSubscriber(subscriber)
        , mNotifications(notifications)
    {
    }

    ~NotifyRunnable()
    {
    }

    void run() Q_DECL_OVERRIDE
    {
        if (mSubscriber) {
            mSubscriber->notify(mNotifications);
        }
    }

private:
    QPointer<NotificationSubscriber> mSubscriber;
    Protocol::ChangeNotification::List mNotifications;
};

void NotificationManager::emitPendingNotifications()
{
    if (mNotifications.isEmpty()) {
        return;
    }

    Q_FOREACH (NotificationSubscriber *subscriber, mSubscribers) {
        mNotifyThreadPool->start(new NotifyRunnable(subscriber, mNotifications));
    }

    mNotifications.clear();
}

