/*
    Copyright (c) 2008 Stephen Kelly <steveire@gmail.com>

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

#ifndef ENTITYTREEMODELPRIVATE_H
#define ENTITYTREEMODELPRIVATE_H

#include <KJob>

#include "item.h"
#include "collectionfetchjob.h"
#include "itemfetchscope.h"
#include "mimetypechecker.h"

#include "entitytreemodel.h"

#include "akonaditests_export.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(DebugETM)

namespace Akonadi
{
class Monitor;
class AgentInstance;
}

struct Node {
    typedef qint64 Id;

    Id id;
    Akonadi::Collection::Id parent;

    enum Type {
        Item,
        Collection
    };

    int type;
};

template<typename Key, typename Value>
class RefCountedHash
{
    mutable Value *defaultValue = nullptr;
public:
    inline ~RefCountedHash()
    {
        delete defaultValue;
    }

    inline auto begin() { return mHash.begin(); }
    inline auto end() { return mHash.end(); }
    inline auto begin() const { return mHash.begin(); }
    inline auto end() const { return mHash.end(); }
    inline auto find(const Key &key) const { return mHash.find(key); }
    inline auto find(const Key &key) { return mHash.find(key); }

    inline bool size() const { return mHash.size(); }
    inline bool isEmpty() const { return mHash.isEmpty(); }

    inline void clear() { mHash.clear(); }
    inline bool contains(const Key &key) const { return mHash.contains(key); }

    inline const Value &value(const Key &key) const
    {
        auto it = mHash.find(key);
        if (it == mHash.end()) {
            return defaultValue ? *defaultValue : *(defaultValue = new Value());
        }
        return it->value;
    }

    inline const Value &operator[](const Key &key) const
    {
        return value(key);
    }

    inline auto ref(const Key &key, const Value &value)
    {
        auto it = mHash.find(key);
        if (it != mHash.end()) {
            ++(it->refCnt);
            return it;
        } else {
            return mHash.insert(key, {1, std::move(value)});
        }
    }

    inline void unref(const Key &key)
    {
        auto it = mHash.find(key);
        if (it == mHash.end()) {
            return;
        }
        --(it->refCnt);
        if (it->refCnt == 0) {
            mHash.erase(it);
        }
    }


private:
    template<typename V>
    struct RefCountedValue {
        uint8_t refCnt = 0;
        V value;
    };
    QHash<Key, RefCountedValue<Value>> mHash;
};

namespace Akonadi
{
/**
 * @internal
 */
class AKONADI_TESTS_EXPORT EntityTreeModelPrivate
{
public:

    explicit EntityTreeModelPrivate(EntityTreeModel *parent);
    ~EntityTreeModelPrivate();
    EntityTreeModel *const q_ptr;

    enum RetrieveDepth {
        Base,
        Recursive
    };

    void init(Monitor *monitor);

    void fetchCollections(const Collection &collection, CollectionFetchJob::Type type = CollectionFetchJob::FirstLevel);
    void fetchCollections(const Collection::List &collections, CollectionFetchJob::Type type = CollectionFetchJob::FirstLevel);
    void fetchCollections(Akonadi::CollectionFetchJob *job);
    void fetchItems(const Collection &collection);
    void collectionsFetched(const Akonadi::Collection::List &collections);
    void collectionListFetched(const Akonadi::Collection::List &collections);
    void itemsFetched(const Akonadi::Item::List &items);
    void itemsFetched(const Collection::Id collectionId, const Akonadi::Item::List &items);

    void monitoredCollectionAdded(const Akonadi::Collection &collection, const Akonadi::Collection &parent);
    void monitoredCollectionRemoved(const Akonadi::Collection &collection);
    void monitoredCollectionChanged(const Akonadi::Collection &collection);
    void monitoredCollectionStatisticsChanged(Akonadi::Collection::Id, const Akonadi::CollectionStatistics &statistics);
    void monitoredCollectionMoved(const Akonadi::Collection &collection,
                                  const Akonadi::Collection &sourceCollection,
                                  const Akonadi::Collection &destCollection);

    void monitoredItemAdded(const Akonadi::Item &item, const Akonadi::Collection &collection);
    void monitoredItemRemoved(const Akonadi::Item &item, const Akonadi::Collection &collection = Akonadi::Collection());
    void monitoredItemChanged(const Akonadi::Item &item, const QSet<QByteArray> &);
    void monitoredItemMoved(const Akonadi::Item &item, const Akonadi::Collection &, const Akonadi::Collection &);

    void monitoredItemLinked(const Akonadi::Item &item, const Akonadi::Collection &);
    void monitoredItemUnlinked(const Akonadi::Item &item, const Akonadi::Collection &);

    void monitoredMimeTypeChanged(const QString &mimeType, bool monitored);
    void monitoredCollectionsChanged(const Akonadi::Collection &collection, bool monitored);
    void monitoredItemsChanged(const Akonadi::Item &item, bool monitored);
    void monitoredResourcesChanged(const QByteArray &resource, bool monitored);

    Collection::List getParentCollections(const Item &item) const;
    void removeChildEntities(Collection::Id collectionId);

    /**
     * Returns the list of names of the child collections of @p collection.
     */
    QStringList childCollectionNames(const Collection &collection) const;

    /**
     * Fetch parent collections and insert this @p collection and its parents into the node tree
     */
    void retrieveAncestors(const Akonadi::Collection &collection, bool insertBaseCollection = true);
    void ancestorsFetched(const Akonadi::Collection::List &collectionList);
    void insertCollection(const Akonadi::Collection &collection, const Akonadi::Collection &parent);

    void beginResetModel();
    void endResetModel();
    /**
     * Start function for filling the Model, finds and fetches the root of the node tree
     * Next relevant function for filling the model is startFirstListJob()
     */
    void fillModel();

    void changeFetchState(const Collection &parent);
    void agentInstanceRemoved(const Akonadi::AgentInstance &instace);

    QIcon iconForName(const QString &name) const;

    QHash<Collection::Id, Collection> m_collections;
    RefCountedHash<Item::Id, Item> m_items;
    QHash<Collection::Id, QList<Node *> > m_childEntities;
    QSet<Collection::Id> m_populatedCols;
    QSet<Collection::Id> m_collectionsWithoutItems;

    QVector<Item::Id> m_pendingCutItems;
    QVector<Item::Id> m_pendingCutCollections;
    mutable QSet<Collection::Id> m_pendingCollectionRetrieveJobs;
    mutable QSet<KJob *> m_pendingCollectionFetchJobs;

    // Icon cache to workaround QIcon::fromTheme being very slow (bug #346644)
    mutable QHash<QString, QIcon> m_iconCache;
    mutable QString m_iconThemeName;

    Monitor *m_monitor = nullptr;
    Collection m_rootCollection;
    Node *m_rootNode = nullptr;
    bool m_needDeleteRootNode = false;
    QString m_rootCollectionDisplayName;
    QStringList m_mimeTypeFilter;
    MimeTypeChecker m_mimeChecker;
    EntityTreeModel::CollectionFetchStrategy m_collectionFetchStrategy;
    EntityTreeModel::ItemPopulationStrategy m_itemPopulation;
    CollectionFetchScope::ListFilter m_listFilter;
    bool m_includeStatistics;
    bool m_showRootCollection;
    bool m_collectionTreeFetched;

    /**
     * Called after the root collection was fetched by fillModel
     *
     * Initiates further fetching of collections depending on the monitored collections
     * (in the monitor) and the m_collectionFetchStrategy.
     *
     * Further collections are either fetched directly with fetchCollections and
     * fetchItems or, in case that collections or resources are monitored explicitly
     * via fetchTopLevelCollections
     */
    void startFirstListJob();

    void serverStarted();

    void monitoredItemsRetrieved(KJob *job);
    void rootFetchJobDone(KJob *job);
    void collectionFetchJobDone(KJob *job);
    void itemFetchJobDone(KJob *job);
    void updateJobDone(KJob *job);
    void pasteJobDone(KJob *job);

    /**
     * Returns the index of the node in @p list with the id @p id. Returns -1 if not found.
     */
    template<Node::Type Type>
    int indexOf(const QList<Node *> &nodes, Node::Id id) const
    {
        int i = 0;
        for (const Node *node : nodes) {
            if (node->id == id && node->type == Type) {
                return i;
            }
            i++;
        }

        return -1;
    }

    /**
     * The id of the collection which starts an item fetch job. This is part of a hack with QObject::sender
     * in itemsReceivedFromJob to correctly insert items into the model.
     */
    static QByteArray FetchCollectionId()
    {
        return "FetchCollectionId";
    }

    Session *m_session = nullptr;

    Q_DECLARE_PUBLIC(EntityTreeModel)

    void fetchTopLevelCollections() const;
    void topLevelCollectionsFetched(const Akonadi::Collection::List &collectionList);

    /**
      @returns True if @p item or one of its descendants is hidden.
    */
    bool isHidden(const Item &item) const;
    bool isHidden(const Collection &collection) const;

    template<typename T>
    bool isHiddenImpl(const T &entity, Node::Type type) const;

    bool m_showSystemEntities;

    void ref(Collection::Id id);
    void deref(Collection::Id id);

    /**
     * @returns true if the collection is actively monitored (referenced or buffered with refcounting enabled)
     *
     * purely for testing
     */
    bool isMonitored(Collection::Id id);

    /**
     * @returns true if the collection is buffered
     *
     * purely for testing
     */
    bool isBuffered(Collection::Id id);

    /**
      @returns true if the Collection with the id of @p id should be purged.
    */
    bool shouldPurge(Collection::Id id);

    /**
      Purges the items in the Collection @p id
    */
    void purgeItems(Collection::Id id);

    /**
      Removes the items starting from @p it and up to a maximum of @p end in Collection @p col. @p pos should be the index of @p it
      in the m_childEntities before calling, and is updated to the position of the next Collection in m_childEntities afterward.
      This is required to emit model remove signals properly.

      @returns an iterator pointing to the next Collection after @p it, or at @p end
    */
    QList<Node *>::iterator removeItems(QList<Node *>::iterator it, QList<Node *>::iterator end,
                                        int *pos, const Collection &col);

    /**
      Skips over Collections in m_childEntities up to a maximum of @p end. @p it is an iterator pointing to the first Collection
      in a block of Collections, and @p pos initially describes the index of @p it in m_childEntities and is updated to point to
      the index of the next Item in the list.

      @returns an iterator pointing to the next Item after @p it, or at @p end
    */
    QList<Node *>::iterator skipCollections(QList<Node *>::iterator it, QList<Node *>::iterator end, int *pos);

    /**
      Emits the data changed signal for the entire row as in the subclass, instead of just for the first column.
    */
    void dataChanged(const QModelIndex &top, const QModelIndex &bottom);

    /**
    * Returns the model index for the given @p collection.
    */
    QModelIndex indexForCollection(const Collection &collection) const;

    /**
    * Returns the model indexes for the given @p item.
    */
    QModelIndexList indexesForItem(const Item &item) const;

    bool canFetchMore(const QModelIndex &parent) const;

    /**
     * Returns true if the collection matches all filters and should be part of the model.
     * This method checks all properties that could change by modifying the collection.
     * Currently that includes:
     * * hidden attribute
     * * content mime types
     */
    bool shouldBePartOfModel(const Collection &collection) const;
    bool hasChildCollection(const Collection &collection) const;
    bool isAncestorMonitored(const Collection &collection) const;
};

}

#endif
