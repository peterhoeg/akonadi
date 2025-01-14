/***************************************************************************
 *   Copyright (C) 2007 by Tobias Koenig <tokoe@kde.org>                   *
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

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QSettings>
#include <QDBusConnection>
#include <QPluginLoader>

#include <KAboutData>

#include <shared/akapplication.h>

#include "controlmanagerinterface.h"
#include "janitorinterface.h"
#include "akonadistarter.h"
#include "akonadi_version.h"

#include <private/protocol_p.h>
#include <private/dbus_p.h>
#include <private/instance_p.h>
#include <private/standarddirs_p.h>

#include <iostream>
#include <thread>
#include <chrono>

static bool startServer(bool verbose)
{
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Control))
            || QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Server))) {
        std::cerr << "Akonadi is already running." << std::endl;
        return false;
    }
    AkonadiStarter starter;
    return starter.start(verbose);
}

static bool stopServer()
{
    org::freedesktop::Akonadi::ControlManager iface(Akonadi::DBus::serviceName(Akonadi::DBus::Control),
            QStringLiteral("/ControlManager"),
            QDBusConnection::sessionBus(), nullptr);
    if (!iface.isValid()) {
        std::cerr << "Akonadi is not running." << std::endl;
        return false;
    }

    iface.shutdown();

    return true;
}

static bool isAkonadiServerRunning()
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Server));
}

static bool checkAkonadiControlStatus()
{
    const bool registered = QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Control));
    std::cerr << "Akonadi Control: " << (registered ? "running" : "stopped") << std::endl;
    return registered;
}

static bool checkAkonadiServerStatus()
{
    const bool registered = isAkonadiServerRunning();
    std::cerr << "Akonadi Server: " << (registered ? "running" : "stopped") << std::endl;
    return registered;
}

static bool checkSearchSupportStatus()
{
    QStringList searchMethods {QStringLiteral("Remote Search")};

    const QString pluginOverride = QString::fromLatin1(qgetenv("AKONADI_OVERRIDE_SEARCHPLUGIN"));
    if (!pluginOverride.isEmpty()) {
        searchMethods << pluginOverride;
    } else {
        const QStringList dirs = QCoreApplication::libraryPaths();
        for (const QString &pluginDir : dirs) {
            QDir dir(pluginDir + QLatin1String("/akonadi/"));
            const QStringList pluginFiles = dir.entryList(QDir::Files);
            for (const QString &pluginFileName : pluginFiles) {
                QPluginLoader loader(dir.absolutePath() + QLatin1Char('/') + pluginFileName);
                const QVariantMap metadata = loader.metaData().value(QStringLiteral("MetaData")).toVariant().toMap();
                if (metadata.value(QStringLiteral("X-Akonadi-PluginType")).toString() != QLatin1String("SearchPlugin")) {
                    continue;
                }
                if (!metadata.value(QStringLiteral("X-Akonadi-LoadByDefault"), true).toBool()) {
                    continue;
                }
                searchMethods << metadata.value(QStringLiteral("X-Akonadi-PluginName")).toString();
            }
        }
    }

    // There's always at least server-search available
    std::cerr << "Akonadi Server Search Support: available (" << searchMethods.join(QStringLiteral(", ")).toStdString() << ")" << std::endl;
    return true;
}

static bool checkAvailableAgentTypes()
{
    const auto dirs = Akonadi::StandardDirs::locateAllResourceDirs(QStringLiteral("akonadi/agents"));
    QStringList types;
    for (const QString &pluginDir : dirs) {
        QDir dir(pluginDir);
        const QStringList plugins = dir.entryList(QStringList() << QStringLiteral("*.desktop"), QDir::Files);
        for (const QString &plugin : plugins) {
            QSettings pluginInfo(pluginDir + QLatin1Char('/') + plugin, QSettings::IniFormat);
            pluginInfo.beginGroup(QStringLiteral("Desktop Entry"));
            types << pluginInfo.value(QStringLiteral("X-Akonadi-Identifier")).toString();
        }
    }

    // Remove duplicates from multiple pluginDirs
    types.removeDuplicates();
    types.sort();

    std::cerr << "Available Agent Types: ";
    if (types.isEmpty()) {
        std::cerr << "No agent types found!" << std::endl;
    } else {
        std::cerr << types.join(QStringLiteral(", ")).toStdString() << std::endl;
    }

    return true;
}

static bool instanceRunning(const QString &instanceName = {})
{
    const auto oldInstance = Akonadi::Instance::identifier();
    Akonadi::Instance::setIdentifier(instanceName);
    const auto service = Akonadi::DBus::serviceName(Akonadi::DBus::Control);
    Akonadi::Instance::setIdentifier(oldInstance);

    return QDBusConnection::sessionBus().interface()->isServiceRegistered(service);
}

static void listInstances()
{
    struct Instance {
        QString name;
        bool running;
    };
    QVector<Instance> instances { { QStringLiteral("(default)"), instanceRunning() } };
#ifdef Q_OS_WIN
    const QDir instanceDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/akonadi/config/instance"));
#else
    const QDir instanceDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/akonadi/instance"));
#endif
    if (instanceDir.exists()) {
        const auto list = instanceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &e : list) {
            instances.push_back({ e, instanceRunning(e) });
        }
    }

    for (const auto &i : qAsConst(instances)) {
        std::cout << i.name.toStdString();
        if (i.running) {
            std::cout << " (running)";
        }
        std::cout << std::endl;
    }
}

static bool statusServer()
{
    checkAkonadiControlStatus();
    checkAkonadiServerStatus();
    checkSearchSupportStatus();
    checkAvailableAgentTypes();
    return true;
}

static void runJanitor(const QString &operation)
{
    if (!isAkonadiServerRunning()) {
        std::cerr << "Akonadi Server is not running, " << operation.toStdString() << " will not run" << std::endl;
        return;
    }

    org::freedesktop::Akonadi::Janitor janitor(Akonadi::DBus::serviceName(Akonadi::DBus::StorageJanitor),
            QStringLiteral(AKONADI_DBUS_STORAGEJANITOR_PATH),
            QDBusConnection::sessionBus());
    QObject::connect(&janitor, &org::freedesktop::Akonadi::Janitor::information,
    [](const QString & msg) {
        std::cerr << msg.toStdString() << std::endl;
    });
    QObject::connect(&janitor, &org::freedesktop::Akonadi::Janitor::done,
    []() {
        qApp->exit();
    });
    janitor.asyncCall(operation);
    qApp->exec();
}

int main(int argc, char **argv)
{
    AkCoreApplication app(argc, argv);

    app.setDescription(QStringLiteral("Akonadi server manipulation tool\n\n"
                                      "Commands:\n"
                                      "  start          Starts the Akonadi server with all its processes\n"
                                      "  stop           Stops the Akonadi server and all its processes cleanly\n"
                                      "  restart        Restart Akonadi server with all its processes\n"
                                      "  status         Shows a status overview of the Akonadi server\n"
                                      "  instances      List all existing Akonadi instances\n"
                                      "  vacuum         Vacuum internal storage (WARNING: needs a lot of time and disk\n"
                                      "                 space!)\n"
                                      "  fsck           Check (and attempt to fix) consistency of the internal storage\n"
                                      "                 (can take some time)"));

    KAboutData aboutData(QStringLiteral("akonadictl"),
                         QStringLiteral("akonadictl"),
                         QStringLiteral(AKONADI_VERSION_STRING),
                         QStringLiteral("akonadictl"),
                         KAboutLicense::LGPL_V2);
    KAboutData::setApplicationData(aboutData);

    app.addPositionalCommandLineOption(QStringLiteral("command"), QStringLiteral("Command to execute"),
                                       QStringLiteral("start|stop|restart|status|vacuum|fsck|instances"));

    app.parseCommandLine();

    const auto &cmdArgs = app.commandLineArguments();
    const QStringList commands = cmdArgs.positionalArguments();
    if (commands.size() != 1) {
        app.printUsage();
        return -1;
    }
    const bool verbose = cmdArgs.isSet(QStringLiteral("verbose"));

    const QString command = commands[0];
    if (command == QLatin1String("start")) {
        if (!startServer(verbose)) {
            return 3;
        }
    } else if (command == QLatin1String("stop")) {
        if (!stopServer()) {
            return 4;
        }
    } else if (command == QLatin1String("status")) {
        if (!statusServer()) {
            return 5;
        }
    } else if (command == QLatin1String("restart")) {
        if (!stopServer()) {
            return 4;
        } else {
            do {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (QDBusConnection::sessionBus().interface()->isServiceRegistered(Akonadi::DBus::serviceName(Akonadi::DBus::Control)));
            if (!startServer(verbose)) {
                return 3;
            }
        }
    } else if (command == QLatin1String("vacuum")) {
        runJanitor(QStringLiteral("vacuum"));
    } else if (command == QLatin1String("fsck")) {
        runJanitor(QStringLiteral("check"));
    } else if (command == QLatin1String("instances")) {
        listInstances();
    } else {
        app.printUsage();
        return -1;
    }
    return 0;
}
