/*
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

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

#include "itemretrievalmanager.h"
#include "itemretrievalrequest.h"
#include "itemretrievaljob.h"
#include "dbusconnectionpool.h"
#include "akonadiserver_debug.h"

#include "resourceinterface.h"

#include <private/dbus_p.h>

#include <QCoreApplication>
#include <QReadWriteLock>
#include <QScopedPointer>
#include <QWaitCondition>
#include <QDBusConnection>
#include <QDBusConnectionInterface>

using namespace Akonadi;
using namespace Akonadi::Server;

ItemRetrievalManager *ItemRetrievalManager::sInstance = 0;

ItemRetrievalManager::ItemRetrievalManager(QObject *parent)
    : AkThread(QThread::HighPriority, parent)
{
    qDBusRegisterMetaType<QByteArrayList>();

    setObjectName(QStringLiteral("ItemRetrievalManager"));

    Q_ASSERT(sInstance == 0);
    sInstance = this;

    mLock = new QReadWriteLock();
    mWaitCondition = new QWaitCondition();
}

ItemRetrievalManager::~ItemRetrievalManager()
{
    quitThread();

    delete mWaitCondition;
    delete mLock;
    sInstance = 0;
}

void ItemRetrievalManager::init()
{
    AkThread::init();

    QDBusConnection conn = DBusConnectionPool::threadConnection();
    connect(conn.interface(), &QDBusConnectionInterface::serviceOwnerChanged,
            this, &ItemRetrievalManager::serviceOwnerChanged);
    connect(this, &ItemRetrievalManager::requestAdded,
            this, &ItemRetrievalManager::processRequest,
            Qt::QueuedConnection);
}

ItemRetrievalManager *ItemRetrievalManager::instance()
{
    Q_ASSERT(sInstance);
    return sInstance;
}

// called within the retrieval thread
void ItemRetrievalManager::serviceOwnerChanged(const QString &serviceName, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(newOwner);
    if (oldOwner.isEmpty()) {
        return;
    }
    DBus::AgentType type = DBus::Unknown;
    const QString resourceId = DBus::parseAgentServiceName(serviceName, type);
    if (resourceId.isEmpty() || type != DBus::Resource) {
        return;
    }
    qCDebug(AKONADISERVER_LOG) << "Lost connection to resource" << serviceName << ", discarding cached interface";
    mResourceInterfaces.remove(resourceId);
}

// called within the retrieval thread
org::freedesktop::Akonadi::Resource *ItemRetrievalManager::resourceInterface(const QString &id)
{
    if (id.isEmpty()) {
        return 0;
    }

    org::freedesktop::Akonadi::Resource *iface = mResourceInterfaces.value(id);
    if (iface && iface->isValid()) {
        return iface;
    }

    delete iface;
    iface = new org::freedesktop::Akonadi::Resource(DBus::agentServiceName(id, DBus::Resource),
                                                    QStringLiteral("/"),
                                                    DBusConnectionPool::threadConnection(),
                                                    this);
    if (!iface || !iface->isValid()) {
        qCCritical(AKONADISERVER_LOG) << QStringLiteral("Cannot connect to agent instance with identifier '%1', error message: '%2'")
                  .arg(id, iface ? iface->lastError().message() : QString());
        delete iface;
        return 0;
    }
    // DBus calls can take some time to reply -- e.g. if a huge local mbox has to be parsed first.
    iface->setTimeout(5 * 60 * 1000);   // 5 minutes, rather than 25 seconds
    mResourceInterfaces.insert(id, iface);
    return iface;
}

// called from any thread
void ItemRetrievalManager::requestItemDelivery(qint64 uid, const QByteArray &remoteId, const QByteArray &mimeType,
                                               const QString &resource, const QVector<QByteArray> &parts)
{
    ItemRetrievalRequest *req = new ItemRetrievalRequest();
    req->id = uid;
    req->remoteId = QString::fromUtf8(remoteId);
    req->mimeType = QString::fromUtf8(mimeType);
    req->resourceId = resource;
    req->parts = parts.toList();

    requestItemDelivery(req);
}

void ItemRetrievalManager::requestItemDelivery(ItemRetrievalRequest *req)
{
    mLock->lockForWrite();
    qCDebug(AKONADISERVER_LOG) << "posting retrieval request for item" << req->id << " there are "
              << mPendingRequests.size() << " queues and "
              << mPendingRequests[req->resourceId].size() << " items in mine";
    mPendingRequests[req->resourceId].append(req);
    mLock->unlock();

    Q_EMIT requestAdded();

    mLock->lockForRead();
    Q_FOREVER {
        //qCDebug(AKONADISERVER_LOG) << "checking if request for item" << req->id << "has been processed...";
        if (req->processed) {
            QScopedPointer<ItemRetrievalRequest> reqDeleter(req);
            Q_ASSERT(!mPendingRequests[req->resourceId].contains(req));
            const QString errorMsg = req->errorMsg;
            mLock->unlock();
            if (errorMsg.isEmpty()) {
                qCDebug(AKONADISERVER_LOG) << "request for item" << req->id << "succeeded";
                return;
            } else {
                qCDebug(AKONADISERVER_LOG) << "request for item" << req->id << req->remoteId << "failed:" << errorMsg;
                throw ItemRetrieverException(errorMsg);
            }
        } else {
            qCDebug(AKONADISERVER_LOG) << "request for item" << req->id << "still pending - waiting";
            mWaitCondition->wait(mLock);
            qCDebug(AKONADISERVER_LOG) << "continuing";
        }
    }

    throw ItemRetrieverException("WTF?");
}

// called within the retrieval thread
void ItemRetrievalManager::processRequest()
{
    QVector<QPair<ItemRetrievalJob *, QString> > newJobs;

    mLock->lockForWrite();
    // look for idle resources
    for (QHash< QString, QList< ItemRetrievalRequest *> >::iterator it = mPendingRequests.begin(); it != mPendingRequests.end();) {
        if (it.value().isEmpty()) {
            it = mPendingRequests.erase(it);
            continue;
        }
        if (!mCurrentJobs.contains(it.key()) || mCurrentJobs.value(it.key()) == 0) {
            // TODO: check if there is another one for the same uid with more parts requested
            ItemRetrievalRequest *req = it.value().takeFirst();
            Q_ASSERT(req->resourceId == it.key());
            ItemRetrievalJob *job = new ItemRetrievalJob(req, this);
            connect(job, &ItemRetrievalJob::requestCompleted, this, &ItemRetrievalManager::retrievalJobFinished);
            mCurrentJobs.insert(req->resourceId, job);
            // delay job execution until after we unlocked the mutex, since the job can emit the finished signal immediately in some cases
            newJobs.append(qMakePair(job, req->resourceId));
        }
        ++it;
    }

    bool nothingGoingOn = mPendingRequests.isEmpty() && mCurrentJobs.isEmpty() && newJobs.isEmpty();
    mLock->unlock();

    if (nothingGoingOn) {   // someone asked as to process requests although everything is done already, he might still be waiting
        mWaitCondition->wakeAll();
        return;
    }

    for (QVector<QPair<ItemRetrievalJob *, QString> >::const_iterator it = newJobs.constBegin(); it != newJobs.constEnd(); ++it) {
        (*it).first->start(resourceInterface((*it).second));
    }
}

void ItemRetrievalManager::retrievalJobFinished(ItemRetrievalRequest *request, const QString &errorMsg)
{
    mLock->lockForWrite();
    request->errorMsg = errorMsg;
    request->processed = true;
    Q_ASSERT(mCurrentJobs.contains(request->resourceId));
    mCurrentJobs.remove(request->resourceId);
    // TODO check if (*it)->parts is a subset of currentRequest->parts
    for (QList<ItemRetrievalRequest *>::Iterator it = mPendingRequests[request->resourceId].begin(); it != mPendingRequests[request->resourceId].end();) {
        if ((*it)->id == request->id) {
            qCDebug(AKONADISERVER_LOG) << "someone else requested item" << request->id << "as well, marking as processed";
            (*it)->errorMsg = errorMsg;
            (*it)->processed = true;
            it = mPendingRequests[request->resourceId].erase(it);
        } else {
            ++it;
        }
    }
    mWaitCondition->wakeAll();
    mLock->unlock();
    Q_EMIT requestAdded(); // trigger processRequest() again, in case there is more in the queues
}

void ItemRetrievalManager::triggerCollectionSync(const QString &resource, qint64 colId)
{
    org::freedesktop::Akonadi::Resource *interface = resourceInterface(resource);
    if (interface) {
        interface->synchronizeCollection(colId);
    }
}

void ItemRetrievalManager::triggerCollectionTreeSync(const QString &resource)
{
    org::freedesktop::Akonadi::Resource *interface = resourceInterface(resource);
    if (interface) {
        interface->synchronizeCollectionTree();
    }
}
