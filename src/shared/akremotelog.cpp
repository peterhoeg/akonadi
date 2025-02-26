/*
    Copyright (c) 2018 Daniel Vrátil <dvratil@kde.org>

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

#include "akremotelog.h"

#include <QString>
#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QDateTime>
#include <QLoggingCategory>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

#include <private/instance_p.h>

#define AKONADICONSOLE_SERVICE "org.kde.akonadiconsole"
#define AKONADICONSOLE_LOGGER_PATH "/logger"
#define AKONADICONSOLE_LOGGER_INTERFACE "org.kde.akonadiconsole.logger"

namespace {

class RemoteLogger : public QObject
{
    Q_OBJECT
public:
    explicit RemoteLogger()
        : mWatcher(akonadiConsoleServiceName(), QDBusConnection::sessionBus(),
                   QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)
    {
        connect(qApp, &QCoreApplication::aboutToQuit,
                this, &RemoteLogger::deleteLater);

        sInstance = this;

        // Don't do remote logging for Akonadi Console because it deadlocks it
        if (QCoreApplication::applicationName() == QLatin1String("akonadiconsole")) {
            return;
        }

        connect(&mWatcher, &QDBusServiceWatcher::serviceRegistered,
                this, &RemoteLogger::serviceRegistered);
        connect(&mWatcher, &QDBusServiceWatcher::serviceUnregistered,
                this, &RemoteLogger::serviceUnregistered);

        mOldHandler = qInstallMessageHandler(dbusLogger);
     }

     ~RemoteLogger()
     {
         sInstance = nullptr;

         QLoggingCategory::installFilter(mOldFilter);
         qInstallMessageHandler(mOldHandler);

         mEnabled = false;
         delete mAkonadiConsoleInterface;
     }

     static RemoteLogger *self()
     {
         return sInstance;
     }

private Q_SLOTS:
    void serviceRegistered(const QString &service)
    {
        delete mAkonadiConsoleInterface;
        mAkonadiConsoleInterface = new QDBusInterface(service,
                                                      QStringLiteral(AKONADICONSOLE_LOGGER_PATH),
                                                      QStringLiteral(AKONADICONSOLE_LOGGER_INTERFACE),
                                                      QDBusConnection::sessionBus(), this);
        if (!mAkonadiConsoleInterface->isValid()) {
            delete mAkonadiConsoleInterface;
            return;
        }

        connect(mAkonadiConsoleInterface, SIGNAL(enabledChanged(bool)),
                this, SLOT(onAkonadiConsoleLoggingEnabled(bool)));

        QTimer::singleShot(0, this, [this]() {
            auto watcher = new QDBusPendingCallWatcher(mAkonadiConsoleInterface->asyncCall(QStringLiteral("enabled")));
            connect(watcher, &QDBusPendingCallWatcher::finished,
                    this, [this](QDBusPendingCallWatcher *watcher) {
                        watcher->deleteLater();
                        QDBusPendingReply<bool> reply = *watcher;
                        if (reply.isError()) {
                            return;
                        }
                        onAkonadiConsoleLoggingEnabled(reply.argumentAt<0>());
                    });
        });
    }

    void serviceUnregistered(const QString &)
    {
        onAkonadiConsoleLoggingEnabled(false);
        delete mAkonadiConsoleInterface;
        mAkonadiConsoleInterface = nullptr;
    }

    void onAkonadiConsoleLoggingEnabled(bool enabled)
    {
        if (mEnabled == enabled) {
            return;
        }

        mEnabled = enabled;
        if (mEnabled) {
            // FIXME: Qt calls our categoryFilter from installFilter() but at that
            // point we cannot refer to mOldFilter yet (as we only receive it after
            // this call returns. So we set our category filter twice: once to get
            // the original Qt filter and second time to force our category filter
            // to be called when we already know the old filter.
            mOldFilter = QLoggingCategory::installFilter(categoryFilter);
            QLoggingCategory::installFilter(categoryFilter);
        } else {
            QLoggingCategory::installFilter(mOldFilter);
            mOldFilter = nullptr;
        }
    }

private:
    QString akonadiConsoleServiceName()
    {
        QString service = QStringLiteral(AKONADICONSOLE_SERVICE);
        if (Akonadi::Instance::hasIdentifier()) {
            service += QStringLiteral("-%1").arg(Akonadi::Instance::identifier());
        }
        return service;
    }

    static void categoryFilter(QLoggingCategory *cat)
    {
        const auto that = self();
        if (!that) {
            return;
        }

        if (qstrncmp(cat->categoryName(), "org.kde.pim.", 12) == 0) {
            cat->setEnabled(QtDebugMsg, true);
            cat->setEnabled(QtInfoMsg, true);
            cat->setEnabled(QtWarningMsg, true);
            cat->setEnabled(QtCriticalMsg, true);
        } else if (that->mOldFilter) {
            that->mOldFilter(cat);
        }
    }

    static void dbusLogger(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {
        const auto that = self();
        if (!that) {
            return;
        }

        // Log to previous logger
        that->mOldHandler(type, ctx, msg);

        if (that->mEnabled) {
            that->mAkonadiConsoleInterface->asyncCallWithArgumentList(
                QStringLiteral("message"),
                QList<QVariant>{
                    QDateTime::currentMSecsSinceEpoch(), qAppName(),
                    qApp->applicationPid(), static_cast<int>(type),
                    QString::fromUtf8(ctx.category), QString::fromUtf8(ctx.file),
                    QString::fromUtf8(ctx.function), ctx.line, ctx.version, msg });
        }
    }

private:
    QDBusServiceWatcher mWatcher;
    QLoggingCategory::CategoryFilter mOldFilter = nullptr;
    QtMessageHandler mOldHandler = nullptr;
    QDBusInterface *mAkonadiConsoleInterface = nullptr;
    bool mEnabled = false;

    static RemoteLogger *sInstance;
};

RemoteLogger *RemoteLogger::sInstance = nullptr;

}

void akInitRemoteLog()
{
    Q_ASSERT(qApp->thread() == QThread::currentThread());

    if (!RemoteLogger::self()) {
        new RemoteLogger();
    }
}


#include "akremotelog.moc"

