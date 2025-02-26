/*
    Copyright (c) 2017  Daniel Vrátil <dvratil@kde.org>

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

#include "dbinitializer.h"
#include "storage/collectiontreecache.h"
#include "storage/selectquerybuilder.h"
#include "private/scope_p.h"
#include "aktest.h"
#include "fakeakonadiserver.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

using namespace Akonadi;
using namespace Akonadi::Server;

class InspectableCollectionTreeCache : public CollectionTreeCache
{
    Q_OBJECT
public:
    InspectableCollectionTreeCache()
        : CollectionTreeCache()
        , mCachePopulated(0)
    {}

    bool waitForCachePopulated()
    {
        QSignalSpy spy(this, &InspectableCollectionTreeCache::cachePopulated);
        return mCachePopulated == 1 || spy.wait(5000);
    }

Q_SIGNALS:
    void cachePopulated();

protected:
    void init() Q_DECL_OVERRIDE
    {
        CollectionTreeCache::init();
        mCachePopulated = 1;
        Q_EMIT cachePopulated();
    }

    void quit() Q_DECL_OVERRIDE
    {
    }

private:
    QAtomicInt mCachePopulated;
};

class CollectionTreeCacheTest : public QObject
{
    Q_OBJECT

public:
    CollectionTreeCacheTest()
    {
        try {
            FakeAkonadiServer::instance()->init();
        } catch (const FakeAkonadiServerException &e) {
            qWarning() << "Server exception: " << e.what();
            qFatal("Fake Akonadi Server failed to start up, aborting test");
        }
    }

    ~CollectionTreeCacheTest()
    {
        FakeAkonadiServer::instance()->quit();
    }

private:
    void populateDb(DbInitializer &db)
    {
        // ResA
        //  |- Col A1
        //  |- Col A2
        //  |   |- Col A3
        //  |   |- Col A7
        //  |       |- Col A5
        //  |       |- Col A8
        //  |- Col A6
        //  |   |- Col A10
        //  |- Col A9
        auto res = db.createResource("TestResource");
        auto resA = db.createCollection("Res A", Collection());
        auto colA1 = db.createCollection("Col A1", resA);
        auto colA2 = db.createCollection("Col A2", resA);
        auto colA3 = db.createCollection("Col A3", colA2);
        auto colA5 = db.createCollection("Col A5", colA2);
        auto colA6 = db.createCollection("Col A6", resA);
        auto colA7 = db.createCollection("Col A7", colA2);
        auto colA8 = db.createCollection("Col A8", colA7);
        auto colA9 = db.createCollection("Col A9", resA);
        auto colA10 = db.createCollection("Col A10", colA6);

        // Move the collection to the final parent
        colA5.setParent(colA7);
        colA5.update();
    }


private Q_SLOTS:
    void populateTest()
    {
        DbInitializer db;
        populateDb(db);

        InspectableCollectionTreeCache treeCache;
        QVERIFY(treeCache.waitForCachePopulated());

        auto allCols = treeCache.retrieveCollections(Scope(), std::numeric_limits<int>::max(), 1);

        SelectQueryBuilder<Collection> qb;
        QVERIFY(qb.exec());
        auto expCols = qb.result();

        const auto sort = [](const Collection &l, const Collection &r) { return l.id() < r.id(); };
        std::sort(allCols.begin(), allCols.end(), sort);
        std::sort(expCols.begin(), expCols.end(), sort);

        QCOMPARE(allCols.size(), expCols.size());
        QCOMPARE(allCols, expCols);
    }

};

AKTEST_FAKESERVER_MAIN(CollectionTreeCacheTest)

#include "collectiontreecachetest.moc"
