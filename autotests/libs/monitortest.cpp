/*
    Copyright (c) 2006 Volker Krause <vkrause@kde.org>

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

#include "monitortest.h"
#include "test_utils.h"

#include <agentmanager.h>
#include <agentinstance.h>
#include <monitor.h>
#include <collectioncreatejob.h>
#include <collectiondeletejob.h>
#include <collectionfetchjob.h>
#include <collectionmodifyjob.h>
#include <collectionmovejob.h>
#include <collectionstatistics.h>
#include <control.h>
#include <itemcreatejob.h>
#include <itemdeletejob.h>
#include <itemfetchjob.h>
#include <itemfetchscope.h>
#include <itemmodifyjob.h>
#include <itemmovejob.h>
#include <searchcreatejob.h>
#include <searchquery.h>
#include <subscriptionjob_p.h>

#include <QVariant>
#include <QSignalSpy>
#include <qtest_akonadi.h>

using namespace Akonadi;

QTEST_AKONADIMAIN(MonitorTest)

static Collection res3;

Q_DECLARE_METATYPE(Akonadi::Collection::Id)
Q_DECLARE_METATYPE(QSet<QByteArray>)

void MonitorTest::initTestCase()
{
    AkonadiTest::checkTestIsIsolated();
    Control::start();

    res3 = Collection(collectionIdFromPath(QStringLiteral("res3")));

    AkonadiTest::setAllResourcesOffline();
}

void MonitorTest::testMonitor_data()
{
    QTest::addColumn<bool>("fetchCol");
    QTest::newRow("with collection fetching") << true;
    QTest::newRow("without collection fetching") << false;
}

void MonitorTest::testMonitor()
{
    QFETCH(bool, fetchCol);

    Monitor *monitor = new Monitor(this);
    monitor->setCollectionMonitored(Collection::root());
    monitor->fetchCollection(fetchCol);
    monitor->itemFetchScope().fetchFullPayload();
    monitor->itemFetchScope().setCacheOnly(true);

    // monitor signals
    qRegisterMetaType<Akonadi::Collection>();
    /*
       qRegisterMetaType<Akonadi::Collection::Id>() registers the type with a
       name of "qlonglong".  Doing
       qRegisterMetaType<Akonadi::Collection::Id>( "Akonadi::Collection::Id" )
       doesn't help. (works now , see QTBUG-937 and QTBUG-6833, -- dvratil)

       The problem here is that Akonadi::Collection::Id is a typedef to qlonglong,
       and qlonglong is already a registered meta type.  So the signal spy will
       give us a QVariant of type Akonadi::Collection::Id, but calling
       .value<Akonadi::Collection::Id>() on that variant will in fact end up
       calling qvariant_cast<qlonglong>.  From the point of view of QMetaType,
       Akonadi::Collection::Id and qlonglong are different types, so QVariant
       can't convert, and returns a default-constructed qlonglong, zero.

       When connecting to a real slot (without QSignalSpy), this problem is
       avoided, because the casting is done differently (via a lot of void
       pointers).

       The docs say nothing about qRegisterMetaType -ing a typedef, so I'm not
       sure if this is a bug or not. (cberzan)
     */
    qRegisterMetaType<Akonadi::Collection::Id>("Akonadi::Collection::Id");
    qRegisterMetaType<Akonadi::Item>();
    qRegisterMetaType<Akonadi::CollectionStatistics>();
    qRegisterMetaType<QSet<QByteArray> >();
    QSignalSpy caddspy(monitor, &Monitor::collectionAdded);
    QSignalSpy cmodspy(monitor, SIGNAL(collectionChanged(Akonadi::Collection,QSet<QByteArray>)));
    QSignalSpy cmvspy(monitor, &Monitor::collectionMoved);
    QSignalSpy crmspy(monitor, &Monitor::collectionRemoved);
    QSignalSpy cstatspy(monitor, &Monitor::collectionStatisticsChanged);
    QSignalSpy cSubscribedSpy(monitor, &Monitor::collectionSubscribed);
    QSignalSpy cUnsubscribedSpy(monitor, &Monitor::collectionUnsubscribed);
    QSignalSpy iaddspy(monitor, &Monitor::itemAdded);
    QSignalSpy imodspy(monitor, &Monitor::itemChanged);
    QSignalSpy imvspy(monitor, &Monitor::itemMoved);
    QSignalSpy irmspy(monitor, &Monitor::itemRemoved);

    QVERIFY(caddspy.isValid());
    QVERIFY(cmodspy.isValid());
    QVERIFY(cmvspy.isValid());
    QVERIFY(crmspy.isValid());
    QVERIFY(cstatspy.isValid());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isValid());
    QVERIFY(imodspy.isValid());
    QVERIFY(imvspy.isValid());
    QVERIFY(irmspy.isValid());

    // create a collection
    Collection monitorCol;
    monitorCol.setParentCollection(res3);
    monitorCol.setName(QStringLiteral("monitor"));
    CollectionCreateJob *create = new CollectionCreateJob(monitorCol, this);
    AKVERIFYEXEC(create);
    monitorCol = create->collection();
    QVERIFY(monitorCol.isValid());
    if (caddspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionAdded(Akonadi::Collection,Akonadi::Collection)), 6000));
    }

    QCOMPARE(caddspy.count(), 1);
    QList<QVariant> arg = caddspy.takeFirst();
    Collection col = arg.at(0).value<Collection>();
    QCOMPARE(col, monitorCol);
    if (fetchCol) {
        QCOMPARE(col.name(), QStringLiteral("monitor"));
    }
    Collection parent = arg.at(1).value<Collection>();
    QCOMPARE(parent, res3);

    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cstatspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // add an item
    Item newItem;
    newItem.setMimeType(QStringLiteral("application/octet-stream"));
    ItemCreateJob *append = new ItemCreateJob(newItem, monitorCol, this);
    AKVERIFYEXEC(append);
    Item monitorRef = append->item();
    QVERIFY(monitorRef.isValid());
    if (cstatspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionStatisticsChanged(Akonadi::Collection::Id,Akonadi::CollectionStatistics)), 1000));
    }

    for (int i = 0; i < cstatspy.count(); i++) {
        qDebug() << "CSTAT" << cstatspy[i][0].toLongLong() << cstatspy[i][1].value<Akonadi::CollectionStatistics>().count();
    }
    QCOMPARE(cstatspy.count(), 1);
    arg = cstatspy.takeFirst();
    QCOMPARE(arg.at(0).value<Akonadi::Collection::Id>(), monitorCol.id());

    QCOMPARE(iaddspy.count(), 1);
    arg = iaddspy.takeFirst();
    Item item = arg.at(0).value<Item>();
    QCOMPARE(item, monitorRef);
    QCOMPARE(item.mimeType(), QString::fromLatin1("application/octet-stream"));
    Collection collection = arg.at(1).value<Collection>();
    QCOMPARE(collection.id(), monitorCol.id());

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // modify an item
    item.setPayload<QByteArray>("some new content");
    ItemModifyJob *store = new ItemModifyJob(item, this);
    AKVERIFYEXEC(store);
    if (cstatspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionStatisticsChanged(Akonadi::Collection::Id,Akonadi::CollectionStatistics)), 1000));
    }

    QCOMPARE(cstatspy.count(), 1);
    arg = cstatspy.takeFirst();
    QCOMPARE(arg.at(0).value<Collection::Id>(), monitorCol.id());

    QCOMPARE(imodspy.count(), 1);
    arg = imodspy.takeFirst();
    item = arg.at(0).value<Item>();
    QCOMPARE(monitorRef, item);
    QVERIFY(item.hasPayload<QByteArray>());
    QCOMPARE(item.payload<QByteArray>(), QByteArray("some new content"));
    QSet<QByteArray> parts = arg.at(1).value<QSet<QByteArray> >();
    QCOMPARE(parts, QSet<QByteArray>() << "PLD:RFC822");

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // move an item
    ItemMoveJob *move = new ItemMoveJob(item, res3);
    AKVERIFYEXEC(move);
    if (cstatspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionStatisticsChanged(Akonadi::Collection::Id,Akonadi::CollectionStatistics)), 1000));
    }
    QCOMPARE(cstatspy.count(), 2);
    // NOTE: We don't make any assumptions about the order of the collectionStatisticsChanged
    // signals, they seem to arrive in random order
    QList<Collection::Id> notifiedCols;
    notifiedCols << cstatspy.takeFirst().at(0).value<Collection::Id>()
                 << cstatspy.takeFirst().at(0).value<Collection::Id>();
    QVERIFY(notifiedCols.contains(res3.id()));  // destination
    QVERIFY(notifiedCols.contains(monitorCol.id())); // source

    QCOMPARE(imvspy.count(), 1);
    arg = imvspy.takeFirst();
    item = arg.at(0).value<Item>();   // the item
    QCOMPARE(monitorRef, item);
    col = arg.at(1).value<Collection>();   // the source collection
    QCOMPARE(col.id(), monitorCol.id());
    col = arg.at(2).value<Collection>();   // the destination collection
    QCOMPARE(col.id(), res3.id());

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // delete an item
    ItemDeleteJob *del = new ItemDeleteJob(monitorRef, this);
    AKVERIFYEXEC(del);
    if (cstatspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionStatisticsChanged(Akonadi::Collection::Id,Akonadi::CollectionStatistics)), 1000));
    }

    QCOMPARE(cstatspy.count(), 1);
    arg = cstatspy.takeFirst();
    QCOMPARE(arg.at(0).value<Collection::Id>(), res3.id());
    cmodspy.clear();

    QCOMPARE(irmspy.count(), 1);
    arg = irmspy.takeFirst();
    Item ref = qvariant_cast<Item>(arg.at(0));
    QCOMPARE(monitorRef, ref);
    QCOMPARE(ref.parentCollection(), res3);

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    imvspy.clear();

    // Unsubscribe and re-subscribed a collection that existed before the monitor was created.
    Collection subCollection = Collection(collectionIdFromPath(QStringLiteral("res2/foo2")));
    subCollection.setName(QStringLiteral("foo2"));
    QVERIFY(subCollection.isValid());

    SubscriptionJob *subscribeJob = new SubscriptionJob(this);
    subscribeJob->unsubscribe(Collection::List() << subCollection);
    AKVERIFYEXEC(subscribeJob);
    // Wait for unsubscribed signal, it goes after changed, so we can check for both
    if (cUnsubscribedSpy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionUnsubscribed(Akonadi::Collection)), 1000));
    }
    QCOMPARE(cmodspy.size(), 1);
    arg = cmodspy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col.id(), subCollection.id());

    QVERIFY(cSubscribedSpy.isEmpty());
    QCOMPARE(cUnsubscribedSpy.size(), 1);
    arg = cUnsubscribedSpy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col.id(), subCollection.id());

    subscribeJob = new SubscriptionJob(this);
    subscribeJob->subscribe(Collection::List() << subCollection);
    AKVERIFYEXEC(subscribeJob);
    // Wait for subscribed signal, it goes after changed, so we can check for both
    if (cSubscribedSpy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionSubscribed(Akonadi::Collection,Akonadi::Collection)), 1000));
    }
    QCOMPARE(cmodspy.size(), 1);
    arg = cmodspy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col.id(), subCollection.id());

    QVERIFY(cUnsubscribedSpy.isEmpty());
    QCOMPARE(cSubscribedSpy.size(), 1);
    arg = cSubscribedSpy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col.id(), subCollection.id());
    if (fetchCol) {
        QVERIFY(!col.name().isEmpty());
        QCOMPARE(col.name(), subCollection.name());
    }

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cstatspy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // modify a collection
    monitorCol.setName(QStringLiteral("changed name"));
    CollectionModifyJob *mod = new CollectionModifyJob(monitorCol, this);
    AKVERIFYEXEC(mod);
    if (cmodspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionChanged(Akonadi::Collection)), 1000));
    }

    QCOMPARE(cmodspy.count(), 1);
    arg = cmodspy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col, monitorCol);
    if (fetchCol) {
        QCOMPARE(col.name(), QStringLiteral("changed name"));
    }

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cstatspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // move a collection
    Collection dest = Collection(collectionIdFromPath(QStringLiteral("res1/foo")));
    CollectionMoveJob *cmove = new CollectionMoveJob(monitorCol, dest, this);
    AKVERIFYEXEC(cmove);
    if (cmvspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionMoved(Akonadi::Collection,Akonadi::Collection,Akonadi::Collection)), 1000));
    }

    QCOMPARE(cmvspy.count(), 1);
    arg = cmvspy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col, monitorCol);
    QCOMPARE(col.parentCollection(), dest);
    if (fetchCol) {
        QCOMPARE(col.name(), monitorCol.name());
    }
    col = arg.at(1).value<Collection>();
    QCOMPARE(col, res3);
    col = arg.at(2).value<Collection>();
    QCOMPARE(col, dest);

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(crmspy.isEmpty());
    QVERIFY(cstatspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());

    // delete a collection
    CollectionDeleteJob *cdel = new CollectionDeleteJob(monitorCol, this);
    AKVERIFYEXEC(cdel);
    if (crmspy.isEmpty()) {
        QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionRemoved(Akonadi::Collection)), 1000));
    }

    QCOMPARE(crmspy.count(), 1);
    arg = crmspy.takeFirst();
    col = arg.at(0).value<Collection>();
    QCOMPARE(col.id(), monitorCol.id());
    QCOMPARE(col.parentCollection(), dest);

    QVERIFY(caddspy.isEmpty());
    QVERIFY(cmodspy.isEmpty());
    QVERIFY(cmvspy.isEmpty());
    QVERIFY(cstatspy.isEmpty());
    QVERIFY(cSubscribedSpy.isEmpty());
    QVERIFY(cUnsubscribedSpy.isEmpty());
    QVERIFY(iaddspy.isEmpty());
    QVERIFY(imodspy.isEmpty());
    QVERIFY(imvspy.isEmpty());
    QVERIFY(irmspy.isEmpty());
}

void MonitorTest::testVirtualCollectionsMonitoring()
{
    Monitor *monitor = new Monitor(this);
    monitor->setCollectionMonitored(Collection(1));       // top-level 'Search' collection

    QSignalSpy caddspy(monitor, &Monitor::collectionAdded);
    QVERIFY(caddspy.isValid());

    SearchCreateJob *job = new SearchCreateJob(QStringLiteral("Test search collection"), Akonadi::SearchQuery(), this);
    AKVERIFYEXEC(job);
    QVERIFY(AkonadiTest::akWaitForSignal(monitor, SIGNAL(collectionAdded(Akonadi::Collection,Akonadi::Collection)), 1000));
    QCOMPARE(caddspy.count(), 1);
}

