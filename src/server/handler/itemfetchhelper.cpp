
/***************************************************************************
 *   Copyright (C) 2006-2009 by Tobias Koenig <tokoe@kde.org>              *
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

#include "itemfetchhelper.h"

#include "akonadi.h"
#include "connection.h"
#include "handler.h"
#include "handlerhelper.h"
#include "storage/selectquerybuilder.h"
#include "storage/itemqueryhelper.h"
#include "storage/itemretrievalmanager.h"
#include "storage/itemretrievalrequest.h"
#include "storage/parthelper.h"
#include "storage/parttypehelper.h"
#include "storage/transaction.h"
#include "shared/akranges.h"

#include "utils.h"
#include "intervalcheck.h"
#include "agentmanagerinterface.h"
#include "dbusconnectionpool.h"
#include "tagfetchhelper.h"
#include "relationfetchhandler.h"
#include "akonadiserver_debug.h"

#include <private/scope_p.h>
#include <private/dbus_p.h>

#include <QStringList>
#include <QVariant>
#include <QDateTime>
#include <QSqlQuery>

#include <QElapsedTimer>

using namespace Akonadi;
using namespace Akonadi::Server;

#define ENABLE_FETCH_PROFILING 0
#if ENABLE_FETCH_PROFILING
#define BEGIN_TIMER(name) \
    QElapsedTimer name##Timer; \
    name##Timer.start();

#define END_TIMER(name) \
    const double name##Elapsed = name##Timer.nsecsElapsed() / 1000000.0;
#define PROF_INC(name) \
    ++name;
#else
#define BEGIN_TIMER(name)
#define END_TIMER(name)
#define PROF_INC(name)
#endif

ItemFetchHelper::ItemFetchHelper(Connection *connection, const Scope &scope,
                                 const Protocol::ItemFetchScope &itemFetchScope,
                                 const Protocol::TagFetchScope &tagFetchScope)
    : ItemFetchHelper(connection, connection->context(), scope, itemFetchScope, tagFetchScope)
{
}


ItemFetchHelper::ItemFetchHelper(Connection *connection, CommandContext *context,
                                 const Scope &scope,
                                 const Protocol::ItemFetchScope &itemFetchScope,
                                 const Protocol::TagFetchScope &tagFetchScope)
    : mConnection(connection)
    , mContext(context)
    , mScope(scope)
    , mItemFetchScope(itemFetchScope)
    , mTagFetchScope(tagFetchScope)
{
    std::fill(mItemQueryColumnMap, mItemQueryColumnMap + ItemQueryColumnCount, -1);
}

void ItemFetchHelper::disableATimeUpdates()
{
    mUpdateATimeEnabled = false;
}

enum PartQueryColumns {
    PartQueryPimIdColumn,
    PartQueryTypeIdColumn,
    PartQueryDataColumn,
    PartQueryStorageColumn,
    PartQueryVersionColumn,
    PartQueryDataSizeColumn
};

QSqlQuery ItemFetchHelper::buildPartQuery(const QVector<QByteArray> &partList, bool allPayload, bool allAttrs)
{
    ///TODO: merge with ItemQuery
    QueryBuilder partQuery(PimItem::tableName());

    if (!partList.isEmpty() || allPayload || allAttrs) {
        partQuery.addJoin(QueryBuilder::InnerJoin, Part::tableName(), PimItem::idFullColumnName(), Part::pimItemIdFullColumnName());
        partQuery.addColumn(PimItem::idFullColumnName());
        partQuery.addColumn(Part::partTypeIdFullColumnName());
        partQuery.addColumn(Part::dataFullColumnName());
        partQuery.addColumn(Part::storageFullColumnName());
        partQuery.addColumn(Part::versionFullColumnName());
        partQuery.addColumn(Part::datasizeFullColumnName());

        partQuery.addSortColumn(PimItem::idFullColumnName(), Query::Descending);

        if (!partList.isEmpty() || allPayload || allAttrs) {
            Query::Condition cond(Query::Or);
            for (const QByteArray &b : qAsConst(partList)) {
                if (b.startsWith("PLD") || b.startsWith("ATR")) {
                    cond.addValueCondition(Part::partTypeIdFullColumnName(), Query::Equals, PartTypeHelper::fromFqName(b).id());
                }
            }
            if (allPayload || allAttrs) {
                partQuery.addJoin(QueryBuilder::InnerJoin, PartType::tableName(), Part::partTypeIdFullColumnName(), PartType::idFullColumnName());
                if (allPayload) {
                    cond.addValueCondition(PartType::nsFullColumnName(), Query::Equals, QStringLiteral("PLD"));
                }
                if (allAttrs) {
                    cond.addValueCondition(PartType::nsFullColumnName(), Query::Equals, QStringLiteral("ATR"));
                }
            }

            partQuery.addCondition(cond);
        }

        ItemQueryHelper::scopeToQuery(mScope, mContext, partQuery);

        if (!partQuery.exec()) {
            throw HandlerException("Unable to list item parts");
        }
        partQuery.query().next();
    }

    return partQuery.query();
}

QSqlQuery ItemFetchHelper::buildItemQuery()
{
    QueryBuilder itemQuery(PimItem::tableName());

    int column = 0;
#define ADD_COLUMN(colName, colId) { itemQuery.addColumn( colName ); mItemQueryColumnMap[colId] = column++; }
    ADD_COLUMN(PimItem::idFullColumnName(), ItemQueryPimItemIdColumn);
    if (mItemFetchScope.fetchRemoteId()) {
        ADD_COLUMN(PimItem::remoteIdFullColumnName(), ItemQueryPimItemRidColumn)
    }
    ADD_COLUMN(PimItem::mimeTypeIdFullColumnName(), ItemQueryMimeTypeIdColumn)
    ADD_COLUMN(PimItem::revFullColumnName(), ItemQueryRevColumn)
    if (mItemFetchScope.fetchRemoteRevision()) {
        ADD_COLUMN(PimItem::remoteRevisionFullColumnName(), ItemQueryRemoteRevisionColumn)
    }
    if (mItemFetchScope.fetchSize()) {
        ADD_COLUMN(PimItem::sizeFullColumnName(), ItemQuerySizeColumn)
    }
    if (mItemFetchScope.fetchMTime()) {
        ADD_COLUMN(PimItem::datetimeFullColumnName(), ItemQueryDatetimeColumn)
    }
    ADD_COLUMN(PimItem::collectionIdFullColumnName(), ItemQueryCollectionIdColumn)
    if (mItemFetchScope.fetchGID()) {
        ADD_COLUMN(PimItem::gidFullColumnName(), ItemQueryPimItemGidColumn)
    }
#undef ADD_COLUMN

    itemQuery.addSortColumn(PimItem::idFullColumnName(), Query::Descending);

    ItemQueryHelper::scopeToQuery(mScope, mContext, itemQuery);

    if (mItemFetchScope.changedSince().isValid()) {
        itemQuery.addValueCondition(PimItem::datetimeFullColumnName(), Query::GreaterOrEqual, mItemFetchScope.changedSince().toUTC());
    }

    if (!itemQuery.exec()) {
        throw HandlerException("Unable to list items");
    }

    itemQuery.query().next();

    return itemQuery.query();
}

enum FlagQueryColumns {
    FlagQueryPimItemIdColumn,
    FlagQueryFlagIdColumn
};

QSqlQuery ItemFetchHelper::buildFlagQuery()
{
    QueryBuilder flagQuery(PimItem::tableName());
    flagQuery.addJoin(QueryBuilder::InnerJoin, PimItemFlagRelation::tableName(),
                      PimItem::idFullColumnName(), PimItemFlagRelation::leftFullColumnName());
    flagQuery.addColumn(PimItem::idFullColumnName());
    flagQuery.addColumn(PimItemFlagRelation::rightFullColumnName());

    ItemQueryHelper::scopeToQuery(mScope, mContext, flagQuery);
    flagQuery.addSortColumn(PimItem::idFullColumnName(), Query::Descending);

    if (!flagQuery.exec()) {
        throw HandlerException("Unable to retrieve item flags");
    }

    flagQuery.query().next();

    return flagQuery.query();
}

enum TagQueryColumns {
    TagQueryItemIdColumn,
    TagQueryTagIdColumn,
};

QSqlQuery ItemFetchHelper::buildTagQuery()
{
    QueryBuilder tagQuery(PimItem::tableName());
    tagQuery.addJoin(QueryBuilder::InnerJoin, PimItemTagRelation::tableName(),
                     PimItem::idFullColumnName(), PimItemTagRelation::leftFullColumnName());
    tagQuery.addJoin(QueryBuilder::InnerJoin, Tag::tableName(),
                     Tag::idFullColumnName(), PimItemTagRelation::rightFullColumnName());
    tagQuery.addColumn(PimItem::idFullColumnName());
    tagQuery.addColumn(Tag::idFullColumnName());

    ItemQueryHelper::scopeToQuery(mScope, mContext, tagQuery);
    tagQuery.addSortColumn(PimItem::idFullColumnName(), Query::Descending);

    if (!tagQuery.exec()) {
        throw HandlerException("Unable to retrieve item tags");
    }

    tagQuery.query().next();

    return tagQuery.query();
}

enum VRefQueryColumns {
    VRefQueryCollectionIdColumn,
    VRefQueryItemIdColumn
};

QSqlQuery ItemFetchHelper::buildVRefQuery()
{
    QueryBuilder vRefQuery(PimItem::tableName());
    vRefQuery.addJoin(QueryBuilder::LeftJoin, CollectionPimItemRelation::tableName(),
                      CollectionPimItemRelation::rightFullColumnName(),
                      PimItem::idFullColumnName());
    vRefQuery.addColumn(CollectionPimItemRelation::leftFullColumnName());
    vRefQuery.addColumn(CollectionPimItemRelation::rightFullColumnName());
    ItemQueryHelper::scopeToQuery(mScope, mContext, vRefQuery);
    vRefQuery.addSortColumn(PimItem::idFullColumnName(), Query::Descending);

    if (!vRefQuery.exec()) {
        throw HandlerException("Unable to retrieve virtual references");
    }

    vRefQuery.query().next();

    return vRefQuery.query();
}

bool ItemFetchHelper::isScopeLocal(const Scope &scope)
{
    // The only agent allowed to override local scope is the Baloo Indexer
    if (!mConnection->sessionId().startsWith("akonadi_indexing_agent")) {
        return false;
    }

    // Get list of all resources that own all items in the scope
    QueryBuilder qb(PimItem::tableName(), QueryBuilder::Select);
    qb.setDistinct(true);
    qb.addColumn(Resource::nameFullColumnName());
    qb.addJoin(QueryBuilder::LeftJoin, Collection::tableName(),
               PimItem::collectionIdFullColumnName(), Collection::idFullColumnName());
    qb.addJoin(QueryBuilder::LeftJoin, Resource::tableName(),
               Collection::resourceIdFullColumnName(), Resource::idFullColumnName());
    ItemQueryHelper::scopeToQuery(scope, mContext, qb);
    if (mContext->resource().isValid()) {
        qb.addValueCondition(Resource::nameFullColumnName(), Query::NotEquals,
                             mContext->resource().name());
    }

    if (!qb.exec()) {
        throw HandlerException("Failed to query database");
        return false;
    }

    // If there is more than one resource, i.e. this is a fetch from multiple
    // collections, then don't bother and just return FALSE. This case is aimed
    // specifically on Baloo, which fetches items from each collection independently,
    // so it will pass this check.
    QSqlQuery query = qb.query();
    if (query.size() != 1) {
        return false;
    }

    query.next();
    const QString resourceName = query.value(0).toString();
    query.finish();

    org::freedesktop::Akonadi::AgentManager manager(DBus::serviceName(DBus::Control),
            QStringLiteral("/AgentManager"),
            DBusConnectionPool::threadConnection());
    const QString typeIdentifier = manager.agentInstanceType(resourceName);
    const QVariantMap properties = manager.agentCustomProperties(typeIdentifier);
    return properties.value(QStringLiteral("HasLocalStorage"), false).toBool();
}

DataStore *ItemFetchHelper::storageBackend() const
{
    if (mConnection) {
        if (auto store = mConnection->storageBackend()) {
            return store;
        }
    }

    return DataStore::self();
}


bool ItemFetchHelper::fetchItems(std::function<void(Protocol::FetchItemsResponse &&)> &&itemCallback)
{
    BEGIN_TIMER(fetch)

    // retrieve missing parts
    // HACK: isScopeLocal() is a workaround for resources that have cache expiration
    // because when the cache expires, Baloo is not able to content of the items. So
    // we allow fetch of items that belong to local resources (like maildir) to ignore
    // cacheOnly and retrieve missing parts from the resource. However ItemRetriever
    // is painfully slow with many items and is generally designed to fetch a few
    // messages, not all of them. In the long term, we need a better way to do this.
    BEGIN_TIMER(itemRetriever)
    BEGIN_TIMER(scopeLocal)
#if ENABLE_FETCH_PROFILING
    double scopeLocalElapsed = 0;
#endif
    if (!mItemFetchScope.cacheOnly() || isScopeLocal(mScope)) {
#if ENABLE_FETCH_PROFILING
        scopeLocalElapsed = scopeLocalTimer.elapsed();
#endif

        // trigger a collection sync if configured to do so
        triggerOnDemandFetch();

        // Prepare for a call to ItemRetriever::exec();
        // From a resource perspective the only parts that can be fetched are payloads.
        ItemRetriever retriever(mConnection);
        retriever.setScope(mScope);
        retriever.setRetrieveParts(mItemFetchScope.requestedPayloads());
        retriever.setRetrieveFullPayload(mItemFetchScope.fullPayload());
        retriever.setChangedSince(mItemFetchScope.changedSince());
        if (!retriever.exec() && !mItemFetchScope.ignoreErrors()) {   // There we go, retrieve the missing parts from the resource.
            if (mContext->resource().isValid()) {
                throw HandlerException(QStringLiteral("Unable to fetch item from backend (collection %1, resource %2) : %3")
                                       .arg(mContext->collectionId())
                                       .arg(mContext->resource().id())
                                       .arg(QString::fromLatin1(retriever.lastError())));
            } else {
                throw HandlerException(QStringLiteral("Unable to fetch item from backend (collection %1) : %2")
                                       .arg(mContext->collectionId())
                                       .arg(QString::fromLatin1(retriever.lastError())));
            }
        }
    }
    END_TIMER(itemRetriever)

    BEGIN_TIMER(items)
    QSqlQuery itemQuery = buildItemQuery();
    END_TIMER(items)

    // error if query did not find any item and scope is not listing items but
    // a request for a specific item
    if (!itemQuery.isValid()) {
        if (mItemFetchScope.ignoreErrors()) {
            return true;
        }
        switch (mScope.scope()) {
        case Scope::Uid: // fall through
        case Scope::Rid: // fall through
        case Scope::HierarchicalRid: // fall through
        case Scope::Gid:
            throw HandlerException("Item query returned empty result set");
            break;
        default:
            break;
        }
    }
    // build part query if needed
    BEGIN_TIMER(parts)
    QSqlQuery partQuery(storageBackend()->database());
    if (!mItemFetchScope.requestedParts().isEmpty() || mItemFetchScope.fullPayload() || mItemFetchScope.allAttributes()) {
        partQuery = buildPartQuery(mItemFetchScope.requestedParts(), mItemFetchScope.fullPayload(), mItemFetchScope.allAttributes());
    }
    END_TIMER(parts)

    // build flag query if needed
    BEGIN_TIMER(flags)
    QSqlQuery flagQuery(storageBackend()->database());
    if (mItemFetchScope.fetchFlags()) {
        flagQuery = buildFlagQuery();
    }
    END_TIMER(flags)

    // build tag query if needed
    BEGIN_TIMER(tags)
    QSqlQuery tagQuery(storageBackend()->database());
    if (mItemFetchScope.fetchTags()) {
        tagQuery = buildTagQuery();
    }
    END_TIMER(tags)

    BEGIN_TIMER(vRefs)
    QSqlQuery vRefQuery(storageBackend()->database());
    if (mItemFetchScope.fetchVirtualReferences()) {
        vRefQuery = buildVRefQuery();
    }
    END_TIMER(vRefs)

#if ENABLE_FETCH_PROFILING
    int itemsCount = 0;
    int flagsCount = 0;
    int partsCount = 0;
    int tagsCount = 0;
    int vRefsCount = 0;
#endif

    BEGIN_TIMER(processing)
    QHash<qint64, QByteArray> flagIdNameCache;
    QHash<qint64, QString> mimeTypeIdNameCache;
    QHash<qint64, QByteArray> partTypeIdNameCache;
    while (itemQuery.isValid()) {
        PROF_INC(itemsCount)

        const qint64 pimItemId = extractQueryResult(itemQuery, ItemQueryPimItemIdColumn).toLongLong();
        const int pimItemRev = extractQueryResult(itemQuery, ItemQueryRevColumn).toInt();

        Protocol::FetchItemsResponse response;
        response.setId(pimItemId);
        response.setRevision(pimItemRev);
        const qint64 mimeTypeId = extractQueryResult(itemQuery, ItemQueryMimeTypeIdColumn).toLongLong();
        auto mtIter = mimeTypeIdNameCache.find(mimeTypeId);
        if (mtIter == mimeTypeIdNameCache.end()) {
            mtIter = mimeTypeIdNameCache.insert(mimeTypeId, MimeType::retrieveById(mimeTypeId).name());
        }
        response.setMimeType(mtIter.value());
        if (mItemFetchScope.fetchRemoteId()) {
            response.setRemoteId(extractQueryResult(itemQuery, ItemQueryPimItemRidColumn).toString());
        }
        response.setParentId(extractQueryResult(itemQuery, ItemQueryCollectionIdColumn).toLongLong());

        if (mItemFetchScope.fetchSize()) {
            response.setSize(extractQueryResult(itemQuery, ItemQuerySizeColumn).toLongLong());
        }
        if (mItemFetchScope.fetchMTime()) {
            response.setMTime(Utils::variantToDateTime(extractQueryResult(itemQuery, ItemQueryDatetimeColumn)));
        }
        if (mItemFetchScope.fetchRemoteRevision()) {
            response.setRemoteRevision(extractQueryResult(itemQuery, ItemQueryRemoteRevisionColumn).toString());
        }
        if (mItemFetchScope.fetchGID()) {
            response.setGid(extractQueryResult(itemQuery, ItemQueryPimItemGidColumn).toString());
        }

        if (mItemFetchScope.fetchFlags()) {
            QVector<QByteArray> flags;
            while (flagQuery.isValid()) {
                const qint64 id = flagQuery.value(FlagQueryPimItemIdColumn).toLongLong();
                if (id > pimItemId) {
                    flagQuery.next();
                    continue;
                } else if (id < pimItemId) {
                    break;
                }
                const qint64 flagId = flagQuery.value(FlagQueryFlagIdColumn).toLongLong();
                auto flagNameIter = flagIdNameCache.find(flagId);
                if (flagNameIter == flagIdNameCache.end()) {
                    flagNameIter = flagIdNameCache.insert(flagId, Flag::retrieveById(flagId).name().toUtf8());
                }
                flags << flagNameIter.value();
                flagQuery.next();
            }
            response.setFlags(flags);
        }

        if (mItemFetchScope.fetchTags()) {
            QVector<qint64> tagIds;
            QVector<Protocol::FetchTagsResponse> tags;
            while (tagQuery.isValid()) {
                PROF_INC(tagsCount)
                const qint64 id = tagQuery.value(TagQueryItemIdColumn).toLongLong();
                if (id > pimItemId) {
                    tagQuery.next();
                    continue;
                } else if (id < pimItemId) {
                    break;
                }
                tagIds << tagQuery.value(TagQueryTagIdColumn).toLongLong();
                tagQuery.next();
            }

            if (mTagFetchScope.fetchIdOnly()) {
                tags = tagIds | transform([](const auto tagId) {
                                        Protocol::FetchTagsResponse resp;
                                        resp.setId(tagId);
                                        return resp;
                                    })
                              | toQVector;
            } else {
                tags = tagIds | transform([this](const auto tagId) {
                                    return HandlerHelper::fetchTagsResponse(Tag::retrieveById(tagId), mTagFetchScope, mConnection);
                                })
                              | toQVector;
            }
            response.setTags(tags);
        }

        if (mItemFetchScope.fetchVirtualReferences()) {
            QVector<qint64> vRefs;
            while (vRefQuery.isValid()) {
                PROF_INC(vRefsCount)
                const qint64 id = vRefQuery.value(VRefQueryItemIdColumn).toLongLong();
                if (id > pimItemId) {
                    vRefQuery.next();
                    continue;
                } else if (id < pimItemId) {
                    break;
                }
                vRefs << vRefQuery.value(VRefQueryCollectionIdColumn).toLongLong();
                vRefQuery.next();
            }
            response.setVirtualReferences(vRefs);
        }

        if (mItemFetchScope.fetchRelations()) {
            SelectQueryBuilder<Relation> qb;
            Query::Condition condition;
            condition.setSubQueryMode(Query::Or);
            condition.addValueCondition(Relation::leftIdFullColumnName(), Query::Equals, pimItemId);
            condition.addValueCondition(Relation::rightIdFullColumnName(), Query::Equals, pimItemId);
            qb.addCondition(condition);
            qb.addGroupColumns(QStringList() << Relation::leftIdColumn() << Relation::rightIdColumn() << Relation::typeIdColumn() << Relation::remoteIdColumn());
            if (!qb.exec()) {
                throw HandlerException("Unable to list item relations");
            }
            QVector<Protocol::FetchRelationsResponse> relations;
            const auto result = qb.result();
            relations.reserve(result.size());
            for (const Relation &rel : result) {
                relations.push_back(HandlerHelper::fetchRelationsResponse(rel));;
            }
            response.setRelations(relations);
        }

        if (mItemFetchScope.ancestorDepth() != Protocol::ItemFetchScope::NoAncestor) {
            response.setAncestors(ancestorsForItem(response.parentId()));
        }

        bool skipItem = false;

        QVector<QByteArray> cachedParts;
        QVector<Protocol::StreamPayloadResponse> parts;
        while (partQuery.isValid()) {
            PROF_INC(partsCount)
            const qint64 id = partQuery.value(PartQueryPimIdColumn).toLongLong();
            if (id > pimItemId) {
                partQuery.next();
                continue;
            } else if (id < pimItemId) {
                break;
            }

            const qint64 partTypeId = partQuery.value(PartQueryTypeIdColumn).toLongLong();
            auto ptIter = partTypeIdNameCache.find(partTypeId);
            if (ptIter == partTypeIdNameCache.end()) {
                ptIter = partTypeIdNameCache.insert(partTypeId, PartTypeHelper::fullName(PartType::retrieveById(partTypeId)).toUtf8());
            }
            Protocol::PartMetaData metaPart;
            Protocol::StreamPayloadResponse partData;
            partData.setPayloadName(ptIter.value());
            metaPart.setName(ptIter.value());
            metaPart.setVersion(partQuery.value(PartQueryVersionColumn).toInt());
            metaPart.setSize(partQuery.value(PartQueryDataSizeColumn).toLongLong());

            const QByteArray data = Utils::variantToByteArray(partQuery.value(PartQueryDataColumn));
            if (mItemFetchScope.checkCachedPayloadPartsOnly()) {
                if (!data.isEmpty()) {
                    cachedParts << ptIter.value();
                }
                partQuery.next();
            } else {
                if (mItemFetchScope.ignoreErrors() && data.isEmpty()) {
                    //We wanted the payload, couldn't get it, and are ignoring errors. Skip the item.
                    //This is not an error though, it's fine to have empty payload parts (to denote existing but not cached parts)
                    qCDebug(AKONADISERVER_LOG) << "item" << id << "has an empty payload part in parttable for part" << metaPart.name();
                    skipItem = true;
                    break;
                }
                metaPart.setStorageType(static_cast<Protocol::PartMetaData::StorageType>(
                    partQuery.value(PartQueryStorageColumn).toInt()));
                if (data.isEmpty()) {
                    partData.setData(QByteArray(""));
                } else {
                    partData.setData(data);
                }
                partData.setMetaData(metaPart);

                if (mItemFetchScope.requestedParts().contains(ptIter.value()) || mItemFetchScope.fullPayload() || mItemFetchScope.allAttributes()) {
                    parts.append(partData);
                }

                partQuery.next();
            }
        }
        response.setParts(parts);

        if (skipItem) {
            itemQuery.next();
            continue;
        }

        if (mItemFetchScope.checkCachedPayloadPartsOnly()) {
            response.setCachedParts(cachedParts);
        }

        if (itemCallback) {
            itemCallback(std::move(response));
        } else {
            mConnection->sendResponse(std::move(response));
        }

        itemQuery.next();
    }
    tagQuery.finish();
    flagQuery.finish();
    partQuery.finish();
    vRefQuery.finish();
    itemQuery.finish();
    END_TIMER(processing)

    // update atime (only if the payload was actually requested, otherwise a simple resource sync prevents cache clearing)
    BEGIN_TIMER(aTime)
    if (mUpdateATimeEnabled &&
            (needsAccessTimeUpdate(mItemFetchScope.requestedParts()) || mItemFetchScope.fullPayload())) {
        updateItemAccessTime();
    }
    END_TIMER(aTime)

    END_TIMER(fetch)
#if ENABLE_FETCH_PROFILING
    qCDebug(AKONADISERVER_LOG) << "ItemFetchHelper execution stats:";
    qCDebug(AKONADISERVER_LOG) << "\tItems query:" << itemsElapsed << "ms," << itemsCount << " items in total";
    qCDebug(AKONADISERVER_LOG) << "\tFlags query:" << flagsElapsed << "ms, " << flagsCount << " flags in total";
    qCDebug(AKONADISERVER_LOG) << "\tParts query:" << partsElapsed << "ms, " << partsCount << " parts in total";
    qCDebug(AKONADISERVER_LOG) << "\tTags query: " << tagsElapsed << "ms, " << tagsCount << " tags in total";
    qCDebug(AKONADISERVER_LOG) << "\tVRefs query:" << vRefsElapsed << "ms, " << vRefsCount << " vRefs in total";
    qCDebug(AKONADISERVER_LOG) << "\t------------";
    qCDebug(AKONADISERVER_LOG) << "\tItem retriever:" << itemRetrieverElapsed << "ms (scope local:" << scopeLocalElapsed << "ms)";
    qCDebug(AKONADISERVER_LOG) << "\tTotal query:" << (itemsElapsed + flagsElapsed + partsElapsed + tagsElapsed + vRefsElapsed) << "ms";
    qCDebug(AKONADISERVER_LOG) << "\tTotal processing: " << processingElapsed << "ms";
    qCDebug(AKONADISERVER_LOG) << "\tATime update:" << aTimeElapsed << "ms";
    qCDebug(AKONADISERVER_LOG) << "\t============";
    qCDebug(AKONADISERVER_LOG) << "\tTotal FETCH:" << fetchElapsed << "ms";
    qCDebug(AKONADISERVER_LOG);
    qCDebug(AKONADISERVER_LOG);
#endif

    return true;
}

bool ItemFetchHelper::needsAccessTimeUpdate(const QVector<QByteArray> &parts)
{
    // TODO technically we should compare the part list with the cache policy of
    // the parent collection of the retrieved items, but that's kinda expensive
    // Only updating the atime if the full payload was requested is a good
    // approximation though.
    return parts.contains(AKONADI_PARAM_PLD_RFC822);
}

void ItemFetchHelper::updateItemAccessTime()
{
    Transaction transaction(storageBackend(), QStringLiteral("update atime"));
    QueryBuilder qb(PimItem::tableName(), QueryBuilder::Update);
    qb.setColumnValue(PimItem::atimeColumn(), QDateTime::currentDateTimeUtc());
    ItemQueryHelper::scopeToQuery(mScope, mContext, qb);

    if (!qb.exec()) {
        qCWarning(AKONADISERVER_LOG) << "Unable to update item access time";
    } else {
        transaction.commit();
    }
}

void ItemFetchHelper::triggerOnDemandFetch()
{
    if (mContext->collectionId() <= 0 || mItemFetchScope.cacheOnly()) {
        return;
    }

    Collection collection = mContext->collection();

    // HACK: don't trigger on-demand syncing if the resource is the one triggering it
    if (mConnection->sessionId() == collection.resource().name().toLatin1()) {
        return;
    }

    storageBackend()->activeCachePolicy(collection);
    if (!collection.cachePolicySyncOnDemand()) {
        return;
    }

    AkonadiServer::instance()->intervalChecker()->requestCollectionSync(collection);
}

QVector<Protocol::Ancestor> ItemFetchHelper::ancestorsForItem(Collection::Id parentColId)
{
    if (mItemFetchScope.ancestorDepth() == Protocol::ItemFetchScope::NoAncestor || parentColId == 0) {
        return QVector<Protocol::Ancestor>();
    }
    const auto it = mAncestorCache.constFind(parentColId);
    if (it != mAncestorCache.cend()) {
        return *it;
    }

    QVector<Protocol::Ancestor> ancestors;
    Collection col = Collection::retrieveById(parentColId);
    const int depthNum = mItemFetchScope.ancestorDepth() == Protocol::ItemFetchScope::ParentAncestor ? 1 : INT_MAX;
    for (int i = 0; i < depthNum; ++i) {
        if (!col.isValid()) {
            Protocol::Ancestor ancestor;
            ancestor.setId(0);
            ancestors << ancestor;
            break;
        }
        Protocol::Ancestor ancestor;
        ancestor.setId(col.id());
        ancestor.setRemoteId(col.remoteId());
        ancestors << ancestor;
        col = col.parent();
    }
    mAncestorCache.insert(parentColId, ancestors);
    return ancestors;
}

QVariant ItemFetchHelper::extractQueryResult(const QSqlQuery &query,
                                             ItemFetchHelper::ItemQueryColumns column) const
{
    const int colId = mItemQueryColumnMap[column];
    Q_ASSERT(colId >= 0);
    return query.value(colId);
}
