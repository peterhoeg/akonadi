/*
 * Copyright (c) 2009 Volker Krause <vkrause@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "collectionattributessynchronizationjob.h"
#include "KDBusConnectionPool"
#include "kjobprivatebase_p.h"
#include "servermanager.h"
#include "akonadicore_debug.h"

#include "agentinstance.h"
#include "agentmanager.h"
#include "collection.h"

#include <KLocalizedString>

#include <QDBusInterface>
#include <QTimer>

namespace Akonadi
{

class CollectionAttributesSynchronizationJobPrivate : public KJobPrivateBase
{
public:
    CollectionAttributesSynchronizationJobPrivate(CollectionAttributesSynchronizationJob *parent)
        : q(parent)
    {
    }

    void doStart() override;

    CollectionAttributesSynchronizationJob *q;
    AgentInstance instance;
    Collection collection;
    QDBusInterface *interface = nullptr;
    QTimer *safetyTimer = nullptr;
    int timeoutCount = 0;
    static const int timeoutCountLimit;

    void slotSynchronized(qlonglong);
    void slotTimeout();
};

const int CollectionAttributesSynchronizationJobPrivate::timeoutCountLimit = 2;

CollectionAttributesSynchronizationJob::CollectionAttributesSynchronizationJob(const Collection &collection, QObject *parent)
    : KJob(parent)
    , d(new CollectionAttributesSynchronizationJobPrivate(this))
{
    d->instance = AgentManager::self()->instance(collection.resource());
    d->collection = collection;
    d->safetyTimer = new QTimer(this);
    connect(d->safetyTimer, &QTimer::timeout, this, [this] { d->slotTimeout(); });
    d->safetyTimer->setInterval(5 * 1000);
    d->safetyTimer->setSingleShot(false);
}

CollectionAttributesSynchronizationJob::~CollectionAttributesSynchronizationJob()
{
    delete d;
}

void CollectionAttributesSynchronizationJob::start()
{
    d->start();
}

void CollectionAttributesSynchronizationJobPrivate::doStart()
{
    if (!collection.isValid()) {
        q->setError(KJob::UserDefinedError);
        q->setErrorText(i18n("Invalid collection instance."));
        q->emitResult();
        return;
    }

    if (!instance.isValid()) {
        q->setError(KJob::UserDefinedError);
        q->setErrorText(i18n("Invalid resource instance."));
        q->emitResult();
        return;
    }

    interface = new QDBusInterface(ServerManager::agentServiceName(ServerManager::Resource, instance.identifier()),
                                   QStringLiteral("/"),
                                   QStringLiteral("org.freedesktop.Akonadi.Resource"),
                                   KDBusConnectionPool::threadConnection(), this);
    connect(interface, SIGNAL(attributesSynchronized(qlonglong)), q, SLOT(slotSynchronized(qlonglong)));

    if (interface->isValid()) {
        const QDBusMessage reply = interface->call(QStringLiteral("synchronizeCollectionAttributes"), collection.id());
        if (reply.type() == QDBusMessage::ErrorMessage) {
            // This means that the resource doesn't provide a synchronizeCollectionAttributes method, so we just finish the job
            q->emitResult();
            return;
        }
        safetyTimer->start();
    } else {
        q->setError(KJob::UserDefinedError);
        q->setErrorText(i18n("Unable to obtain D-Bus interface for resource '%1'", instance.identifier()));
        q->emitResult();
        return;
    }
}

void CollectionAttributesSynchronizationJobPrivate::slotSynchronized(qlonglong id)
{
    if (id == collection.id()) {
        q->disconnect(interface, SIGNAL(attributesSynchronized(qlonglong)), q, SLOT(slotSynchronized(qlonglong)));
        safetyTimer->stop();
        q->emitResult();
    }
}

void CollectionAttributesSynchronizationJobPrivate::slotTimeout()
{
    instance = AgentManager::self()->instance(instance.identifier());
    timeoutCount++;

    if (timeoutCount > timeoutCountLimit) {
        safetyTimer->stop();
        q->setError(KJob::UserDefinedError);
        q->setErrorText(i18n("Collection attributes synchronization timed out."));
        q->emitResult();
        return;
    }

    if (instance.status() == AgentInstance::Idle) {
        // try again, we might have lost the synchronized() signal
        qCDebug(AKONADICORE_LOG) << "collection attributes" << collection.id() << instance.identifier();
        interface->call(QStringLiteral("synchronizeCollectionAttributes"), collection.id());
    }
}

}

#include "moc_collectionattributessynchronizationjob.cpp"
