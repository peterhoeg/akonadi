/*
    Copyright (c) 2006-2008 Tobias Koenig <tokoe@kde.org>

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

#include "agentmanager.h"
#include "agentmanager_p.h"

#include "agenttype_p.h"
#include "agentinstance_p.h"
#include "KDBusConnectionPool"
#include "servermanager.h"
#include "collection.h"

#include "shared/akranges.h"

#include <QDBusServiceWatcher>
#include <QWidget>

using namespace Akonadi;

// @cond PRIVATE

AgentInstance AgentManagerPrivate::createInstance(const AgentType &type)
{
    const QString &identifier = mManager->createAgentInstance(type.identifier());
    if (identifier.isEmpty()) {
        return AgentInstance();
    }

    return fillAgentInstanceLight(identifier);
}

void AgentManagerPrivate::agentTypeAdded(const QString &identifier)
{
    // Ignore agent types we already know about, for example because we called
    // readAgentTypes before.
    if (mTypes.contains(identifier)) {
        return;
    }

    if (mTypes.isEmpty()) {
        // The Akonadi ServerManager assumes that the server is up and running as soon
        // as it knows about at least one agent type.
        // If we Q_EMIT the typeAdded() signal here, it therefore thinks the server is
        // running. However, the AgentManager does not know about all agent types yet,
        // as the server might still have pending agentTypeAdded() signals, even though
        // it internally knows all agent types already.
        // This can cause situations where the client gets told by the ServerManager that
        // the server is running, yet the client will encounter an error because the
        // AgentManager doesn't know all types yet.
        //
        // Therefore, we read all agent types from the server here so they are known.
        readAgentTypes();
    }

    const AgentType type = fillAgentType(identifier);
    if (type.isValid()) {
        mTypes.insert(identifier, type);

        Q_EMIT mParent->typeAdded(type);
    }
}

void AgentManagerPrivate::agentTypeRemoved(const QString &identifier)
{
    if (!mTypes.contains(identifier)) {
        return;
    }

    const AgentType type = mTypes.take(identifier);
    Q_EMIT mParent->typeRemoved(type);
}

void AgentManagerPrivate::agentInstanceAdded(const QString &identifier)
{
    const AgentInstance instance = fillAgentInstance(identifier);
    if (instance.isValid()) {

        // It is possible that this function is called when the instance is already
        // in our list we filled initially in the constructor.
        // This happens when the constructor is called during Akonadi startup, when
        // the agent processes are not fully loaded and have no D-Bus interface yet.
        // The server-side agent manager then emits the instance added signal when
        // the D-Bus interface for the agent comes up.
        // In this case, we simply notify that the instance status has changed.
        const bool newAgentInstance = !mInstances.contains(identifier);
        if (newAgentInstance) {
            mInstances.insert(identifier, instance);
            Q_EMIT mParent->instanceAdded(instance);
        } else {
            mInstances.remove(identifier);
            mInstances.insert(identifier, instance);
            Q_EMIT mParent->instanceStatusChanged(instance);
        }
    }
}

void AgentManagerPrivate::agentInstanceRemoved(const QString &identifier)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    const AgentInstance instance = mInstances.take(identifier);
    Q_EMIT mParent->instanceRemoved(instance);
}

void AgentManagerPrivate::agentInstanceStatusChanged(const QString &identifier, int status, const QString &msg)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    AgentInstance &instance = mInstances[identifier];
    instance.d->mStatus = status;
    instance.d->mStatusMessage = msg;

    Q_EMIT mParent->instanceStatusChanged(instance);
}

void AgentManagerPrivate::agentInstanceProgressChanged(const QString &identifier, uint progress, const QString &msg)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    AgentInstance &instance = mInstances[identifier];
    instance.d->mProgress = progress;
    if (!msg.isEmpty()) {
        instance.d->mStatusMessage = msg;
    }

    Q_EMIT mParent->instanceProgressChanged(instance);
}

void AgentManagerPrivate::agentInstanceWarning(const QString &identifier, const QString &msg)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    AgentInstance &instance = mInstances[identifier];
    Q_EMIT mParent->instanceWarning(instance, msg);
}

void AgentManagerPrivate::agentInstanceError(const QString &identifier, const QString &msg)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    AgentInstance &instance = mInstances[identifier];
    Q_EMIT mParent->instanceError(instance, msg);
}

void AgentManagerPrivate::agentInstanceOnlineChanged(const QString &identifier, bool state)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    AgentInstance &instance = mInstances[identifier];
    instance.d->mIsOnline = state;
    Q_EMIT mParent->instanceOnline(instance, state);
}

void AgentManagerPrivate::agentInstanceNameChanged(const QString &identifier, const QString &name)
{
    if (!mInstances.contains(identifier)) {
        return;
    }

    AgentInstance &instance = mInstances[identifier];
    instance.d->mName = name;

    Q_EMIT mParent->instanceNameChanged(instance);
}

void AgentManagerPrivate::readAgentTypes()
{
    const QDBusReply<QStringList> types = mManager->agentTypes();
    if (types.isValid()) {
        const QStringList lst = types.value();
        for (const QString &type : lst) {
            const AgentType agentType = fillAgentType(type);
            if (agentType.isValid()) {
                mTypes.insert(type, agentType);
                Q_EMIT mParent->typeAdded(agentType);
            }
        }
    }
}

void AgentManagerPrivate::readAgentInstances()
{
    const QDBusReply<QStringList> instances = mManager->agentInstances();
    if (instances.isValid()) {
        const QStringList lst = instances.value();
        for (const QString &instance : lst) {
            const AgentInstance agentInstance = fillAgentInstance(instance);
            if (agentInstance.isValid()) {
                mInstances.insert(instance, agentInstance);
                Q_EMIT mParent->instanceAdded(agentInstance);
            }
        }
    }
}

AgentType AgentManagerPrivate::fillAgentType(const QString &identifier) const
{
    AgentType type;
    type.d->mIdentifier = identifier;
    type.d->mName = mManager->agentName(identifier);
    type.d->mDescription = mManager->agentComment(identifier);
    type.d->mIconName = mManager->agentIcon(identifier);
    type.d->mMimeTypes = mManager->agentMimeTypes(identifier);
    type.d->mCapabilities = mManager->agentCapabilities(identifier);
    type.d->mCustomProperties = mManager->agentCustomProperties(identifier);

    return type;
}

void AgentManagerPrivate::setName(const AgentInstance &instance, const QString &name)
{
    mManager->setAgentInstanceName(instance.identifier(), name);
}

void AgentManagerPrivate::setOnline(const AgentInstance &instance, bool state)
{
    mManager->setAgentInstanceOnline(instance.identifier(), state);
}

void AgentManagerPrivate::configure(const AgentInstance &instance, QWidget *parent)
{
    qlonglong winId = 0;
    if (parent) {
        winId = static_cast<qlonglong>(parent->window()->winId());
    }

    mManager->agentInstanceConfigure(instance.identifier(), winId);
}

void AgentManagerPrivate::synchronize(const AgentInstance &instance)
{
    mManager->agentInstanceSynchronize(instance.identifier());
}

void AgentManagerPrivate::synchronizeCollectionTree(const AgentInstance &instance)
{
    mManager->agentInstanceSynchronizeCollectionTree(instance.identifier());
}

void AgentManagerPrivate::synchronizeTags(const AgentInstance &instance)
{
    mManager->agentInstanceSynchronizeTags(instance.identifier());
}

void AgentManagerPrivate::synchronizeRelations(const AgentInstance &instance)
{
    mManager->agentInstanceSynchronizeRelations(instance.identifier());
}

AgentInstance AgentManagerPrivate::fillAgentInstance(const QString &identifier) const
{
    AgentInstance instance;

    const QString agentTypeIdentifier = mManager->agentInstanceType(identifier);
    if (!mTypes.contains(agentTypeIdentifier)) {
        return instance;
    }

    instance.d->mType = mTypes.value(agentTypeIdentifier);
    instance.d->mIdentifier = identifier;
    instance.d->mName = mManager->agentInstanceName(identifier);
    instance.d->mStatus = mManager->agentInstanceStatus(identifier);
    instance.d->mStatusMessage = mManager->agentInstanceStatusMessage(identifier);
    instance.d->mProgress = mManager->agentInstanceProgress(identifier);
    instance.d->mIsOnline = mManager->agentInstanceOnline(identifier);

    return instance;
}

AgentInstance AgentManagerPrivate::fillAgentInstanceLight(const QString &identifier) const
{
    AgentInstance instance;

    const QString agentTypeIdentifier = mManager->agentInstanceType(identifier);
    Q_ASSERT_X(mTypes.contains(agentTypeIdentifier), "fillAgentInstanceLight", "Requests non-existing agent type");

    instance.d->mType = mTypes.value(agentTypeIdentifier);
    instance.d->mIdentifier = identifier;

    return instance;
}

void AgentManagerPrivate::serviceOwnerChanged(const QString &, const QString &oldOwner, const QString &)
{
    if (oldOwner.isEmpty()) {
        if (mTypes.isEmpty()) { // just to be safe
            readAgentTypes();
        }
        if (mInstances.isEmpty()) {
            readAgentInstances();
        }
    }
}

void AgentManagerPrivate::createDBusInterface()
{
    mTypes.clear();
    mInstances.clear();
    delete mManager;

    mManager = new org::freedesktop::Akonadi::AgentManager(ServerManager::serviceName(ServerManager::Control),
            QStringLiteral("/AgentManager"),
            KDBusConnectionPool::threadConnection(), mParent);

    QObject::connect(mManager, SIGNAL(agentTypeAdded(QString)),
                     mParent, SLOT(agentTypeAdded(QString)));
    QObject::connect(mManager, SIGNAL(agentTypeRemoved(QString)),
                     mParent, SLOT(agentTypeRemoved(QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceAdded(QString)),
                     mParent, SLOT(agentInstanceAdded(QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceRemoved(QString)),
                     mParent, SLOT(agentInstanceRemoved(QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceStatusChanged(QString,int,QString)),
                     mParent, SLOT(agentInstanceStatusChanged(QString,int,QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceProgressChanged(QString,uint,QString)),
                     mParent, SLOT(agentInstanceProgressChanged(QString,uint,QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceNameChanged(QString,QString)),
                     mParent, SLOT(agentInstanceNameChanged(QString,QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceWarning(QString,QString)),
                     mParent, SLOT(agentInstanceWarning(QString,QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceError(QString,QString)),
                     mParent, SLOT(agentInstanceError(QString,QString)));
    QObject::connect(mManager, SIGNAL(agentInstanceOnlineChanged(QString,bool)),
                     mParent, SLOT(agentInstanceOnlineChanged(QString,bool)));

    if (mManager->isValid()) {
        readAgentTypes();
        readAgentInstances();
    }
}

AgentManager *AgentManagerPrivate::mSelf = nullptr;

AgentManager::AgentManager()
    : QObject(nullptr)
    , d(new AgentManagerPrivate(this))
{
    // needed for queued connections on our signals
    qRegisterMetaType<Akonadi::AgentType>();
    qRegisterMetaType<Akonadi::AgentInstance>();

    d->createDBusInterface();

    QDBusServiceWatcher *watcher = new QDBusServiceWatcher(ServerManager::serviceName(ServerManager::Control),
            KDBusConnectionPool::threadConnection(),
            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, [this](const QString &arg1, const QString &arg2 , const QString &arg3) { d->serviceOwnerChanged(arg1, arg2, arg3); });
}

// @endcond

AgentManager::~AgentManager()
{
    delete d;
}

AgentManager *AgentManager::self()
{
    if (!AgentManagerPrivate::mSelf) {
        AgentManagerPrivate::mSelf = new AgentManager();
    }

    return AgentManagerPrivate::mSelf;
}

AgentType::List AgentManager::types() const
{
    // Maybe the Control process is up and ready but we haven't been to the event loop yet so serviceOwnerChanged wasn't called yet.
    // In that case make sure to do it here, to avoid going into Broken state.
    if (d->mTypes.isEmpty()) {
        d->readAgentTypes();
    }
    return d->mTypes | values | toQVector;
}

AgentType AgentManager::type(const QString &identifier) const
{
    return d->mTypes.value(identifier);
}

AgentInstance::List AgentManager::instances() const
{
    return d->mInstances | values | toQVector;
}

AgentInstance AgentManager::instance(const QString &identifier) const
{
    return d->mInstances.value(identifier);
}

void AgentManager::removeInstance(const AgentInstance &instance)
{
    d->mManager->removeAgentInstance(instance.identifier());
}

void AgentManager::synchronizeCollection(const Collection &collection)
{
    synchronizeCollection(collection, false);
}

void AgentManager::synchronizeCollection(const Collection &collection, bool recursive)
{
    const QString resId = collection.resource();
    Q_ASSERT(!resId.isEmpty());
    d->mManager->agentInstanceSynchronizeCollection(resId, collection.id(), recursive);
}

#include "moc_agentmanager.cpp"
