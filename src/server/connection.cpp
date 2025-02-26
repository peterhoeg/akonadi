/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
 *   Copyright (C) 2013 by Volker Krause <vkrause@kde.org>                 *
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
#include "connection.h"
#include "akonadiserver_debug.h"


#include <QSettings>
#include <QEventLoop>
#include <QThreadStorage>

#include "storage/datastore.h"
#include "storage/dbdeadlockcatcher.h"
#include "handler.h"
#include "notificationmanager.h"

#include "tracer.h"

#include <cassert>

#ifndef Q_OS_WIN
#include <cxxabi.h>
#endif


#include <private/protocol_p.h>
#include <private/datastream_p_p.h>
#include <private/standarddirs_p.h>

using namespace Akonadi;
using namespace Akonadi::Server;

#define IDLE_TIMER_TIMEOUT 180000 // 3 min

static QString connectionIdentifier(Connection *c) {
    QString id;
    id.sprintf("%p", static_cast<void *>(c));
    return id;
}

Connection::Connection(QObject *parent)
    : AkThread(connectionIdentifier(this), QThread::InheritPriority, parent)
{
}

Connection::Connection(quintptr socketDescriptor, QObject *parent)
    : AkThread(connectionIdentifier(this), QThread::InheritPriority, parent)
{
    m_socketDescriptor = socketDescriptor;
    m_identifier = connectionIdentifier(this); // same as objectName()

    const QSettings settings(Akonadi::StandardDirs::serverConfigFile(), QSettings::IniFormat);
    m_verifyCacheOnRetrieval = settings.value(QStringLiteral("Cache/VerifyOnRetrieval"), m_verifyCacheOnRetrieval).toBool();
}

void Connection::init()
{
    AkThread::init();

    QLocalSocket *socket = new QLocalSocket();

    if (!socket->setSocketDescriptor(m_socketDescriptor)) {
        qCWarning(AKONADISERVER_LOG) << "Connection(" << m_identifier
                                     << ")::run: failed to set socket descriptor: "
                                     << socket->error() << "(" << socket->errorString() << ")";
        delete socket;
        return;
    }

    m_socket = socket;
    connect(socket, &QLocalSocket::disconnected,
            this, &Connection::slotSocketDisconnected);

    m_idleTimer = new QTimer(this);
    connect(m_idleTimer, &QTimer::timeout,
            this, &Connection::slotConnectionIdle);

    storageBackend()->notificationCollector()->setConnection(this);

    if (socket->state() == QLocalSocket::ConnectedState) {
        QTimer::singleShot(0, this, &Connection::handleIncomingData);
    } else {
        connect(socket, &QLocalSocket::connected, this, &Connection::handleIncomingData,
                Qt::QueuedConnection);
    }

    try {
        slotSendHello();
    } catch (const ProtocolException &e) {
        qCWarning(AKONADISERVER_LOG) << "Protocol Exception sending \"hello\" on connection" << m_identifier << ":" << e.what();
        m_socket->disconnectFromServer();
    }
}

void Connection::quit()
{
    if (QThread::currentThread()->loopLevel() > 1) {
        m_connectionClosing = true;
        Q_EMIT connectionClosing();
        return;
    }

    Tracer::self()->endConnection(m_identifier, QString());

    delete m_socket;
    m_socket = nullptr;

    if (m_idleTimer) {
        m_idleTimer->stop();
    }
    delete m_idleTimer;

    AkThread::quit();
}

void Connection::slotSendHello()
{
    SchemaVersion version = SchemaVersion::retrieveAll().at(0);

    Protocol::HelloResponse hello;
    hello.setServerName(QStringLiteral("Akonadi"));
    hello.setMessage(QStringLiteral("Not Really IMAP server"));
    hello.setProtocolVersion(Protocol::version());
    hello.setGeneration(version.generation());
    sendResponse(0, std::move(hello));
}

DataStore *Connection::storageBackend()
{
    if (!m_backend) {
        m_backend = DataStore::self();
    }
    return m_backend;
}

Connection::~Connection()
{
    quitThread();

    if (m_reportTime) {
        reportTime();
    }
}

void Connection::slotConnectionIdle()
{
    Q_ASSERT(m_currentHandler == nullptr);
    if (m_backend && m_backend->isOpened()) {
        if (m_backend->inTransaction()) {
            // This is a programming error, the timer should not have fired.
            // But it is safer to abort and leave the connection open, until
            // a later operation causes the idle timer to fire (than crash
            // the akonadi server).
            qCInfo(AKONADISERVER_LOG) << m_sessionId << "NOT Closing idle db connection; we are in transaction";
            return;
        }
        m_backend->close();
    }
}

void Connection::slotSocketDisconnected()
{
    // If we have active handler, wait for it to finish, then we emit the signal
    // from slotNewDate()
    if (m_currentHandler) {
        return;
    }

    Q_EMIT disconnected();
}

void Connection::parseStream(const Protocol::CommandPtr &cmd)
{
    if (!m_currentHandler->parseStream()) {
        try {
            m_currentHandler->failureResponse("Error while handling a command");
        } catch (...) {
            m_connectionClosing = true;
        }
        qCWarning(AKONADISERVER_LOG) << "Error while handling command" << cmd->type()
                                     << "on connection" << m_identifier;
    }
}

void Connection::handleIncomingData()
{
    Q_FOREVER {

        if (m_connectionClosing || !m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
            break;
        }

        // Blocks with event loop until some data arrive, allows us to still use QTimers
        // and similar while waiting for some data to arrive
        if (m_socket->bytesAvailable() < int(sizeof(qint64))) {
            QEventLoop loop;
            connect(m_socket, &QLocalSocket::readyRead, &loop, &QEventLoop::quit);
            connect(m_socket, &QLocalSocket::stateChanged, &loop, &QEventLoop::quit);
            connect(this, &Connection::connectionClosing, &loop, &QEventLoop::quit);
            loop.exec();
        }

        if (m_connectionClosing || !m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
            break;
        }

        m_idleTimer->stop();

        // will only open() a previously idle backend.
        // Otherwise, a new backend could lazily be constructed by later calls.
        if (!storageBackend()->isOpened()) {
            m_backend->open();
        }

        QString currentCommand;
        while (m_socket->bytesAvailable() >= int(sizeof(qint64))) {
            Protocol::DataStream stream(m_socket);
            qint64 tag = -1;
            stream >> tag;
            // TODO: Check tag is incremental sequence

            Protocol::CommandPtr cmd;
            try {
                cmd = Protocol::deserialize(m_socket);
            } catch (const Akonadi::ProtocolException &e) {
                qCWarning(AKONADISERVER_LOG) << "ProtocolException while deserializing incoming data on connection"
                                             << m_identifier << ":" <<  e.what();
                setState(Server::LoggingOut);
                return;
            } catch (const std::exception &e) {
                qCWarning(AKONADISERVER_LOG) << "Unknown exception while deserializing incoming data on connection"
                                             << m_identifier << ":" << e.what();
                setState(Server::LoggingOut);
                return;
            }
            if (cmd->type() == Protocol::Command::Invalid) {
                qCWarning(AKONADISERVER_LOG) << "Received an invalid command on connection" << m_identifier
                                             << ": resetting connection";
                setState(Server::LoggingOut);
                return;
            }

            // Tag context and collection context is not persistent.
            context()->setTag(-1);
            context()->setCollection(Collection());
            if (Tracer::self()->currentTracer() != QLatin1String("null")) {
                Tracer::self()->connectionInput(m_identifier, tag, cmd);
            }

            m_currentHandler = findHandlerForCommand(cmd->type());
            if (!m_currentHandler) {
                qCWarning(AKONADISERVER_LOG) << "Invalid command: no such handler for" << cmd->type()
                                             << "on connection" << m_identifier;
                setState(Server::LoggingOut);
                return;
            }
            if (m_reportTime) {
                startTime();
            }

            m_currentHandler->setConnection(this);
            m_currentHandler->setTag(tag);
            m_currentHandler->setCommand(cmd);
            try {
                DbDeadlockCatcher catcher([this, &cmd]() { parseStream(cmd); });
            } catch (const Akonadi::Server::HandlerException &e) {
                if (m_currentHandler) {
                    try {
                        m_currentHandler->failureResponse(e.what());
                    } catch (...) {
                        m_connectionClosing = true;
                    }
                    qCWarning(AKONADISERVER_LOG) << "Handler exception when handling command" << cmd->type()
                                                 << "on connection" << m_identifier << ":" << e.what();
                }
            } catch (const Akonadi::Server::Exception &e) {
                if (m_currentHandler) {
                    try {
                        m_currentHandler->failureResponse(QString::fromUtf8(e.type()) + QLatin1String(": ") + QString::fromUtf8(e.what()));
                    } catch (...) {
                        m_connectionClosing = true;
                    }
                    qCWarning(AKONADISERVER_LOG) << "General exception when handling command" << cmd->type()
                                                 << "on connection" << m_identifier << ":" << e.what();
                }
            } catch (const Akonadi::ProtocolException &e) {
                // No point trying to send anything back to client, the connection is
                // already messed up
                qCWarning(AKONADISERVER_LOG) << "Protocol exception when handling command" << cmd->type()
                                             << "on connection" << m_identifier << ":" << e.what();
                m_connectionClosing = true;
#if defined(Q_OS_LINUX) && !defined(_LIBCPP_VERSION)
            } catch (abi::__forced_unwind&) {
                // HACK: NPTL throws __forced_unwind during thread cancellation and
                // we *must* rethrow it otherwise the program aborts. Due to the issue
                // described in #376385 we might end up destroying (cancelling) the
                // thread from a nested loop executed inside parseStream() above,
                // so the exception raised in there gets caught by this try..catch
                // statement and it must be rethrown at all cost. Remove this hack
                // once the root problem is fixed.
                throw;
#endif
            } catch (...) {
                qCCritical(AKONADISERVER_LOG) << "Unknown exception while handling command" << cmd->type()
                                              << "on connection" << m_identifier;
                if (m_currentHandler) {
                    try {
                        m_currentHandler->failureResponse("Unknown exception caught");
                    } catch (...) {
                        m_connectionClosing = true;
                    }
                }
            }
            if (m_reportTime) {
                stopTime(currentCommand);
            }
            m_currentHandler.reset();

            if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
                Q_EMIT disconnected();
                return;
            }

            if (m_connectionClosing) {
                break;
            }
        }

        // reset, arm the timer
        m_idleTimer->start(IDLE_TIMER_TIMEOUT);

        if (m_connectionClosing) {
            break;
        }
    }

    if (m_connectionClosing) {
        m_socket->disconnect(this);
        m_socket->close();
        QTimer::singleShot(0, this, &Connection::quit);
    }
}

CommandContext *Connection::context() const
{
    return const_cast<CommandContext *>(&m_context);
}

std::unique_ptr<Handler> Connection::findHandlerForCommand(Protocol::Command::Type command)
{
    auto handler = Handler::findHandlerForCommandAlwaysAllowed(command);
    if (handler) {
        return handler;
    }

    switch (m_connectionState) {
    case NonAuthenticated:
        handler =  Handler::findHandlerForCommandNonAuthenticated(command);
        break;
    case Authenticated:
        handler =  Handler::findHandlerForCommandAuthenticated(command);
        break;
    case Selected:
        break;
    case LoggingOut:
        break;
    }

    return handler;
}

qint64 Connection::currentTag() const
{
    return m_currentHandler->tag();
}

void Connection::setState(ConnectionState state)
{
    if (state == m_connectionState) {
        return;
    }
    m_connectionState = state;
    switch (m_connectionState) {
    case NonAuthenticated:
        assert(0);   // can't happen, it's only the initial state, we can't go back to it
        break;
    case Authenticated:
        break;
    case Selected:
        break;
    case LoggingOut:
        m_socket->disconnectFromServer();
        break;
    }
}

void Connection::setSessionId(const QByteArray &id)
{
    m_identifier.sprintf("%s (%p)", id.data(), static_cast<void *>(this));
    Tracer::self()->beginConnection(m_identifier, QString());
    //m_streamParser->setTracerIdentifier(m_identifier);

    m_sessionId = id;
    setObjectName(QString::fromLatin1(id));
    // this races with the use of objectName() in QThreadPrivate::start
    //thread()->setObjectName(objectName() + QStringLiteral("-Thread"));
    storageBackend()->setSessionId(id);
}

QByteArray Connection::sessionId() const
{
    return m_sessionId;
}

bool Connection::isOwnerResource(const PimItem &item) const
{
    if (context()->resource().isValid() && item.collection().resourceId() == context()->resource().id()) {
        return true;
    }
    // fallback for older resources
    if (sessionId() == item.collection().resource().name().toUtf8()) {
        return true;
    }
    return false;
}

bool Connection::isOwnerResource(const Collection &collection) const
{
    if (context()->resource().isValid() && collection.resourceId() == context()->resource().id()) {
        return true;
    }
    if (sessionId() == collection.resource().name().toUtf8()) {
        return true;
    }
    return false;
}

bool Connection::verifyCacheOnRetrieval() const
{
    return m_verifyCacheOnRetrieval;
}

void Connection::startTime()
{
    m_time.start();
}

void Connection::stopTime(const QString &identifier)
{
    int elapsed = m_time.elapsed();
    m_totalTime += elapsed;
    m_totalTimeByHandler[identifier] += elapsed;
    m_executionsByHandler[identifier]++;
    qCDebug(AKONADISERVER_LOG) << identifier << " time : " << elapsed << " total: " << m_totalTime;
}

void Connection::reportTime() const
{
    qCDebug(AKONADISERVER_LOG) << "===== Time report for " << m_identifier << " =====";
    qCDebug(AKONADISERVER_LOG) << " total: " << m_totalTime;
    for (auto it = m_totalTimeByHandler.cbegin(), end = m_totalTimeByHandler.cend(); it != end; ++it) {
        const QString &handler = it.key();
        qCDebug(AKONADISERVER_LOG) << "handler : " << handler << " time: " << m_totalTimeByHandler.value(handler) << " executions " << m_executionsByHandler.value(handler) << " avg: " << m_totalTimeByHandler.value(handler) / m_executionsByHandler.value(handler);
    }
}

void Connection::sendResponse(qint64 tag, const Protocol::CommandPtr &response)
{
    if (Tracer::self()->currentTracer() != QLatin1String("null")) {
        Tracer::self()->connectionOutput(m_identifier, tag, response);
    }
    Protocol::DataStream stream(m_socket);
    stream << tag;
    Protocol::serialize(m_socket, response);
    if (!m_socket->waitForBytesWritten()) {
        if (m_socket->state() == QLocalSocket::ConnectedState) {
            throw ProtocolException("Server write timeout");
        } else {
            // The client has disconnected before we managed to send our response,
            // which is not an error
        }
    }
}


Protocol::CommandPtr Connection::readCommand()
{
    while (m_socket->bytesAvailable() < (int) sizeof(qint64)) {
        Protocol::DataStream::waitForData(m_socket, 10000); // 10 seconds, just in case client is busy
    }

    Protocol::DataStream stream(m_socket);
    qint64 tag;
    stream >> tag;

    // TODO: compare tag with m_currentHandler->tag() ?
    return Protocol::deserialize(m_socket);
}
