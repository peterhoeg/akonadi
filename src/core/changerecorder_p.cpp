/*
    Copyright (c) 2007 Volker Krause <vkrause@kde.org>

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

#include "changerecorder_p.h"
#include "akonadicore_debug.h"

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QFileInfo>
#include <QtCore/QDataStream>

using namespace Akonadi;

ChangeRecorderPrivate::ChangeRecorderPrivate(ChangeNotificationDependenciesFactory *dependenciesFactory_,
        ChangeRecorder *parent)
    : MonitorPrivate(dependenciesFactory_, parent)
    , settings(0)
    , enableChangeRecording(true)
    , m_lastKnownNotificationsCount(0)
    , m_startOffset(0)
    , m_needFullSave(true)
{
}

int ChangeRecorderPrivate::pipelineSize() const
{
    if (enableChangeRecording) {
        return 0; // we fill the pipeline ourselves when using change recording
    }
    return MonitorPrivate::pipelineSize();
}

void ChangeRecorderPrivate::slotNotify(const Akonadi::Protocol::ChangeNotification &msg)
{
    Q_Q(ChangeRecorder);
    const int oldChanges = pendingNotifications.size();
    // with change recording disabled this will automatically take care of dispatching notification messages and saving
    MonitorPrivate::slotNotify(msg);
    if (enableChangeRecording && pendingNotifications.size() != oldChanges) {
        emit q->changesAdded();
    }
}

// The QSettings object isn't actually used anymore, except for migrating old data
// and it gives us the base of the filename to use. This is all historical.
QString ChangeRecorderPrivate::notificationsFileName() const
{
    return settings->fileName() + QStringLiteral("_changes.dat");
}

void ChangeRecorderPrivate::loadNotifications()
{
    pendingNotifications.clear();
    Q_ASSERT(pipeline.isEmpty());
    pipeline.clear();

    const QString changesFileName = notificationsFileName();

    /**
     * In an older version we recorded changes inside the settings object, however
     * for performance reasons we changed that to store them in a separated file.
     * If this file doesn't exists, it means we run the new version the first time,
     * so we have to read in the legacy list of changes first.
     */
    if (!QFile::exists(changesFileName)) {
        QStringList list;
        settings->beginGroup(QStringLiteral("ChangeRecorder"));
        const int size = settings->beginReadArray(QStringLiteral("change"));

        for (int i = 0; i < size; ++i) {
            settings->setArrayIndex(i);
            Protocol::ChangeNotification msg;
            msg.setSessionId(settings->value(QStringLiteral("sessionId")).toByteArray());
            msg.setType((Protocol::ChangeNotification::Type)settings->value(QStringLiteral("type")).toInt());
            msg.setOperation((Protocol::ChangeNotification::Operation)settings->value(QStringLiteral("op")).toInt());
            msg.addEntity(settings->value(QStringLiteral("uid")).toLongLong(),
                          settings->value(QStringLiteral("rid")).toString(),
                          QString(),
                          settings->value(QStringLiteral("mimeType")).toString());
            msg.setResource(settings->value(QStringLiteral("resource")).toByteArray());
            msg.setParentCollection(settings->value(QStringLiteral("parentCol")).toLongLong());
            msg.setParentDestCollection(settings->value(QStringLiteral("parentDestCol")).toLongLong());
            list = settings->value(QStringLiteral("itemParts")).toStringList();
            QSet<QByteArray> itemParts;
            Q_FOREACH (const QString &entry, list) {
                itemParts.insert(entry.toLatin1());
            }
            msg.setItemParts(itemParts);
            pendingNotifications << msg;
        }

        settings->endArray();

        // save notifications to the new file...
        saveNotifications();

        // ...delete the legacy list...
        settings->remove(QString());
        settings->endGroup();

        // ...and continue as usually
    }

    QFile file(changesFileName);
    if (file.open(QIODevice::ReadOnly)) {
        m_needFullSave = false;
        pendingNotifications = loadFrom(&file, m_needFullSave);
    } else {
        m_needFullSave = true;
    }
    notificationsLoaded();
}

static const quint64 s_currentVersion = Q_UINT64_C(0x000400000000);
static const quint64 s_versionMask    = Q_UINT64_C(0xFFFF00000000);
static const quint64 s_sizeMask       = Q_UINT64_C(0x0000FFFFFFFF);

QQueue<Protocol::ChangeNotification> ChangeRecorderPrivate::loadFrom(QIODevice *device, bool &needsFullSave) const
{
    QDataStream stream(device);
    stream.setVersion(QDataStream::Qt_4_6);

    QByteArray sessionId, resource, destinationResource;
    int type, operation, entityCnt;
    quint64 uid, parentCollection, parentDestCollection;
    QString remoteId, mimeType, remoteRevision;
    QSet<QByteArray> itemParts, addedFlags, removedFlags;
    QSet<qint64> addedTags, removedTags;

    QQueue<Protocol::ChangeNotification> list;

    quint64 sizeAndVersion;
    stream >> sizeAndVersion;

    const quint64 size = sizeAndVersion & s_sizeMask;
    const quint64 version = (sizeAndVersion & s_versionMask) >> 32;

    quint64 startOffset = 0;
    if (version >= 1) {
        stream >> startOffset;
    }

    // If we skip the first N items, then we'll need to rewrite the file on saving.
    // Also, if the file is old, it needs to be rewritten.
    needsFullSave = startOffset > 0 || version == 0;

    for (quint64 i = 0; i < size && !stream.atEnd(); ++i) {
        Protocol::ChangeNotification msg;

        if (version == 1) {
            stream >> sessionId;
            stream >> type;
            stream >> operation;
            stream >> uid;
            stream >> remoteId;
            stream >> resource;
            stream >> parentCollection;
            stream >> parentDestCollection;
            stream >> mimeType;
            stream >> itemParts;

            if (i < startOffset) {
                continue;
            }

            msg.setSessionId(sessionId);
            msg.setType(static_cast<Protocol::ChangeNotification::Type>(type));
            msg.setOperation(static_cast<Protocol::ChangeNotification::Operation>(operation));
            msg.addEntity(uid, remoteId, QString(), mimeType);
            msg.setResource(resource);
            msg.setParentCollection(parentCollection);
            msg.setParentDestCollection(parentDestCollection);
            msg.setItemParts(itemParts);

        } else if (version >= 2) {

            Protocol::ChangeNotification msg;

            stream >> sessionId;
            stream >> type;
            stream >> operation;
            stream >> entityCnt;
            for (int j = 0; j < entityCnt; ++j) {
                stream >> uid;
                stream >> remoteId;
                stream >> remoteRevision;
                stream >> mimeType;
                msg.addEntity(uid, remoteId, remoteRevision, mimeType);
            }
            stream >> resource;
            stream >> destinationResource;
            stream >> parentCollection;
            stream >> parentDestCollection;
            stream >> itemParts;
            stream >> addedFlags;
            stream >> removedFlags;
            if (version >= 3) {
                stream >> addedTags;
                stream >> removedTags;
            }

            if (i < startOffset) {
                continue;
            }

            msg.setSessionId(sessionId);
            msg.setType(static_cast<Protocol::ChangeNotification::Type>(type));
            msg.setOperation(static_cast<Protocol::ChangeNotification::Operation>(operation));
            msg.setResource(resource);
            msg.setDestinationResource(destinationResource);
            msg.setParentCollection(parentCollection);
            msg.setParentDestCollection(parentDestCollection);
            msg.setItemParts(itemParts);
            msg.setAddedFlags(addedFlags);
            msg.setRemovedFlags(removedFlags);
            msg.setAddedTags(addedTags);
            msg.setRemovedTags(removedTags);

            list << msg;
        }
    }

    return list;
}

static QString join(const QSet<QByteArray> &set)
{
    QString string;
    Q_FOREACH (const QByteArray &b, set) {
        string += QString::fromLatin1(b) + QLatin1String(", ");
    }
    return string;
}

static QString join(const QList<qint64> &set)
{
    QString string;
    Q_FOREACH (qint64 b, set) {
        string += QString::number(b) + QLatin1String(", ");
    }
    return string;
}

QString ChangeRecorderPrivate::dumpNotificationListToString() const
{
    if (!settings) {
        return QStringLiteral("No settings set in ChangeRecorder yet.");
    }
    const QString changesFileName = notificationsFileName();
    QFile file(changesFileName);

    if (!file.open(QIODevice::ReadOnly)) {
        return QLatin1String("Error reading ") + changesFileName;
    }

    QString result;
    bool dummy;
    const QQueue<Protocol::ChangeNotification> notifications = loadFrom(&file, dummy);
    Q_FOREACH (const Protocol::ChangeNotification &n, notifications) {
        QString typeString;
        switch (n.type()) {
        case Protocol::ChangeNotification::Collections:
            typeString = QStringLiteral("Collections");
            break;
        case Protocol::ChangeNotification::Items:
            typeString = QStringLiteral("Items");
            break;
        case Protocol::ChangeNotification::Tags:
            typeString = QStringLiteral("Tags");
            break;
        default:
            typeString = QStringLiteral("InvalidType");
            break;
        };

        QString operationString;
        switch (n.operation()) {
        case Protocol::ChangeNotification::Add:
            operationString = QStringLiteral("Add");
            break;
        case Protocol::ChangeNotification::Modify:
            operationString = QStringLiteral("Modify");
            break;
        case Protocol::ChangeNotification::ModifyFlags:
            operationString = QStringLiteral("ModifyFlags");
            break;
        case Protocol::ChangeNotification::ModifyTags:
            operationString = QStringLiteral("ModifyTags");
            break;
        case Protocol::ChangeNotification::Move:
            operationString = QStringLiteral("Move");
            break;
        case Protocol::ChangeNotification::Remove:
            operationString = QStringLiteral("Remove");
            break;
        case Protocol::ChangeNotification::Link:
            operationString = QStringLiteral("Link");
            break;
        case Protocol::ChangeNotification::Unlink:
            operationString = QStringLiteral("Unlink");
            break;
        case Protocol::ChangeNotification::Subscribe:
            operationString = QStringLiteral("Subscribe");
            break;
        case Protocol::ChangeNotification::Unsubscribe:
            operationString = QStringLiteral("Unsubscribe");
            break;
        default:
            operationString = QStringLiteral("InvalidOp");
            break;
        };

        const QString entities = join(n.entities().keys());
        const QString addedTags = join(n.addedTags().toList());
        const QString removedTags = join(n.removedTags().toList());

        const QString entry = QStringLiteral("session=%1 type=%2 operation=%3 items=%4 resource=%5 destResource=%6 parentCollection=%7 parentDestCollection=%8 itemParts=%9 addedFlags=%10 removedFlags=%11 addedTags=%12 removedTags=%13")
                              .arg(QString::fromLatin1(n.sessionId()))
                              .arg(typeString)
                              .arg(operationString)
                              .arg(entities)
                              .arg(QString::fromLatin1(n.resource()))
                              .arg(QString::fromLatin1(n.destinationResource()))
                              .arg(n.parentCollection())
                              .arg(n.parentDestCollection())
                              .arg(join(n.itemParts()))
                              .arg(join(n.addedFlags()))
                              .arg(join(n.removedFlags()))
                              .arg(addedTags)
                              .arg(removedTags);
        result += entry + QLatin1Char('\n');
    }
    return result;
}

void ChangeRecorderPrivate::addToStream(QDataStream &stream, const Protocol::ChangeNotification &msg)
{
    // We deliberately don't use Factory::serialize(), because the internal
    // serialization format could change at any point

    stream << msg.sessionId();
    stream << int(msg.type());
    stream << int(msg.operation());
    stream << msg.entities().count();
    Q_FOREACH (const Protocol::ChangeNotification::Entity &entity, msg.entities()) {
        stream << quint64(entity.id);
        stream << entity.remoteId;
        stream << entity.remoteRevision;
        stream << entity.mimeType;
    }
    stream << msg.resource();
    stream << msg.destinationResource();
    stream << quint64(msg.parentCollection());
    stream << quint64(msg.parentDestCollection());
    stream << msg.itemParts();
    stream << msg.addedFlags();
    stream << msg.removedFlags();
    stream << msg.addedTags();
    stream << msg.removedTags();
}

void ChangeRecorderPrivate::writeStartOffset()
{
    if (!settings) {
        return;
    }

    QFile file(notificationsFileName());
    if (!file.open(QIODevice::ReadWrite)) {
        qCWarning(AKONADICORE_LOG) << "Could not update notifications in file" << file.fileName();
        return;
    }

    // Skip "countAndVersion"
    file.seek(8);

    //qCDebug(AKONADICORE_LOG) << "Writing start offset=" << m_startOffset;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_4_6);
    stream << static_cast<quint64>(m_startOffset);

    // Everything else stays unchanged
}

void ChangeRecorderPrivate::saveNotifications()
{
    if (!settings) {
        return;
    }

    QFile file(notificationsFileName());
    QFileInfo info(file);
    if (!QFile::exists(info.absolutePath())) {
        QDir dir;
        dir.mkpath(info.absolutePath());
    }
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(AKONADICORE_LOG) << "Could not save notifications to file" << file.fileName();
        return;
    }
    saveTo(&file);
    m_needFullSave = false;
    m_startOffset = 0;
}

void ChangeRecorderPrivate::saveTo(QIODevice *device)
{
    // Version 0 of this file format was writing a quint64 count, followed by the notifications.
    // Version 1 bundles a version number into that quint64, to be able to detect a version number at load time.

    const quint64 countAndVersion = static_cast<quint64>(pendingNotifications.count()) | s_currentVersion;

    QDataStream stream(device);
    stream.setVersion(QDataStream::Qt_4_6);

    stream << countAndVersion;
    stream << quint64(0); // no start offset

    //qCDebug(AKONADICORE_LOG) << "Saving" << pendingNotifications.count() << "notifications (full save)";

    for (int i = 0; i < pendingNotifications.count(); ++i) {
        const Protocol::ChangeNotification msg = pendingNotifications.at(i);
        addToStream(stream, msg);
    }
}

void ChangeRecorderPrivate::notificationsEnqueued(int count)
{
    // Just to ensure the contract is kept, and these two methods are always properly called.
    if (enableChangeRecording) {
        m_lastKnownNotificationsCount += count;
        if (m_lastKnownNotificationsCount != pendingNotifications.count()) {
            qCWarning(AKONADICORE_LOG) << this << "The number of pending notifications changed without telling us! Expected"
                       << m_lastKnownNotificationsCount << "but got" << pendingNotifications.count()
                       << "Caller just added" << count;
            Q_ASSERT(pendingNotifications.count() == m_lastKnownNotificationsCount);
        }

        saveNotifications();
    }
}

void ChangeRecorderPrivate::dequeueNotification()
{
    if (pendingNotifications.isEmpty()) {
        return;
    }

    pendingNotifications.dequeue();
    if (enableChangeRecording) {

        Q_ASSERT(pendingNotifications.count() == m_lastKnownNotificationsCount - 1);
        --m_lastKnownNotificationsCount;

        if (m_needFullSave || pendingNotifications.isEmpty()) {
            saveNotifications();
        } else {
            ++m_startOffset;
            writeStartOffset();
        }
    }
}

void ChangeRecorderPrivate::notificationsErased()
{
    if (enableChangeRecording) {
        m_lastKnownNotificationsCount = pendingNotifications.count();
        m_needFullSave = true;
        saveNotifications();
    }
}

void ChangeRecorderPrivate::notificationsLoaded()
{
    m_lastKnownNotificationsCount = pendingNotifications.count();
    m_startOffset = 0;
}

bool ChangeRecorderPrivate::emitNotification(const Protocol::ChangeNotification &msg)
{
    const bool someoneWasListening = MonitorPrivate::emitNotification(msg);
    if (!someoneWasListening && enableChangeRecording) {
        //If no signal was emitted (e.g. because no one was connected to it), no one is going to call changeProcessed, so we help ourselves.
        dequeueNotification();
        QMetaObject::invokeMethod(q_ptr, "replayNext", Qt::QueuedConnection);
    }
    return someoneWasListening;
}

