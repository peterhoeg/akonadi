/***************************************************************************
 *   Copyright (C) 2006 by Tobias Koenig <tokoe@kde.org>                   *
 *   Copyright (c) 2007 Volker Krause <vkrause@kde.org>                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "agentmanager.h"

#include "agentmanageradaptor.h"
#include "agentmanagerinternaladaptor.h"
#include "agentprocessinstance.h"
#include "agentserverinterface.h"
#include "agentthreadinstance.h"
#include "preprocessor_manager.h"
#include "processcontrol.h"
#include "resource_manager.h"
#include "serverinterface.h"
#include "akonadicontrol_debug.h"

#include <private/protocol_p.h>
#include <private/instance_p.h>
#include <private/standarddirs_p.h>
#include <private/dbus_p.h>

#include <shared/akapplication.h>

#include <QCoreApplication>
#include <QDir>
#include <QScopedPointer>
#include <QSettings>
#include <QDBusConnection>

using Akonadi::ProcessControl;

static const bool enableAgentServerDefault = false;

AgentManager::AgentManager(bool verbose, QObject *parent)
    : QObject(parent)
    , mAgentServer(nullptr)
    , mVerbose(verbose)
{
    new AgentManagerAdaptor(this);
    new AgentManagerInternalAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/AgentManager"), this);

    connect(QDBusConnection::sessionBus().interface(), &QDBusConnectionInterface::serviceOwnerChanged,
            this, &AgentManager::serviceOwnerChanged);

    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Server))) {
        qFatal("akonadiserver already running!");
    }

    const QSettings settings(Akonadi::StandardDirs::agentsConfigFile(Akonadi::StandardDirs::ReadOnly), QSettings::IniFormat);
    mAgentServerEnabled = settings.value(QStringLiteral("AgentServer/Enabled"), enableAgentServerDefault).toBool();

    QStringList serviceArgs;
    if (Akonadi::Instance::hasIdentifier()) {
        serviceArgs << QStringLiteral("--instance") << Akonadi::Instance::identifier();
    }
    if (verbose) {
        serviceArgs << QStringLiteral("--verbose");
    }

    mStorageController = new Akonadi::ProcessControl;
    mStorageController->setShutdownTimeout(15 * 1000);   // the server needs more time for shutdown if we are using an internal mysqld
    connect(mStorageController, &Akonadi::ProcessControl::unableToStart, this, &AgentManager::serverFailure);
    mStorageController->start(QStringLiteral("akonadiserver"), serviceArgs, Akonadi::ProcessControl::RestartOnCrash);

    if (mAgentServerEnabled) {
        mAgentServer = new Akonadi::ProcessControl;
        connect(mAgentServer, &Akonadi::ProcessControl::unableToStart, this, &AgentManager::agentServerFailure);
        mAgentServer->start(QStringLiteral("akonadi_agent_server"), serviceArgs, Akonadi::ProcessControl::RestartOnCrash);
    }
}

void AgentManager::continueStartup()
{
    // prevent multiple calls in case the server has to be restarted
    static bool first = true;
    if (!first) {
        return;
    }

    first = false;

    readPluginInfos();
    for (const AgentType &info : qAsConst(mAgents)) {
        Q_EMIT agentTypeAdded(info.identifier);
    }

    load();
    for (const AgentType &info : qAsConst(mAgents)) {
        ensureAutoStart(info);
    }

    // register the real service name once everything is up an running
    if (!QDBusConnection::sessionBus().registerService(Akonadi::DBus::serviceName(Akonadi::DBus::Control))) {
        // besides a race with an older Akonadi server I have no idea how we could possibly get here...
        qFatal("Unable to register service as %s despite having the lock. Error was: %s",
               qPrintable(Akonadi::DBus::serviceName(Akonadi::DBus::Control)),
               qPrintable(QDBusConnection::sessionBus().lastError().message()));
    }
    qCInfo(AKONADICONTROL_LOG) << "Akonadi server is now operational.";
}

AgentManager::~AgentManager()
{
    cleanup();
}

void AgentManager::cleanup()
{
    for (const AgentInstance::Ptr &instance : qAsConst(mAgentInstances)) {
        instance->quit();
    }

    mAgentInstances.clear();

    mStorageController->setCrashPolicy(ProcessControl::StopOnCrash);
    org::freedesktop::Akonadi::Server *serverIface =
        new org::freedesktop::Akonadi::Server(Akonadi::DBus::serviceName(Akonadi::DBus::Server), QStringLiteral("/Server"),
                QDBusConnection::sessionBus(), this);
    serverIface->quit();

    if (mAgentServer) {
        mAgentServer->setCrashPolicy(ProcessControl::StopOnCrash);
        org::freedesktop::Akonadi::AgentServer *agentServerIface =
            new org::freedesktop::Akonadi::AgentServer(Akonadi::DBus::serviceName(Akonadi::DBus::AgentServer),
                    QStringLiteral("/AgentServer"), QDBusConnection::sessionBus(), this);
        agentServerIface->quit();
    }

    delete mStorageController;
    mStorageController = nullptr;

    delete mAgentServer;
    mAgentServer = nullptr;
}

QStringList AgentManager::agentTypes() const
{
    return mAgents.keys();
}

QString AgentManager::agentName(const QString &identifier) const
{
    if (!checkAgentExists(identifier)) {
        return QString();
    }

    return mAgents.value(identifier).name;
}

QString AgentManager::agentComment(const QString &identifier) const
{
    if (!checkAgentExists(identifier)) {
        return QString();
    }

    return mAgents.value(identifier).comment;
}

QString AgentManager::agentIcon(const QString &identifier) const
{
    if (!checkAgentExists(identifier)) {
        return QString();
    }

    const AgentType info = mAgents.value(identifier);
    if (!info.icon.isEmpty()) {
        return info.icon;
    }

    return QStringLiteral("application-x-executable");
}

QStringList AgentManager::agentMimeTypes(const QString &identifier) const
{
    if (!checkAgentExists(identifier)) {
        return QStringList();
    }

    return mAgents.value(identifier).mimeTypes;
}

QStringList AgentManager::agentCapabilities(const QString &identifier) const
{
    if (!checkAgentExists(identifier)) {
        return QStringList();
    }
    return mAgents.value(identifier).capabilities;
}

QVariantMap AgentManager::agentCustomProperties(const QString &identifier) const
{
    if (!checkAgentExists(identifier)) {
        return QVariantMap();
    }

    return mAgents.value(identifier).custom;
}

AgentInstance::Ptr AgentManager::createAgentInstance(const AgentType &info)
{
    switch (info.launchMethod) {
    case AgentType::Server:
        return AgentInstance::Ptr(new Akonadi::AgentThreadInstance(this));
    case AgentType::Launcher: // Fall through
    case AgentType::Process:
        return AgentInstance::Ptr(new Akonadi::AgentProcessInstance(this));
    default:
        Q_ASSERT_X(false, "AgentManger::createAgentInstance", "Unhandled AgentType::LaunchMethod case");
    }

    return AgentInstance::Ptr();
}

QString AgentManager::createAgentInstance(const QString &identifier)
{
    if (!checkAgentExists(identifier)) {
        return QString();
    }

    const AgentType agentInfo = mAgents.value(identifier);
    mAgents[identifier].instanceCounter++;

    const AgentInstance::Ptr instance = createAgentInstance(agentInfo);
    if (agentInfo.capabilities.contains(AgentType::CapabilityUnique)) {
        instance->setIdentifier(identifier);
    } else {
        instance->setIdentifier(QStringLiteral("%1_%2").arg(identifier, QString::number(agentInfo.instanceCounter)));
    }

    const QString instanceIdentifier = instance->identifier();
    if (mAgentInstances.contains(instanceIdentifier)) {
        qCWarning(AKONADICONTROL_LOG) << "Cannot create another instance of agent" << identifier;
        return QString();
    }

    // Return from this dbus call before we do the next. Otherwise dbus brakes for
    // this process.
    if (calledFromDBus()) {
        connection().send(message().createReply(instanceIdentifier));
    }

    if (!instance->start(agentInfo)) {
        return QString();
    }

    mAgentInstances.insert(instanceIdentifier, instance);
    registerAgentAtServer(instanceIdentifier, agentInfo);
    save();

    return instanceIdentifier;
}

void AgentManager::removeAgentInstance(const QString &identifier)
{
    const AgentInstance::Ptr instance = mAgentInstances.value(identifier);
    if (!instance) {
        qCWarning(AKONADICONTROL_LOG) << Q_FUNC_INFO << "Agent instance with identifier" << identifier << "does not exist";
        return;
    }

    if (instance->hasAgentInterface()) {
        instance->cleanup();
    } else {
        qCWarning(AKONADICONTROL_LOG) << "Agent instance" << identifier << "has no interface!";
    }

    mAgentInstances.remove(identifier);

    save();

    org::freedesktop::Akonadi::ResourceManager resmanager(Akonadi::DBus::serviceName(Akonadi::DBus::Server), QStringLiteral("/ResourceManager"), QDBusConnection::sessionBus(), this);
    resmanager.removeResourceInstance(instance->identifier());

    // Kill the preprocessor instance, if any.
    org::freedesktop::Akonadi::PreprocessorManager preProcessorManager(
        Akonadi::DBus::serviceName(Akonadi::DBus::Server),
        QStringLiteral("/PreprocessorManager"),
        QDBusConnection::sessionBus(),
        this);

    preProcessorManager.unregisterInstance(instance->identifier());

    if (instance->hasAgentInterface()) {
        qCDebug(AKONADICONTROL_LOG) << "AgentManager::removeAgentInstance: calling instance->quit()";
        instance->quit();
    } else {
        qCWarning(AKONADICONTROL_LOG) << "Agent instance" << identifier << "has no interface!";
    }

    Q_EMIT agentInstanceRemoved(identifier);
}

QString AgentManager::agentInstanceType(const QString &identifier)
{
    const AgentInstance::Ptr agent = mAgentInstances.value(identifier);
    if (!agent) {
        qCWarning(AKONADICONTROL_LOG) << "Agent instance with identifier" << identifier << "does not exist";
        return QString();
    }

    return agent->agentType();
}

QStringList AgentManager::agentInstances() const
{
    return mAgentInstances.keys();
}

int AgentManager::agentInstanceStatus(const QString &identifier) const
{
    if (!checkInstance(identifier)) {
        return 2;
    }

    return mAgentInstances.value(identifier)->status();
}

QString AgentManager::agentInstanceStatusMessage(const QString &identifier) const
{
    if (!checkInstance(identifier)) {
        return QString();
    }

    return mAgentInstances.value(identifier)->statusMessage();
}

uint AgentManager::agentInstanceProgress(const QString &identifier) const
{
    if (!checkInstance(identifier)) {
        return 0;
    }

    return mAgentInstances.value(identifier)->progress();
}

QString AgentManager::agentInstanceProgressMessage(const QString &identifier) const
{
    Q_UNUSED(identifier);

    return QString();
}

void AgentManager::agentInstanceConfigure(const QString &identifier, qlonglong windowId)
{
    if (!checkAgentInterfaces(identifier, QStringLiteral("agentInstanceConfigure"))) {
        return;
    }

    mAgentInstances.value(identifier)->configure(windowId);
}

bool AgentManager::agentInstanceOnline(const QString &identifier)
{
    if (!checkInstance(identifier)) {
        return false;
    }

    return mAgentInstances.value(identifier)->isOnline();
}

void AgentManager::setAgentInstanceOnline(const QString &identifier, bool state)
{
    if (!checkAgentInterfaces(identifier, QStringLiteral("setAgentInstanceOnline"))) {
        return;
    }

    mAgentInstances.value(identifier)->statusInterface()->setOnline(state);
}

// resource specific methods //
void AgentManager::setAgentInstanceName(const QString &identifier, const QString &name)
{
    if (!checkResourceInterface(identifier, QStringLiteral("setAgentInstanceName"))) {
        return;
    }

    mAgentInstances.value(identifier)->resourceInterface()->setName(name);
}

QString AgentManager::agentInstanceName(const QString &identifier) const
{
    if (!checkInstance(identifier)) {
        return QString();
    }

    const AgentInstance::Ptr instance = mAgentInstances.value(identifier);
    if (!instance->resourceName().isEmpty()) {
        return instance->resourceName();
    }

    if (!checkAgentExists(instance->agentType())) {
        return QString();
    }

    return mAgents.value(instance->agentType()).name;
}

void AgentManager::agentInstanceSynchronize(const QString &identifier)
{
    if (!checkResourceInterface(identifier, QStringLiteral("agentInstanceSynchronize"))) {
        return;
    }

    mAgentInstances.value(identifier)->resourceInterface()->synchronize();
}

void AgentManager::agentInstanceSynchronizeCollectionTree(const QString &identifier)
{
    if (!checkResourceInterface(identifier, QStringLiteral("agentInstanceSynchronizeCollectionTree"))) {
        return;
    }

    mAgentInstances.value(identifier)->resourceInterface()->synchronizeCollectionTree();
}

void AgentManager::agentInstanceSynchronizeCollection(const QString &identifier, qint64 collection)
{
    agentInstanceSynchronizeCollection(identifier, collection, false);
}

void AgentManager::agentInstanceSynchronizeCollection(const QString &identifier, qint64 collection, bool recursive)
{
    if (!checkResourceInterface(identifier, QStringLiteral("agentInstanceSynchronizeCollection"))) {
        return;
    }

    mAgentInstances.value(identifier)->resourceInterface()->synchronizeCollection(collection, recursive);
}

void AgentManager::agentInstanceSynchronizeTags(const QString &identifier)
{
    if (!checkResourceInterface(identifier, QStringLiteral("agentInstanceSynchronizeTags"))) {
        return;
    }

    mAgentInstances.value(identifier)->resourceInterface()->synchronizeTags();
}

void AgentManager::agentInstanceSynchronizeRelations(const QString &identifier)
{
    if (!checkResourceInterface(identifier, QStringLiteral("agentInstanceSynchronizeRelations"))) {
        return;
    }

    mAgentInstances.value(identifier)->resourceInterface()->synchronizeRelations();
}

void AgentManager::restartAgentInstance(const QString &identifier)
{
    if (!checkInstance(identifier)) {
        return;
    }

    mAgentInstances.value(identifier)->restartWhenIdle();
}

void AgentManager::updatePluginInfos()
{
    const QHash<QString, AgentType> oldInfos = mAgents;
    readPluginInfos();

    for (const AgentType &oldInfo : oldInfos) {
        if (!mAgents.contains(oldInfo.identifier)) {
            Q_EMIT agentTypeRemoved(oldInfo.identifier);
        }
    }

    for (const AgentType &newInfo : qAsConst(mAgents)) {
        if (!oldInfos.contains(newInfo.identifier)) {
            Q_EMIT agentTypeAdded(newInfo.identifier);
            ensureAutoStart(newInfo);
        }
    }
}

void AgentManager::readPluginInfos()
{
    mAgents.clear();

    const QStringList pathList = pluginInfoPathList();

    for (const QString &path : pathList) {
        const QDir directory(path, QStringLiteral("*.desktop"));
        readPluginInfos(directory);
    }
}

void AgentManager::readPluginInfos(const QDir &directory)
{
    const QStringList files = directory.entryList();
    qCDebug(AKONADICONTROL_LOG) << "PLUGINS: " << directory.canonicalPath();
    qCDebug(AKONADICONTROL_LOG) << "PLUGINS: " << files;

    for (int i = 0; i < files.count(); ++i) {
        const QString fileName = directory.absoluteFilePath(files[i]);

        AgentType agentInfo;
        if (agentInfo.load(fileName, this)) {
            if (mAgents.contains(agentInfo.identifier)) {
                qCWarning(AKONADICONTROL_LOG) << "Duplicated agent identifier" << agentInfo.identifier << "from file" << fileName;
                continue;
            }

            const QString disableAutostart = akGetEnv("AKONADI_DISABLE_AGENT_AUTOSTART");
            if (!disableAutostart.isEmpty()) {
                qCDebug(AKONADICONTROL_LOG) << "Autostarting of agents is disabled.";
                agentInfo.capabilities.removeOne(AgentType::CapabilityAutostart);
            }

            if (!mAgentServerEnabled && agentInfo.launchMethod == AgentType::Server) {
                agentInfo.launchMethod = AgentType::Launcher;
            }

            if (agentInfo.launchMethod == AgentType::Process) {
                const QString executable = Akonadi::StandardDirs::findExecutable(agentInfo.exec);
                if (executable.isEmpty()) {
                    qCWarning(AKONADICONTROL_LOG) << "Executable" << agentInfo.exec << "for agent" << agentInfo.identifier << "could not be found!";
                    continue;
                }
            }

            qCDebug(AKONADICONTROL_LOG) << "PLUGINS inserting: " << agentInfo.identifier << agentInfo.instanceCounter << agentInfo.capabilities;
            mAgents.insert(agentInfo.identifier, agentInfo);
        }
    }
}

QStringList AgentManager::pluginInfoPathList()
{
    return Akonadi::StandardDirs::locateAllResourceDirs(QStringLiteral("akonadi/agents"));
}

void AgentManager::load()
{
    org::freedesktop::Akonadi::ResourceManager resmanager(Akonadi::DBus::serviceName(Akonadi::DBus::Server), QStringLiteral("/ResourceManager"), QDBusConnection::sessionBus(), this);
    const QStringList knownResources = resmanager.resourceInstances();

    QSettings file(Akonadi::StandardDirs::agentsConfigFile(Akonadi::StandardDirs::ReadOnly), QSettings::IniFormat);
    file.beginGroup(QStringLiteral("Instances"));
    const QStringList entries = file.childGroups();
    for (int i = 0; i < entries.count(); ++i) {
        const QString instanceIdentifier = entries[i];

        if (mAgentInstances.contains(instanceIdentifier)) {
            qCWarning(AKONADICONTROL_LOG) << "Duplicated instance identifier" << instanceIdentifier << "found in agentsrc";
            continue;
        }

        file.beginGroup(entries[i]);

        const QString agentType = file.value(QStringLiteral("AgentType")).toString();
        if (!mAgents.contains(agentType)) {
            qCWarning(AKONADICONTROL_LOG) << "Reference to unknown agent type" << agentType << "in agentsrc";
            file.endGroup();
            continue;
        }
        const AgentType type = mAgents.value(agentType);

        // recover if the db has been deleted in the meantime or got otherwise corrupted
        if (!knownResources.contains(instanceIdentifier) && type.capabilities.contains(AgentType::CapabilityResource)) {
            qCDebug(AKONADICONTROL_LOG) << "Recovering instance" << instanceIdentifier << "after database loss";
            registerAgentAtServer(instanceIdentifier, type);
        }

        const AgentInstance::Ptr instance = createAgentInstance(type);
        instance->setIdentifier(instanceIdentifier);
        if (instance->start(type)) {
            mAgentInstances.insert(instanceIdentifier, instance);
        }

        file.endGroup();
    }

    file.endGroup();
}

void AgentManager::save()
{
    QSettings file(Akonadi::StandardDirs::agentsConfigFile(Akonadi::StandardDirs::WriteOnly), QSettings::IniFormat);

    for (const AgentType &info : qAsConst(mAgents)) {
        info.save(&file);
    }

    file.beginGroup(QStringLiteral("Instances"));
    file.remove(QString());
    for (const AgentInstance::Ptr &instance : qAsConst(mAgentInstances)) {
        file.beginGroup(instance->identifier());
        file.setValue(QStringLiteral("AgentType"), instance->agentType());
        file.endGroup();
    }

    file.endGroup();
}

void AgentManager::serviceOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(oldOwner);
    // This is called by the D-Bus server when a service comes up, goes down or changes ownership for some reason
    // and this is where we "hook up" our different Agent interfaces.

    //qCDebug(AKONADICONTROL_LOG) << "Service " << name << " owner changed from " << oldOwner << " to " << newOwner;

    if ((name == Akonadi::DBus::serviceName(Akonadi::DBus::Server) || name == Akonadi::DBus::serviceName(Akonadi::DBus::AgentServer)) && !newOwner.isEmpty()) {
        if (QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Server))
                && (!mAgentServer || QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::AgentServer)))) {
            // server is operational, start agents
            continueStartup();
        }
    }

    const auto service = Akonadi::DBus::parseAgentServiceName(name);
    if (!service.has_value()) {
        return;
    }
    switch (service->agentType) {
    case Akonadi::DBus::Agent: {
        // An agent service went up or down
        if (newOwner.isEmpty()) {
            return; // It went down: we don't care here.
        }

        if (!mAgentInstances.contains(service->serviceName)) {
            return;
        }

        const AgentInstance::Ptr instance = mAgentInstances.value(service->serviceName);
        const bool restarting = instance->hasAgentInterface();
        if (!instance->obtainAgentInterface()) {
            return;
        }

        if (!restarting) {
            Q_EMIT agentInstanceAdded(service->serviceName);
        }

        break;
    }
    case Akonadi::DBus::Resource: {
        // A resource service went up or down
        if (newOwner.isEmpty()) {
            return; // It went down: we don't care here.
        }

        if (!mAgentInstances.contains(service->serviceName)) {
            return;
        }

        mAgentInstances.value(service->serviceName)->obtainResourceInterface();

        break;
    }
    case Akonadi::DBus::Preprocessor: {
        // A preprocessor service went up or down

        // If the preprocessor is going up then the org.freedesktop.Akonadi.Agent.* interface
        // should be already up (as it's registered before the preprocessor one).
        // So if we don't know about the preprocessor as agent instance
        // then it's not our preprocessor.

        // If the preprocessor is going down then either the agent interface already
        // went down (and it has been already unregistered on the manager side)
        // or it's still registered as agent and WE have to unregister it.
        // The order of interface deletions depends on Qt but we handle both cases.

        // Check if we "know" about it.
        qCDebug(AKONADICONTROL_LOG) << "Preprocessor " << service->serviceName << " is going up or down...";

        if (!mAgentInstances.contains(service->serviceName)) {
            qCDebug(AKONADICONTROL_LOG) << "But it isn't registered as agent... not mine (anymore?)";
            return; // not our agent (?)
        }

        org::freedesktop::Akonadi::PreprocessorManager preProcessorManager(
            Akonadi::DBus::serviceName(Akonadi::DBus::Server),
            QStringLiteral("/PreprocessorManager"),
            QDBusConnection::sessionBus(),
            this);

        if (!preProcessorManager.isValid()) {
            qCWarning(AKONADICONTROL_LOG) << "Could not connect to PreprocessorManager via D-Bus:" << preProcessorManager.lastError().message();
        } else {
            if (newOwner.isEmpty()) {
                // The preprocessor went down. Unregister it on server side.

                preProcessorManager.unregisterInstance(service->serviceName);

            } else {

                // The preprocessor went up. Register it on server side.

                if (!mAgentInstances.value(service->serviceName)->obtainPreprocessorInterface()) {
                    // Hm.. couldn't hook up its preprocessor interface..
                    // Make sure we don't have it in the preprocessor chain
                    qCWarning(AKONADICONTROL_LOG) << "Couldn't obtain preprocessor interface for instance" << service->serviceName;

                    preProcessorManager.unregisterInstance(service->serviceName);
                    return;
                }

                qCDebug(AKONADICONTROL_LOG) << "Registering preprocessor instance" << service->serviceName;

                // Add to the preprocessor chain
                preProcessorManager.registerInstance(service->serviceName);
            }
        }

        break;
    }
    default:
        break;
    }
}

bool AgentManager::checkInstance(const QString &identifier) const
{
    if (!mAgentInstances.contains(identifier)) {
        qCWarning(AKONADICONTROL_LOG) << "Agent instance with identifier " << identifier << " does not exist";
        return false;
    }

    return true;
}

bool AgentManager::checkResourceInterface(const QString &identifier, const QString &method) const
{
    if (!checkInstance(identifier)) {
        return false;
    }

    if (!mAgents[mAgentInstances[identifier]->agentType()].capabilities.contains(QStringLiteral("Resource"))) {
        return false;
    }

    if (!mAgentInstances[identifier]->hasResourceInterface()) {
        qCWarning(AKONADICONTROL_LOG) << QLatin1String("AgentManager::") + method << " Agent instance "
                                      << identifier << " has no resource interface!";
        return false;
    }

    return true;
}

bool AgentManager::checkAgentExists(const QString &identifier) const
{
    if (!mAgents.contains(identifier)) {
        qCWarning(AKONADICONTROL_LOG) << "Agent instance " << identifier << " does not exist.";
        return false;
    }

    return true;
}

bool AgentManager::checkAgentInterfaces(const QString &identifier, const QString &method) const
{
    if (!checkInstance(identifier)) {
        return false;
    }

    if (!mAgentInstances.value(identifier)->hasAgentInterface()) {
        qCWarning(AKONADICONTROL_LOG) << "Agent instance (" << method << ") " << identifier << " has no agent interface.";
        return false;
    }

    return true;
}

void AgentManager::ensureAutoStart(const AgentType &info)
{
    if (!info.capabilities.contains(AgentType::CapabilityAutostart)) {
        return; // no an autostart agent
    }

    org::freedesktop::Akonadi::AgentServer agentServer(Akonadi::DBus::serviceName(Akonadi::DBus::AgentServer),
            QStringLiteral("/AgentServer"), QDBusConnection::sessionBus(), this);

    if (mAgentInstances.contains(info.identifier) ||
            (agentServer.isValid() && agentServer.started(info.identifier))) {
        return; // already running
    }

    const AgentInstance::Ptr instance = createAgentInstance(info);
    instance->setIdentifier(info.identifier);
    if (instance->start(info)) {
        mAgentInstances.insert(instance->identifier(), instance);
        registerAgentAtServer(instance->identifier(), info);
        save();
    }
}

void AgentManager::agentExeChanged(const QString &fileName)
{
    if (!QFile::exists(fileName)) {
        return;
    }

    for (const AgentType &type : qAsConst(mAgents)) {
        if (fileName.endsWith(type.exec)) {
            for (const AgentInstance::Ptr &instance : qAsConst(mAgentInstances)) {
                if (instance->agentType() == type.identifier) {
                    instance->restartWhenIdle();
                }
            }
        }
    }
}

void AgentManager::registerAgentAtServer(const QString &agentIdentifier, const AgentType &type)
{
    if (type.capabilities.contains(AgentType::CapabilityResource)) {
        QScopedPointer<org::freedesktop::Akonadi::ResourceManager> resmanager(
            new org::freedesktop::Akonadi::ResourceManager(Akonadi::DBus::serviceName(Akonadi::DBus::Server),
                    QStringLiteral("/ResourceManager"),
                    QDBusConnection::sessionBus(), this));
        resmanager->addResourceInstance(agentIdentifier, type.capabilities);
    }
}

void AgentManager::addSearch(const QString &query, const QString &queryLanguage, qint64 resultCollectionId)
{
    qCDebug(AKONADICONTROL_LOG) << "AgentManager::addSearch" << query << queryLanguage << resultCollectionId;
    for (const AgentInstance::Ptr &instance : qAsConst(mAgentInstances)) {
        const AgentType type = mAgents.value(instance->agentType());
        if (type.capabilities.contains(AgentType::CapabilitySearch) && instance->searchInterface()) {
            instance->searchInterface()->addSearch(query, queryLanguage, resultCollectionId);
        }
    }
}

void AgentManager::removeSearch(quint64 resultCollectionId)
{
    qCDebug(AKONADICONTROL_LOG) << "AgentManager::removeSearch" << resultCollectionId;
    for (const AgentInstance::Ptr &instance : qAsConst(mAgentInstances)) {
        const AgentType type = mAgents.value(instance->agentType());
        if (type.capabilities.contains(AgentType::CapabilitySearch) && instance->searchInterface()) {
            instance->searchInterface()->removeSearch(resultCollectionId);
        }
    }
}

void AgentManager::agentServerFailure()
{
    qCCritical(AKONADICONTROL_LOG) << "Failed to start AgentServer!";
    // if ( requiresAgentServer )
    //   QCoreApplication::instance()->exit( 255 );
}

void AgentManager::serverFailure()
{
    QCoreApplication::instance()->exit(255);
}
