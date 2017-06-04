/*
    Copyright (c) 2006 - 2007 Volker Krause <vkrause@kde.org>

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

#ifndef AKONADI_NOTIFICATIONMANAGER_H
#define AKONADI_NOTIFICATIONMANAGER_H

#include "akthread.h"

#include <private/protocol_p.h>

#include <QTimer>
#include <QPointer>

class NotificationManagerTest;
class QThreadPool;

namespace Akonadi
{
namespace Server
{

class NotificationCollector;
class NotificationSubscriber;

class NotificationManager : public AkThread
{
    Q_OBJECT

public:
    explicit NotificationManager();
    virtual ~NotificationManager();

    void connectNotificationCollector(NotificationCollector *collector);
    void forgetSubscriber(NotificationSubscriber *subscriber);

public Q_SLOTS:
    void registerConnection(quintptr socketDescriptor);

    void emitPendingNotifications();

private Q_SLOTS:
    void slotNotify(const Akonadi::Protocol::ChangeNotificationList &msgs);

protected:
    void init() override;
    void quit() override;

    void emitDebugNotification(const Protocol::ChangeNotificationPtr &ntf,
                               const QVector<QByteArray> &listeners);

private:
    Protocol::ChangeNotificationList mNotifications;
    QTimer *mTimer;

    QThreadPool *mNotifyThreadPool;
    QVector<QPointer<NotificationSubscriber>> mSubscribers;
    int mDebugNotifications;

    friend class NotificationSubscriber;
    friend class ::NotificationManagerTest;
};

} // namespace Server
} // namespace Akonadi

#endif
