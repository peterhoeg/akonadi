/*
    Copyright (c) 2014 Christian Mollekopf <mollekopf@kolabsys.com>

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

#include <QObject>

#include <handler/list.h>
#include <imapstreamparser.h>
#include <response.h>

#include <libs/notificationmessagev3_p.h>
#include <libs/notificationmessagev2_p.h>

#include "fakeakonadiserver.h"
#include "aktest.h"
#include "akdebug.h"
#include "entities.h"

#include <QtTest/QTest>
#include <QSignalSpy>

using namespace Akonadi;
using namespace Akonadi::Server;

class ListHandlerTest : public QObject
{
    Q_OBJECT

public:
    ListHandlerTest()
    {
        qRegisterMetaType<Akonadi::Server::Response>();

        try {
            FakeAkonadiServer::instance()->initialize();
        } catch (const FakeAkonadiServerException &e) {
            akError() << "Server exception: " << e.what();
            akFatal() << "Fake Akonadi Server failed to start up, aborting test";
        }
    }

    ~ListHandlerTest()
    {
    }


private Q_SLOTS:
    void testList_data()
    {
        QTest::addColumn<QList<QByteArray> >("scenario");
        QTest::addColumn<Akonadi::NotificationMessageV3>("notification");
        QTest::addColumn<bool>("expectFail");

        {
            QList<QByteArray> scenario;
            scenario << FakeAkonadiServer::defaultScenario()
                    << "C: 2 LIST 0 INF () ()"
                    << "S: * 5 4 (NAME \"Collection D\" MIMETYPE () REMOTEID \"ColD\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 4 3 (NAME \"Collection C\" MIMETYPE (inode/directory) REMOTEID \"ColC\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 3 2 (NAME \"Collection B\" MIMETYPE (application/octet-stream inode/directory) REMOTEID \"ColB\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 2 0 (NAME \"Collection A\" MIMETYPE (inode/directory) REMOTEID \"ColA\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 1 0 (NAME \"Search\" MIMETYPE () REMOTEID \"\" REMOTEREVISION \"\" RESOURCE \"akonadi_search_resource\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 7 6 (NAME \"Virtual Subcollection\" MIMETYPE () REMOTEID \"virtual2\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 6 0 (NAME \"Virtual Collection\" MIMETYPE () REMOTEID \"virtual\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: 2 OK List completed";
            QTest::newRow("recursive list") << scenario << NotificationMessageV3() << false;
        }
        {
            QList<QByteArray> scenario;
            scenario << FakeAkonadiServer::defaultScenario()
                    << "C: 2 LIST 2 0 () ()"
                    << "S: * 2 0 (NAME \"Collection A\" MIMETYPE (inode/directory) REMOTEID \"ColA\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: 2 OK List completed";
            QTest::newRow("base list") << scenario << NotificationMessageV3() << false;
        }        {
            QList<QByteArray> scenario;
            scenario << FakeAkonadiServer::defaultScenario()
                    << "C: 2 LIST 2 1 () ()"
                    << "S: * 3 2 (NAME \"Collection B\" MIMETYPE (application/octet-stream inode/directory) REMOTEID \"ColB\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: 2 OK List completed";
            QTest::newRow("first level list") << scenario << NotificationMessageV3() << false;
        }
        {
            QList<QByteArray> scenario;
            scenario << FakeAkonadiServer::defaultScenario()
                    << "C: 2 LIST 0 INF (DISPLAY  ) ()"
                    << "S: * 3 2 (NAME \"Collection B\" MIMETYPE (application/octet-stream inode/directory) REMOTEID \"ColB\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 2 0 (NAME \"Collection A\" MIMETYPE (inode/directory) REMOTEID \"ColA\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 1 0 (NAME \"Search\" MIMETYPE () REMOTEID \"\" REMOTEREVISION \"\" RESOURCE \"akonadi_search_resource\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 7 6 (NAME \"Virtual Subcollection\" MIMETYPE () REMOTEID \"virtual2\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 6 0 (NAME \"Virtual Collection\" MIMETYPE () REMOTEID \"virtual\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: 2 OK List completed";
            QTest::newRow("recursive list to display including local override") << scenario << NotificationMessageV3() << false;
        }
        {
            QList<QByteArray> scenario;
            scenario << FakeAkonadiServer::defaultScenario()
                    << "C: 2 LIST 0 INF (SYNC  ) ()"
                    << "S: * 3 2 (NAME \"Collection B\" MIMETYPE (application/octet-stream inode/directory) REMOTEID \"ColB\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 2 0 (NAME \"Collection A\" MIMETYPE (inode/directory) REMOTEID \"ColA\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 1 0 (NAME \"Search\" MIMETYPE () REMOTEID \"\" REMOTEREVISION \"\" RESOURCE \"akonadi_search_resource\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 7 6 (NAME \"Virtual Subcollection\" MIMETYPE () REMOTEID \"virtual2\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 6 0 (NAME \"Virtual Collection\" MIMETYPE () REMOTEID \"virtual\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: 2 OK List completed";
            QTest::newRow("recursive list to sync including local override") << scenario << NotificationMessageV3() << false;
        }
        {
            QList<QByteArray> scenario;
            scenario << FakeAkonadiServer::defaultScenario()
                    << "C: 2 LIST 0 INF (INDEX  ) ()"
                    << "S: * 3 2 (NAME \"Collection B\" MIMETYPE (application/octet-stream inode/directory) REMOTEID \"ColB\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 2 0 (NAME \"Collection A\" MIMETYPE (inode/directory) REMOTEID \"ColA\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_0\" VIRTUAL 0 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 1 0 (NAME \"Search\" MIMETYPE () REMOTEID \"\" REMOTEREVISION \"\" RESOURCE \"akonadi_search_resource\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 7 6 (NAME \"Virtual Subcollection\" MIMETYPE () REMOTEID \"virtual2\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: * 6 0 (NAME \"Virtual Collection\" MIMETYPE () REMOTEID \"virtual\" REMOTEREVISION \"\" RESOURCE \"akonadi_fake_resource_with_virtual_collections_0\" VIRTUAL 1 CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL)) )"
                    << "S: 2 OK List completed";
            QTest::newRow("recursive list to sync including local override") << scenario << NotificationMessageV3() << false;
        }
    }

    void testList()
    {
        QFETCH(QList<QByteArray>, scenario);
        QFETCH(NotificationMessageV3, notification);
        QFETCH(bool, expectFail);

        FakeAkonadiServer::instance()->setScenario(scenario);
        FakeAkonadiServer::instance()->runTest();

//         QSignalSpy *notificationSpy = FakeAkonadiServer::instance()->notificationSpy();
//         if (notification.isValid()) {
//             QCOMPARE(notificationSpy->count(), 1);
//             const NotificationMessageV3::List notifications = notificationSpy->takeFirst().first().value<NotificationMessageV3::List>();
//             QCOMPARE(notifications.count(), 1);
//             QCOMPARE(notifications.first(), notification);
//         } else {
//             QVERIFY(notificationSpy->isEmpty() || notificationSpy->takeFirst().first().value<NotificationMessageV3::List>().isEmpty());
//         }

//         Q_FOREACH (const NotificationMessageV2::Entity &entity, notification.entities()) {
//             if (expectFail) {
//               QVERIFY(!Collection::relatesToPimItem(notification.parentCollection(), entity.id));
//             } else {
//               QVERIFY(Collection::relatesToPimItem(notification.parentCollection(), entity.id));
//             }
//         }
    }

};

AKTEST_FAKESERVER_MAIN(ListHandlerTest)

#include "listhandlertest.moc"
