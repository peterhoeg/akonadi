/*
    Copyright (c) 2006 Till Adam <adam@kde.org>
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

#include "resourcebase.h"
#include "agentbase_p.h"


#include "resourceadaptor.h"
#include "collectiondeletejob.h"
#include "collectionsync_p.h"
#include "KDBusConnectionPool"
#include "itemsync.h"
#include "akonadi_version.h"
#include "tagsync.h"
#include "relationsync.h"
#include "resourcescheduler_p.h"
#include "tracerinterface.h"

#include "changerecorder.h"
#include "collectionfetchjob.h"
#include "collectionfetchscope.h"
#include "collectionmodifyjob.h"
#include "invalidatecachejob_p.h"
#include "itemfetchjob.h"
#include "itemfetchscope.h"
#include "itemmodifyjob.h"
#include "itemmodifyjob_p.h"
#include "itemcreatejob.h"
#include "session.h"
#include "resourceselectjob_p.h"
#include "monitor_p.h"
#include "servermanager_p.h"
#include "recursivemover_p.h"
#include "tagmodifyjob.h"
#include "specialcollectionattribute.h"
#include "favoritecollectionattribute.h"

#include "akonadiagentbase_debug.h"
#include <KLocalizedString>
#include <KAboutData>

#include <QHash>
#include <QTimer>
#include <QApplication>

using namespace Akonadi;

class Akonadi::ResourceBasePrivate : public AgentBasePrivate
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.dfaure")

public:
    ResourceBasePrivate(ResourceBase *parent)
        : AgentBasePrivate(parent)
        , scheduler(nullptr)
        , mItemSyncer(nullptr)
        , mItemSyncFetchScope(nullptr)
        , mItemTransactionMode(ItemSync::SingleTransaction)
        , mItemMergeMode(ItemSync::RIDMerge)
        , mCollectionSyncer(nullptr)
        , mTagSyncer(nullptr)
        , mRelationSyncer(nullptr)
        , mHierarchicalRid(false)
        , mUnemittedProgress(0)
        , mAutomaticProgressReporting(true)
        , mDisableAutomaticItemDeliveryDone(false)
        , mItemSyncBatchSize(10)
        , mCurrentCollectionFetchJob(nullptr)
        , mScheduleAttributeSyncBeforeCollectionSync(false)
    {
        Internal::setClientType(Internal::Resource);
        mStatusMessage = defaultReadyMessage();
        mProgressEmissionCompressor.setInterval(1000);
        mProgressEmissionCompressor.setSingleShot(true);
        // HACK: skip local changes of the EntityDisplayAttribute by default. Remove this for KDE5 and adjust resource implementations accordingly.
        mKeepLocalCollectionChanges << "ENTITYDISPLAY";
    }

    ~ResourceBasePrivate() override
    {
        delete mItemSyncFetchScope;
    }

    Q_DECLARE_PUBLIC(ResourceBase)

    void delayedInit() override {
        const QString serviceId = ServerManager::agentServiceName(ServerManager::Resource, mId);
        if (!KDBusConnectionPool::threadConnection().registerService(serviceId))
        {
            QString reason = KDBusConnectionPool::threadConnection().lastError().message();
            if (reason.isEmpty()) {
                reason = QStringLiteral("this service is probably running already.");
            }
            qCCritical(AKONADIAGENTBASE_LOG) << "Unable to register service" << serviceId << "at D-Bus:" << reason;

            if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
                QCoreApplication::instance()->exit(1);
            }

        } else {
            AgentBasePrivate::delayedInit();
        }
    }

    void changeProcessed() override {
        if (m_recursiveMover)
        {
            m_recursiveMover->changeProcessed();
            QTimer::singleShot(0, m_recursiveMover.data(), &RecursiveMover::replayNext);
            return;
        }

        mChangeRecorder->changeProcessed();
        if (!mChangeRecorder->isEmpty())
        {
            scheduler->scheduleChangeReplay();
        }
        scheduler->taskDone();
    }

    void slotAbortRequested();

    void slotDeliveryDone(KJob *job);
    void slotCollectionSyncDone(KJob *job);
    void slotLocalListDone(KJob *job);
    void slotSynchronizeCollection(const Collection &col);
    void slotItemRetrievalCollectionFetchDone(KJob *job);
    void slotCollectionListDone(KJob *job);
    void slotSynchronizeCollectionAttributes(const Collection &col);
    void slotCollectionListForAttributesDone(KJob *job);
    void slotCollectionAttributesSyncDone(KJob *job);
    void slotSynchronizeTags();
    void slotSynchronizeRelations();
    void slotAttributeRetrievalCollectionFetchDone(KJob *job);

    void slotItemSyncDone(KJob *job);

    void slotPercent(KJob *job, unsigned long percent);
    void slotDelayedEmitProgress();
    void slotDeleteResourceCollection();
    void slotDeleteResourceCollectionDone(KJob *job);
    void slotCollectionDeletionDone(KJob *job);

    void slotInvalidateCache(const Akonadi::Collection &collection);

    void slotPrepareItemRetrieval(const Akonadi::Item &item);
    void slotPrepareItemRetrievalResult(KJob *job);

    void slotPrepareItemsRetrieval(const QVector<Akonadi::Item> &item);
    void slotPrepareItemsRetrievalResult(KJob *job);

    void changeCommittedResult(KJob *job);

    void slotRecursiveMoveReplay(RecursiveMover *mover);
    void slotRecursiveMoveReplayResult(KJob *job);

    void slotTagSyncDone(KJob *job);
    void slotRelationSyncDone(KJob *job);

    void slotSessionReconnected()
    {
        Q_Q(ResourceBase);

        new ResourceSelectJob(q->identifier());
    }

    void createItemSyncInstanceIfMissing()
    {
        Q_Q(ResourceBase);
        Q_ASSERT_X(scheduler->currentTask().type == ResourceScheduler::SyncCollection,
                   "createItemSyncInstance", "Calling items retrieval methods although no item retrieval is in progress");
        if (!mItemSyncer) {
            mItemSyncer = new ItemSync(q->currentCollection());
            mItemSyncer->setTransactionMode(mItemTransactionMode);
            mItemSyncer->setBatchSize(mItemSyncBatchSize);
            mItemSyncer->setMergeMode(mItemMergeMode);
            if (mItemSyncFetchScope) {
                mItemSyncer->setFetchScope(*mItemSyncFetchScope);
            }
            mItemSyncer->setDisableAutomaticDeliveryDone(mDisableAutomaticItemDeliveryDone);
            mItemSyncer->setProperty("collection", QVariant::fromValue(q->currentCollection()));
            connect(mItemSyncer, SIGNAL(percent(KJob*,ulong)), q, SLOT(slotPercent(KJob*,ulong)));
            connect(mItemSyncer, SIGNAL(result(KJob*)), q, SLOT(slotItemSyncDone(KJob*)));
            connect(mItemSyncer, &ItemSync::readyForNextBatch, q, &ResourceBase::retrieveNextItemSyncBatch);
        }
        Q_ASSERT(mItemSyncer);
    }

public Q_SLOTS:
    // Dump the state of the scheduler
    Q_SCRIPTABLE QString dumpToString() const
    {
        Q_Q(const ResourceBase);
        return scheduler->dumpToString() + QLatin1Char('\n') + q->dumpResourceToString();
    }

    Q_SCRIPTABLE void dump()
    {
        scheduler->dump();
    }

    Q_SCRIPTABLE void clear()
    {
        scheduler->clear();
    }

protected Q_SLOTS:
    // reimplementations from AgentbBasePrivate, containing sanity checks that only apply to resources
    // such as making sure that RIDs are present as well as translations of cross-resource moves
    // TODO: we could possibly add recovery code for no-RID notifications by re-enquing those to the change recorder
    // as the corresponding Add notifications, although that contains a risk of endless fail/retry loops

    void itemAdded(const Akonadi::Item &item, const Akonadi::Collection &collection) override {
        if (collection.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::itemAdded(item, collection);
    }

    void itemChanged(const Akonadi::Item &item, const QSet<QByteArray> &partIdentifiers) override {
        if (item.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::itemChanged(item, partIdentifiers);
    }

    void itemsFlagsChanged(const Item::List &items, const QSet<QByteArray> &addedFlags,
                           const QSet<QByteArray> &removedFlags) override {
        if (addedFlags.isEmpty() && removedFlags.isEmpty())
        {
            changeProcessed();
            return;
        }

        Item::List validItems;
        for (const Akonadi::Item &item : items)
        {
            if (!item.remoteId().isEmpty()) {
                validItems << item;
            }
        }
        if (validItems.isEmpty())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::itemsFlagsChanged(validItems, addedFlags, removedFlags);
    }

    void itemsTagsChanged(const Item::List &items, const QSet<Tag> &addedTags, const QSet<Tag> &removedTags) override {
        if (addedTags.isEmpty() && removedTags.isEmpty())
        {
            changeProcessed();
            return;
        }

        Item::List validItems;
        for (const Akonadi::Item &item : items)
        {
            if (!item.remoteId().isEmpty()) {
                validItems << item;
            }
        }
        if (validItems.isEmpty())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::itemsTagsChanged(validItems, addedTags, removedTags);
    }

    // TODO move the move translation code from AgentBasePrivate here, it's wrong for agents
    void itemMoved(const Akonadi::Item &item, const Akonadi::Collection &source, const Akonadi::Collection &destination) override {
        if (item.remoteId().isEmpty() || destination.remoteId().isEmpty() || destination == source)
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::itemMoved(item, source, destination);
    }

    void itemsMoved(const Item::List &items, const Collection &source, const Collection &destination) override {
        if (destination.remoteId().isEmpty() || destination == source)
        {
            changeProcessed();
            return;
        }

        Item::List validItems;
        for (const Akonadi::Item &item : items)
        {
            if (!item.remoteId().isEmpty()) {
                validItems << item;
            }
        }
        if (validItems.isEmpty())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::itemsMoved(validItems, source, destination);
    }

    void itemRemoved(const Akonadi::Item &item) override {
        if (item.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::itemRemoved(item);
    }

    void itemsRemoved(const Item::List &items) override {
        Item::List validItems;
        for (const Akonadi::Item &item : items)
        {
            if (!item.remoteId().isEmpty()) {
                validItems << item;
            }
        }
        if (validItems.isEmpty())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::itemsRemoved(validItems);
    }

    void collectionAdded(const Akonadi::Collection &collection, const Akonadi::Collection &parent) override {
        if (parent.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::collectionAdded(collection, parent);
    }

    void collectionChanged(const Akonadi::Collection &collection) override {
        if (collection.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::collectionChanged(collection);
    }

    void collectionChanged(const Akonadi::Collection &collection, const QSet<QByteArray> &partIdentifiers) override {
        if (collection.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::collectionChanged(collection, partIdentifiers);
    }

    void collectionMoved(const Akonadi::Collection &collection, const Akonadi::Collection &source, const Akonadi::Collection &destination) override {
        // unknown destination or source == destination means we can't do/don't have to do anything
        if (destination.remoteId().isEmpty() || source == destination)
        {
            changeProcessed();
            return;
        }

        // inter-resource moves, requires we know which resources the source and destination are in though
        if (!source.resource().isEmpty() && !destination.resource().isEmpty() && source.resource() != destination.resource())
        {
            if (source.resource() == q_ptr->identifier()) {   // moved away from us
                AgentBasePrivate::collectionRemoved(collection);
            } else if (destination.resource() == q_ptr->identifier()) {   // moved to us
                scheduler->taskDone(); // stop change replay for now
                RecursiveMover *mover = new RecursiveMover(this);
                mover->setCollection(collection, destination);
                scheduler->scheduleMoveReplay(collection, mover);
            }
            return;
        }

        // intra-resource move, requires the moved collection to have a valid id though
        if (collection.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }

        // intra-resource move, ie. something we can handle internally
        AgentBasePrivate::collectionMoved(collection, source, destination);
    }

    void collectionRemoved(const Akonadi::Collection &collection) override {
        if (collection.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }
        AgentBasePrivate::collectionRemoved(collection);
    }

    void tagAdded(const Akonadi::Tag &tag) override {
        if (!tag.isValid())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::tagAdded(tag);
    }

    void tagChanged(const Akonadi::Tag &tag) override {
        if (tag.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::tagChanged(tag);
    }

    void tagRemoved(const Akonadi::Tag &tag) override {
        if (tag.remoteId().isEmpty())
        {
            changeProcessed();
            return;
        }

        AgentBasePrivate::tagRemoved(tag);
    }

public:
    // synchronize states
    Collection currentCollection;

    ResourceScheduler *scheduler = nullptr;
    ItemSync *mItemSyncer = nullptr;
    ItemFetchScope *mItemSyncFetchScope = nullptr;
    ItemSync::TransactionMode mItemTransactionMode;
    ItemSync::MergeMode mItemMergeMode;
    CollectionSync *mCollectionSyncer = nullptr;
    TagSync *mTagSyncer = nullptr;
    RelationSync *mRelationSyncer = nullptr;
    bool mHierarchicalRid;
    QTimer mProgressEmissionCompressor;
    int mUnemittedProgress;
    QMap<Akonadi::Collection::Id, QVariantMap> mUnemittedAdvancedStatus;
    bool mAutomaticProgressReporting;
    bool mDisableAutomaticItemDeliveryDone;
    QPointer<RecursiveMover> m_recursiveMover;
    int mItemSyncBatchSize;
    QSet<QByteArray> mKeepLocalCollectionChanges;
    KJob *mCurrentCollectionFetchJob = nullptr;
    bool mScheduleAttributeSyncBeforeCollectionSync;
};

ResourceBase::ResourceBase(const QString &id)
    : AgentBase(new ResourceBasePrivate(this), id)
{
    Q_D(ResourceBase);

    qDBusRegisterMetaType<QByteArrayList>();

    new Akonadi__ResourceAdaptor(this);

    d->scheduler = new ResourceScheduler(this);

    d->mChangeRecorder->setChangeRecordingEnabled(true);
    d->mChangeRecorder->setCollectionMoveTranslationEnabled(false);   // we deal with this ourselves
    connect(d->mChangeRecorder, &ChangeRecorder::changesAdded,
            d->scheduler, &ResourceScheduler::scheduleChangeReplay);

    d->mChangeRecorder->setResourceMonitored(d->mId.toLatin1());
    d->mChangeRecorder->fetchCollection(true);

    connect(d->scheduler, &ResourceScheduler::executeFullSync,
            this, &ResourceBase::retrieveCollections);
    connect(d->scheduler, &ResourceScheduler::executeCollectionTreeSync,
            this, &ResourceBase::retrieveCollections);
    connect(d->scheduler, SIGNAL(executeCollectionSync(Akonadi::Collection)),
            SLOT(slotSynchronizeCollection(Akonadi::Collection)));
    connect(d->scheduler, SIGNAL(executeCollectionAttributesSync(Akonadi::Collection)),
            SLOT(slotSynchronizeCollectionAttributes(Akonadi::Collection)));
    connect(d->scheduler, SIGNAL(executeTagSync()),
            SLOT(slotSynchronizeTags()));
    connect(d->scheduler, SIGNAL(executeRelationSync()),
            SLOT(slotSynchronizeRelations()));
    connect(d->scheduler, SIGNAL(executeItemFetch(Akonadi::Item,QSet<QByteArray>)),
            SLOT(slotPrepareItemRetrieval(Akonadi::Item)));
    connect(d->scheduler, SIGNAL(executeItemsFetch(QVector<Akonadi::Item>,QSet<QByteArray>)),
            SLOT(slotPrepareItemsRetrieval(QVector<Akonadi::Item>)));
    connect(d->scheduler, SIGNAL(executeResourceCollectionDeletion()),
            SLOT(slotDeleteResourceCollection()));
    connect(d->scheduler, SIGNAL(executeCacheInvalidation(Akonadi::Collection)),
            SLOT(slotInvalidateCache(Akonadi::Collection)));
    connect(d->scheduler, SIGNAL(status(int,QString)),
            SIGNAL(status(int,QString)));
    connect(d->scheduler, &ResourceScheduler::executeChangeReplay,
            d->mChangeRecorder, &ChangeRecorder::replayNext);
    connect(d->scheduler, SIGNAL(executeRecursiveMoveReplay(RecursiveMover*)),
            SLOT(slotRecursiveMoveReplay(RecursiveMover*)));
    connect(d->scheduler, &ResourceScheduler::fullSyncComplete, this, &ResourceBase::synchronized);
    connect(d->scheduler, &ResourceScheduler::collectionTreeSyncComplete, this, &ResourceBase::collectionTreeSynchronized);
    connect(d->mChangeRecorder, &ChangeRecorder::nothingToReplay, d->scheduler, &ResourceScheduler::taskDone);
    connect(d->mChangeRecorder, &Monitor::collectionRemoved,
            d->scheduler, &ResourceScheduler::collectionRemoved);
    connect(this, SIGNAL(abortRequested()), this, SLOT(slotAbortRequested()));
    connect(this, &ResourceBase::synchronized, d->scheduler, &ResourceScheduler::taskDone);
    connect(this, &ResourceBase::collectionTreeSynchronized, d->scheduler, &ResourceScheduler::taskDone);
    connect(this, &AgentBase::agentNameChanged,
            this, &ResourceBase::nameChanged);

    connect(&d->mProgressEmissionCompressor, SIGNAL(timeout()),
            this, SLOT(slotDelayedEmitProgress()));

    d->scheduler->setOnline(d->mOnline);
    if (!d->mChangeRecorder->isEmpty()) {
        d->scheduler->scheduleChangeReplay();
    }

    new ResourceSelectJob(identifier());

    connect(d->mChangeRecorder->session(), SIGNAL(reconnected()), SLOT(slotSessionReconnected()));
}

ResourceBase::~ResourceBase()
{
}

void ResourceBase::synchronize()
{
    d_func()->scheduler->scheduleFullSync();
}

void ResourceBase::setName(const QString &name)
{
    AgentBase::setAgentName(name);
}

QString ResourceBase::name() const
{
    return AgentBase::agentName();
}

QString ResourceBase::parseArguments(int argc, char **argv)
{
    Q_UNUSED(argc);

    QCommandLineOption identifierOption(QStringLiteral("identifier"),
                                        i18nc("@label command line option", "Resource identifier"),
                                        QStringLiteral("argument"));
    QCommandLineParser parser;
    parser.addOption(identifierOption);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(*qApp);
    parser.setApplicationDescription(i18n("Akonadi Resource"));

    if (!parser.isSet(identifierOption)) {
        qCDebug(AKONADIAGENTBASE_LOG) << "Identifier argument missing";
        exit(1);
    }

    const QString identifier = parser.value(identifierOption);

    if (identifier.isEmpty()) {
        qCDebug(AKONADIAGENTBASE_LOG) << "Identifier is empty";
        exit(1);
    }

    QCoreApplication::setApplicationName(ServerManager::addNamespace(identifier));
    QCoreApplication::setApplicationVersion(QStringLiteral(AKONADI_VERSION_STRING));

    const QFileInfo fi(QString::fromLocal8Bit(argv[0]));
    // strip off full path and possible .exe suffix
    const QString catalog = fi.baseName();

    QTranslator *translator = new QTranslator();
    translator->load(catalog);
    QCoreApplication::installTranslator(translator);

    return identifier;
}

int ResourceBase::init(ResourceBase &r)
{
    KLocalizedString::setApplicationDomain("libakonadi5");
    KAboutData::setApplicationData(r.aboutData());
    return qApp->exec();
}

void ResourceBasePrivate::slotAbortRequested()
{
    Q_Q(ResourceBase);

    scheduler->cancelQueues();
    q->abortActivity();
}

void ResourceBase::itemRetrieved(const Item &item)
{
    Q_D(ResourceBase);
    Q_ASSERT(d->scheduler->currentTask().type == ResourceScheduler::FetchItem);
    if (!item.isValid()) {
        d->scheduler->itemFetchDone(i18nc("@info", "Invalid item retrieved"));
        return;
    }

    Item i(item);
    const QSet<QByteArray> requestedParts = d->scheduler->currentTask().itemParts;
    for (const QByteArray &part : requestedParts) {
        if (!item.loadedPayloadParts().contains(part)) {
            qCWarning(AKONADIAGENTBASE_LOG) << "Item does not provide part" << part;
        }
    }

    ItemModifyJob *job = new ItemModifyJob(i);
    job->d_func()->setSilent(true);
    // FIXME: remove once the item with which we call retrieveItem() has a revision number
    job->disableRevisionCheck();
    connect(job, SIGNAL(result(KJob*)), SLOT(slotDeliveryDone(KJob*)));
}

void ResourceBasePrivate::slotDeliveryDone(KJob *job)
{
    Q_Q(ResourceBase);
    Q_ASSERT(scheduler->currentTask().type == ResourceScheduler::FetchItem);
    if (job->error()) {
        Q_EMIT q->error(i18nc("@info", "Error while creating item: %1", job->errorString()));
    }
    scheduler->itemFetchDone(QString());
}

void ResourceBase::collectionAttributesRetrieved(const Collection &collection)
{
    Q_D(ResourceBase);
    Q_ASSERT(d->scheduler->currentTask().type == ResourceScheduler::SyncCollectionAttributes);
    if (!collection.isValid()) {
        Q_EMIT attributesSynchronized(d->scheduler->currentTask().collection.id());
        d->scheduler->taskDone();
        return;
    }

    CollectionModifyJob *job = new CollectionModifyJob(collection);
    connect(job, SIGNAL(result(KJob*)), SLOT(slotCollectionAttributesSyncDone(KJob*)));
}

void ResourceBasePrivate::slotCollectionAttributesSyncDone(KJob *job)
{
    Q_Q(ResourceBase);
    Q_ASSERT(scheduler->currentTask().type == ResourceScheduler::SyncCollectionAttributes);
    if (job->error()) {
        Q_EMIT q->error(i18nc("@info", "Error while updating collection: %1", job->errorString()));
    }
    Q_EMIT q->attributesSynchronized(scheduler->currentTask().collection.id());
    scheduler->taskDone();
}

void ResourceBasePrivate::slotDeleteResourceCollection()
{
    Q_Q(ResourceBase);

    CollectionFetchJob *job = new CollectionFetchJob(Collection::root(), CollectionFetchJob::FirstLevel);
    job->fetchScope().setResource(q->identifier());
    connect(job, SIGNAL(result(KJob*)), q, SLOT(slotDeleteResourceCollectionDone(KJob*)));
}

void ResourceBasePrivate::slotDeleteResourceCollectionDone(KJob *job)
{
    Q_Q(ResourceBase);
    if (job->error()) {
        Q_EMIT q->error(job->errorString());
        scheduler->taskDone();
    } else {
        const CollectionFetchJob *fetchJob = static_cast<const CollectionFetchJob *>(job);

        if (!fetchJob->collections().isEmpty()) {
            CollectionDeleteJob *job = new CollectionDeleteJob(fetchJob->collections().at(0));
            connect(job, SIGNAL(result(KJob*)), q, SLOT(slotCollectionDeletionDone(KJob*)));
        } else {
            // there is no resource collection, so just ignore the request
            scheduler->taskDone();
        }
    }
}

void ResourceBasePrivate::slotCollectionDeletionDone(KJob *job)
{
    Q_Q(ResourceBase);
    if (job->error()) {
        Q_EMIT q->error(job->errorString());
    }

    scheduler->taskDone();
}

void ResourceBasePrivate::slotInvalidateCache(const Akonadi::Collection &collection)
{
    Q_Q(ResourceBase);
    InvalidateCacheJob *job = new InvalidateCacheJob(collection, q);
    connect(job, &KJob::result, scheduler, &ResourceScheduler::taskDone);
}

void ResourceBase::changeCommitted(const Item &item)
{
    changesCommitted(Item::List() << item);
}

void ResourceBase::changesCommitted(const Item::List &items)
{
    TransactionSequence *transaction = new TransactionSequence(this);
    connect(transaction, SIGNAL(finished(KJob*)),
            this, SLOT(changeCommittedResult(KJob*)));

    // Modify the items one-by-one, because STORE does not support mass RID change
    for (const Item &item : items) {
        ItemModifyJob *job = new ItemModifyJob(item, transaction);
        job->d_func()->setClean();
        job->disableRevisionCheck(); // TODO: remove, but where/how do we handle the error?
        job->setIgnorePayload(true);   // we only want to reset the dirty flag and update the remote id
    }
}

void ResourceBase::changeCommitted(const Collection &collection)
{
    CollectionModifyJob *job = new CollectionModifyJob(collection);
    connect(job, SIGNAL(result(KJob*)), SLOT(changeCommittedResult(KJob*)));
}

void ResourceBasePrivate::changeCommittedResult(KJob *job)
{
    if (job->error()) {
        qCWarning(AKONADIAGENTBASE_LOG) << job->errorText();
    }

    Q_Q(ResourceBase);
    if (qobject_cast<CollectionModifyJob *>(job)) {
        if (job->error()) {
            Q_EMIT q->error(i18nc("@info", "Updating local collection failed: %1.", job->errorText()));
        }
        mChangeRecorder->d_ptr->invalidateCache(static_cast<CollectionModifyJob *>(job)->collection());
    } else {
        if (job->error()) {
            Q_EMIT q->error(i18nc("@info", "Updating local items failed: %1.", job->errorText()));
        }
        // Item and tag cache is invalidated by modify job
    }

    changeProcessed();
}

void ResourceBase::changeCommitted(const Tag &tag)
{
    TagModifyJob *job = new TagModifyJob(tag);
    connect(job, SIGNAL(result(KJob*)), SLOT(changeCommittedResult(KJob*)));
}

QString ResourceBase::requestItemDelivery(const QList<qint64> &uids, const QByteArrayList &parts)
{
    Q_D(ResourceBase);
    if (!isOnline()) {
        const QString errorMsg = i18nc("@info", "Cannot fetch item in offline mode.");
        Q_EMIT error(errorMsg);
        return errorMsg;
    }

    setDelayedReply(true);

    Item::List items;
    items.reserve(uids.size());
    for (auto uid : uids) {
        items.push_back(Item(uid));
    }
    d->scheduler->scheduleItemsFetch(items, QSet<QByteArray>::fromList(parts), message());

    return QString();
}

void ResourceBase::collectionsRetrieved(const Collection::List &collections)
{
    Q_D(ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncCollectionTree ||
               d->scheduler->currentTask().type == ResourceScheduler::SyncAll,
               "ResourceBase::collectionsRetrieved()",
               "Calling collectionsRetrieved() although no collection retrieval is in progress");
    if (!d->mCollectionSyncer) {
        d->mCollectionSyncer = new CollectionSync(identifier());
        d->mCollectionSyncer->setHierarchicalRemoteIds(d->mHierarchicalRid);
        d->mCollectionSyncer->setKeepLocalChanges(d->mKeepLocalCollectionChanges);
        connect(d->mCollectionSyncer, SIGNAL(percent(KJob*,ulong)), SLOT(slotPercent(KJob*,ulong)));
        connect(d->mCollectionSyncer, SIGNAL(result(KJob*)), SLOT(slotCollectionSyncDone(KJob*)));
    }
    d->mCollectionSyncer->setRemoteCollections(collections);
}

void ResourceBase::collectionsRetrievedIncremental(const Collection::List &changedCollections,
        const Collection::List &removedCollections)
{
    Q_D(ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncCollectionTree ||
               d->scheduler->currentTask().type == ResourceScheduler::SyncAll,
               "ResourceBase::collectionsRetrievedIncremental()",
               "Calling collectionsRetrievedIncremental() although no collection retrieval is in progress");
    if (!d->mCollectionSyncer) {
        d->mCollectionSyncer = new CollectionSync(identifier());
        d->mCollectionSyncer->setHierarchicalRemoteIds(d->mHierarchicalRid);
        d->mCollectionSyncer->setKeepLocalChanges(d->mKeepLocalCollectionChanges);
        connect(d->mCollectionSyncer, SIGNAL(percent(KJob*,ulong)), SLOT(slotPercent(KJob*,ulong)));
        connect(d->mCollectionSyncer, SIGNAL(result(KJob*)), SLOT(slotCollectionSyncDone(KJob*)));
    }
    d->mCollectionSyncer->setRemoteCollections(changedCollections, removedCollections);
}

void ResourceBase::setCollectionStreamingEnabled(bool enable)
{
    Q_D(ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncCollectionTree ||
               d->scheduler->currentTask().type == ResourceScheduler::SyncAll,
               "ResourceBase::setCollectionStreamingEnabled()",
               "Calling setCollectionStreamingEnabled() although no collection retrieval is in progress");
    if (!d->mCollectionSyncer) {
        d->mCollectionSyncer = new CollectionSync(identifier());
        d->mCollectionSyncer->setHierarchicalRemoteIds(d->mHierarchicalRid);
        connect(d->mCollectionSyncer, SIGNAL(percent(KJob*,ulong)), SLOT(slotPercent(KJob*,ulong)));
        connect(d->mCollectionSyncer, SIGNAL(result(KJob*)), SLOT(slotCollectionSyncDone(KJob*)));
    }
    d->mCollectionSyncer->setStreamingEnabled(enable);
}

void ResourceBase::collectionsRetrievalDone()
{
    Q_D(ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncCollectionTree ||
               d->scheduler->currentTask().type == ResourceScheduler::SyncAll,
               "ResourceBase::collectionsRetrievalDone()",
               "Calling collectionsRetrievalDone() although no collection retrieval is in progress");
    // streaming enabled, so finalize the sync
    if (d->mCollectionSyncer) {
        d->mCollectionSyncer->retrievalDone();
    } else {
        // user did the sync himself, we are done now
        // FIXME: we need the same special case for SyncAll as in slotCollectionSyncDone here!
        d->scheduler->taskDone();
    }
}

void ResourceBase::setKeepLocalCollectionChanges(const QSet<QByteArray> &parts)
{
    Q_D(ResourceBase);
    d->mKeepLocalCollectionChanges = parts;
}

void ResourceBasePrivate::slotCollectionSyncDone(KJob *job)
{
    Q_Q(ResourceBase);
    mCollectionSyncer = nullptr;
    if (job->error()) {
        if (job->error() != Job::UserCanceled) {
            Q_EMIT q->error(job->errorString());
        }
    } else {
        if (scheduler->currentTask().type == ResourceScheduler::SyncAll) {
            CollectionFetchJob *list = new CollectionFetchJob(Collection::root(), CollectionFetchJob::Recursive);
            list->setFetchScope(q->changeRecorder()->collectionFetchScope());
            list->fetchScope().fetchAttribute<SpecialCollectionAttribute>();
            list->fetchScope().fetchAttribute<FavoriteCollectionAttribute>();
            list->fetchScope().setResource(mId);
            list->fetchScope().setListFilter(CollectionFetchScope::Sync);
            q->connect(list, SIGNAL(result(KJob*)), q, SLOT(slotLocalListDone(KJob*)));
            return;
        } else if (scheduler->currentTask().type == ResourceScheduler::SyncCollectionTree) {
            scheduler->scheduleCollectionTreeSyncCompletion();
        }
    }
    scheduler->taskDone();
}


namespace {

bool sortCollectionsForSync(const Collection &l, const Collection &r)
{
    const auto lType = l.hasAttribute<SpecialCollectionAttribute>()
        ? l.attribute<SpecialCollectionAttribute>()->collectionType()
        : QByteArray();
    const bool lInbox = (lType == "inbox") || (l.remoteId().midRef(1).compare(QLatin1String("inbox"), Qt::CaseInsensitive) == 0);
    const bool lFav = l.hasAttribute<FavoriteCollectionAttribute>();

    const auto rType = r.hasAttribute<SpecialCollectionAttribute>()
        ? r.attribute<SpecialCollectionAttribute>()->collectionType()
        : QByteArray();
    const bool rInbox = (rType == "inbox") || (r.remoteId().midRef(1).compare(QLatin1String("inbox"), Qt::CaseInsensitive) == 0);
    const bool rFav = r.hasAttribute<FavoriteCollectionAttribute>();

    // inbox is always first
    if (lInbox) {
        return true;
    } else if (rInbox) {
        return false;
    }

    // favorites right after inbox
    if (lFav) {
        return !rInbox;
    } else if (rFav) {
        return lInbox;
    }

    // trash is always last (unless it's favorite)
    if (lType == "trash") {
        return false;
    } else if (rType == "trash") {
        return true;
    }

    // Fallback to sorting by id
    return l.id() < r.id();
}

}

void ResourceBasePrivate::slotLocalListDone(KJob *job)
{
    Q_Q(ResourceBase);
    if (job->error()) {
        Q_EMIT q->error(job->errorString());
    } else {
        Collection::List cols = static_cast<CollectionFetchJob *>(job)->collections();
        std::sort(cols.begin(), cols.end(), sortCollectionsForSync);
        for (const Collection &col : qAsConst(cols)) {
            scheduler->scheduleSync(col);
        }
        scheduler->scheduleFullSyncCompletion();
    }
    scheduler->taskDone();
}

void ResourceBasePrivate::slotSynchronizeCollection(const Collection &col)
{
    Q_Q(ResourceBase);
    currentCollection = col;
    // This can happen due to FetchHelper::triggerOnDemandFetch() in the akonadi server (not an error).
    if (!col.remoteId().isEmpty()) {
        // check if this collection actually can contain anything
        QStringList contentTypes = currentCollection.contentMimeTypes();
        contentTypes.removeAll(Collection::mimeType());
        contentTypes.removeAll(Collection::virtualMimeType());
        if (!contentTypes.isEmpty() || col.isVirtual()) {
            if (mAutomaticProgressReporting) {
                Q_EMIT q->status(AgentBase::Running, i18nc("@info:status", "Syncing folder '%1'", currentCollection.displayName()));
            }

            qCDebug(AKONADIAGENTBASE_LOG) << "Preparing collection sync of collection"
                                          << currentCollection.id() << currentCollection.displayName();
            Akonadi::CollectionFetchJob *fetchJob = new Akonadi::CollectionFetchJob(col, CollectionFetchJob::Base, this);
            fetchJob->setFetchScope(q->changeRecorder()->collectionFetchScope());
            connect(fetchJob, SIGNAL(result(KJob*)), q, SLOT(slotItemRetrievalCollectionFetchDone(KJob*)));
            mCurrentCollectionFetchJob = fetchJob;
            return;
        }
    }
    scheduler->taskDone();
}

void ResourceBasePrivate::slotItemRetrievalCollectionFetchDone(KJob *job)
{
    Q_Q(ResourceBase);
    mCurrentCollectionFetchJob = nullptr;
    if (job->error()) {
        qCWarning(AKONADIAGENTBASE_LOG) << "Failed to retrieve collection for sync: " << job->errorString();
        q->cancelTask(i18n("Failed to retrieve collection for sync."));
        return;
    }
    Akonadi::CollectionFetchJob *fetchJob = static_cast<Akonadi::CollectionFetchJob *>(job);
    const Collection::List collections = fetchJob->collections();
    if (collections.isEmpty()) {
        qCWarning(AKONADIAGENTBASE_LOG) << "The fetch job returned empty collection set. This is unexpected.";
        q->cancelTask(i18n("Failed to retrieve collection for sync."));
        return;
    }
    q->retrieveItems(collections.at(0));
}

int ResourceBase::itemSyncBatchSize() const
{
    Q_D(const ResourceBase);
    return d->mItemSyncBatchSize;
}

void ResourceBase::setItemSyncBatchSize(int batchSize)
{
    Q_D(ResourceBase);
    d->mItemSyncBatchSize = batchSize;
}

void ResourceBase::setScheduleAttributeSyncBeforeItemSync(bool enable)
{
    Q_D(ResourceBase);
    d->mScheduleAttributeSyncBeforeCollectionSync = enable;
}

void ResourceBasePrivate::slotSynchronizeCollectionAttributes(const Collection &col)
{
    Q_Q(ResourceBase);
    Akonadi::CollectionFetchJob *fetchJob = new Akonadi::CollectionFetchJob(col, CollectionFetchJob::Base, this);
    fetchJob->setFetchScope(q->changeRecorder()->collectionFetchScope());
    connect(fetchJob, SIGNAL(result(KJob*)), q, SLOT(slotAttributeRetrievalCollectionFetchDone(KJob*)));
    Q_ASSERT(!mCurrentCollectionFetchJob);
    mCurrentCollectionFetchJob = fetchJob;
}

void ResourceBasePrivate::slotAttributeRetrievalCollectionFetchDone(KJob *job)
{
    mCurrentCollectionFetchJob = nullptr;
    Q_Q(ResourceBase);
    if (job->error()) {
        qCWarning(AKONADIAGENTBASE_LOG) << "Failed to retrieve collection for attribute sync: " << job->errorString();
        q->cancelTask(i18n("Failed to retrieve collection for attribute sync."));
        return;
    }
    Akonadi::CollectionFetchJob *fetchJob = static_cast<Akonadi::CollectionFetchJob *>(job);
    QMetaObject::invokeMethod(q, "retrieveCollectionAttributes", Q_ARG(Akonadi::Collection, fetchJob->collections().at(0)));
}

void ResourceBasePrivate::slotSynchronizeTags()
{
    Q_Q(ResourceBase);
    QMetaObject::invokeMethod(this, [q] { q->retrieveTags(); });
}

void ResourceBasePrivate::slotSynchronizeRelations()
{
    Q_Q(ResourceBase);
    QMetaObject::invokeMethod(this, [q] { q->retrieveRelations(); });
}

void ResourceBasePrivate::slotPrepareItemRetrieval(const Item &item)
{
    Q_Q(ResourceBase);
    auto fetch = new ItemFetchJob(item, this);
    // we always need at least parent so we can use ItemCreateJob to merge
    fetch->fetchScope().setAncestorRetrieval(qMax(ItemFetchScope::Parent,
            q->changeRecorder()->itemFetchScope().ancestorRetrieval()));
    fetch->fetchScope().setCacheOnly(true);
    fetch->fetchScope().setFetchRemoteIdentification(true);

    // copy list of attributes to fetch
    const QSet<QByteArray> attributes = q->changeRecorder()->itemFetchScope().attributes();
    for (const auto &attribute : attributes) {
        fetch->fetchScope().fetchAttribute(attribute);
    }

    q->connect(fetch, SIGNAL(result(KJob*)), SLOT(slotPrepareItemRetrievalResult(KJob*)));
}

void ResourceBasePrivate::slotPrepareItemRetrievalResult(KJob *job)
{
    Q_Q(ResourceBase);
    Q_ASSERT_X(scheduler->currentTask().type == ResourceScheduler::FetchItem,
               "ResourceBasePrivate::slotPrepareItemRetrievalResult()",
               "Preparing item retrieval although no item retrieval is in progress");
    if (job->error()) {
        q->cancelTask(job->errorText());
        return;
    }
    ItemFetchJob *fetch = qobject_cast<ItemFetchJob *>(job);
    if (fetch->items().count() != 1) {
        q->cancelTask(i18n("The requested item no longer exists"));
        return;
    }
    const QSet<QByteArray> parts = scheduler->currentTask().itemParts;
    if (!q->retrieveItem(fetch->items().at(0), parts)) {
        q->cancelTask();
    }
}

void ResourceBasePrivate::slotPrepareItemsRetrieval(const QVector<Item> &items)
{
    Q_Q(ResourceBase);
    ItemFetchJob *fetch = new ItemFetchJob(items, this);
    // we always need at least parent so we can use ItemCreateJob to merge
    fetch->fetchScope().setAncestorRetrieval(qMax(ItemFetchScope::Parent,
            q->changeRecorder()->itemFetchScope().ancestorRetrieval()));
    fetch->fetchScope().setCacheOnly(true);
    fetch->fetchScope().setFetchRemoteIdentification(true);
    // It's possible that one or more items were removed before this task was
    // executed, so ignore it and just handle the rest.
    fetch->fetchScope().setIgnoreRetrievalErrors(true);

    // copy list of attributes to fetch
    const QSet<QByteArray> attributes = q->changeRecorder()->itemFetchScope().attributes();
    for (const auto &attribute : attributes) {
        fetch->fetchScope().fetchAttribute(attribute);
    }

    q->connect(fetch, SIGNAL(result(KJob*)), SLOT(slotPrepareItemsRetrievalResult(KJob*)));
}

void ResourceBasePrivate::slotPrepareItemsRetrievalResult(KJob *job)
{
    Q_Q(ResourceBase);
    Q_ASSERT_X(scheduler->currentTask().type == ResourceScheduler::FetchItems,
               "ResourceBasePrivate::slotPrepareItemsRetrievalResult()",
               "Preparing items retrieval although no items retrieval is in progress");
    if (job->error()) {
        q->cancelTask(job->errorText());
        return;
    }
    ItemFetchJob *fetch = qobject_cast<ItemFetchJob *>(job);
    const auto items = fetch->items();
    if (items.isEmpty()) {
        q->cancelTask();
        return;
    }

    const QSet<QByteArray> parts = scheduler->currentTask().itemParts;
    Q_ASSERT(items.first().parentCollection().isValid());
    if (!q->retrieveItems(items, parts)) {
        q->cancelTask();
    }
}

void ResourceBasePrivate::slotRecursiveMoveReplay(RecursiveMover *mover)
{
    Q_Q(ResourceBase);
    Q_ASSERT(mover);
    Q_ASSERT(!m_recursiveMover);
    m_recursiveMover = mover;
    connect(mover, SIGNAL(result(KJob*)), q, SLOT(slotRecursiveMoveReplayResult(KJob*)));
    mover->start();
}

void ResourceBasePrivate::slotRecursiveMoveReplayResult(KJob *job)
{
    Q_Q(ResourceBase);
    m_recursiveMover = nullptr;

    if (job->error()) {
        q->deferTask();
        return;
    }

    changeProcessed();
}

void ResourceBase::itemsRetrievalDone()
{
    Q_D(ResourceBase);
    // streaming enabled, so finalize the sync
    if (d->mItemSyncer) {
        d->mItemSyncer->deliveryDone();
    } else {
        if (d->scheduler->currentTask().type == ResourceScheduler::FetchItems) {
            d->scheduler->currentTask().sendDBusReplies(QString());
        }
        // user did the sync himself, we are done now
        d->scheduler->taskDone();
    }
}

void ResourceBase::clearCache()
{
    Q_D(ResourceBase);
    d->scheduler->scheduleResourceCollectionDeletion();
}

void ResourceBase::invalidateCache(const Collection &collection)
{
    Q_D(ResourceBase);
    d->scheduler->scheduleCacheInvalidation(collection);
}

Collection ResourceBase::currentCollection() const
{
    Q_D(const ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncCollection,
               "ResourceBase::currentCollection()",
               "Trying to access current collection although no item retrieval is in progress");
    return d->currentCollection;
}

Item ResourceBase::currentItem() const
{
    Q_D(const ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::FetchItem,
               "ResourceBase::currentItem()",
               "Trying to access current item although no item retrieval is in progress");
    return d->scheduler->currentTask().items[0];
}

Item::List ResourceBase::currentItems() const
{
    Q_D(const ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::FetchItems,
               "ResourceBase::currentItems()",
               "Trying to access current items although no items retrieval is in progress");
    return d->scheduler->currentTask().items;
}

void ResourceBase::synchronizeCollectionTree()
{
    d_func()->scheduler->scheduleCollectionTreeSync();
}

void ResourceBase::synchronizeTags()
{
    d_func()->scheduler->scheduleTagSync();
}

void ResourceBase::synchronizeRelations()
{
    d_func()->scheduler->scheduleRelationSync();
}

void ResourceBase::cancelTask()
{
    Q_D(ResourceBase);
    if (d->mCurrentCollectionFetchJob) {
        d->mCurrentCollectionFetchJob->kill();
        d->mCurrentCollectionFetchJob = nullptr;
    }
    switch (d->scheduler->currentTask().type) {
    case ResourceScheduler::FetchItem:
        itemRetrieved(Item());   // sends the error reply and
        break;
    case ResourceScheduler::FetchItems:
        itemsRetrieved(Item::List());
        break;
    case ResourceScheduler::ChangeReplay:
        d->changeProcessed();
        break;
    case ResourceScheduler::SyncCollectionTree:
    case ResourceScheduler::SyncAll:
        if (d->mCollectionSyncer) {
            d->mCollectionSyncer->rollback();
        } else {
            d->scheduler->taskDone();
        }
        break;
    case ResourceScheduler::SyncCollection:
        if (d->mItemSyncer) {
            d->mItemSyncer->rollback();
        } else {
            d->scheduler->taskDone();
        }
        break;
    default:
        d->scheduler->taskDone();
    }
}

void ResourceBase::cancelTask(const QString &msg)
{
    cancelTask();

    Q_EMIT error(msg);
}

void ResourceBase::deferTask()
{
    Q_D(ResourceBase);
    qCDebug(AKONADIAGENTBASE_LOG) << "Deferring task" << d->scheduler->currentTask();
    // Deferring a CollectionSync is just not implemented.
    // We'd need to d->mItemSyncer->rollback() but also to NOT call taskDone in slotItemSyncDone() here...
    Q_ASSERT(!d->mItemSyncer);
    d->scheduler->deferTask();
}

void ResourceBase::doSetOnline(bool state)
{
    d_func()->scheduler->setOnline(state);
}

void ResourceBase::synchronizeCollection(qint64 collectionId)
{
    synchronizeCollection(collectionId, false);
}

void ResourceBase::synchronizeCollection(qint64 collectionId, bool recursive)
{
    CollectionFetchJob *job = new CollectionFetchJob(Collection(collectionId), recursive ? CollectionFetchJob::Recursive : CollectionFetchJob::Base);
    job->setFetchScope(changeRecorder()->collectionFetchScope());
    job->fetchScope().setResource(identifier());
    job->fetchScope().setListFilter(CollectionFetchScope::Sync);
    connect(job, SIGNAL(result(KJob*)), SLOT(slotCollectionListDone(KJob*)));
}

void ResourceBasePrivate::slotCollectionListDone(KJob *job)
{
    if (!job->error()) {
        const Collection::List list = static_cast<CollectionFetchJob *>(job)->collections();
        for (const Collection &collection : list) {
            //We also get collections that should not be synced but are part of the tree.
            if (collection.shouldList(Collection::ListSync)) {
                if (mScheduleAttributeSyncBeforeCollectionSync) {
                    scheduler->scheduleAttributesSync(collection);
                }
                scheduler->scheduleSync(collection);
            }
        }
    } else {
        qCWarning(AKONADIAGENTBASE_LOG) << "Failed to fetch collection for collection sync: " << job->errorString();
    }
}

void ResourceBase::synchronizeCollectionAttributes(const Akonadi::Collection &col)
{
    Q_D(ResourceBase);
    d->scheduler->scheduleAttributesSync(col);
}

void ResourceBase::synchronizeCollectionAttributes(qint64 collectionId)
{
    CollectionFetchJob *job = new CollectionFetchJob(Collection(collectionId), CollectionFetchJob::Base);
    job->setFetchScope(changeRecorder()->collectionFetchScope());
    job->fetchScope().setResource(identifier());
    connect(job, SIGNAL(result(KJob*)), SLOT(slotCollectionListForAttributesDone(KJob*)));
}

void ResourceBasePrivate::slotCollectionListForAttributesDone(KJob *job)
{
    if (!job->error()) {
        const Collection::List list = static_cast<CollectionFetchJob *>(job)->collections();
        if (!list.isEmpty()) {
            const Collection col = list.first();
            scheduler->scheduleAttributesSync(col);
        }
    }
    // TODO: error handling
}

void ResourceBase::setTotalItems(int amount)
{
    qCDebug(AKONADIAGENTBASE_LOG) << amount;
    Q_D(ResourceBase);
    setItemStreamingEnabled(true);
    if (d->mItemSyncer) {
        d->mItemSyncer->setTotalItems(amount);
    }
}

void ResourceBase::setDisableAutomaticItemDeliveryDone(bool disable)
{
    Q_D(ResourceBase);
    if (d->mItemSyncer) {
        d->mItemSyncer->setDisableAutomaticDeliveryDone(disable);
    }
    d->mDisableAutomaticItemDeliveryDone = disable;
}

void ResourceBase::setItemStreamingEnabled(bool enable)
{
    Q_D(ResourceBase);
    d->createItemSyncInstanceIfMissing();
    if (d->mItemSyncer) {
        d->mItemSyncer->setStreamingEnabled(enable);
    }
}

void ResourceBase::itemsRetrieved(const Item::List &items)
{
    Q_D(ResourceBase);
    if (d->scheduler->currentTask().type == ResourceScheduler::FetchItems) {
        auto trx = new TransactionSequence(this);
        connect(trx, SIGNAL(result(KJob*)),
                this, SLOT(slotItemSyncDone(KJob*)));
        for (const Item &item : items) {
            Q_ASSERT(item.parentCollection().isValid());
            if (item.isValid()) {
                new ItemModifyJob(item, trx);
            } else if (!item.remoteId().isEmpty()) {
                auto job = new ItemCreateJob(item, item.parentCollection(), trx);
                job->setMerge(ItemCreateJob::RID);
            } else {
                // This should not happen, but just to be sure...
                new ItemModifyJob(item, trx);
            }
        }
        trx->commit();
    } else {
        d->createItemSyncInstanceIfMissing();
        if (d->mItemSyncer) {
            d->mItemSyncer->setFullSyncItems(items);
        }
    }
}

void ResourceBase::itemsRetrievedIncremental(const Item::List &changedItems,
        const Item::List &removedItems)
{
    Q_D(ResourceBase);
    d->createItemSyncInstanceIfMissing();
    if (d->mItemSyncer) {
        d->mItemSyncer->setIncrementalSyncItems(changedItems, removedItems);
    }
}

void ResourceBasePrivate::slotItemSyncDone(KJob *job)
{
    mItemSyncer = nullptr;
    Q_Q(ResourceBase);
    if (job->error() && job->error() != Job::UserCanceled) {
        Q_EMIT q->error(job->errorString());
    }
    if (scheduler->currentTask().type == ResourceScheduler::FetchItems) {
        scheduler->currentTask().sendDBusReplies((job->error() && job->error() != Job::UserCanceled) ? job->errorString() : QString());
    }
    scheduler->taskDone();
}

void ResourceBasePrivate::slotDelayedEmitProgress()
{
    Q_Q(ResourceBase);
    if (mAutomaticProgressReporting) {
        Q_EMIT q->percent(mUnemittedProgress);

        for (const QVariantMap &statusMap : qAsConst(mUnemittedAdvancedStatus)) {
            Q_EMIT q->advancedStatus(statusMap);
        }
    }
    mUnemittedProgress = 0;
    mUnemittedAdvancedStatus.clear();
}

void ResourceBasePrivate::slotPercent(KJob *job, unsigned long percent)
{
    mUnemittedProgress = percent;

    const Collection collection = job->property("collection").value<Collection>();
    if (collection.isValid()) {
        QVariantMap statusMap;
        statusMap.insert(QStringLiteral("key"), QStringLiteral("collectionSyncProgress"));
        statusMap.insert(QStringLiteral("collectionId"), collection.id());
        statusMap.insert(QStringLiteral("percent"), static_cast<unsigned int>(percent));

        mUnemittedAdvancedStatus[collection.id()] = statusMap;
    }
    // deliver completion right away, intermediate progress at 1s intervals
    if (percent == 100) {
        mProgressEmissionCompressor.stop();
        slotDelayedEmitProgress();
    } else if (!mProgressEmissionCompressor.isActive()) {
        mProgressEmissionCompressor.start();
    }
}

void ResourceBase::setHierarchicalRemoteIdentifiersEnabled(bool enable)
{
    Q_D(ResourceBase);
    d->mHierarchicalRid = enable;
}

void ResourceBase::scheduleCustomTask(QObject *receiver, const char *method, const QVariant &argument, SchedulePriority priority)
{
    Q_D(ResourceBase);
    d->scheduler->scheduleCustomTask(receiver, method, argument, priority);
}

void ResourceBase::taskDone()
{
    Q_D(ResourceBase);
    d->scheduler->taskDone();
}

void ResourceBase::retrieveCollectionAttributes(const Collection &collection)
{
    collectionAttributesRetrieved(collection);
}

void ResourceBase::retrieveTags()
{
    Q_D(ResourceBase);
    d->scheduler->taskDone();
}

void ResourceBase::retrieveRelations()
{
    Q_D(ResourceBase);
    d->scheduler->taskDone();
}

bool ResourceBase::retrieveItem(const Akonadi::Item &item, const QSet<QByteArray> &parts)
{
    Q_UNUSED(item);
    Q_UNUSED(parts);
    // retrieveItem() can no longer be pure virtual, because then we could not mark
    // it as deprecated (i.e. implementations would still be forced to implement it),
    // so instead we assert here.
    // NOTE: Don't change to Q_ASSERT_X here: while the macro can be disabled at
    // compile time, we want to hit this assert *ALWAYS*.
    qt_assert_x("Akonadi::ResourceBase::retrieveItem()",
                "The base implementation of retrieveItem() must never be reached. "
                "You must implement either retrieveItem() or retrieveItems(Akonadi::Item::List, QSet<QByteArray>) overload "
                "to handle item retrieval requests.", __FILE__, __LINE__);
    return false;
}

bool ResourceBase::retrieveItems(const Akonadi::Item::List &items, const QSet<QByteArray> &parts)
{
    Q_D(ResourceBase);

    // If we reach this implementation of retrieveItems() then it means that the
    // resource is still using the deprecated retrieveItem() method, so we explode
    // this to a myriad of tasks in scheduler and let them be processed one by one

    const qint64 id = d->scheduler->currentTask().serial;
    for (const auto &item : items) {
        d->scheduler->scheduleItemFetch(item, parts, d->scheduler->currentTask().dbusMsgs, id);
    }
    taskDone();
    return true;
}

void Akonadi::ResourceBase::abortActivity()
{
}

void ResourceBase::setItemTransactionMode(ItemSync::TransactionMode mode)
{
    Q_D(ResourceBase);
    d->mItemTransactionMode = mode;
}

void ResourceBase::setItemSynchronizationFetchScope(const ItemFetchScope &fetchScope)
{
    Q_D(ResourceBase);
    if (!d->mItemSyncFetchScope) {
        d->mItemSyncFetchScope = new ItemFetchScope;
    }
    *(d->mItemSyncFetchScope) = fetchScope;
}

void ResourceBase::setItemMergingMode(ItemSync::MergeMode mode)
{
    Q_D(ResourceBase);
    d->mItemMergeMode = mode;
}

void ResourceBase::setAutomaticProgressReporting(bool enabled)
{
    Q_D(ResourceBase);
    d->mAutomaticProgressReporting = enabled;
}

QString ResourceBase::dumpNotificationListToString() const
{
    Q_D(const ResourceBase);
    return d->dumpNotificationListToString();
}

QString ResourceBase::dumpSchedulerToString() const
{
    Q_D(const ResourceBase);
    return d->dumpToString();
}

void ResourceBase::dumpMemoryInfo() const
{
    Q_D(const ResourceBase);
    return d->dumpMemoryInfo();
}

QString ResourceBase::dumpMemoryInfoToString() const
{
    Q_D(const ResourceBase);
    return d->dumpMemoryInfoToString();
}

void ResourceBase::tagsRetrieved(const Tag::List &tags, const QHash<QString, Item::List> &tagMembers)
{
    Q_D(ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncTags ||
               d->scheduler->currentTask().type == ResourceScheduler::SyncAll ||
               d->scheduler->currentTask().type == ResourceScheduler::Custom,
               "ResourceBase::tagsRetrieved()",
               "Calling tagsRetrieved() although no tag retrieval is in progress");
    if (!d->mTagSyncer) {
        d->mTagSyncer = new TagSync(this);
        connect(d->mTagSyncer, SIGNAL(percent(KJob*,ulong)), SLOT(slotPercent(KJob*,ulong)));
        connect(d->mTagSyncer, SIGNAL(result(KJob*)), SLOT(slotTagSyncDone(KJob*)));
    }
    d->mTagSyncer->setFullTagList(tags);
    d->mTagSyncer->setTagMembers(tagMembers);
}

void ResourceBasePrivate::slotTagSyncDone(KJob *job)
{
    Q_Q(ResourceBase);
    mTagSyncer = nullptr;
    if (job->error()) {
        if (job->error() != Job::UserCanceled) {
            qCWarning(AKONADIAGENTBASE_LOG) << "TagSync failed: " << job->errorString();
            Q_EMIT q->error(job->errorString());
        }
    }

    scheduler->taskDone();
}

void ResourceBase::relationsRetrieved(const Relation::List &relations)
{
    Q_D(ResourceBase);
    Q_ASSERT_X(d->scheduler->currentTask().type == ResourceScheduler::SyncRelations ||
               d->scheduler->currentTask().type == ResourceScheduler::SyncAll ||
               d->scheduler->currentTask().type == ResourceScheduler::Custom,
               "ResourceBase::relationsRetrieved()",
               "Calling relationsRetrieved() although no relation retrieval is in progress");
    if (!d->mRelationSyncer) {
        d->mRelationSyncer = new RelationSync(this);
        connect(d->mRelationSyncer, SIGNAL(percent(KJob*,ulong)), SLOT(slotPercent(KJob*,ulong)));
        connect(d->mRelationSyncer, SIGNAL(result(KJob*)), SLOT(slotRelationSyncDone(KJob*)));
    }
    d->mRelationSyncer->setRemoteRelations(relations);
}

void ResourceBasePrivate::slotRelationSyncDone(KJob *job)
{
    Q_Q(ResourceBase);
    mRelationSyncer = nullptr;
    if (job->error()) {
        if (job->error() != Job::UserCanceled) {
            qCWarning(AKONADIAGENTBASE_LOG) << "RelationSync failed: " << job->errorString();
            Q_EMIT q->error(job->errorString());
        }
    }

    scheduler->taskDone();
}

#include "resourcebase.moc"
#include "moc_resourcebase.cpp"
