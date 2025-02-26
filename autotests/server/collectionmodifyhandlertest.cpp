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

#include <storage/entity.h>

#include "fakeakonadiserver.h"
#include "aktest.h"
#include "entities.h"

#include <private/scope_p.h>
#include <private/imapset_p.h>

#include <QTest>

using namespace Akonadi;
using namespace Akonadi::Server;

class CollectionModifyHandlerTest : public QObject
{
    Q_OBJECT

public:
    CollectionModifyHandlerTest()
    {
        FakeAkonadiServer::instance()->init();
    }

    ~CollectionModifyHandlerTest()
    {
        FakeAkonadiServer::instance()->quit();
    }

private Q_SLOTS:
    void testModify_data()
    {
        QTest::addColumn<TestScenario::List>("scenarios");
        QTest::addColumn<Protocol::ChangeNotificationList>("expectedNotifications");
        QTest::addColumn<QVariant>("newValue");

        auto notificationTemplate = Protocol::CollectionChangeNotificationPtr::create();
        notificationTemplate->setOperation(Protocol::CollectionChangeNotification::Modify);
        notificationTemplate->setParentCollection(4);
        notificationTemplate->setResource("akonadi_fake_resource_0");
        notificationTemplate->setSessionId(FakeAkonadiServer::instanceName().toLatin1());

        auto collectionTemplate = Protocol::FetchCollectionsResponsePtr::create();
        collectionTemplate->setId(5);
        collectionTemplate->setRemoteId(QStringLiteral("ColD"));
        collectionTemplate->setRemoteRevision(QStringLiteral(""));
        collectionTemplate->setName(QStringLiteral("New Name"));
        collectionTemplate->setParentId(4);
        collectionTemplate->setResource(QStringLiteral("akonadi_fake_resource_0"));

        {
            auto cmd = Protocol::ModifyCollectionCommandPtr::create(5);
            cmd->setName(QStringLiteral("New Name"));

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponsePtr::create());

            auto notification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            notification->setChangedParts(QSet<QByteArray>() << "NAME");
            notification->setCollection(*collectionTemplate);
            QTest::newRow("modify collection") << scenarios << Protocol::ChangeNotificationList{ notification } << QVariant::fromValue(QStringLiteral("New Name"));
        }
        {
            auto cmd = Protocol::ModifyCollectionCommandPtr::create(5);
            cmd->setEnabled(false);

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponsePtr::create());

            Protocol::FetchCollectionsResponse collection(*collectionTemplate);
            collection.setEnabled(false);
            auto notification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            notification->setChangedParts(QSet<QByteArray>() << "ENABLED");
            notification->setCollection(collection);
            auto unsubscribeNotification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            unsubscribeNotification->setOperation(Protocol::CollectionChangeNotification::Unsubscribe);
            unsubscribeNotification->setCollection(std::move(collection));

            QTest::newRow("disable collection") << scenarios << Protocol::ChangeNotificationList{ notification, unsubscribeNotification} << QVariant::fromValue(false);
        }
        {
            auto cmd = Protocol::ModifyCollectionCommandPtr::create(5);
            cmd->setEnabled(true);

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponsePtr::create());

            auto notification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            notification->setChangedParts(QSet<QByteArray>() << "ENABLED");
            notification->setCollection(*collectionTemplate);
            auto subscribeNotification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            subscribeNotification->setOperation(Protocol::CollectionChangeNotification::Subscribe);
            subscribeNotification->setCollection(*collectionTemplate);

            QTest::newRow("enable collection") << scenarios << Protocol::ChangeNotificationList{ notification, subscribeNotification } << QVariant::fromValue(true);
        }
        {
            auto cmd = Protocol::ModifyCollectionCommandPtr::create(5);
            cmd->setEnabled(false);
            cmd->setSyncPref(Tristate::True);
            cmd->setDisplayPref(Tristate::True);
            cmd->setIndexPref(Tristate::True);

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::ModifyCollectionResponsePtr::create());

            Protocol::FetchCollectionsResponse collection(*collectionTemplate);
            collection.setEnabled(false);
            collection.setSyncPref(Tristate::True);
            collection.setDisplayPref(Tristate::True);
            collection.setIndexPref(Tristate::True);
            auto notification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            notification->setChangedParts(QSet<QByteArray>() << "ENABLED" << "SYNC" << "DISPLAY" << "INDEX");
            notification->setCollection(collection);
            auto unsubscribeNotification = Protocol::CollectionChangeNotificationPtr::create(*notificationTemplate);
            unsubscribeNotification->setOperation(Protocol::CollectionChangeNotification::Unsubscribe);
            unsubscribeNotification->setCollection(std::move(collection));

            QTest::newRow("local override enable") << scenarios << Protocol::ChangeNotificationList{ notification, unsubscribeNotification } << QVariant::fromValue(true);
        }
    }

    void testModify()
    {
        QFETCH(TestScenario::List, scenarios);
        QFETCH(Protocol::ChangeNotificationList, expectedNotifications);
        QFETCH(QVariant, newValue);

        FakeAkonadiServer::instance()->setScenarios(scenarios);
        FakeAkonadiServer::instance()->runTest();

        auto notificationSpy = FakeAkonadiServer::instance()->notificationSpy();
        if (expectedNotifications.isEmpty()) {
            QVERIFY(notificationSpy->isEmpty() || notificationSpy->takeFirst().first().value<Protocol::ChangeNotificationList>().isEmpty());
            return;
        }
        QCOMPARE(notificationSpy->count(), 1);
        //Only one notify call
        QCOMPARE(notificationSpy->first().count(), 1);
        const auto receivedNotifications = notificationSpy->first().first().value<Protocol::ChangeNotificationList>();
        QCOMPARE(receivedNotifications.size(), expectedNotifications.count());

        for (int i = 0; i < expectedNotifications.size(); i++) {
            const auto recvNtf = receivedNotifications.at(i).staticCast<Protocol::CollectionChangeNotification>();
            const auto expNtf = expectedNotifications.at(i).staticCast<Protocol::CollectionChangeNotification>();
            if (*recvNtf != *expNtf) {
                qDebug() << "Actual:  " << Protocol::debugString(recvNtf);
                qDebug() << "Expected:" << Protocol::debugString(expNtf);
            }
            QCOMPARE(*recvNtf, *expNtf);
            const auto notification = receivedNotifications.at(i).staticCast<Protocol::CollectionChangeNotification>();
            if (notification->changedParts().contains("NAME")) {
                Collection col = Collection::retrieveById(notification->collection().id());
                QCOMPARE(col.name(), newValue.toString());
            }
            if (!notification->changedParts().intersects({ "ENABLED", "SYNC", "DISPLAY", "INDEX" })) {
                Collection col = Collection::retrieveById(notification->collection().id());
                const bool sync = col.syncPref() == Collection::Undefined ? col.enabled() : col.syncPref() == Collection::True;
                QCOMPARE(sync, newValue.toBool());
                const bool display = col.displayPref() == Collection::Undefined ? col.enabled() : col.displayPref() == Collection::True;
                QCOMPARE(display, newValue.toBool());
                const bool index = col.indexPref() == Collection::Undefined ? col.enabled() : col.indexPref() == Collection::True;
                QCOMPARE(index, newValue.toBool());
            }
        }
    }

};

AKTEST_FAKESERVER_MAIN(CollectionModifyHandlerTest)

#include "collectionmodifyhandlertest.moc"
