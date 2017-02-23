/*
 *  Copyright (c) 2015 Daniel Vrátil <dvratil@redhat.com>
 *
 *  This library is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Library General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This library is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 *  License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include "protocol_p.h"
#include "scope_p.h"
#include "imapset_p.h"
#include "datastream_p_p.h"

#include <type_traits>
#include <typeinfo>

#include <QHash>
#include <QMap>
#include <QDateTime>
#include <QStack>

#include <cassert>

#undef AKONADI_DECLARE_PRIVATE
#define AKONADI_DECLARE_PRIVATE(Class) \
    inline Class##Private* Class::d_func() {\
        return reinterpret_cast<Class##Private*>(d_ptr.data()); \
    } \
    inline const Class##Private* Class::d_func() const {\
        return reinterpret_cast<const Class##Private*>(d_ptr.constData()); \
    }

#define COMPARE(prop) \
    (prop == ((decltype(this)) other)->prop)

namespace Akonadi
{
namespace Protocol
{

int version()
{
    return 56;
}

}
}

QDebug operator<<(QDebug _dbg, Akonadi::Protocol::Command::Type type)
{
    QDebug dbg(_dbg.noquote());

    switch (type) {
    case Akonadi::Protocol::Command::Invalid:
        return dbg << "Invalid";

    case Akonadi::Protocol::Command::Hello:
        return dbg << "Hello";
    case Akonadi::Protocol::Command::Login:
        return dbg << "Login";
    case Akonadi::Protocol::Command::Logout:
        return dbg << "Logout";

    case Akonadi::Protocol::Command::Transaction:
        return dbg << "Transaction";

    case Akonadi::Protocol::Command::CreateItem:
        return dbg << "CreateItem";
    case Akonadi::Protocol::Command::CopyItems:
        return dbg << "CopyItems";
    case Akonadi::Protocol::Command::DeleteItems:
        return dbg << "DeleteItems";
    case Akonadi::Protocol::Command::FetchItems:
        return dbg << "FetchItems";
    case Akonadi::Protocol::Command::LinkItems:
        return dbg << "LinkItems";
    case Akonadi::Protocol::Command::ModifyItems:
        return dbg << "ModifyItems";
    case Akonadi::Protocol::Command::MoveItems:
        return dbg << "MoveItems";

    case Akonadi::Protocol::Command::CreateCollection:
        return dbg << "CreateCollection";
    case Akonadi::Protocol::Command::CopyCollection:
        return dbg << "CopyCollection";
    case Akonadi::Protocol::Command::DeleteCollection:
        return dbg << "DeleteCollection";
    case Akonadi::Protocol::Command::FetchCollections:
        return dbg << "FetchCollections";
    case Akonadi::Protocol::Command::FetchCollectionStats:
        return dbg << "FetchCollectionStats";
    case Akonadi::Protocol::Command::ModifyCollection:
        return dbg << "ModifyCollection";
    case Akonadi::Protocol::Command::MoveCollection:
        return dbg << "MoveCollection";

    case Akonadi::Protocol::Command::Search:
        return dbg << "Search";
    case Akonadi::Protocol::Command::SearchResult:
        return dbg << "SearchResult";
    case Akonadi::Protocol::Command::StoreSearch:
        return dbg << "StoreSearch";

    case Akonadi::Protocol::Command::CreateTag:
        return dbg << "CreateTag";
    case Akonadi::Protocol::Command::DeleteTag:
        return dbg << "DeleteTag";
    case Akonadi::Protocol::Command::FetchTags:
        return dbg << "FetchTags";
    case Akonadi::Protocol::Command::ModifyTag:
        return dbg << "ModifyTag";

    case Akonadi::Protocol::Command::FetchRelations:
        return dbg << "FetchRelations";
    case Akonadi::Protocol::Command::ModifyRelation:
        return dbg << "ModifyRelation";
    case Akonadi::Protocol::Command::RemoveRelations:
        return dbg << "RemoveRelations";

    case Akonadi::Protocol::Command::SelectResource:
        return dbg << "SelectResource";

    case Akonadi::Protocol::Command::StreamPayload:
        return dbg << "StreamPayload";
    case Akonadi::Protocol::Command::ItemChangeNotification:
        return dbg << "ItemChangeNotification";
    case Akonadi::Protocol::Command::CollectionChangeNotification:
        return dbg << "CollectionChangeNotification";
    case Akonadi::Protocol::Command::TagChangeNotification:
        return dbg << "TagChangeNotification";
    case Akonadi::Protocol::Command::RelationChangeNotification:
        return dbg << "RelationChangeNotification";
    case Akonadi::Protocol::Command::SubscriptionChangeNotification:
        return dbg << "SubscriptionChangeNotification";
    case Akonadi::Protocol::Command::DebugChangeNotification:
        return dbg << "DebugChangeNotification";
    case Akonadi::Protocol::Command::CreateSubscription:
        return dbg << "CreateSubscription";
    case Akonadi::Protocol::Command::ModifySubscription:
        return dbg << "ModifySubscription";

    case Akonadi::Protocol::Command::_ResponseBit:
        Q_ASSERT(false);
        return dbg;
    }

    Q_ASSERT(false);
    return dbg;
}

QDebug operator<<(QDebug _dbg, const Akonadi::Protocol::ItemChangeNotification::Relation &rel)
{
    QDebug dbg(_dbg.noquote());
    return dbg << "Left: " << rel.leftId << ", Right:" << rel.rightId << ", Type: " << rel.type;
}

namespace Akonadi
{
namespace Protocol
{

class DebugBlock
{
public:
    DebugBlock(QDebug &dbg)
        : mIndent(0)
        , mDbg(dbg)
    {
        beginBlock();
    }

    ~DebugBlock()
    {
        endBlock();
    }

    void beginBlock(const QByteArray &name = QByteArray())
    {
        mDbg << "\n";
        if (!name.isNull()) {
            mBlocks.push(name.size() + 4);
            mDbg << QStringLiteral(" ").repeated(mIndent) << name << ": { ";
            mIndent += mBlocks.top();
        } else {
            mBlocks.push(2);
            mDbg << QStringLiteral(" ").repeated(mIndent) << "{ ";
        }
        mBlockInit.push(false);
    }

    void endBlock()
    {
        mDbg << " }";
        mIndent -= mBlocks.pop();
        mBlockInit.pop();
    }

    template<typename T>
    void write(const char *name, const T &val)
    {
        if (mBlockInit.top()) {
            mDbg.noquote() << QByteArray("\n");
            mDbg << QStringLiteral(" ").repeated(mIndent);
        } else {
            mBlockInit.top() = true;
        }

        mDbg << name << ": \"" << val << "\"";
    }

private:
    Q_DISABLE_COPY(DebugBlock)
    QStack<int> mBlocks;
    QStack<bool> mBlockInit;
    int mIndent;
    QDebug &mDbg;
};

}
}

/******************************************************************************/

namespace Akonadi
{
namespace Protocol
{

class CommandPrivate : public QSharedData
{
public:
    CommandPrivate(quint8 type)
        : QSharedData()
        , commandType(type)
    {}

    CommandPrivate(const CommandPrivate &other)
        : QSharedData(other)
        , commandType(other.commandType)
    {}

    virtual ~CommandPrivate()
    {}

    virtual bool compare(const CommandPrivate *other) const
    {
        return typeid(*this) == typeid(*other)
               && COMPARE(commandType);
    }

    virtual DataStream &serialize(DataStream &stream) const
    {
        return stream << commandType;
    }

    virtual DataStream &deserialize(DataStream &stream)
    {
        return stream >> commandType;
    }

    virtual CommandPrivate *clone() const
    {
        return new CommandPrivate(*this);
    }

    virtual void debugString(DebugBlock &blck) const
    {
        blck.write("Command", static_cast<Protocol::Command::Type>(commandType));
    }

    quint8 commandType;
};

}
}

template <>
Akonadi::Protocol::CommandPrivate *QSharedDataPointer<Akonadi::Protocol::CommandPrivate>::clone()
{
    return d->clone();
}

namespace Akonadi
{
namespace Protocol
{

AKONADI_DECLARE_PRIVATE(Command)

Command::Command()
    : d_ptr(new CommandPrivate(Invalid))
{
}

Command::Command(CommandPrivate *dd)
    : d_ptr(dd)
{
}

Command::Command(Command &&other)
{
    d_ptr.swap(other.d_ptr);
}

Command::Command(const Command &other)
    : d_ptr(other.d_ptr)
{
}

Command::~Command()
{
}

Command &Command::operator=(Command &&other)
{
    d_ptr.swap(other.d_ptr);
    return *this;
}

Command &Command::operator=(const Command &other)
{
    d_ptr = other.d_ptr;
    return *this;
}

bool Command::operator==(const Command &other) const
{
    return d_ptr == other.d_ptr || d_ptr->compare(other.d_ptr.constData());
}

bool Command::operator!=(const Command &other) const
{
    return d_ptr != other.d_ptr && !d_ptr->compare(other.d_ptr.constData());
}

Command::Type Command::type() const
{
    return static_cast<Command::Type>(d_func()->commandType & ~_ResponseBit);
}

bool Command::isValid() const
{
    return type() != Invalid;
}

bool Command::isResponse() const
{
    return d_func()->commandType & _ResponseBit;
}

QString Command::debugString() const
{
    QString out;
    QDebug dbg(&out);
    DebugBlock blck(dbg.noquote().nospace());
    d_func()->debugString(blck);
    return out;
}

QString Command::debugString(DebugBlock &blck) const
{
    d_func()->debugString(blck);
    return QString();
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::Command &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::Command &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

class ResponsePrivate : public CommandPrivate
{
public:
    ResponsePrivate(Command::Type type)
        : CommandPrivate(type | Command::_ResponseBit)
        , errorCode(0)
    {}

    ResponsePrivate(const ResponsePrivate &other)
        : CommandPrivate(other)
        , errorMsg(other.errorMsg)
        , errorCode(other.errorCode)
    {}

    virtual ~ResponsePrivate() Q_DECL_OVERRIDE
    {}

    virtual bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(errorMsg)
               && COMPARE(errorCode);
    }

    virtual DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << errorCode
               << errorMsg;
    }

    virtual DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> errorCode
        >> errorMsg;
    }

    virtual CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ResponsePrivate(*this);
    }

    virtual void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Response", static_cast<Protocol::Command::Type>(commandType & ~Command::_ResponseBit));
        blck.write("Error Code", errorCode);
        blck.write("Error Msg", errorMsg);
    }

    QString errorMsg;
    int errorCode;
};

AKONADI_DECLARE_PRIVATE(Response)

Response::Response()
    : Command(new ResponsePrivate(Protocol::Command::Invalid))
{
}

Response::Response(ResponsePrivate *dd)
    : Command(dd)
{
}

Response::Response(const Command &command)
    : Command(command)
{
    Q_ASSERT(command.isResponse());
}

void Response::setError(int code, const QString &message)
{
    d_func()->errorCode = code;
    d_func()->errorMsg = message;
}

bool Response::isError() const
{
    return d_func()->errorCode;
}

int Response::errorCode() const
{
    return d_func()->errorCode;
}

QString Response::errorMessage() const
{
    return d_func()->errorMsg;
}

DataStream &operator<<(DataStream &stream, const Response &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, Response &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

class FactoryPrivate
{
public:
    typedef Command(*CommandFactoryFunc)();
    typedef Response(*ResponseFactoryFunc)();

    FactoryPrivate()
    {
        // Session management
        registerType<Command::Hello, Command /* invalid */, HelloResponse>();
        registerType<Command::Login, LoginCommand, LoginResponse>();
        registerType<Command::Logout, LogoutCommand, LogoutResponse>();

        // Transactions
        registerType<Command::Transaction, TransactionCommand, TransactionResponse>();

        // Items
        registerType<Command::CreateItem, CreateItemCommand, CreateItemResponse>();
        registerType<Command::CopyItems, CopyItemsCommand, CopyItemsResponse>();
        registerType<Command::DeleteItems, DeleteItemsCommand, DeleteItemsResponse>();
        registerType<Command::FetchItems, FetchItemsCommand, FetchItemsResponse>();
        registerType<Command::LinkItems, LinkItemsCommand, LinkItemsResponse>();
        registerType<Command::ModifyItems, ModifyItemsCommand, ModifyItemsResponse>();
        registerType<Command::MoveItems, MoveItemsCommand, MoveItemsResponse>();

        // Collections
        registerType<Command::CreateCollection, CreateCollectionCommand, CreateCollectionResponse>();
        registerType<Command::CopyCollection, CopyCollectionCommand, CopyCollectionResponse>();
        registerType<Command::DeleteCollection, DeleteCollectionCommand, DeleteCollectionResponse>();
        registerType<Command::FetchCollections, FetchCollectionsCommand, FetchCollectionsResponse>();
        registerType<Command::FetchCollectionStats, FetchCollectionStatsCommand, FetchCollectionStatsResponse>();
        registerType<Command::ModifyCollection, ModifyCollectionCommand, ModifyCollectionResponse>();
        registerType<Command::MoveCollection, MoveCollectionCommand, MoveCollectionResponse>();

        // Search
        registerType<Command::Search, SearchCommand, SearchResponse>();
        registerType<Command::SearchResult, SearchResultCommand, SearchResultResponse>();
        registerType<Command::StoreSearch, StoreSearchCommand, StoreSearchResponse>();

        // Tag
        registerType<Command::CreateTag, CreateTagCommand, CreateTagResponse>();
        registerType<Command::DeleteTag, DeleteTagCommand, DeleteTagResponse>();
        registerType<Command::FetchTags, FetchTagsCommand, FetchTagsResponse>();
        registerType<Command::ModifyTag, ModifyTagCommand, ModifyTagResponse>();

        // Relation
        registerType<Command::FetchRelations, FetchRelationsCommand, FetchRelationsResponse>();
        registerType<Command::ModifyRelation, ModifyRelationCommand, ModifyRelationResponse>();
        registerType<Command::RemoveRelations, RemoveRelationsCommand, RemoveRelationsResponse>();

        // Resources
        registerType<Command::SelectResource, SelectResourceCommand, SelectResourceResponse>();

        // Other...?
        registerType<Command::StreamPayload, StreamPayloadCommand, StreamPayloadResponse>();
        registerType<Command::ItemChangeNotification, ItemChangeNotification, Response /* invalid */>();
        registerType<Command::CollectionChangeNotification, CollectionChangeNotification, Response /* invalid */>();
        registerType<Command::TagChangeNotification, TagChangeNotification, Response /* invalid */>();
        registerType<Command::RelationChangeNotification, RelationChangeNotification, Response /* invalid */>();
        registerType<Command::SubscriptionChangeNotification, SubscriptionChangeNotification, Response /* invalid */>();
        registerType<Command::DebugChangeNotification, DebugChangeNotification, Response /* invalid */>();
        registerType<Command::CreateSubscription, CreateSubscriptionCommand, CreateSubscriptionResponse>();
        registerType<Command::ModifySubscription, ModifySubscriptionCommand, ModifySubscriptionResponse>();
    }

    // clang has problem resolving the right qHash() overload for Command::Type,
    // so use its underlying integer type instead
    QHash<std::underlying_type<Command::Type>::type, QPair<CommandFactoryFunc, ResponseFactoryFunc>> registrar;

private:
    template<typename T>
    static Command commandFactoryFunc()
    {
        return T();
    }
    template<typename T>
    static Response responseFactoryFunc()
    {
        return T();
    }

    template<Command::Type T, typename CmdType, typename RespType>
    void registerType()
    {
        CommandFactoryFunc cmdFunc = &commandFactoryFunc<CmdType>;
        ResponseFactoryFunc respFunc = &responseFactoryFunc<RespType>;
        registrar.insert(T, qMakePair<CommandFactoryFunc, ResponseFactoryFunc>(cmdFunc, respFunc));
    }
};

Q_GLOBAL_STATIC(FactoryPrivate, sFactoryPrivate)

Command Factory::command(Command::Type type)
{
    auto iter = sFactoryPrivate->registrar.constFind(type);
    if (iter == sFactoryPrivate->registrar.constEnd()) {
        return Command();
    }
    return iter.value().first();
}

Response Factory::response(Command::Type type)
{
    auto iter = sFactoryPrivate->registrar.constFind(type);
    if (iter == sFactoryPrivate->registrar.constEnd()) {
        return Response();
    }
    return iter.value().second();
}

/******************************************************************************/

void serialize(QIODevice *device, const Command &command)
{
    DataStream stream(device);
    stream << command;

#if 0
    QLocalSocket *socket
    if ((socket == qobject_cast<QLocalSocket *>(device))) {
        socket->flush();
    }
#endif
}

Command deserialize(QIODevice *device)
{
    DataStream stream(device);

    stream.waitForData(sizeof(Command::Type));
    Command::Type cmdType;
    if (device->peek((char *) &cmdType, sizeof(Command::Type)) != sizeof(Command::Type)) {
        throw ProtocolException("Failed to peek command type");
    }

    Command cmd;
    if (cmdType & Command::_ResponseBit) {
        cmd = Factory::response(Command::Type(cmdType & ~Command::_ResponseBit));
    } else {
        cmd = Factory::command(cmdType);
    }

    stream >> cmd;
    return cmd;
}

/******************************************************************************/

class FetchScopePrivate : public QSharedData
{
public:
    FetchScopePrivate()
        : fetchFlags(FetchScope::None)
        , ancestorDepth(Ancestor::NoAncestor)
    {}

    FetchScope::FetchFlags fetchFlags;
    QVector<QByteArray> requestedParts;
    QDateTime changedSince;
    QSet<QByteArray> tagFetchScope;
    Ancestor::Depth ancestorDepth;
};

FetchScope::FetchScope()
    : d(new FetchScopePrivate)
{
}

FetchScope::FetchScope(FetchScope &&other)
{
    d.swap(other.d);
}

FetchScope::FetchScope(const FetchScope &other)
    : d(other.d)
{
}

FetchScope::~FetchScope()
{
}

FetchScope &FetchScope::operator=(FetchScope &&other)
{
    d.swap(other.d);
    return *this;
}

FetchScope &FetchScope::operator=(const FetchScope &other)
{
    d = other.d;
    return *this;
}

bool FetchScope::operator==(const FetchScope &other) const
{
    return (d == other.d)
           || (d->requestedParts == other.d->requestedParts
               && d->changedSince == other.d->changedSince
               && d->tagFetchScope == other.d->tagFetchScope
               && d->ancestorDepth == other.d->ancestorDepth
               && d->fetchFlags == other.d->fetchFlags);
}

bool FetchScope::operator!=(const FetchScope &other) const
{
    return !(*this == other);
}

void FetchScope::setRequestedParts(const QVector<QByteArray> &requestedParts)
{
    d->requestedParts = requestedParts;
}

QVector<QByteArray> FetchScope::requestedParts() const
{
    return d->requestedParts;
}

QVector<QByteArray> FetchScope::requestedPayloads() const
{
    QVector<QByteArray> rv;
    std::copy_if(d->requestedParts.begin(), d->requestedParts.end(),
                 std::back_inserter(rv),
    [](const QByteArray & ba) {
        return ba.startsWith("PLD:");
    });
    return rv;
}

void FetchScope::setChangedSince(const QDateTime &changedSince)
{
    d->changedSince = changedSince;
}

QDateTime FetchScope::changedSince() const
{
    return d->changedSince;
}

void FetchScope::setTagFetchScope(const QSet<QByteArray> &tagFetchScope)
{
    d->tagFetchScope = tagFetchScope;
}

QSet<QByteArray> FetchScope::tagFetchScope() const
{
    return d->tagFetchScope;
}

void FetchScope::setAncestorDepth(Ancestor::Depth depth)
{
    d->ancestorDepth = depth;
}

Ancestor::Depth FetchScope::ancestorDepth() const
{
    return d->ancestorDepth;
}

bool FetchScope::cacheOnly() const
{
    return d->fetchFlags & CacheOnly;
}

bool FetchScope::checkCachedPayloadPartsOnly() const
{
    return d->fetchFlags & CheckCachedPayloadPartsOnly;
}
bool FetchScope::fullPayload() const
{
    return d->fetchFlags & FullPayload;
}
bool FetchScope::allAttributes() const
{
    return d->fetchFlags & AllAttributes;
}
bool FetchScope::fetchSize() const
{
    return d->fetchFlags & Size;
}
bool FetchScope::fetchMTime() const
{
    return d->fetchFlags & MTime;
}
bool FetchScope::fetchRemoteRevision() const
{
    return d->fetchFlags & RemoteRevision;
}
bool FetchScope::ignoreErrors() const
{
    return d->fetchFlags & IgnoreErrors;
}
bool FetchScope::fetchFlags() const
{
    return d->fetchFlags & Flags;
}
bool FetchScope::fetchRemoteId() const
{
    return d->fetchFlags & RemoteID;
}
bool FetchScope::fetchGID() const
{
    return d->fetchFlags & GID;
}
bool FetchScope::fetchTags() const
{
    return d->fetchFlags & Tags;
}
bool FetchScope::fetchRelations() const
{
    return d->fetchFlags & Relations;
}
bool FetchScope::fetchVirtualReferences() const
{
    return d->fetchFlags & VirtReferences;
}

void FetchScope::setFetch(FetchFlags attributes, bool fetch)
{
    if (fetch) {
        d->fetchFlags |= attributes;
        if (attributes & FullPayload) {
            if (!d->requestedParts.contains(AKONADI_PARAM_PLD_RFC822)) {
                d->requestedParts << AKONADI_PARAM_PLD_RFC822;
            }
        }
    } else {
        d->fetchFlags &= ~attributes;
    }
}

bool FetchScope::fetch(FetchFlags flags) const
{
    if (flags == None) {
        return d->fetchFlags == None;
    } else {
        return d->fetchFlags & flags;
    }
}

void FetchScope::debugString(DebugBlock &blck) const
{
    blck.write("Fetch Flags", d->fetchFlags);
    blck.write("Tag Fetch Scope", d->tagFetchScope);
    blck.write("Changed Since", d->changedSince);
    blck.write("Ancestor Depth", d->ancestorDepth);
    blck.write("Requested Parts", d->requestedParts);
}

DataStream &operator<<(DataStream &stream, const FetchScope &scope)
{
    return stream << scope.d->requestedParts
           << scope.d->changedSince
           << scope.d->tagFetchScope
           << scope.d->ancestorDepth
           << scope.d->fetchFlags;
}

DataStream &operator>>(DataStream &stream, FetchScope &scope)
{
    return stream >> scope.d->requestedParts
           >> scope.d->changedSince
           >> scope.d->tagFetchScope
           >> scope.d->ancestorDepth
           >> scope.d->fetchFlags;
}

/******************************************************************************/

class ScopeContextPrivate : public QSharedData
{
public:
    ScopeContextPrivate(ScopeContext::Type type = ScopeContext::Collection, const QVariant &ctx = QVariant())
    {
        if (type == ScopeContext::Tag) {
            tagCtx = ctx;
        } else if (type == ScopeContext::Collection) {
            collectionCtx = ctx;
        }
    }

    ScopeContextPrivate(const ScopeContextPrivate &other)
        : QSharedData(other)
        , collectionCtx(other.collectionCtx)
        , tagCtx(other.tagCtx)
    {}

    QVariant ctx(ScopeContext::Type type) const
    {
        switch (type) {
        case ScopeContext::Collection:
            return collectionCtx;
        case ScopeContext::Tag:
            return tagCtx;
        case ScopeContext::Any:
            return QVariant();
        }
        return QVariant();
    }

    void setCtx(ScopeContext::Type type, const QVariant &val)
    {
        switch (type) {
        case ScopeContext::Collection:
            collectionCtx = val;
            break;
        case ScopeContext::Tag:
            tagCtx = val;
            break;
        case ScopeContext::Any:
            break;
        }
    }

    QVariant collectionCtx;
    QVariant tagCtx;
};

ScopeContext::ScopeContext()
    : d(new ScopeContextPrivate)
{
}

ScopeContext::ScopeContext(Type type, qint64 id)
    : d(new ScopeContextPrivate(type, id))
{
}

ScopeContext::ScopeContext(Type type, const QString &rid)
    : d(new ScopeContextPrivate(type, rid))
{
}

ScopeContext::ScopeContext(const ScopeContext &other)
    : d(other.d)
{
}

ScopeContext::ScopeContext(ScopeContext &&other)
{
    d.swap(other.d);
}

ScopeContext::~ScopeContext()
{
}

ScopeContext &ScopeContext::operator=(const ScopeContext &other)
{
    d = other.d;
    return *this;
}

ScopeContext &ScopeContext::operator=(ScopeContext &&other)
{
    d.swap(other.d);
    return *this;
}

bool ScopeContext::operator==(const ScopeContext &other) const
{
    return d == other.d ||
           (d->collectionCtx == other.d->collectionCtx &&
            d->tagCtx == other.d->tagCtx);
}

bool ScopeContext::operator!=(const ScopeContext &other) const
{
    return !(*this == other);
}

bool ScopeContext::isEmpty() const
{
    return d->collectionCtx.isNull() && d->tagCtx.isNull();
}

void ScopeContext::setContext(Type type, qint64 id)
{
    d->setCtx(type, id);
}

void ScopeContext::setContext(Type type, const QString &rid)
{
    d->setCtx(type, rid);
}

void ScopeContext::clearContext(Type type)
{
    d->setCtx(type, QVariant());
}

bool ScopeContext::hasContextId(Type type) const
{
    return d->ctx(type).type() == QVariant::LongLong;
}

qint64 ScopeContext::contextId(Type type) const
{
    return hasContextId(type) ? d->ctx(type).toLongLong() : 0;
}

bool ScopeContext::hasContextRID(Type type) const
{
    return d->ctx(type).type() == QVariant::String;
}

QString ScopeContext::contextRID(Type type) const
{
    return hasContextRID(type) ? d->ctx(type).toString() : QString();
}

void ScopeContext::debugString(DebugBlock &blck) const
{
    blck.write("Tag", d->tagCtx);
    blck.write("Collection", d->collectionCtx);
}

DataStream &operator<<(DataStream &stream, const ScopeContext &context)
{
    // We don't have a custom generic DataStream streaming operator for QVariant
    // because it's very hard, esp. without access to QVariant private
    // stuff, so we have have to decompose it manually here.
    QVariant::Type vType = context.d->collectionCtx.type();
    stream << vType;
    if (vType == QVariant::LongLong) {
        stream << context.d->collectionCtx.toLongLong();
    } else if (vType == QVariant::String) {
        stream << context.d->collectionCtx.toString();
    }

    vType = context.d->tagCtx.type();
    stream << vType;
    if (vType == QVariant::LongLong) {
        stream << context.d->tagCtx.toLongLong();
    } else if (vType == QVariant::String) {
        stream << context.d->tagCtx.toString();
    }

    return stream;
}

DataStream &operator>>(DataStream &stream, ScopeContext &context)
{
    QVariant::Type vType;
    qint64 id;
    QString rid;

    for (ScopeContext::Type type : {
                ScopeContext::Collection, ScopeContext::Tag
            }) {
        stream >> vType;
        if (vType == QVariant::LongLong) {
            stream >> id;
            context.setContext(type, id);
        } else if (vType == QVariant::String) {
            stream >> rid;
            context.setContext(type, rid);
        }
    }

    return stream;
}

#undef CTX

/******************************************************************************/

class PartMetaDataPrivate : public QSharedData
{
public:
    PartMetaDataPrivate(const QByteArray &name = QByteArray(), qint64 size = 0,
                        int version = 0, bool external = false)
        : QSharedData()
        , name(name)
        , size(size)
        , version(version)
        , external(external)
    {}

    PartMetaDataPrivate(const PartMetaDataPrivate &other)
        : QSharedData(other)
        , name(other.name)
        , size(other.size)
        , version(other.version)
        , external(other.external)
    {}

    QByteArray name;
    qint64 size;
    int version;
    bool external;
};

PartMetaData::PartMetaData()
    : d(new PartMetaDataPrivate)
{
}

PartMetaData::PartMetaData(const QByteArray &name, qint64 size, int version, bool external)
    : d(new PartMetaDataPrivate(name, size, version, external))
{
}

PartMetaData::PartMetaData(PartMetaData &&other)
{
    d.swap(other.d);
}

PartMetaData::PartMetaData(const PartMetaData &other)
{
    d = other.d;
}

PartMetaData::~PartMetaData()
{
}

PartMetaData &PartMetaData::operator=(PartMetaData &&other)
{
    d.swap(other.d);
    return *this;
}

PartMetaData &PartMetaData::operator=(const PartMetaData &other)
{
    d = other.d;
    return *this;
}

bool PartMetaData::operator==(const PartMetaData &other) const
{
    return (d == other.d)
           || (d->name == other.d->name
               && d->size == other.d->size
               && d->version == other.d->version
               && d->external == other.d->external);
}

bool PartMetaData::operator!=(const PartMetaData &other) const
{
    return !(*this == other);
}

bool PartMetaData::operator<(const PartMetaData &other) const
{
    return d->name < other.d->name;
}

void PartMetaData::setName(const QByteArray &name)
{
    d->name = name;
}
QByteArray PartMetaData::name() const
{
    return d->name;
}

void PartMetaData::setSize(qint64 size)
{
    d->size = size;
}
qint64 PartMetaData::size() const
{
    return d->size;
}

void PartMetaData::setVersion(int version)
{
    d->version = version;
}
int PartMetaData::version() const
{
    return d->version;
}

void PartMetaData::setIsExternal(bool external)
{
    d->external = external;
}
bool PartMetaData::isExternal() const
{
    return d->external;
}

DataStream &operator<<(DataStream &stream, const PartMetaData &part)
{
    return stream << part.d->name
           << part.d->size
           << part.d->version
           << part.d->external;
}

DataStream &operator>>(DataStream &stream, PartMetaData &part)
{
    return stream >> part.d->name
           >> part.d->size
           >> part.d->version
           >> part.d->external;
}

/******************************************************************************/

class CachePolicyPrivate : public QSharedData
{
public:
    CachePolicyPrivate()
        : syncOnDemand(false)
        , inherit(true)
        , interval(-1)
        , cacheTimeout(-1)
    {}

    bool syncOnDemand;
    bool inherit;
    QStringList localParts;
    int interval;
    int cacheTimeout;
};

CachePolicy::CachePolicy()
    : d(new CachePolicyPrivate)
{
}

CachePolicy::CachePolicy(CachePolicy &&other)
{
    d.swap(other.d);
}

CachePolicy::CachePolicy(const CachePolicy &other)
    : d(other.d)
{
}

CachePolicy::~CachePolicy()
{
}

CachePolicy &CachePolicy::operator=(CachePolicy &&other)
{
    d.swap(other.d);
    return *this;
}

CachePolicy &CachePolicy::operator=(const CachePolicy &other)
{
    d = other.d;
    return *this;
}

bool CachePolicy::operator==(const CachePolicy &other) const
{
    return (d == other.d)
           || (d->localParts == other.d->localParts
               && d->interval == other.d->interval
               && d->cacheTimeout == other.d->cacheTimeout
               && d->syncOnDemand == other.d->syncOnDemand
               && d->inherit == other.d->inherit);
}

bool CachePolicy::operator!=(const CachePolicy &other) const
{
    return !(*this == other);
}

void CachePolicy::setInherit(bool inherit)
{
    d->inherit = inherit;
}
bool CachePolicy::inherit() const
{
    return d->inherit;
}

void CachePolicy::setCheckInterval(int interval)
{
    d->interval = interval;
}
int CachePolicy::checkInterval() const
{
    return d->interval;
}

void CachePolicy::setCacheTimeout(int timeout)
{
    d->cacheTimeout = timeout;
}
int CachePolicy::cacheTimeout() const
{
    return d->cacheTimeout;
}

void CachePolicy::setSyncOnDemand(bool onDemand)
{
    d->syncOnDemand = onDemand;
}
bool CachePolicy::syncOnDemand() const
{
    return d->syncOnDemand;
}

void CachePolicy::setLocalParts(const QStringList &localParts)
{
    d->localParts = localParts;
}
QStringList CachePolicy::localParts() const
{
    return d->localParts;
}

void CachePolicy::debugString(DebugBlock &blck) const
{
    blck.write("Inherit", d->inherit);
    blck.write("Interval", d->interval);
    blck.write("Cache Timeout", d->cacheTimeout);
    blck.write("Sync on Demand", d->syncOnDemand);
    blck.write("Local Parts", d->localParts);
}

DataStream &operator<<(DataStream &stream, const CachePolicy &policy)
{
    return stream << policy.d->inherit
           << policy.d->interval
           << policy.d->cacheTimeout
           << policy.d->syncOnDemand
           << policy.d->localParts;
}

DataStream &operator>>(DataStream &stream, CachePolicy &policy)
{
    return stream >> policy.d->inherit
           >> policy.d->interval
           >> policy.d->cacheTimeout
           >> policy.d->syncOnDemand
           >> policy.d->localParts;
}

/******************************************************************************/

class AncestorPrivate : public QSharedData
{
public:
    AncestorPrivate(qint64 id = -1, const QString &remoteId = QString())
        : id(id)
        , remoteId(remoteId)
    {}

    qint64 id;
    QString remoteId;
    QString name;
    Attributes attrs;
};

Ancestor::Ancestor()
    : d(new AncestorPrivate)
{
}

Ancestor::Ancestor(qint64 id)
    : d(new AncestorPrivate(id))
{
}

Ancestor::Ancestor(qint64 id, const QString &remoteId)
    : d(new AncestorPrivate(id, remoteId))
{
}

Ancestor::Ancestor(Ancestor &&other)
{
    d.swap(other.d);
}

Ancestor::Ancestor(const Ancestor &other)
    : d(other.d)
{
}

Ancestor::~Ancestor()
{
}

Ancestor &Ancestor::operator=(Ancestor &&other)
{
    d.swap(other.d);
    return *this;
}

Ancestor &Ancestor::operator=(const Ancestor &other)
{
    d = other.d;
    return *this;
}

bool Ancestor::operator==(const Ancestor &other) const
{
    return (d == other.d)
           || (d->id == other.d->id
               && d->remoteId == other.d->remoteId
               && d->name == other.d->name
               && d->attrs == other.d->attrs);
}

bool Ancestor::operator!=(const Ancestor &other) const
{
    return !(*this == other);
}

void Ancestor::setId(qint64 id)
{
    d->id = id;
}
qint64 Ancestor::id() const
{
    return d->id;
}

void Ancestor::setRemoteId(const QString &remoteId)
{
    d->remoteId = remoteId;
}
QString Ancestor::remoteId() const
{
    return d->remoteId;
}

void Ancestor::setName(const QString &name)
{
    d->name = name;
}
QString Ancestor::name() const
{
    return d->name;
}

void Ancestor::setAttributes(const Attributes &attributes)
{
    d->attrs = attributes;
}
Attributes Ancestor::attributes() const
{
    return d->attrs;
}

void Ancestor::debugString(DebugBlock &blck) const
{
    blck.write("ID", d->id);
    blck.write("Remote ID", d->remoteId);
    blck.write("Name", d->name);
    blck.write("Attributes", d->attrs);
}

DataStream &operator<<(DataStream &stream, const Ancestor &ancestor)
{
    return stream << ancestor.d->id
           << ancestor.d->remoteId
           << ancestor.d->name
           << ancestor.d->attrs;
}

DataStream &operator>>(DataStream &stream, Ancestor &ancestor)
{
    return stream >> ancestor.d->id
           >> ancestor.d->remoteId
           >> ancestor.d->name
           >> ancestor.d->attrs;
}

/******************************************************************************/

class HelloResponsePrivate : public ResponsePrivate
{
public:
    HelloResponsePrivate()
        : ResponsePrivate(Command::Hello)
        , protocol(0)
        , generation(0)
    {}

    HelloResponsePrivate(const HelloResponsePrivate &other)
        : ResponsePrivate(other)
        , server(other.server)
        , message(other.message)
        , protocol(other.protocol)
        , generation(other.generation)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(server)
               && COMPARE(message)
               && COMPARE(protocol)
               && COMPARE(generation);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << server
               << message
               << protocol
               << generation;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> server
        >> message
        >> protocol
        >> generation;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("Server", server);
        blck.write("Protocol Version", protocol);
        blck.write("Message", message);
        blck.write("Generation", generation);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new HelloResponsePrivate(*this);
    }

    QString server;
    QString message;
    int protocol;
    uint generation;
};

#define checkCopyInvariant(_cmdType) \
    if (std::is_base_of<Response, std::remove_pointer<decltype(this)>::type>::value) { \
        assert(d_func()->commandType == Command::Invalid || d_func()->commandType == (_cmdType | Command::_ResponseBit)); \
    } else { \
        assert(d_func()->commandType == Command::Invalid || d_func()->commandType == _cmdType); \
    }

AKONADI_DECLARE_PRIVATE(HelloResponse)

HelloResponse::HelloResponse()
    : Response(new HelloResponsePrivate)
{
}

HelloResponse::HelloResponse(const Command &command)
    : Response(command)
{
    checkCopyInvariant(Command::Hello);
}

void HelloResponse::setServerName(const QString &server)
{
    d_func()->server = server;
}

QString HelloResponse::serverName() const
{
    return d_func()->server;
}

void HelloResponse::setMessage(const QString &message)
{
    d_func()->message = message;
}

QString HelloResponse::message() const
{
    return d_func()->message;
}

void HelloResponse::setProtocolVersion(int protocolVersion)
{
    d_func()->protocol = protocolVersion;
}

int HelloResponse::protocolVersion() const
{
    return d_func()->protocol;
}

void HelloResponse::setGeneration(uint generation)
{
    d_func()->generation = generation;
}

uint HelloResponse::generation() const
{
    return d_func()->generation;
}

DataStream &operator<<(DataStream &stream, const HelloResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, HelloResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

class LoginCommandPrivate : public CommandPrivate
{
public:
    LoginCommandPrivate(const QByteArray &sessionId = QByteArray())
        : CommandPrivate(Command::Login)
        , sessionId(sessionId)

    {}

    LoginCommandPrivate(const LoginCommandPrivate &other)
        : CommandPrivate(other)
        , sessionId(other.sessionId)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(sessionId);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << sessionId;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> sessionId;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Session ID", sessionId);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new LoginCommandPrivate(*this);
    }

    QByteArray sessionId;
};

AKONADI_DECLARE_PRIVATE(LoginCommand)

LoginCommand::LoginCommand()
    : Command(new LoginCommandPrivate)
{
}

LoginCommand::LoginCommand(const QByteArray &sessionId)
    : Command(new LoginCommandPrivate(sessionId))
{
}

LoginCommand::LoginCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::Login);
}

void LoginCommand::setSessionId(const QByteArray &sessionId)
{
    d_func()->sessionId = sessionId;
}

QByteArray LoginCommand::sessionId() const
{
    return d_func()->sessionId;
}

DataStream &operator<<(DataStream &stream, const LoginCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, LoginCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

LoginResponse::LoginResponse()
    : Response(new ResponsePrivate(Command::Login))
{
}

LoginResponse::LoginResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::Login);
}

/******************************************************************************/

LogoutCommand::LogoutCommand()
    : Command(new CommandPrivate(Command::Logout))
{
}

LogoutCommand::LogoutCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::Logout);
}

/******************************************************************************/

LogoutResponse::LogoutResponse()
    : Response(new ResponsePrivate(Command::Logout))
{
}

LogoutResponse::LogoutResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::Logout);
}

/******************************************************************************/

class TransactionCommandPrivate : public CommandPrivate
{
public:
    TransactionCommandPrivate(TransactionCommand::Mode mode = TransactionCommand::Invalid)
        : CommandPrivate(Command::Transaction)
        , mode(mode)
    {}

    TransactionCommandPrivate(const TransactionCommandPrivate &other)
        : CommandPrivate(other)
        , mode(other.mode)
    {}

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Mode", [this]() -> const char * {
            switch (mode) {
            case TransactionCommand::Begin:
                return "Begin";
            case TransactionCommand::Commit:
                return "Commit";
            case TransactionCommand::Rollback:
                return "Rollback";
            default:
                return "Invalid";
            }
        }());
    }

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(mode);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << mode;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> mode;
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new TransactionCommandPrivate(*this);
    }

    TransactionCommand::Mode mode;
};

AKONADI_DECLARE_PRIVATE(TransactionCommand)

TransactionCommand::TransactionCommand()
    : Command(new TransactionCommandPrivate)
{
}

TransactionCommand::TransactionCommand(TransactionCommand::Mode mode)
    : Command(new TransactionCommandPrivate(mode))
{
}

TransactionCommand::TransactionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::Transaction);
}

void TransactionCommand::setMode(Mode mode)
{
    d_func()->mode = mode;
}

TransactionCommand::Mode TransactionCommand::mode() const
{
    return d_func()->mode;
}

DataStream &operator<<(DataStream &stream, const TransactionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, TransactionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

TransactionResponse::TransactionResponse()
    : Response(new ResponsePrivate(Command::Transaction))
{
}

TransactionResponse::TransactionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::Transaction);
}

/******************************************************************************/

class CreateItemCommandPrivate : public CommandPrivate
{
public:
    CreateItemCommandPrivate()
        : CommandPrivate(Command::CreateItem)
        , mergeMode(CreateItemCommand::None)
        , itemSize(0)
    {}

    CreateItemCommandPrivate(const CreateItemCommandPrivate &other)
        : CommandPrivate(other)
        , collection(other.collection)
        , mimeType(other.mimeType)
        , gid(other.gid)
        , remoteId(other.remoteId)
        , remoteRev(other.remoteRev)
        , dateTime(other.dateTime)
        , tags(other.tags)
        , addedTags(other.addedTags)
        , removedTags(other.removedTags)
        , flags(other.flags)
        , addedFlags(other.addedFlags)
        , removedFlags(other.removedFlags)
        , parts(other.parts)
        , attributes(other.attributes)
        , mergeMode(other.mergeMode)
        , itemSize(other.itemSize)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(mergeMode)
               && COMPARE(itemSize)
               && COMPARE(collection)
               && COMPARE(mimeType)
               && COMPARE(gid)
               && COMPARE(remoteId)
               && COMPARE(remoteRev)
               && COMPARE(dateTime)
               && COMPARE(tags)
               && COMPARE(addedTags)
               && COMPARE(removedTags)
               && COMPARE(flags)
               && COMPARE(addedFlags)
               && COMPARE(removedFlags)
               && COMPARE(attributes)
               && COMPARE(parts);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << mergeMode
               << collection
               << itemSize
               << mimeType
               << gid
               << remoteId
               << remoteRev
               << dateTime
               << flags
               << addedFlags
               << removedFlags
               << tags
               << addedTags
               << removedTags
               << attributes
               << parts;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> mergeMode
        >> collection
        >> itemSize
        >> mimeType
        >> gid
        >> remoteId
        >> remoteRev
        >> dateTime
        >> flags
        >> addedFlags
        >> removedFlags
        >> tags
        >> addedTags
        >> removedTags
        >> attributes
        >> parts;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Merge mode", [this]() {
            QStringList mm;
            if (mergeMode == CreateItemCommand::None) {
                mm << QStringLiteral("None");
            } else {
                if (mergeMode & CreateItemCommand::GID) {
                    mm << QStringLiteral("GID");
                }
                if (mergeMode & CreateItemCommand::RemoteID) {
                    mm << QStringLiteral("Remote ID");
                }
                if (mergeMode & CreateItemCommand::Silent) {
                    mm << QStringLiteral("Silent");
                }
            }
            return mm;
        }());
        blck.write("Collection", collection);
        blck.write("MimeType", mimeType);
        blck.write("GID", gid);
        blck.write("Remote ID", remoteId);
        blck.write("Remote Revision", remoteRev);
        blck.write("Size", itemSize);
        blck.write("Time", dateTime);
        blck.write("Tags", tags);
        blck.write("Added Tags", addedTags);
        blck.write("Removed Tags", removedTags);
        blck.write("Flags", flags);
        blck.write("Added Flags", addedFlags);
        blck.write("Removed Flags", removedFlags);
        blck.beginBlock("Attributes");
        for (auto iter = attributes.constBegin(); iter != attributes.constEnd(); ++iter) {
            blck.write(iter.key().constData(), iter.value());
        }
        blck.endBlock();
        blck.write("Parts", parts);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CreateItemCommandPrivate(*this);
    }

    Scope collection;
    QString mimeType;
    QString gid;
    QString remoteId;
    QString remoteRev;
    QDateTime dateTime;
    Scope tags;
    Scope addedTags;
    Scope removedTags;
    QSet<QByteArray> flags;
    QSet<QByteArray> addedFlags;
    QSet<QByteArray> removedFlags;
    QSet<QByteArray> parts;
    Attributes attributes;
    CreateItemCommand::MergeModes mergeMode;
    qint64 itemSize;
};

AKONADI_DECLARE_PRIVATE(CreateItemCommand)

CreateItemCommand::CreateItemCommand()
    : Command(new CreateItemCommandPrivate)
{
}

CreateItemCommand::CreateItemCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::CreateItem);
}

void CreateItemCommand::setMergeModes(const MergeModes &mode)
{
    d_func()->mergeMode = mode;
}
CreateItemCommand::MergeModes CreateItemCommand::mergeModes() const
{
    return d_func()->mergeMode;
}

void CreateItemCommand::setCollection(const Scope &collection)
{
    d_func()->collection = collection;
}
Scope CreateItemCommand::collection() const
{
    return d_func()->collection;
}

void CreateItemCommand::setItemSize(qint64 size)
{
    d_func()->itemSize = size;
}
qint64 CreateItemCommand::itemSize() const
{
    return d_func()->itemSize;
}

void CreateItemCommand::setMimeType(const QString &mimeType)
{
    d_func()->mimeType = mimeType;
}
QString CreateItemCommand::mimeType() const
{
    return d_func()->mimeType;
}

void CreateItemCommand::setGID(const QString &gid)
{
    d_func()->gid = gid;
}
QString CreateItemCommand::gid() const
{
    return d_func()->gid;
}

void CreateItemCommand::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString CreateItemCommand::remoteId() const
{
    return d_func()->remoteId;
}

void CreateItemCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}

QString CreateItemCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void CreateItemCommand::setDateTime(const QDateTime &dateTime)
{
    d_func()->dateTime = dateTime;
}
QDateTime CreateItemCommand::dateTime() const
{
    return d_func()->dateTime;
}

void CreateItemCommand::setFlags(const QSet<QByteArray> &flags)
{
    d_func()->flags = flags;
}
QSet<QByteArray> CreateItemCommand::flags() const
{
    return d_func()->flags;
}
void CreateItemCommand::setAddedFlags(const QSet<QByteArray> &flags)
{
    d_func()->addedFlags = flags;
}
QSet<QByteArray> CreateItemCommand::addedFlags() const
{
    return d_func()->addedFlags;
}
void CreateItemCommand::setRemovedFlags(const QSet<QByteArray> &flags)
{
    d_func()->removedFlags = flags;
}
QSet<QByteArray> CreateItemCommand::removedFlags() const
{
    return d_func()->removedFlags;
}

void CreateItemCommand::setTags(const Scope &tags)
{
    d_func()->tags = tags;
}
Scope CreateItemCommand::tags() const
{
    return d_func()->tags;
}
void CreateItemCommand::setAddedTags(const Scope &tags)
{
    d_func()->addedTags = tags;
}
Scope CreateItemCommand::addedTags() const
{
    return d_func()->addedTags;
}
void CreateItemCommand::setRemovedTags(const Scope &tags)
{
    d_func()->removedTags = tags;
}
Scope CreateItemCommand::removedTags() const
{
    return d_func()->removedTags;
}
void CreateItemCommand::setAttributes(const Attributes &attrs)
{
    d_func()->attributes = attrs;
}
Attributes CreateItemCommand::attributes() const
{
    return d_func()->attributes;
}
void CreateItemCommand::setParts(const QSet<QByteArray> &parts)
{
    d_func()->parts = parts;
}
QSet<QByteArray> CreateItemCommand::parts() const
{
    return d_func()->parts;
}

DataStream &operator<<(DataStream &stream, const CreateItemCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, CreateItemCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

CreateItemResponse::CreateItemResponse()
    : Response(new ResponsePrivate(Command::CreateItem))
{
}

CreateItemResponse::CreateItemResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::CreateItem);
}

/******************************************************************************/

class CopyItemsCommandPrivate : public CommandPrivate
{
public:
    CopyItemsCommandPrivate()
        : CommandPrivate(Command::CopyItems)
    {}
    CopyItemsCommandPrivate(const Scope &items, const Scope &dest)
        : CommandPrivate(Command::CopyItems)
        , items(items)
        , dest(dest)
    {}
    CopyItemsCommandPrivate(const CopyItemsCommandPrivate &other)
        : CommandPrivate(other)
        , items(other.items)
        , dest(other.dest)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(items)
               && COMPARE(dest);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << items
               << dest;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> items
        >> dest;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Items", items);
        blck.write("Destination", dest);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CopyItemsCommandPrivate(*this);
    }

    Scope items;
    Scope dest;
};

AKONADI_DECLARE_PRIVATE(CopyItemsCommand)

CopyItemsCommand::CopyItemsCommand()
    : Command(new CopyItemsCommandPrivate)
{
}

CopyItemsCommand::CopyItemsCommand(const Scope &items, const Scope &dest)
    : Command(new CopyItemsCommandPrivate(items, dest))
{
}

CopyItemsCommand::CopyItemsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::CopyItems);
}

void CopyItemsCommand::setItems(const Scope &items)
{
    d_func()->items = items;
}

Scope CopyItemsCommand::items() const
{
    return d_func()->items;
}

void CopyItemsCommand::setDestination(const Scope &dest)
{
    d_func()->dest = dest;
}

Scope CopyItemsCommand::destination() const
{
    return d_func()->dest;
}

DataStream &operator<<(DataStream &stream, const CopyItemsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, CopyItemsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

CopyItemsResponse::CopyItemsResponse()
    : Response(new ResponsePrivate(Command::CopyItems))
{
}

CopyItemsResponse::CopyItemsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::CopyItems);
}

/******************************************************************************/

class DeleteItemsCommandPrivate : public CommandPrivate
{
public:
    DeleteItemsCommandPrivate(const Scope &items = Scope(), const ScopeContext &context = ScopeContext())
        : CommandPrivate(Command::DeleteItems)
        , items(items)
        , context(context)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(items)
               && COMPARE(context);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << items
               << context;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> items
        >> context;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Items", items);
        blck.beginBlock("Context");
        context.debugString(blck);
        blck.endBlock();
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new DeleteItemsCommandPrivate(*this);
    }

    Scope items;
    ScopeContext context;
};

AKONADI_DECLARE_PRIVATE(DeleteItemsCommand)

DeleteItemsCommand::DeleteItemsCommand()
    : Command(new DeleteItemsCommandPrivate)
{
}

DeleteItemsCommand::DeleteItemsCommand(const Scope &items, const ScopeContext &context)
    : Command(new DeleteItemsCommandPrivate(items, context))
{
}

DeleteItemsCommand::DeleteItemsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::DeleteItems);
}

Scope DeleteItemsCommand::items() const
{
    return d_func()->items;
}

ScopeContext DeleteItemsCommand::scopeContext() const
{
    return d_func()->context;
}

DataStream &operator<<(DataStream &stream, const DeleteItemsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, DeleteItemsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

DeleteItemsResponse::DeleteItemsResponse()
    : Response(new ResponsePrivate(Command::DeleteItems))
{
}

DeleteItemsResponse::DeleteItemsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::DeleteItems);
}

/******************************************************************************/

class FetchRelationsCommandPrivate : public CommandPrivate
{
public:
    FetchRelationsCommandPrivate(qint64 left = -1, qint64 right = -1, qint64 side = -1,
                                 const QVector<QByteArray> &types = QVector<QByteArray>(),
                                 const QString &resource = QString())
        : CommandPrivate(Command::FetchRelations)
        , left(left)
        , right(right)
        , side(side)
        , types(types)
        , resource(resource)
    {}
    FetchRelationsCommandPrivate(const FetchRelationsCommandPrivate &other)
        : CommandPrivate(other)
        , left(other.left)
        , right(other.right)
        , side(other.side)
        , types(other.types)
        , resource(other.resource)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(left)
               && COMPARE(right)
               && COMPARE(side)
               && COMPARE(types)
               && COMPARE(resource);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << left
               << right
               << side
               << types
               << resource;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> left
        >> right
        >> side
        >> types
        >> resource;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Left", left);
        blck.write("Right", right);
        blck.write("Side", side);
        blck.write("Types", types);
        blck.write("Resource", resource);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchRelationsCommandPrivate(*this);
    }

    qint64 left;
    qint64 right;
    qint64 side;
    QVector<QByteArray> types;
    QString resource;
};

AKONADI_DECLARE_PRIVATE(FetchRelationsCommand)

FetchRelationsCommand::FetchRelationsCommand()
    : Command(new FetchRelationsCommandPrivate)
{
}

FetchRelationsCommand::FetchRelationsCommand(qint64 side, const QVector<QByteArray> &types,
        const QString &resource)
    : Command(new FetchRelationsCommandPrivate(-1, -1, side, types, resource))
{
}

FetchRelationsCommand::FetchRelationsCommand(qint64 left, qint64 right,
        const QVector<QByteArray> &types,
        const QString &resource)
    : Command(new FetchRelationsCommandPrivate(left, right, -1, types, resource))
{
}

FetchRelationsCommand::FetchRelationsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::FetchRelations);
}

void FetchRelationsCommand::setLeft(qint64 left)
{
    d_func()->left = left;
}
qint64 FetchRelationsCommand::left() const
{
    return d_func()->left;
}

void FetchRelationsCommand::setRight(qint64 right)
{
    d_func()->right = right;
}
qint64 FetchRelationsCommand::right() const
{
    return d_func()->right;
}

void FetchRelationsCommand::setSide(qint64 side)
{
    d_func()->side = side;
}
qint64 FetchRelationsCommand::side() const
{
    return d_func()->side;
}

void FetchRelationsCommand::setTypes(const QVector<QByteArray> &types)
{
    d_func()->types = types;
}
QVector<QByteArray> FetchRelationsCommand::types() const
{
    return d_func()->types;
}

void FetchRelationsCommand::setResource(const QString &resource)
{
    d_func()->resource = resource;
}
QString FetchRelationsCommand::resource() const
{
    return d_func()->resource;
}

DataStream &operator<<(DataStream &stream, const FetchRelationsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchRelationsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/*****************************************************************************/

class FetchRelationsResponsePrivate : public ResponsePrivate
{
public:
    FetchRelationsResponsePrivate(qint64 left = -1, const QByteArray &leftMimeType = QByteArray(),
                                  qint64 right = -1, const QByteArray &rightMimeType = QByteArray(),
                                  const QByteArray &type = QByteArray(),
                                  const QByteArray &remoteId = QByteArray())
        : ResponsePrivate(Command::FetchRelations)
        , left(left)
        , leftMimeType(leftMimeType)
        , right(right)
        , rightMimeType(rightMimeType)
        , type(type)
        , remoteId(remoteId)
    {}
    FetchRelationsResponsePrivate(const FetchRelationsResponsePrivate &other)
        : ResponsePrivate(other)
        , left(other.left)
        , leftMimeType(other.leftMimeType)
        , right(other.right)
        , rightMimeType(other.rightMimeType)
        , type(other.type)
        , remoteId(other.remoteId)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(left)
               && COMPARE(leftMimeType)
               && COMPARE(right)
               && COMPARE(rightMimeType)
               && COMPARE(type)
               && COMPARE(remoteId);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << left
               << leftMimeType
               << right
               << rightMimeType
               << type
               << remoteId;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> left
        >> leftMimeType
        >> right
        >> rightMimeType
        >> type
        >> remoteId;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("Left", left);
        blck.write("LeftMimeType", leftMimeType);
        blck.write("Right", right);
        blck.write("RightMimeType", rightMimeType);
        blck.write("Type", type);
        blck.write("Remote ID", remoteId);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchRelationsResponsePrivate(*this);
    }

    qint64 left;
    QByteArray leftMimeType;
    qint64 right;
    QByteArray rightMimeType;
    QByteArray type;
    QByteArray remoteId;
};

AKONADI_DECLARE_PRIVATE(FetchRelationsResponse)

FetchRelationsResponse::FetchRelationsResponse()
    : Response(new FetchRelationsResponsePrivate)
{
}

FetchRelationsResponse::FetchRelationsResponse(qint64 left, const QByteArray &leftMimeType,
        qint64 right, const QByteArray &rightMimeType,
        const QByteArray &type,
        const QByteArray &remoteId)
    : Response(new FetchRelationsResponsePrivate(left, leftMimeType, right, rightMimeType, type, remoteId))
{
}

FetchRelationsResponse::FetchRelationsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::FetchRelations);
}

qint64 FetchRelationsResponse::left() const
{
    return d_func()->left;
}
QByteArray FetchRelationsResponse::leftMimeType() const
{
    return d_func()->leftMimeType;
}
qint64 FetchRelationsResponse::right() const
{
    return d_func()->right;
}
QByteArray FetchRelationsResponse::rightMimeType() const
{
    return d_func()->rightMimeType;
}
QByteArray FetchRelationsResponse::type() const
{
    return d_func()->type;
}
void FetchRelationsResponse::setRemoteId(const QByteArray &remoteId)
{
    d_func()->remoteId = remoteId;
}
QByteArray FetchRelationsResponse::remoteId() const
{
    return d_func()->remoteId;
}

DataStream &operator<<(DataStream &stream, const FetchRelationsResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchRelationsResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

class FetchTagsCommandPrivate : public CommandPrivate
{
public:
    FetchTagsCommandPrivate(const Scope &scope = Scope())
        : CommandPrivate(Command::FetchTags)
        , scope(scope)
        , idOnly(false)
    {}
    FetchTagsCommandPrivate(const FetchTagsCommandPrivate &other)
        : CommandPrivate(other)
        , scope(other.scope)
        , attributes(other.attributes)
        , idOnly(other.idOnly)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(idOnly)
               && COMPARE(scope)
               && COMPARE(attributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << scope
               << attributes
               << idOnly;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> scope
        >> attributes
        >> idOnly;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Tags", scope);
        blck.write("Attributes", attributes);
        blck.write("ID only", idOnly);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchTagsCommandPrivate(*this);
    }

    Scope scope;
    QSet<QByteArray> attributes;
    bool idOnly;
};

AKONADI_DECLARE_PRIVATE(FetchTagsCommand)

FetchTagsCommand::FetchTagsCommand()
    : Command(new FetchTagsCommandPrivate)
{
}

FetchTagsCommand::FetchTagsCommand(const Scope &scope)
    : Command(new FetchTagsCommandPrivate(scope))
{
}

FetchTagsCommand::FetchTagsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::FetchTags);
}

Scope FetchTagsCommand::scope() const
{
    return d_func()->scope;
}

void FetchTagsCommand::setAttributes(const QSet<QByteArray> &attributes)
{
    d_func()->attributes = attributes;
}
QSet<QByteArray> FetchTagsCommand::attributes() const
{
    return d_func()->attributes;
}

void FetchTagsCommand::setIdOnly(bool idOnly)
{
    d_func()->idOnly = idOnly;
}
bool FetchTagsCommand::idOnly() const
{
    return d_func()->idOnly;
}

DataStream &operator<<(DataStream &stream, const FetchTagsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchTagsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/*****************************************************************************/

class FetchTagsResponsePrivate : public ResponsePrivate
{
public:
    FetchTagsResponsePrivate(qint64 id = -1, const QByteArray &gid = QByteArray(),
                             const QByteArray &type = QByteArray(),
                             const QByteArray &remoteId = QByteArray(),
                             qint64 parentId = -1,
                             const Attributes &attrs = Attributes())
        : ResponsePrivate(Command::FetchTags)
        , id(id)
        , parentId(parentId)
        , gid(gid)
        , type(type)
        , remoteId(remoteId)
        , attributes(attrs)
    {}
    FetchTagsResponsePrivate(const FetchTagsResponsePrivate &other)
        : ResponsePrivate(other)
        , id(other.id)
        , parentId(other.parentId)
        , gid(other.gid)
        , type(other.type)
        , remoteId(other.remoteId)
        , attributes(other.attributes)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(id)
               && COMPARE(parentId)
               && COMPARE(gid)
               && COMPARE(type)
               && COMPARE(remoteId)
               && COMPARE(attributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << id
               << parentId
               << gid
               << type
               << remoteId
               << attributes;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> id
        >> parentId
        >> gid
        >> type
        >> remoteId
        >> attributes;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("ID", id);
        blck.write("Parent ID", parentId);
        blck.write("GID", gid);
        blck.write("Type", type);
        blck.write("Remote ID", remoteId);
        blck.write("Attributes", attributes);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchTagsResponsePrivate(*this);
    }

    qint64 id;
    qint64 parentId;
    QByteArray gid;
    QByteArray type;
    QByteArray remoteId;
    Attributes attributes;
};

AKONADI_DECLARE_PRIVATE(FetchTagsResponse)

FetchTagsResponse::FetchTagsResponse()
    : Response(new FetchTagsResponsePrivate)
{
}

FetchTagsResponse::FetchTagsResponse(qint64 id)
    : Response(new FetchTagsResponsePrivate(id))
{
}

FetchTagsResponse::FetchTagsResponse(qint64 id, const QByteArray &gid, const QByteArray &type,
                                     const QByteArray &remoteId,
                                     qint64 parentId, const Attributes &attrs)
    : Response(new FetchTagsResponsePrivate(id, gid, type, remoteId, parentId, attrs))
{
}

FetchTagsResponse::FetchTagsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::FetchTags);
}

qint64 FetchTagsResponse::id() const
{
    return d_func()->id;
}

void FetchTagsResponse::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 FetchTagsResponse::parentId() const
{
    return d_func()->parentId;
}

void FetchTagsResponse::setGid(const QByteArray &gid)
{
    d_func()->gid = gid;
}
QByteArray FetchTagsResponse::gid() const
{
    return d_func()->gid;
}

void FetchTagsResponse::setType(const QByteArray &type)
{
    d_func()->type = type;
}
QByteArray FetchTagsResponse::type() const
{
    return d_func()->type;
}

void FetchTagsResponse::setRemoteId(const QByteArray &remoteId)
{
    d_func()->remoteId = remoteId;
}
QByteArray FetchTagsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchTagsResponse::setAttributes(const Attributes &attributes)
{
    d_func()->attributes = attributes;
}
Attributes FetchTagsResponse::attributes() const
{
    return d_func()->attributes;
}

DataStream &operator<<(DataStream &stream, const FetchTagsResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchTagsResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/*****************************************************************************/

class FetchItemsCommandPrivate : public CommandPrivate
{
public:
    FetchItemsCommandPrivate(const Scope &scope = Scope(),
                             const ScopeContext &context = ScopeContext(),
                             const FetchScope &fetchScope = FetchScope())
        : CommandPrivate(Command::FetchItems)
        , scope(scope)
        , scopeContext(context)
        , fetchScope(fetchScope)
    {}

    FetchItemsCommandPrivate(const FetchItemsCommandPrivate &other)
        : CommandPrivate(other)
        , scope(other.scope)
        , scopeContext(other.scopeContext)
        , fetchScope(other.fetchScope)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(scope)
               && COMPARE(scopeContext)
               && COMPARE(fetchScope);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << scope
               << scopeContext
               << fetchScope;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> scope
        >> scopeContext
        >> fetchScope;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Items", scope);
        blck.beginBlock("Scope Context");
        scopeContext.debugString(blck);
        blck.endBlock();
        blck.beginBlock("Fetch Scope");
        fetchScope.debugString(blck);
        blck.endBlock();
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchItemsCommandPrivate(*this);
    }

    Scope scope;
    ScopeContext scopeContext;
    FetchScope fetchScope;
};

AKONADI_DECLARE_PRIVATE(FetchItemsCommand)

FetchItemsCommand::FetchItemsCommand()
    : Command(new FetchItemsCommandPrivate)
{
}

FetchItemsCommand::FetchItemsCommand(const Scope &scope, const FetchScope &fetchScope)
    : Command(new FetchItemsCommandPrivate(scope, ScopeContext(), fetchScope))
{
}

FetchItemsCommand::FetchItemsCommand(const Scope &scope, const ScopeContext &context,
                                     const FetchScope &fetchScope)
    : Command(new FetchItemsCommandPrivate(scope, context, fetchScope))
{
}

FetchItemsCommand::FetchItemsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::FetchItems);
}

Scope FetchItemsCommand::scope() const
{
    return d_func()->scope;
}

ScopeContext FetchItemsCommand::scopeContext() const
{
    return d_func()->scopeContext;
}

FetchScope FetchItemsCommand::fetchScope() const
{
    return d_func()->fetchScope;
}

FetchScope &FetchItemsCommand::fetchScope()
{
    return d_func()->fetchScope;
}

DataStream &operator<<(DataStream &stream, const FetchItemsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchItemsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class FetchItemsResponsePrivate : public ResponsePrivate
{
public:
    FetchItemsResponsePrivate(qint64 id = -1)
        : ResponsePrivate(Command::FetchItems)
        , id(id)
        , collectionId(-1)
        , size(0)
        , revision(0)
    {}
    FetchItemsResponsePrivate(const FetchItemsResponsePrivate &other)
        : ResponsePrivate(other)
        , remoteId(other.remoteId)
        , remoteRev(other.remoteRev)
        , gid(other.gid)
        , mimeType(other.mimeType)
        , time(other.time)
        , flags(other.flags)
        , tags(other.tags)
        , virtRefs(other.virtRefs)
        , relations(other.relations)
        , ancestors(other.ancestors)
        , parts(other.parts)
        , cachedParts(other.cachedParts)
        , id(other.id)
        , collectionId(other.collectionId)
        , size(other.size)
        , revision(other.revision)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(id)
               && COMPARE(collectionId)
               && COMPARE(size)
               && COMPARE(revision)
               && COMPARE(remoteId)
               && COMPARE(remoteRev)
               && COMPARE(gid)
               && COMPARE(mimeType)
               && COMPARE(time)
               && COMPARE(flags)
               && COMPARE(tags)
               && COMPARE(virtRefs)
               && COMPARE(relations)
               && COMPARE(ancestors)
               && COMPARE(parts)
               && COMPARE(cachedParts);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << id
               << revision
               << collectionId
               << remoteId
               << remoteRev
               << gid
               << size
               << mimeType
               << time
               << flags
               << tags
               << virtRefs
               << relations
               << ancestors
               << parts
               << cachedParts;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> id
        >> revision
        >> collectionId
        >> remoteId
        >> remoteRev
        >> gid
        >> size
        >> mimeType
        >> time
        >> flags
        >> tags
        >> virtRefs
        >> relations
        >> ancestors
        >> parts
        >> cachedParts;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("ID", id);
        blck.write("Revision", revision);
        blck.write("Collection ID", collectionId);
        blck.write("Remote ID", remoteId);
        blck.write("Remote Revision", remoteRev);
        blck.write("GID", gid);
        blck.write("Size", size);
        blck.write("Mimetype", mimeType);
        blck.write("Time", time);
        blck.write("Flags", flags);
        blck.beginBlock("Tags");
        Q_FOREACH (const FetchTagsResponse &tag, tags) {
            blck.beginBlock();
            tag.debugString(blck);
            blck.endBlock();
        }
        blck.endBlock();
        blck.beginBlock("Relations");
        Q_FOREACH (const FetchRelationsResponse &rel, relations) {
            blck.beginBlock();
            rel.debugString(blck);
            blck.endBlock();
        }
        blck.endBlock();
        blck.write("Virtual References", virtRefs);
        blck.beginBlock("Ancestors");
        Q_FOREACH (const Ancestor &anc, ancestors) {
            blck.beginBlock();
            anc.debugString(blck);
            blck.endBlock();
        }
        blck.endBlock();
        blck.write("Cached Parts", cachedParts);
        blck.beginBlock("Parts");
        Q_FOREACH (const StreamPayloadResponse &part, parts) {
            blck.beginBlock(part.payloadName());
            blck.write("Size", part.metaData().size());
            blck.write("External", part.metaData().isExternal());
            blck.write("Version", part.metaData().version());
            blck.write("Data", part.data());
            blck.endBlock();
        }
        blck.endBlock();
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchItemsResponsePrivate(*this);
    }

    QString remoteId;
    QString remoteRev;
    QString gid;
    QString mimeType;
    QDateTime time;
    QVector<QByteArray> flags;
    QVector<FetchTagsResponse> tags;
    QVector<qint64> virtRefs;
    QVector<FetchRelationsResponse> relations;
    QVector<Ancestor> ancestors;
    QVector<StreamPayloadResponse> parts;
    QVector<QByteArray> cachedParts;
    qint64 id;
    qint64 collectionId;
    qint64 size;
    int revision;
};

AKONADI_DECLARE_PRIVATE(FetchItemsResponse)

FetchItemsResponse::FetchItemsResponse()
    : Response(new FetchItemsResponsePrivate)
{
}

FetchItemsResponse::FetchItemsResponse(qint64 id)
    : Response(new FetchItemsResponsePrivate(id))
{
}

FetchItemsResponse::FetchItemsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::FetchItems);
}

qint64 FetchItemsResponse::id() const
{
    return d_func()->id;
}

void FetchItemsResponse::setRevision(int revision)
{
    d_func()->revision = revision;
}
int FetchItemsResponse::revision() const
{
    return d_func()->revision;
}

void FetchItemsResponse::setParentId(qint64 parentId)
{
    d_func()->collectionId = parentId;
}
qint64 FetchItemsResponse::parentId() const
{
    return d_func()->collectionId;
}

void FetchItemsResponse::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString FetchItemsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchItemsResponse::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}
QString FetchItemsResponse::remoteRevision() const
{
    return d_func()->remoteRev;
}

void FetchItemsResponse::setGid(const QString &gid)
{
    d_func()->gid = gid;
}
QString FetchItemsResponse::gid() const
{
    return d_func()->gid;
}

void FetchItemsResponse::setSize(qint64 size)
{
    d_func()->size = size;
}
qint64 FetchItemsResponse::size() const
{
    return d_func()->size;
}

void FetchItemsResponse::setMimeType(const QString &mimeType)
{
    d_func()->mimeType = mimeType;
}
QString FetchItemsResponse::mimeType() const
{
    return d_func()->mimeType;
}

void FetchItemsResponse::setMTime(const QDateTime &mtime)
{
    d_func()->time = mtime;
}
QDateTime FetchItemsResponse::MTime() const
{
    return d_func()->time;
}

void FetchItemsResponse::setFlags(const QVector<QByteArray> &flags)
{
    d_func()->flags = flags;
}
QVector<QByteArray> FetchItemsResponse::flags() const
{
    return d_func()->flags;
}

void FetchItemsResponse::setTags(const QVector<FetchTagsResponse> &tags)
{
    d_func()->tags = tags;
}
QVector<FetchTagsResponse> FetchItemsResponse::tags() const
{
    return d_func()->tags;
}

void FetchItemsResponse::setVirtualReferences(const QVector<qint64> &refs)
{
    d_func()->virtRefs = refs;
}
QVector<qint64> FetchItemsResponse::virtualReferences() const
{
    return d_func()->virtRefs;
}

void FetchItemsResponse::setRelations(const QVector<FetchRelationsResponse> &relations)
{
    d_func()->relations = relations;
}
QVector<FetchRelationsResponse> FetchItemsResponse::relations() const
{
    return d_func()->relations;
}

void FetchItemsResponse::setAncestors(const QVector<Ancestor> &ancestors)
{
    d_func()->ancestors = ancestors;
}
QVector<Ancestor> FetchItemsResponse::ancestors() const
{
    return d_func()->ancestors;
}

void FetchItemsResponse::setParts(const QVector<StreamPayloadResponse> &parts)
{
    d_func()->parts = parts;
}
QVector<StreamPayloadResponse> FetchItemsResponse::parts() const
{
    return d_func()->parts;
}

void FetchItemsResponse::setCachedParts(const QVector<QByteArray> &cachedParts)
{
    d_func()->cachedParts = cachedParts;
}
QVector<QByteArray> FetchItemsResponse::cachedParts() const
{
    return d_func()->cachedParts;
}

DataStream &operator<<(DataStream &stream, const FetchItemsResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchItemsResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/*****************************************************************************/

class LinkItemsCommandPrivate : public CommandPrivate
{
public:
    LinkItemsCommandPrivate(LinkItemsCommand::Action action = LinkItemsCommand::Link,
                            const Scope &items = Scope(),
                            const Scope &dest = Scope())
        : CommandPrivate(Command::LinkItems)
        , items(items)
        , dest(dest)
        , action(action)
    {}
    LinkItemsCommandPrivate(const LinkItemsCommandPrivate &other)
        : CommandPrivate(other)
        , items(other.items)
        , dest(other.dest)
        , action(other.action)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(action)
               && COMPARE(items)
               && COMPARE(dest);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << action
               << items
               << dest;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> action
        >> items
        >> dest;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Action", (action == LinkItemsCommand::Link ? "Link" : "Unlink"));
        blck.write("Items", items);
        blck.write("Destination", dest);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new LinkItemsCommandPrivate(*this);
    }

    Scope items;
    Scope dest;
    LinkItemsCommand::Action action;
};

AKONADI_DECLARE_PRIVATE(LinkItemsCommand)

LinkItemsCommand::LinkItemsCommand()
    : Command(new LinkItemsCommandPrivate)
{
}

LinkItemsCommand::LinkItemsCommand(Action action, const Scope &items, const Scope &dest)
    : Command(new LinkItemsCommandPrivate(action, items, dest))
{
}

LinkItemsCommand::LinkItemsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::LinkItems);
}

LinkItemsCommand::Action LinkItemsCommand::action() const
{
    return d_func()->action;
}
Scope LinkItemsCommand::items() const
{
    return d_func()->items;
}
Scope LinkItemsCommand::destination() const
{
    return d_func()->dest;
}

DataStream &operator<<(DataStream &stream, const LinkItemsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, LinkItemsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

LinkItemsResponse::LinkItemsResponse()
    : Response(new ResponsePrivate(Command::LinkItems))
{
}

LinkItemsResponse::LinkItemsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::LinkItems);
}

/****************************************************************************/

class ModifyItemsCommandPrivate : public CommandPrivate
{
public:
    ModifyItemsCommandPrivate(const Scope &items = Scope())
        : CommandPrivate(Command::ModifyItems)
        , items(items)
        , size(0)
        , oldRevision(-1)
        , dirty(true)
        , invalidate(false)
        , noResponse(false)
        , notify(true)
        , modifiedParts(ModifyItemsCommand::None)
    {}
    ModifyItemsCommandPrivate(const ModifyItemsCommandPrivate &other)
        : CommandPrivate(other)
        , items(other.items)
        , flags(other.flags)
        , addedFlags(other.addedFlags)
        , removedFlags(other.removedFlags)
        , tags(other.tags)
        , addedTags(other.addedTags)
        , removedTags(other.removedTags)
        , remoteId(other.remoteId)
        , remoteRev(other.remoteRev)
        , gid(other.gid)
        , removedParts(other.removedParts)
        , parts(other.parts)
        , attributes(other.attributes)
        , size(other.size)
        , oldRevision(other.oldRevision)
        , dirty(other.dirty)
        , invalidate(other.invalidate)
        , noResponse(other.noResponse)
        , notify(other.notify)
        , modifiedParts(other.modifiedParts)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(modifiedParts)
               && COMPARE(size)
               && COMPARE(oldRevision)
               && COMPARE(dirty)
               && COMPARE(invalidate)
               && COMPARE(noResponse)
               && COMPARE(notify)
               && COMPARE(items)
               && COMPARE(flags) && COMPARE(addedFlags) && COMPARE(removedFlags)
               && COMPARE(tags) && COMPARE(addedTags) && COMPARE(removedTags)
               && COMPARE(remoteId)
               && COMPARE(remoteRev)
               && COMPARE(gid)
               && COMPARE(removedParts) && COMPARE(parts)
               && COMPARE(attributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        CommandPrivate::serialize(stream)
                << items
                << oldRevision
                << modifiedParts
                << dirty
                << invalidate
                << noResponse
                << notify;

        if (modifiedParts & ModifyItemsCommand::Flags) {
            stream << flags;
        }
        if (modifiedParts & ModifyItemsCommand::AddedFlags) {
            stream << addedFlags;
        }
        if (modifiedParts & ModifyItemsCommand::RemovedFlags) {
            stream << removedFlags;
        }
        if (modifiedParts & ModifyItemsCommand::Tags) {
            stream << tags;
        }
        if (modifiedParts & ModifyItemsCommand::AddedTags) {
            stream << addedTags;
        }
        if (modifiedParts & ModifyItemsCommand::RemovedTags) {
            stream << removedTags;
        }
        if (modifiedParts & ModifyItemsCommand::RemoteID) {
            stream << remoteId;
        }
        if (modifiedParts & ModifyItemsCommand::RemoteRevision) {
            stream << remoteRev;
        }
        if (modifiedParts & ModifyItemsCommand::GID) {
            stream << gid;
        }
        if (modifiedParts & ModifyItemsCommand::Size) {
            stream << size;
        }
        if (modifiedParts & ModifyItemsCommand::Parts) {
            stream << parts;
        }
        if (modifiedParts & ModifyItemsCommand::RemovedParts) {
            stream << removedParts;
        }
        if (modifiedParts & ModifyItemsCommand::Attributes) {
            stream << attributes;
        }
        return stream;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        CommandPrivate::deserialize(stream)
                >> items
        >> oldRevision
        >> modifiedParts
        >> dirty
        >> invalidate
        >> noResponse
        >> notify;

        if (modifiedParts & ModifyItemsCommand::Flags)
        {
            stream >> flags;
        }
        if (modifiedParts & ModifyItemsCommand::AddedFlags)
        {
            stream >> addedFlags;
        }
        if (modifiedParts & ModifyItemsCommand::RemovedFlags)
        {
            stream >> removedFlags;
        }
        if (modifiedParts & ModifyItemsCommand::Tags)
        {
            stream >> tags;
        }
        if (modifiedParts & ModifyItemsCommand::AddedTags)
        {
            stream >> addedTags;
        }
        if (modifiedParts & ModifyItemsCommand::RemovedTags)
        {
            stream >> removedTags;
        }
        if (modifiedParts & ModifyItemsCommand::RemoteID)
        {
            stream >> remoteId;
        }
        if (modifiedParts & ModifyItemsCommand::RemoteRevision)
        {
            stream >> remoteRev;
        }
        if (modifiedParts & ModifyItemsCommand::GID)
        {
            stream >> gid;
        }
        if (modifiedParts & ModifyItemsCommand::Size)
        {
            stream >> size;
        }
        if (modifiedParts & ModifyItemsCommand::Parts)
        {
            stream >> parts;
        }
        if (modifiedParts & ModifyItemsCommand::RemovedParts)
        {
            stream >> removedParts;
        }
        if (modifiedParts & ModifyItemsCommand::Attributes)
        {
            stream >> attributes;
        }
        return stream;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        QStringList mps;
        if (modifiedParts & ModifyItemsCommand::Flags) {
            mps << QStringLiteral("Flags");
        }
        if (modifiedParts & ModifyItemsCommand::AddedFlags) {
            mps << QStringLiteral("Added Flags");
        }
        if (modifiedParts & ModifyItemsCommand::RemovedFlags) {
            mps << QStringLiteral("Removed Flags");
        }
        if (modifiedParts & ModifyItemsCommand::Tags) {
            mps << QStringLiteral("Tags");
        }
        if (modifiedParts & ModifyItemsCommand::AddedTags) {
            mps << QStringLiteral("Added Tags");
        }
        if (modifiedParts & ModifyItemsCommand::RemovedTags) {
            mps << QStringLiteral("Removed Tags");
        }
        if (modifiedParts & ModifyItemsCommand::RemoteID) {
            mps << QStringLiteral("Remote ID");
        }
        if (modifiedParts & ModifyItemsCommand::RemoteRevision) {
            mps << QStringLiteral("Remote Revision");
        }
        if (modifiedParts & ModifyItemsCommand::GID) {
            mps << QStringLiteral("GID");
        }
        if (modifiedParts & ModifyItemsCommand::Size) {
            mps << QStringLiteral("Size");
        }
        if (modifiedParts & ModifyItemsCommand::Parts) {
            mps << QStringLiteral("Parts");
        }
        if (modifiedParts & ModifyItemsCommand::RemovedParts) {
            mps << QStringLiteral("Removed Parts");
        }
        if (modifiedParts & ModifyItemsCommand::Attributes) {
            mps << QStringLiteral("Attributes");
        }

        CommandPrivate::debugString(blck);
        blck.write("Modified PartS", mps);
        blck.write("Items", items);
        blck.write("Old Revision", oldRevision);
        blck.write("Dirty", dirty);
        blck.write("Invalidate Cache", invalidate);
        blck.write("No Response", noResponse);
        blck.write("Notify", notify);
        if (modifiedParts & ModifyItemsCommand::Flags) {
            blck.write("Flags", flags);
        }
        if (modifiedParts & ModifyItemsCommand::AddedFlags) {
            blck.write("Added Flags", addedFlags);
        }
        if (modifiedParts & ModifyItemsCommand::RemovedFlags) {
            blck.write("Removed Flags", removedFlags);
        }
        if (modifiedParts & ModifyItemsCommand::Tags) {
            blck.write("Tags", tags);
        }
        if (modifiedParts & ModifyItemsCommand::AddedTags) {
            blck.write("Added Tags", addedTags);
        }
        if (modifiedParts & ModifyItemsCommand::RemovedTags) {
            blck.write("Removed Tags", removedTags);
        }
        if (modifiedParts & ModifyItemsCommand::RemoteID) {
            blck.write("Remote ID", remoteId);
        }
        if (modifiedParts & ModifyItemsCommand::RemoteRevision) {
            blck.write("Remote Revision", remoteRev);
        }
        if (modifiedParts & ModifyItemsCommand::GID) {
            blck.write("GID", gid);
        }
        if (modifiedParts & ModifyItemsCommand::Size) {
            blck.write("Size", size);
        }
        if (modifiedParts & ModifyItemsCommand::Parts) {
            blck.write("Parts", parts);
        }
        if (modifiedParts & ModifyItemsCommand::RemovedParts) {
            blck.write("Removed Parts", removedParts);
        }
        if (modifiedParts & ModifyItemsCommand::Attributes) {
            blck.beginBlock("Attributes");
            for (auto iter = attributes.constBegin(); iter != attributes.constEnd(); ++iter) {
                blck.write(iter.key().constData(), iter.value());
            }
            blck.endBlock();
        }
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ModifyItemsCommandPrivate(*this);
    }

    Scope items;
    QSet<QByteArray> flags;
    QSet<QByteArray> addedFlags;
    QSet<QByteArray> removedFlags;
    Scope tags;
    Scope addedTags;
    Scope removedTags;

    QString remoteId;
    QString remoteRev;
    QString gid;
    QSet<QByteArray> removedParts;
    QSet<QByteArray> parts;
    Attributes attributes;
    qint64 size;
    int oldRevision;
    bool dirty;
    bool invalidate;
    bool noResponse;
    bool notify;

    ModifyItemsCommand::ModifiedParts modifiedParts;
};

AKONADI_DECLARE_PRIVATE(ModifyItemsCommand)

ModifyItemsCommand::ModifyItemsCommand()
    : Command(new ModifyItemsCommandPrivate)
{
}

ModifyItemsCommand::ModifyItemsCommand(const Scope &items)
    : Command(new ModifyItemsCommandPrivate(items))
{
}

ModifyItemsCommand::ModifyItemsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::ModifyItems);
}

ModifyItemsCommand::ModifiedParts ModifyItemsCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifyItemsCommand::setItems(const Scope &items)
{
    d_func()->items = items;
}
Scope ModifyItemsCommand::items() const
{
    return d_func()->items;
}

void ModifyItemsCommand::setOldRevision(int oldRevision)
{
    d_func()->oldRevision = oldRevision;
}
int ModifyItemsCommand::oldRevision() const
{
    return d_func()->oldRevision;
}

void ModifyItemsCommand::setFlags(const QSet<QByteArray> &flags)
{
    d_func()->modifiedParts |= Flags;
    d_func()->flags = flags;
}
QSet<QByteArray> ModifyItemsCommand::flags() const
{
    return d_func()->flags;
}

void ModifyItemsCommand::setAddedFlags(const QSet<QByteArray> &addedFlags)
{
    d_func()->modifiedParts |= AddedFlags;
    d_func()->addedFlags = addedFlags;
}
QSet<QByteArray> ModifyItemsCommand::addedFlags() const
{
    return d_func()->addedFlags;
}

void ModifyItemsCommand::setRemovedFlags(const QSet<QByteArray> &removedFlags)
{
    d_func()->modifiedParts |= RemovedFlags;
    d_func()->removedFlags = removedFlags;
}
QSet<QByteArray> ModifyItemsCommand::removedFlags() const
{
    return d_func()->removedFlags;
}

void ModifyItemsCommand::setTags(const Scope &tags)
{
    d_func()->modifiedParts |= Tags;
    d_func()->tags = tags;
}
Scope ModifyItemsCommand::tags() const
{
    return d_func()->tags;
}

void ModifyItemsCommand::setAddedTags(const Scope &addedTags)
{
    d_func()->modifiedParts |= AddedTags;
    d_func()->addedTags = addedTags;
}
Scope ModifyItemsCommand::addedTags() const
{
    return d_func()->addedTags;
}

void ModifyItemsCommand::setRemovedTags(const Scope &removedTags)
{
    d_func()->modifiedParts |= RemovedTags;
    d_func()->removedTags = removedTags;
}
Scope ModifyItemsCommand::removedTags() const
{
    return d_func()->removedTags;
}

void ModifyItemsCommand::setRemoteId(const QString &remoteId)
{
    d_func()->modifiedParts |= RemoteID;
    d_func()->remoteId = remoteId;
}
QString ModifyItemsCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyItemsCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->modifiedParts |= RemoteRevision;
    d_func()->remoteRev = remoteRevision;
}
QString ModifyItemsCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void ModifyItemsCommand::setGid(const QString &gid)
{
    d_func()->modifiedParts |= GID;
    d_func()->gid = gid;
}
QString ModifyItemsCommand::gid() const
{
    return d_func()->gid;
}

void ModifyItemsCommand::setDirty(bool dirty)
{
    d_func()->dirty = dirty;
}
bool ModifyItemsCommand::dirty() const
{
    return d_func()->dirty;
}

void ModifyItemsCommand::setInvalidateCache(bool invalidate)
{
    d_func()->invalidate = invalidate;
}
bool ModifyItemsCommand::invalidateCache() const
{
    return d_func()->invalidate;
}

void ModifyItemsCommand::setNoResponse(bool noResponse)
{
    d_func()->noResponse = noResponse;
}
bool ModifyItemsCommand::noResponse() const
{
    return d_func()->noResponse;
}

void ModifyItemsCommand::setNotify(bool notify)
{
    d_func()->notify = notify;
}
bool ModifyItemsCommand::notify() const
{
    return d_func()->notify;
}

void ModifyItemsCommand::setItemSize(qint64 size)
{
    d_func()->modifiedParts |= Size;
    d_func()->size = size;
}
qint64 ModifyItemsCommand::itemSize() const
{
    return d_func()->size;
}

void ModifyItemsCommand::setRemovedParts(const QSet<QByteArray> &removedParts)
{
    d_func()->modifiedParts |= RemovedParts;
    d_func()->removedParts = removedParts;
}
QSet<QByteArray> ModifyItemsCommand::removedParts() const
{
    return d_func()->removedParts;
}

void ModifyItemsCommand::setParts(const QSet<QByteArray> &parts)
{
    d_func()->modifiedParts |= Parts;
    d_func()->parts = parts;
}
QSet<QByteArray> ModifyItemsCommand::parts() const
{
    return d_func()->parts;
}

void ModifyItemsCommand::setAttributes(const Protocol::Attributes &attributes)
{
    d_func()->modifiedParts |= Attributes;
    d_func()->attributes = attributes;
}
Attributes ModifyItemsCommand::attributes() const
{
    return d_func()->attributes;
}

DataStream &operator<<(DataStream &stream, const ModifyItemsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, ModifyItemsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class ModifyItemsResponsePrivate : public ResponsePrivate
{
public:
    ModifyItemsResponsePrivate(qint64 id = -1, int newRevision = -1, const QDateTime &modifyDt = QDateTime())
        : ResponsePrivate(Command::ModifyItems)
        , id(id)
        , newRevision(newRevision)
        , modificationDt(modifyDt)
    {}
    ModifyItemsResponsePrivate(const ModifyItemsResponsePrivate &other)
        : ResponsePrivate(other)
        , id(other.id)
        , newRevision(other.newRevision)
        , modificationDt(other.modificationDt)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(id)
               && COMPARE(newRevision)
               && COMPARE(modificationDt);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << id
               << newRevision
               << modificationDt;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> id
        >> newRevision
        >> modificationDt;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("ID", id);
        blck.write("New Revision", newRevision);
        blck.write("Modification datetime", modificationDt);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ModifyItemsResponsePrivate(*this);
    }

    qint64 id;
    int newRevision;
    QDateTime modificationDt;
};

AKONADI_DECLARE_PRIVATE(ModifyItemsResponse)

ModifyItemsResponse::ModifyItemsResponse()
    : Response(new ModifyItemsResponsePrivate)
{
}

ModifyItemsResponse::ModifyItemsResponse(qint64 id, int newRevision)
    : Response(new ModifyItemsResponsePrivate(id, newRevision))
{
}

ModifyItemsResponse::ModifyItemsResponse(const QDateTime &modificationDt)
    : Response(new ModifyItemsResponsePrivate(-1, -1, modificationDt))
{
}

ModifyItemsResponse::ModifyItemsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::ModifyItems);
}

qint64 ModifyItemsResponse::id() const
{
    return d_func()->id;
}
int ModifyItemsResponse::newRevision() const
{
    return d_func()->newRevision;
}

QDateTime ModifyItemsResponse::modificationDateTime() const
{
    return d_func()->modificationDt;
}

DataStream &operator<<(DataStream &stream, const ModifyItemsResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, ModifyItemsResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class MoveItemsCommandPrivate : public CommandPrivate
{
public:
    MoveItemsCommandPrivate(const Scope &items = Scope(),
                            const ScopeContext &context = ScopeContext(),
                            const Scope &dest = Scope())
        : CommandPrivate(Command::MoveItems)
        , items(items)
        , dest(dest)
        , context(context)
    {}
    MoveItemsCommandPrivate(const MoveItemsCommandPrivate &other)
        : CommandPrivate(other)
        , items(other.items)
        , dest(other.dest)
        , context(other.context)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(items)
               && COMPARE(dest)
               && COMPARE(context);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << items
               << dest
               << context;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> items
        >> dest
        >> context;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Items", items);
        blck.beginBlock("Context");
        context.debugString(blck);
        blck.endBlock();
        blck.write("Destination", dest);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new MoveItemsCommandPrivate(*this);
    }

    Scope items;
    Scope dest;
    ScopeContext context;
};

AKONADI_DECLARE_PRIVATE(MoveItemsCommand)

MoveItemsCommand::MoveItemsCommand()
    : Command(new MoveItemsCommandPrivate)
{
}

MoveItemsCommand::MoveItemsCommand(const Scope &items, const Scope &dest)
    : Command(new MoveItemsCommandPrivate(items, ScopeContext(), dest))
{
}

MoveItemsCommand::MoveItemsCommand(const Scope &items, const ScopeContext &ctx,
                                   const Scope &dest)
    : Command(new MoveItemsCommandPrivate(items, ctx, dest))
{
}

MoveItemsCommand::MoveItemsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::MoveItems);
}

Scope MoveItemsCommand::items() const
{
    return d_func()->items;
}

ScopeContext MoveItemsCommand::itemsContext() const
{
    return d_func()->context;
}

Scope MoveItemsCommand::destination() const
{
    return d_func()->dest;
}

DataStream &operator<<(DataStream &stream, const MoveItemsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, MoveItemsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

MoveItemsResponse::MoveItemsResponse()
    : Response(new ResponsePrivate(Command::MoveItems))
{
}

MoveItemsResponse::MoveItemsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::MoveItems);
}

/****************************************************************************/

class CreateCollectionCommandPrivate : public CommandPrivate
{
public:
    CreateCollectionCommandPrivate()
        : CommandPrivate(Command::CreateCollection)
        , sync(Tristate::Undefined)
        , display(Tristate::Undefined)
        , index(Tristate::Undefined)
        , enabled(true)
        , isVirtual(false)
    {}
    CreateCollectionCommandPrivate(const CreateCollectionCommandPrivate &other)
        : CommandPrivate(other)
        , parent(other.parent)
        , name(other.name)
        , remoteId(other.remoteId)
        , remoteRev(other.remoteRev)
        , mimeTypes(other.mimeTypes)
        , cachePolicy(other.cachePolicy)
        , attributes(other.attributes)
        , sync(other.sync)
        , display(other.display)
        , index(other.index)
        , enabled(other.enabled)
        , isVirtual(other.isVirtual)
    {}

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << parent
               << name
               << remoteId
               << remoteRev
               << mimeTypes
               << cachePolicy
               << attributes
               << enabled
               << sync
               << display
               << index
               << isVirtual;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> parent
        >> name
        >> remoteId
        >> remoteRev
        >> mimeTypes
        >> cachePolicy
        >> attributes
        >> enabled
        >> sync
        >> display
        >> index
        >> isVirtual;
    }

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(sync)
               && COMPARE(display)
               && COMPARE(index)
               && COMPARE(enabled)
               && COMPARE(isVirtual)
               && COMPARE(parent)
               && COMPARE(name)
               && COMPARE(remoteId)
               && COMPARE(remoteRev)
               && COMPARE(mimeTypes)
               && COMPARE(cachePolicy)
               && COMPARE(attributes);
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Name", name);
        blck.write("Parent", parent);
        blck.write("Remote ID", remoteId);
        blck.write("Remote Revision", remoteRev);
        blck.write("Mimetypes", mimeTypes);
        blck.write("Sync", sync);
        blck.write("Display", display);
        blck.write("Index", index);
        blck.write("Enabled", enabled);
        blck.write("Virtual", isVirtual);
        blck.beginBlock("CachePolicy");
        cachePolicy.debugString(blck);
        blck.endBlock();
        blck.write("Attributes", attributes);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CreateCollectionCommandPrivate(*this);
    }

    Scope parent;
    QString name;
    QString remoteId;
    QString remoteRev;
    QStringList mimeTypes;
    CachePolicy cachePolicy;
    Attributes attributes;
    Tristate sync;
    Tristate display;
    Tristate index;
    bool enabled;
    bool isVirtual;
};

AKONADI_DECLARE_PRIVATE(CreateCollectionCommand)

CreateCollectionCommand::CreateCollectionCommand()
    : Command(new CreateCollectionCommandPrivate)
{
}

CreateCollectionCommand::CreateCollectionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::CreateCollection);
}

void CreateCollectionCommand::setParent(const Scope &parent)
{
    d_func()->parent = parent;
}
Scope CreateCollectionCommand::parent() const
{
    return d_func()->parent;
}

void CreateCollectionCommand::setName(const QString &name)
{
    d_func()->name = name;
}
QString CreateCollectionCommand::name() const
{
    return d_func()->name;
}

void CreateCollectionCommand::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString CreateCollectionCommand::remoteId() const
{
    return d_func()->remoteId;
}

void CreateCollectionCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}
QString CreateCollectionCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void CreateCollectionCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList CreateCollectionCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void CreateCollectionCommand::setCachePolicy(const CachePolicy &cachePolicy)
{
    d_func()->cachePolicy = cachePolicy;
}
CachePolicy CreateCollectionCommand::cachePolicy() const
{
    return d_func()->cachePolicy;
}

void CreateCollectionCommand::setAttributes(const Attributes &attributes)
{
    d_func()->attributes = attributes;
}
Attributes CreateCollectionCommand::attributes() const
{
    return d_func()->attributes;
}

void CreateCollectionCommand::setIsVirtual(bool isVirtual)
{
    d_func()->isVirtual = isVirtual;
}
bool CreateCollectionCommand::isVirtual() const
{
    return d_func()->isVirtual;
}

void CreateCollectionCommand::setEnabled(bool enabled)
{
    d_func()->enabled = enabled;
}
bool CreateCollectionCommand::enabled() const
{
    return d_func()->enabled;
}

void CreateCollectionCommand::setSyncPref(Tristate sync)
{
    d_func()->sync = sync;
}
Tristate CreateCollectionCommand::syncPref() const
{
    return d_func()->sync;
}

void CreateCollectionCommand::setDisplayPref(Tristate display)
{
    d_func()->display = display;
}
Tristate CreateCollectionCommand::displayPref() const
{
    return d_func()->display;
}

void CreateCollectionCommand::setIndexPref(Tristate index)
{
    d_func()->index = index;
}
Tristate CreateCollectionCommand::indexPref() const
{
    return d_func()->index;
}

DataStream &operator<<(DataStream &stream, const CreateCollectionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, CreateCollectionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

CreateCollectionResponse::CreateCollectionResponse()
    : Response(new ResponsePrivate(Command::CreateCollection))
{
}

CreateCollectionResponse::CreateCollectionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::CreateCollection);
}

/****************************************************************************/

class CopyCollectionCommandPrivate : public CommandPrivate
{
public:
    CopyCollectionCommandPrivate(const Scope &collection = Scope(),
                                 const Scope &dest = Scope())
        : CommandPrivate(Command::CopyCollection)
        , collection(collection)
        , dest(dest)
    {}
    CopyCollectionCommandPrivate(const CopyCollectionCommandPrivate &other)
        : CommandPrivate(other)
        , collection(other.collection)
        , dest(other.dest)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(collection)
               && COMPARE(dest);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << collection
               << dest;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> collection
        >> dest;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Collection", collection);
        blck.write("Destination", dest);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CopyCollectionCommandPrivate(*this);
    }

    Scope collection;
    Scope dest;
};

AKONADI_DECLARE_PRIVATE(CopyCollectionCommand)

CopyCollectionCommand::CopyCollectionCommand()
    : Command(new CopyCollectionCommandPrivate)
{
}

CopyCollectionCommand::CopyCollectionCommand(const Scope &collection,
        const Scope &destination)
    : Command(new CopyCollectionCommandPrivate(collection, destination))
{
}

CopyCollectionCommand::CopyCollectionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::CopyCollection);
}

Scope CopyCollectionCommand::collection() const
{
    return d_func()->collection;
}
Scope CopyCollectionCommand::destination() const
{
    return d_func()->dest;
}

DataStream &operator<<(DataStream &stream, const CopyCollectionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, CopyCollectionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

CopyCollectionResponse::CopyCollectionResponse()
    : Response(new ResponsePrivate(Command::CopyCollection))
{
}

CopyCollectionResponse::CopyCollectionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::CopyCollection);
}

/****************************************************************************/

class DeleteCollectionCommandPrivate : public CommandPrivate
{
public:
    DeleteCollectionCommandPrivate(const Scope &col = Scope())
        : CommandPrivate(Command::DeleteCollection)
        , collection(col)
    {}
    DeleteCollectionCommandPrivate(const DeleteCollectionCommandPrivate &other)
        : CommandPrivate(other)
        , collection(other.collection)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(collection);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << collection;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> collection;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Collection", collection);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new DeleteCollectionCommandPrivate(*this);
    }

    Scope collection;
};

AKONADI_DECLARE_PRIVATE(DeleteCollectionCommand)

DeleteCollectionCommand::DeleteCollectionCommand()
    : Command(new DeleteCollectionCommandPrivate)
{
}

DeleteCollectionCommand::DeleteCollectionCommand(const Scope &collection)
    : Command(new DeleteCollectionCommandPrivate(collection))
{
}

DeleteCollectionCommand::DeleteCollectionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::DeleteCollection);
}

Scope DeleteCollectionCommand::collection() const
{
    return d_func()->collection;
}

DataStream &operator<<(DataStream &stream, const DeleteCollectionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, DeleteCollectionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

DeleteCollectionResponse::DeleteCollectionResponse()
    : Response(new ResponsePrivate(Command::DeleteCollection))
{
}

DeleteCollectionResponse::DeleteCollectionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::DeleteCollection);
}

/****************************************************************************/

class FetchCollectionStatsCommandPrivate : public CommandPrivate
{
public:
    FetchCollectionStatsCommandPrivate(const Scope &collection = Scope())
        : CommandPrivate(Command::FetchCollectionStats)
        , collection(collection)
    {}
    FetchCollectionStatsCommandPrivate(const FetchCollectionStatsCommandPrivate &other)
        : CommandPrivate(other)
        , collection(other.collection)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(collection);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << collection;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> collection;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Collection", collection);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchCollectionStatsCommandPrivate(*this);
    }

    Scope collection;
};

AKONADI_DECLARE_PRIVATE(FetchCollectionStatsCommand)

FetchCollectionStatsCommand::FetchCollectionStatsCommand()
    : Command(new FetchCollectionStatsCommandPrivate)
{
}

FetchCollectionStatsCommand::FetchCollectionStatsCommand(const Scope &collection)
    : Command(new FetchCollectionStatsCommandPrivate(collection))
{
}

FetchCollectionStatsCommand::FetchCollectionStatsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::FetchCollectionStats);
}

Scope FetchCollectionStatsCommand::collection() const
{
    return d_func()->collection;
}

DataStream &operator<<(DataStream &stream, const FetchCollectionStatsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchCollectionStatsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class FetchCollectionStatsResponsePrivate : public ResponsePrivate
{
public:
    FetchCollectionStatsResponsePrivate(qint64 count = -1,
                                        qint64 unseen = -1,
                                        qint64 size = -1)
        : ResponsePrivate(Command::FetchCollectionStats)
        , count(count)
        , unseen(unseen)
        , size(size)
    {}
    FetchCollectionStatsResponsePrivate(const FetchCollectionStatsResponsePrivate &other)
        : ResponsePrivate(other)
        , count(other.count)
        , unseen(other.unseen)
        , size(other.size)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(count)
               && COMPARE(unseen)
               && COMPARE(size);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << count
               << unseen
               << size;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> count
        >> unseen
        >> size;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("Count", count);
        blck.write("Unseen", unseen);
        blck.write("Size", size);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchCollectionStatsResponsePrivate(*this);
    }

    qint64 count;
    qint64 unseen;
    qint64 size;
};

AKONADI_DECLARE_PRIVATE(FetchCollectionStatsResponse)

FetchCollectionStatsResponse::FetchCollectionStatsResponse()
    : Response(new FetchCollectionStatsResponsePrivate)
{
}

FetchCollectionStatsResponse::FetchCollectionStatsResponse(qint64 count,
        qint64 unseen,
        qint64 size)
    : Response(new FetchCollectionStatsResponsePrivate(count, unseen, size))
{
}

FetchCollectionStatsResponse::FetchCollectionStatsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::FetchCollectionStats);
}

qint64 FetchCollectionStatsResponse::count() const
{
    return d_func()->count;
}
qint64 FetchCollectionStatsResponse::unseen() const
{
    return d_func()->unseen;
}
qint64 FetchCollectionStatsResponse::size() const
{
    return d_func()->size;
}

DataStream &operator<<(DataStream &stream, const FetchCollectionStatsResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchCollectionStatsResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class FetchCollectionsCommandPrivate : public CommandPrivate
{
public:
    FetchCollectionsCommandPrivate(const Scope &collections = Scope())
        : CommandPrivate(Command::FetchCollections)
        , collections(collections)
        , depth(FetchCollectionsCommand::BaseCollection)
        , ancestorsDepth(Ancestor::NoAncestor)
        , enabled(false)
        , sync(false)
        , display(false)
        , index(false)
        , stats(false)
    {}
    FetchCollectionsCommandPrivate(const FetchCollectionsCommandPrivate &other)
        : CommandPrivate(other)
        , collections(other.collections)
        , resource(other.resource)
        , mimeTypes(other.mimeTypes)
        , ancestorsAttributes(other.ancestorsAttributes)
        , depth(other.depth)
        , ancestorsDepth(other.ancestorsDepth)
        , enabled(other.enabled)
        , sync(other.sync)
        , display(other.display)
        , index(other.index)
        , stats(other.stats)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(depth)
               && COMPARE(ancestorsDepth)
               && COMPARE(enabled)
               && COMPARE(sync)
               && COMPARE(display)
               && COMPARE(index)
               && COMPARE(stats)
               && COMPARE(collections)
               && COMPARE(resource)
               && COMPARE(mimeTypes)
               && COMPARE(ancestorsAttributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << collections
               << resource
               << mimeTypes
               << depth
               << ancestorsDepth
               << ancestorsAttributes
               << enabled
               << sync
               << display
               << index
               << stats;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> collections
        >> resource
        >> mimeTypes
        >> depth
        >> ancestorsDepth
        >> ancestorsAttributes
        >> enabled
        >> sync
        >> display
        >> index
        >> stats;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Collections", collections);
        blck.write("Depth", depth);
        blck.write("Resource", resource);
        blck.write("Mimetypes", mimeTypes);
        blck.write("Ancestors Depth", ancestorsDepth);
        blck.write("Ancestors Attributes", ancestorsAttributes);
        blck.write("Enabled", enabled);
        blck.write("Sync", sync);
        blck.write("Display", display);
        blck.write("Index", index);
        blck.write("Status", stats);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchCollectionsCommandPrivate(*this);
    }

    Scope collections;
    QString resource;
    QStringList mimeTypes;
    QSet<QByteArray> ancestorsAttributes;
    FetchCollectionsCommand::Depth depth;
    Ancestor::Depth ancestorsDepth;
    bool enabled;
    bool sync;
    bool display;
    bool index;
    bool stats;
};

AKONADI_DECLARE_PRIVATE(FetchCollectionsCommand)

FetchCollectionsCommand::FetchCollectionsCommand()
    : Command(new FetchCollectionsCommandPrivate)
{
}

FetchCollectionsCommand::FetchCollectionsCommand(const Scope &collections)
    : Command(new FetchCollectionsCommandPrivate(collections))
{
}

FetchCollectionsCommand::FetchCollectionsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::FetchCollections);
}

Scope FetchCollectionsCommand::collections() const
{
    return d_func()->collections;
}

void FetchCollectionsCommand::setDepth(Depth depth)
{
    d_func()->depth = depth;
}
FetchCollectionsCommand::Depth FetchCollectionsCommand::depth() const
{
    return d_func()->depth;
}

void FetchCollectionsCommand::setResource(const QString &resourceId)
{
    d_func()->resource = resourceId;
}
QString FetchCollectionsCommand::resource() const
{
    return d_func()->resource;
}

void FetchCollectionsCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList FetchCollectionsCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void FetchCollectionsCommand::setAncestorsDepth(Ancestor::Depth depth)
{
    d_func()->ancestorsDepth = depth;
}
Ancestor::Depth FetchCollectionsCommand::ancestorsDepth() const
{
    return d_func()->ancestorsDepth;
}

void FetchCollectionsCommand::setAncestorsAttributes(const QSet<QByteArray> &attributes)
{
    d_func()->ancestorsAttributes = attributes;
}
QSet<QByteArray> FetchCollectionsCommand::ancestorsAttributes() const
{
    return d_func()->ancestorsAttributes;
}

void FetchCollectionsCommand::setEnabled(bool enabled)
{
    d_func()->enabled = enabled;
}
bool FetchCollectionsCommand::enabled() const
{
    return d_func()->enabled;
}

void FetchCollectionsCommand::setSyncPref(bool sync)
{
    d_func()->sync = sync;
}
bool FetchCollectionsCommand::syncPref() const
{
    return d_func()->sync;
}

void FetchCollectionsCommand::setDisplayPref(bool display)
{
    d_func()->display = display;
}
bool FetchCollectionsCommand::displayPref() const
{
    return d_func()->display;
}

void FetchCollectionsCommand::setIndexPref(bool index)
{
    d_func()->index = index;
}
bool FetchCollectionsCommand::indexPref() const
{
    return d_func()->index;
}

void FetchCollectionsCommand::setFetchStats(bool stats)
{
    d_func()->stats = stats;
}
bool FetchCollectionsCommand::fetchStats() const
{
    return d_func()->stats;
}

DataStream &operator<<(DataStream &stream, const FetchCollectionsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchCollectionsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class FetchCollectionsResponsePrivate : public ResponsePrivate
{
public:
    FetchCollectionsResponsePrivate(qint64 id = -1)
        : ResponsePrivate(Command::FetchCollections)
        , id(id)
        , parentId(-1)
        , display(Tristate::Undefined)
        , sync(Tristate::Undefined)
        , index(Tristate::Undefined)
        , isVirtual(false)
        , referenced(false)
        , enabled(true)
    {}
    FetchCollectionsResponsePrivate(const FetchCollectionsResponsePrivate &other)
        : ResponsePrivate(other)
        , name(other.name)
        , remoteId(other.remoteId)
        , remoteRev(other.remoteRev)
        , resource(other.resource)
        , mimeTypes(other.mimeTypes)
        , stats(other.stats)
        , searchQuery(other.searchQuery)
        , searchCols(other.searchCols)
        , ancestors(other.ancestors)
        , cachePolicy(other.cachePolicy)
        , attributes(other.attributes)
        , id(other.id)
        , parentId(other.parentId)
        , display(other.display)
        , sync(other.sync)
        , index(other.index)
        , isVirtual(other.isVirtual)
        , referenced(other.referenced)
        , enabled(other.enabled)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(id)
               && COMPARE(parentId)
               && COMPARE(display) && COMPARE(sync) && COMPARE(index)
               && COMPARE(isVirtual) && COMPARE(referenced) && COMPARE(enabled)
               && COMPARE(name)
               && COMPARE(remoteId) && COMPARE(remoteRev)
               && COMPARE(resource)
               && COMPARE(mimeTypes)
               && COMPARE(stats)
               && COMPARE(searchQuery) && COMPARE(searchCols)
               && COMPARE(ancestors)
               && COMPARE(cachePolicy)
               && COMPARE(attributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << id
               << parentId
               << name
               << mimeTypes
               << remoteId
               << remoteRev
               << resource
               << stats
               << searchQuery
               << searchCols
               << ancestors
               << cachePolicy
               << attributes
               << display
               << sync
               << index
               << isVirtual
               << referenced
               << enabled;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> id
        >> parentId
        >> name
        >> mimeTypes
        >> remoteId
        >> remoteRev
        >> resource
        >> stats
        >> searchQuery
        >> searchCols
        >> ancestors
        >> cachePolicy
        >> attributes
        >> display
        >> sync
        >> index
        >> isVirtual
        >> referenced
        >> enabled;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.write("ID", id);
        blck.write("Name", name);
        blck.write("Parent ID", parentId);
        blck.write("Remote ID", remoteId);
        blck.write("Remote Revision", remoteRev);
        blck.write("Resource", resource);
        blck.write("Mimetypes", mimeTypes);
        blck.beginBlock("Statistics");
        blck.write("Count", stats.count());
        blck.write("Unseen", stats.unseen());
        blck.write("Size", stats.size());
        blck.endBlock();
        blck.write("Search Query", searchQuery);
        blck.write("Search Collections", searchCols);
        blck.beginBlock("Cache Policy");
        cachePolicy.debugString(blck);
        blck.endBlock();
        blck.beginBlock("Ancestors");
        Q_FOREACH (const Ancestor &anc, ancestors) {
            blck.beginBlock();
            anc.debugString(blck);
            blck.endBlock();
        }
        blck.endBlock();
        blck.write("Attributes", attributes);
        blck.write("Display", display);
        blck.write("Sync", sync);
        blck.write("Index", index);
        blck.write("Enabled", enabled);
        blck.write("Virtual", isVirtual);
        blck.write("Referenced", referenced);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new FetchCollectionsResponsePrivate(*this);
    }

    QString name;
    QString remoteId;
    QString remoteRev;
    QString resource;
    QStringList mimeTypes;
    FetchCollectionStatsResponse stats;
    QString searchQuery;
    QVector<qint64> searchCols;
    QVector<Ancestor> ancestors;
    CachePolicy cachePolicy;
    Attributes attributes;
    qint64 id;
    qint64 parentId;
    Tristate display;
    Tristate sync;
    Tristate index;
    bool isVirtual;
    bool referenced;
    bool enabled;
};

AKONADI_DECLARE_PRIVATE(FetchCollectionsResponse)

FetchCollectionsResponse::FetchCollectionsResponse()
    : Response(new FetchCollectionsResponsePrivate)
{
}

FetchCollectionsResponse::FetchCollectionsResponse(qint64 id)
    : Response(new FetchCollectionsResponsePrivate(id))
{
}

FetchCollectionsResponse::FetchCollectionsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::FetchCollections);
}

qint64 FetchCollectionsResponse::id() const
{
    return d_func()->id;
}

void FetchCollectionsResponse::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 FetchCollectionsResponse::parentId() const
{
    return d_func()->parentId;
}

void FetchCollectionsResponse::setName(const QString &name)
{
    d_func()->name = name;
}
QString FetchCollectionsResponse::name() const
{
    return d_func()->name;
}

void FetchCollectionsResponse::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList FetchCollectionsResponse::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void FetchCollectionsResponse::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}
QString FetchCollectionsResponse::remoteId() const
{
    return d_func()->remoteId;
}

void FetchCollectionsResponse::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRev = remoteRevision;
}
QString FetchCollectionsResponse::remoteRevision() const
{
    return d_func()->remoteRev;
}

void FetchCollectionsResponse::setResource(const QString &resourceId)
{
    d_func()->resource = resourceId;
}
QString FetchCollectionsResponse::resource() const
{
    return d_func()->resource;
}

void FetchCollectionsResponse::setStatistics(const FetchCollectionStatsResponse &stats)
{
    d_func()->stats = stats;
}
FetchCollectionStatsResponse FetchCollectionsResponse::statistics() const
{
    return d_func()->stats;
}

void FetchCollectionsResponse::setSearchQuery(const QString &query)
{
    d_func()->searchQuery = query;
}
QString FetchCollectionsResponse::searchQuery() const
{
    return d_func()->searchQuery;
}

void FetchCollectionsResponse::setSearchCollections(const QVector<qint64> &searchCols)
{
    d_func()->searchCols = searchCols;
}
QVector<qint64> FetchCollectionsResponse::searchCollections() const
{
    return d_func()->searchCols;
}

void FetchCollectionsResponse::setAncestors(const QVector<Ancestor> &ancestors)
{
    d_func()->ancestors = ancestors;
}
QVector<Ancestor> FetchCollectionsResponse::ancestors() const
{
    return d_func()->ancestors;
}

void FetchCollectionsResponse::setCachePolicy(const CachePolicy &cachePolicy)
{
    d_func()->cachePolicy = cachePolicy;
}
CachePolicy FetchCollectionsResponse::cachePolicy() const
{
    return d_func()->cachePolicy;
}

CachePolicy &FetchCollectionsResponse::cachePolicy()
{
    return d_func()->cachePolicy;
}

void FetchCollectionsResponse::setAttributes(const Attributes &attrs)
{
    d_func()->attributes = attrs;
}
Attributes FetchCollectionsResponse::attributes() const
{
    return d_func()->attributes;
}

void FetchCollectionsResponse::setEnabled(bool enabled)
{
    d_func()->enabled = enabled;
}
bool FetchCollectionsResponse::enabled() const
{
    return d_func()->enabled;
}

void FetchCollectionsResponse::setDisplayPref(Tristate display)
{
    d_func()->display = display;
}
Tristate FetchCollectionsResponse::displayPref() const
{
    return d_func()->display;
}

void FetchCollectionsResponse::setSyncPref(Tristate sync)
{
    d_func()->sync = sync;
}
Tristate FetchCollectionsResponse::syncPref() const
{
    return d_func()->sync;
}

void FetchCollectionsResponse::setIndexPref(Tristate index)
{
    d_func()->index = index;
}
Tristate FetchCollectionsResponse::indexPref() const
{
    return d_func()->index;
}

void FetchCollectionsResponse::setReferenced(bool referenced)
{
    d_func()->referenced = referenced;
}
bool FetchCollectionsResponse::referenced() const
{
    return d_func()->referenced;
}

void FetchCollectionsResponse::setIsVirtual(bool isVirtual)
{
    d_func()->isVirtual = isVirtual;
}
bool FetchCollectionsResponse::isVirtual() const
{
    return d_func()->isVirtual;
}

DataStream &operator<<(DataStream &stream, const FetchCollectionsResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, FetchCollectionsResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

class ModifyCollectionCommandPrivate : public CommandPrivate
{
public:
    ModifyCollectionCommandPrivate(const Scope &collection = Scope())
        : CommandPrivate(Command::ModifyCollection)
        , collection(collection)
        , parentId(-1)
        , sync(Tristate::Undefined)
        , display(Tristate::Undefined)
        , index(Tristate::Undefined)
        , enabled(true)
        , referenced(false)
        , persistentSearchRemote(false)
        , persistentSearchRecursive(false)
        , modifiedParts(ModifyCollectionCommand::None)
    {}
    ModifyCollectionCommandPrivate(const ModifyCollectionCommandPrivate &other)
        : CommandPrivate(other)
        , collection(other.collection)
        , mimeTypes(other.mimeTypes)
        , cachePolicy(other.cachePolicy)
        , name(other.name)
        , remoteId(other.remoteId)
        , remoteRev(other.remoteRev)
        , persistentSearchQuery(other.persistentSearchQuery)
        , persistentSearchCols(other.persistentSearchCols)
        , removedAttributes(other.removedAttributes)
        , attributes(other.attributes)
        , parentId(other.parentId)
        , sync(other.sync)
        , display(other.display)
        , index(other.index)
        , enabled(other.enabled)
        , referenced(other.referenced)
        , persistentSearchRemote(other.persistentSearchRemote)
        , persistentSearchRecursive(other.persistentSearchRecursive)
        , modifiedParts(other.modifiedParts)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(modifiedParts)
               && COMPARE(parentId)
               && COMPARE(sync) && COMPARE(display) && COMPARE(index)
               && COMPARE(enabled) && COMPARE(referenced)
               && COMPARE(persistentSearchRemote) && COMPARE(persistentSearchRecursive)
               && COMPARE(collection)
               && COMPARE(mimeTypes)
               && COMPARE(cachePolicy)
               && COMPARE(name)
               && COMPARE(remoteId) && COMPARE(remoteRev)
               && COMPARE(persistentSearchQuery) && COMPARE(persistentSearchCols)
               && COMPARE(removedAttributes) && COMPARE(attributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        CommandPrivate::serialize(stream)
                << collection
                << modifiedParts;

        if (modifiedParts & ModifyCollectionCommand::Name) {
            stream << name;
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteID) {
            stream << remoteId;
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteRevision) {
            stream << remoteRev;
        }
        if (modifiedParts & ModifyCollectionCommand::ParentID) {
            stream << parentId;
        }
        if (modifiedParts & ModifyCollectionCommand::MimeTypes) {
            stream << mimeTypes;
        }
        if (modifiedParts & ModifyCollectionCommand::CachePolicy) {
            stream << cachePolicy;
        }
        if (modifiedParts & ModifyCollectionCommand::PersistentSearch) {
            stream << persistentSearchQuery
                   << persistentSearchCols
                   << persistentSearchRemote
                   << persistentSearchRecursive;
        }
        if (modifiedParts & ModifyCollectionCommand::RemovedAttributes) {
            stream << removedAttributes;
        }
        if (modifiedParts & ModifyCollectionCommand::Attributes) {
            stream << attributes;
        }
        if (modifiedParts & ModifyCollectionCommand::ListPreferences) {
            stream << enabled
                   << sync
                   << display
                   << index;
        }
        if (modifiedParts & ModifyCollectionCommand::Referenced) {
            stream << referenced;
        }
        return stream;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        CommandPrivate::deserialize(stream)
                >> collection
        >> modifiedParts;

        if (modifiedParts & ModifyCollectionCommand::Name)
        {
            stream >> name;
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteID)
        {
            stream >> remoteId;
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteRevision)
        {
            stream >> remoteRev;
        }
        if (modifiedParts & ModifyCollectionCommand::ParentID)
        {
            stream >> parentId;
        }
        if (modifiedParts & ModifyCollectionCommand::MimeTypes)
        {
            stream >> mimeTypes;
        }
        if (modifiedParts & ModifyCollectionCommand::CachePolicy)
        {
            stream >> cachePolicy;
        }
        if (modifiedParts & ModifyCollectionCommand::PersistentSearch)
        {
            stream >> persistentSearchQuery
            >> persistentSearchCols
            >> persistentSearchRemote
            >> persistentSearchRecursive;
        }
        if (modifiedParts & ModifyCollectionCommand::RemovedAttributes)
        {
            stream >> removedAttributes;
        }
        if (modifiedParts & ModifyCollectionCommand::Attributes)
        {
            stream >> attributes;
        }
        if (modifiedParts & ModifyCollectionCommand::ListPreferences)
        {
            stream >> enabled
            >> sync
            >> display
            >> index;
        }
        if (modifiedParts & ModifyCollectionCommand::Referenced)
        {
            stream >> referenced;
        }
        return stream;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        QStringList mps;

        if (modifiedParts & ModifyCollectionCommand::Name) {
            mps << QStringLiteral("Name");
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteID) {
            mps << QStringLiteral("Remote ID");
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteRevision) {
            mps << QStringLiteral("Remote Revision");
        }
        if (modifiedParts & ModifyCollectionCommand::ParentID) {
            mps << QStringLiteral("Parent ID");
        }
        if (modifiedParts & ModifyCollectionCommand::MimeTypes) {
            mps << QStringLiteral("Mimetypes");
        }
        if (modifiedParts & ModifyCollectionCommand::CachePolicy) {
            mps << QStringLiteral("Cache Policy");
        }
        if (modifiedParts & ModifyCollectionCommand::PersistentSearch) {
            mps << QStringLiteral("Persistent Search");
        }
        if (modifiedParts & ModifyCollectionCommand::RemovedAttributes) {
            mps << QStringLiteral("Remote Attributes");
        }
        if (modifiedParts & ModifyCollectionCommand::Attributes) {
            mps << QStringLiteral("Attributes");
        }
        if (modifiedParts & ModifyCollectionCommand::ListPreferences) {
            mps << QStringLiteral("List Preferences");
        }
        if (modifiedParts & ModifyCollectionCommand::Referenced) {
            mps << QStringLiteral("Referenced");
        }

        CommandPrivate::debugString(blck);
        blck.write("Collection", collection);
        blck.write("Modified Parts", mps);
        if (modifiedParts & ModifyCollectionCommand::Name) {
            blck.write("Name", name);
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteID) {
            blck.write("Remote ID", remoteId);
        }
        if (modifiedParts & ModifyCollectionCommand::RemoteRevision) {
            blck.write("Remote Revision", remoteRev);
        }
        if (modifiedParts & ModifyCollectionCommand::ParentID) {
            blck.write("Parent ID", parentId);
        }
        if (modifiedParts & ModifyCollectionCommand::MimeTypes) {
            blck.write("Mimetypes", mimeTypes);
        }
        if (modifiedParts & ModifyCollectionCommand::CachePolicy) {
            blck.beginBlock("Cache Policy");
            cachePolicy.debugString(blck);
            blck.endBlock();
        }
        if (modifiedParts & ModifyCollectionCommand::PersistentSearch) {
            blck.beginBlock("Persistent Search");
            blck.write("Query", persistentSearchQuery);
            blck.write("Cols", persistentSearchCols);
            blck.write("Remote", persistentSearchRemote);
            blck.write("Recursive", persistentSearchRecursive);
            blck.endBlock();
        }
        if (modifiedParts & ModifyCollectionCommand::RemovedAttributes) {
            blck.write("Removed Attributes", removedAttributes);
        }
        if (modifiedParts & ModifyCollectionCommand::Attributes) {
            blck.write("Attributes", attributes);
        }
        if (modifiedParts & ModifyCollectionCommand::ListPreferences) {
            blck.write("Sync", sync);
            blck.write("Display", display);
            blck.write("Index", index);
            blck.write("Enabled", enabled);
        }
        if (modifiedParts & ModifyCollectionCommand::Referenced) {
            blck.write("Referenced", referenced);
        }
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ModifyCollectionCommandPrivate(*this);
    }

    Scope collection;
    QStringList mimeTypes;
    Protocol::CachePolicy cachePolicy;
    QString name;
    QString remoteId;
    QString remoteRev;
    QString persistentSearchQuery;
    QVector<qint64> persistentSearchCols;
    QSet<QByteArray> removedAttributes;
    Attributes attributes;
    qint64 parentId;
    Tristate sync;
    Tristate display;
    Tristate index;
    bool enabled;
    bool referenced;
    bool persistentSearchRemote;
    bool persistentSearchRecursive;

    ModifyCollectionCommand::ModifiedParts modifiedParts;
};

AKONADI_DECLARE_PRIVATE(ModifyCollectionCommand)

ModifyCollectionCommand::ModifyCollectionCommand()
    : Command(new ModifyCollectionCommandPrivate)
{
}

ModifyCollectionCommand::ModifyCollectionCommand(const Scope &collection)
    : Command(new ModifyCollectionCommandPrivate(collection))
{
}

ModifyCollectionCommand::ModifyCollectionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::ModifyCollection);
}

Scope ModifyCollectionCommand::collection() const
{
    return d_func()->collection;
}

ModifyCollectionCommand::ModifiedParts ModifyCollectionCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifyCollectionCommand::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 ModifyCollectionCommand::parentId() const
{
    return d_func()->parentId;
}

void ModifyCollectionCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->modifiedParts |= MimeTypes;
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->mimeTypes = mimeTypes;
}
QStringList ModifyCollectionCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void ModifyCollectionCommand::setCachePolicy(const Protocol::CachePolicy &cachePolicy)
{
    d_func()->modifiedParts |= CachePolicy;
    d_func()->cachePolicy = cachePolicy;
}
Protocol::CachePolicy ModifyCollectionCommand::cachePolicy() const
{
    return d_func()->cachePolicy;
}

void ModifyCollectionCommand::setName(const QString &name)
{
    d_func()->modifiedParts |= Name;
    d_func()->name = name;
}
QString ModifyCollectionCommand::name() const
{
    return d_func()->name;
}

void ModifyCollectionCommand::setRemoteId(const QString &remoteId)
{
    d_func()->modifiedParts |= RemoteID;
    d_func()->remoteId = remoteId;
}
QString ModifyCollectionCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyCollectionCommand::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->modifiedParts |= RemoteRevision;
    d_func()->remoteRev = remoteRevision;
}
QString ModifyCollectionCommand::remoteRevision() const
{
    return d_func()->remoteRev;
}

void ModifyCollectionCommand::setPersistentSearchQuery(const QString &query)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchQuery = query;
}
QString ModifyCollectionCommand::persistentSearchQuery() const
{
    return d_func()->persistentSearchQuery;
}

void ModifyCollectionCommand::setPersistentSearchCollections(const QVector<qint64> &cols)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchCols = cols;
}
QVector<qint64> ModifyCollectionCommand::persistentSearchCollections() const
{
    return d_func()->persistentSearchCols;
}

void ModifyCollectionCommand::setPersistentSearchRemote(bool remote)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchRemote = remote;
}
bool ModifyCollectionCommand::persistentSearchRemote() const
{
    return d_func()->persistentSearchRemote;
}

void ModifyCollectionCommand::setPersistentSearchRecursive(bool recursive)
{
    d_func()->modifiedParts |= PersistentSearch;
    d_func()->persistentSearchRecursive = recursive;
}
bool ModifyCollectionCommand::persistentSearchRecursive() const
{
    return d_func()->persistentSearchRecursive;
}

void ModifyCollectionCommand::setRemovedAttributes(const QSet<QByteArray> &removedAttrs)
{
    d_func()->modifiedParts |= RemovedAttributes;
    d_func()->removedAttributes = removedAttrs;
}
QSet<QByteArray> ModifyCollectionCommand::removedAttributes() const
{
    return d_func()->removedAttributes;
}

void ModifyCollectionCommand::setAttributes(const Protocol::Attributes &attributes)
{
    d_func()->modifiedParts |= Attributes;
    d_func()->attributes = attributes;
}
Attributes ModifyCollectionCommand::attributes() const
{
    return d_func()->attributes;
}

void ModifyCollectionCommand::setEnabled(bool enabled)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->enabled = enabled;
}
bool ModifyCollectionCommand::enabled() const
{
    return d_func()->enabled;
}

void ModifyCollectionCommand::setSyncPref(Tristate sync)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->sync = sync;
}
Tristate ModifyCollectionCommand::syncPref() const
{
    return d_func()->sync;
}

void ModifyCollectionCommand::setDisplayPref(Tristate display)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->display = display;
}
Tristate ModifyCollectionCommand::displayPref() const
{
    return d_func()->display;
}

void ModifyCollectionCommand::setIndexPref(Tristate index)
{
    d_func()->modifiedParts |= ListPreferences;
    d_func()->index = index;
}
Tristate ModifyCollectionCommand::indexPref() const
{
    return d_func()->index;
}

void ModifyCollectionCommand::setReferenced(bool referenced)
{
    d_func()->modifiedParts |= Referenced;
    d_func()->referenced = referenced;
}
bool ModifyCollectionCommand::referenced() const
{
    return d_func()->referenced;
}

DataStream &operator<<(DataStream &stream, const ModifyCollectionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, ModifyCollectionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

ModifyCollectionResponse::ModifyCollectionResponse()
    : Response(new ResponsePrivate(Command::ModifyCollection))
{
}

ModifyCollectionResponse::ModifyCollectionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::ModifyCollection);
}

/****************************************************************************/

class MoveCollectionCommandPrivate : public CommandPrivate
{
public:
    MoveCollectionCommandPrivate(const Scope &collection = Scope(),
                                 const Scope &dest = Scope())
        : CommandPrivate(Command::MoveCollection)
        , collection(collection)
        , dest(dest)
    {}
    MoveCollectionCommandPrivate(const MoveCollectionCommandPrivate &other)
        : CommandPrivate(other)
        , collection(other.collection)
        , dest(other.dest)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(collection)
               && COMPARE(dest);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << collection
               << dest;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> collection
        >> dest;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Collection", collection);
        blck.write("Destination", dest);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new MoveCollectionCommandPrivate(*this);
    }

    Scope collection;
    Scope dest;
};

AKONADI_DECLARE_PRIVATE(MoveCollectionCommand)

MoveCollectionCommand::MoveCollectionCommand()
    : Command(new MoveCollectionCommandPrivate)
{
}

MoveCollectionCommand::MoveCollectionCommand(const Scope &collection,
        const Scope &destination)
    : Command(new MoveCollectionCommandPrivate(collection, destination))
{
}

MoveCollectionCommand::MoveCollectionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::MoveCollection);
}

Scope MoveCollectionCommand::collection() const
{
    return d_func()->collection;
}
Scope MoveCollectionCommand::destination() const
{
    return d_func()->dest;
}

DataStream &operator<<(DataStream &stream, const MoveCollectionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, MoveCollectionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

MoveCollectionResponse::MoveCollectionResponse()
    : Response(new ResponsePrivate(Command::MoveCollection))
{
}

MoveCollectionResponse::MoveCollectionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::MoveCollection);
}

/****************************************************************************/

class SearchCommandPrivate : public CommandPrivate
{
public:
    SearchCommandPrivate()
        : CommandPrivate(Command::Search)
        , recursive(false)
        , remote(false)
    {}
    SearchCommandPrivate(const SearchCommandPrivate &other)
        : CommandPrivate(other)
        , mimeTypes(other.mimeTypes)
        , collections(other.collections)
        , query(other.query)
        , fetchScope(other.fetchScope)
        , recursive(other.recursive)
        , remote(other.remote)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(recursive) && COMPARE(remote)
               && COMPARE(mimeTypes)
               && COMPARE(collections)
               && COMPARE(query)
               && COMPARE(fetchScope);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << mimeTypes
               << collections
               << query
               << fetchScope
               << recursive
               << remote;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> mimeTypes
        >> collections
        >> query
        >> fetchScope
        >> recursive
        >> remote;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Query", query);
        blck.write("Collections", collections);
        blck.write("Mimetypes", mimeTypes);
        blck.beginBlock("Fetch Scope");
        fetchScope.debugString(blck);
        blck.endBlock();
        blck.write("Recursive", recursive);
        blck.write("Remote", remote);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new SearchCommandPrivate(*this);
    }

    QStringList mimeTypes;
    QVector<qint64> collections;
    QString query;
    FetchScope fetchScope;
    bool recursive;
    bool remote;
};

AKONADI_DECLARE_PRIVATE(SearchCommand)

SearchCommand::SearchCommand()
    : Command(new SearchCommandPrivate)
{
}

SearchCommand::SearchCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::Search);
}

void SearchCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList SearchCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void SearchCommand::setCollections(const QVector<qint64> &collections)
{
    d_func()->collections = collections;
}
QVector<qint64> SearchCommand::collections() const
{
    return d_func()->collections;
}

void SearchCommand::setQuery(const QString &query)
{
    d_func()->query = query;
}
QString SearchCommand::query() const
{
    return d_func()->query;
}

void SearchCommand::setFetchScope(const FetchScope &fetchScope)
{
    d_func()->fetchScope = fetchScope;
}
FetchScope SearchCommand::fetchScope() const
{
    return d_func()->fetchScope;
}

void SearchCommand::setRecursive(bool recursive)
{
    d_func()->recursive = recursive;
}
bool SearchCommand::recursive() const
{
    return d_func()->recursive;
}

void SearchCommand::setRemote(bool remote)
{
    d_func()->remote = remote;
}
bool SearchCommand::remote() const
{
    return d_func()->remote;
}

DataStream &operator<<(DataStream &stream, const SearchCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, SearchCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

SearchResponse::SearchResponse()
    : Response(new ResponsePrivate(Command::Search))
{
}

SearchResponse::SearchResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::Search);
}

/****************************************************************************/

class SearchResultCommandPrivate : public CommandPrivate
{
public:
    SearchResultCommandPrivate(const QByteArray &searchId = QByteArray(),
                               qint64 collectionId = -1,
                               const Scope &result = Scope())
        : CommandPrivate(Command::SearchResult)
        , searchId(searchId)
        , result(result)
        , collectionId(collectionId)
    {}
    SearchResultCommandPrivate(const SearchResultCommandPrivate &other)
        : CommandPrivate(other)
        , searchId(other.searchId)
        , result(other.result)
        , collectionId(other.collectionId)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(searchId)
               && COMPARE(collectionId)
               && COMPARE(result);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << searchId
               << collectionId
               << result;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> searchId
        >> collectionId
        >> result;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Search ID", searchId);
        blck.write("Collection ID", collectionId);
        blck.write("Result", result);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new SearchResultCommandPrivate(*this);
    }

    QByteArray searchId;
    Scope result;
    qint64 collectionId;
};

AKONADI_DECLARE_PRIVATE(SearchResultCommand)

SearchResultCommand::SearchResultCommand()
    : Command(new SearchResultCommandPrivate)
{
}

SearchResultCommand::SearchResultCommand(const QByteArray &searchId,
        qint64 collectionId,
        const Scope &result)
    : Command(new SearchResultCommandPrivate(searchId, collectionId, result))
{
}

SearchResultCommand::SearchResultCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::SearchResult);
}

QByteArray SearchResultCommand::searchId() const
{
    return d_func()->searchId;
}

qint64 SearchResultCommand::collectionId() const
{
    return d_func()->collectionId;
}

Scope SearchResultCommand::result() const
{
    return d_func()->result;
}

DataStream &operator<<(DataStream &stream, const SearchResultCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, SearchResultCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

SearchResultResponse::SearchResultResponse()
    : Response(new ResponsePrivate(Command::SearchResult))
{
}

SearchResultResponse::SearchResultResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::SearchResult);
}

/****************************************************************************/

class StoreSearchCommandPrivate : public CommandPrivate
{
public:
    StoreSearchCommandPrivate()
        : CommandPrivate(Command::StoreSearch)
        , remote(false)
        , recursive(false)
    {}
    StoreSearchCommandPrivate(const StoreSearchCommandPrivate &other)
        : CommandPrivate(other)
        , name(other.name)
        , query(other.query)
        , mimeTypes(other.mimeTypes)
        , queryCols(other.queryCols)
        , remote(other.remote)
        , recursive(other.recursive)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(remote) && COMPARE(recursive)
               && COMPARE(name)
               && COMPARE(query)
               && COMPARE(mimeTypes)
               && COMPARE(queryCols);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << name
               << query
               << mimeTypes
               << queryCols
               << remote
               << recursive;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> name
        >> query
        >> mimeTypes
        >> queryCols
        >> remote
        >> recursive;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Name", name);
        blck.write("Query", query);
        blck.write("Mimetypes", mimeTypes);
        blck.write("Query Collections", queryCols);
        blck.write("Remote", remote);
        blck.write("Recursive", recursive);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new StoreSearchCommandPrivate(*this);
    }

    QString name;
    QString query;
    QStringList mimeTypes;
    QVector<qint64> queryCols;
    bool remote;
    bool recursive;
};

AKONADI_DECLARE_PRIVATE(StoreSearchCommand)

StoreSearchCommand::StoreSearchCommand()
    : Command(new StoreSearchCommandPrivate)
{
}

StoreSearchCommand::StoreSearchCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::StoreSearch);
}

void StoreSearchCommand::setName(const QString &name)
{
    d_func()->name = name;
}
QString StoreSearchCommand::name() const
{
    return d_func()->name;
}

void StoreSearchCommand::setQuery(const QString &query)
{
    d_func()->query = query;
}
QString StoreSearchCommand::query() const
{
    return d_func()->query;
}

void StoreSearchCommand::setMimeTypes(const QStringList &mimeTypes)
{
    d_func()->mimeTypes = mimeTypes;
}
QStringList StoreSearchCommand::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void StoreSearchCommand::setQueryCollections(const QVector<qint64> &queryCols)
{
    d_func()->queryCols = queryCols;
}
QVector<qint64> StoreSearchCommand::queryCollections() const
{
    return d_func()->queryCols;
}

void StoreSearchCommand::setRemote(bool remote)
{
    d_func()->remote = remote;
}
bool StoreSearchCommand::remote() const
{
    return d_func()->remote;
}

void StoreSearchCommand::setRecursive(bool recursive)
{
    d_func()->recursive = recursive;
}
bool StoreSearchCommand::recursive() const
{
    return d_func()->recursive;
}

DataStream &operator<<(DataStream &stream, const StoreSearchCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, StoreSearchCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

StoreSearchResponse::StoreSearchResponse()
    : Response(new ResponsePrivate(Command::StoreSearch))
{
}

StoreSearchResponse::StoreSearchResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::StoreSearch);
}

/****************************************************************************/

class CreateTagCommandPrivate : public CommandPrivate
{
public:
    CreateTagCommandPrivate()
        : CommandPrivate(Command::CreateTag)
        , parentId(-1)
        , merge(false)
    {}
    CreateTagCommandPrivate(const CreateTagCommandPrivate &other)
        : CommandPrivate(other)
        , gid(other.gid)
        , remoteId(other.remoteId)
        , type(other.type)
        , attributes(other.attributes)
        , parentId(other.parentId)
        , merge(other.merge)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(parentId)
               && COMPARE(merge)
               && COMPARE(gid)
               && COMPARE(remoteId)
               && COMPARE(type)
               && COMPARE(attributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << gid
               << remoteId
               << type
               << attributes
               << parentId
               << merge;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> gid
        >> remoteId
        >> type
        >> attributes
        >> parentId
        >> merge;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Merge", merge);
        blck.write("GID", gid);
        blck.write("Remote ID", remoteId);
        blck.write("Parent ID", parentId);
        blck.write("Type", type);
        blck.write("Attributes", attributes);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CreateTagCommandPrivate(*this);
    }

    QByteArray gid;
    QByteArray remoteId;
    QByteArray type;
    Attributes attributes;
    qint64 parentId;
    bool merge;
};

AKONADI_DECLARE_PRIVATE(CreateTagCommand)

CreateTagCommand::CreateTagCommand()
    : Command(new CreateTagCommandPrivate)
{
}

CreateTagCommand::CreateTagCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::CreateTag);
}

void CreateTagCommand::setGid(const QByteArray &gid)
{
    d_func()->gid = gid;
}
QByteArray CreateTagCommand::gid() const
{
    return d_func()->gid;
}

void CreateTagCommand::setRemoteId(const QByteArray &remoteId)
{
    d_func()->remoteId = remoteId;
}
QByteArray CreateTagCommand::remoteId() const
{
    return d_func()->remoteId;
}

void CreateTagCommand::setType(const QByteArray &type)
{
    d_func()->type = type;
}
QByteArray CreateTagCommand::type() const
{
    return d_func()->type;
}

void CreateTagCommand::setParentId(qint64 parentId)
{
    d_func()->parentId = parentId;
}
qint64 CreateTagCommand::parentId() const
{
    return d_func()->parentId;
}

void CreateTagCommand::setMerge(bool merge)
{
    d_func()->merge = merge;
}
bool CreateTagCommand::merge() const
{
    return d_func()->merge;
}

void CreateTagCommand::setAttributes(const Attributes &attributes)
{
    d_func()->attributes = attributes;
}
Attributes CreateTagCommand::attributes() const
{
    return d_func()->attributes;
}

DataStream &operator<<(DataStream &stream, const CreateTagCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, CreateTagCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

CreateTagResponse::CreateTagResponse()
    : Response(new ResponsePrivate(Command::CreateTag))
{
}

CreateTagResponse::CreateTagResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::CreateTag);
}

/****************************************************************************/

class DeleteTagCommandPrivate : public CommandPrivate
{
public:
    DeleteTagCommandPrivate(const Scope &tag = Scope())
        : CommandPrivate(Command::DeleteTag)
        , tag(tag)
    {}
    DeleteTagCommandPrivate(const DeleteTagCommandPrivate &other)
        : CommandPrivate(other)
        , tag(other.tag)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(tag);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << tag;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> tag;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Tag", tag);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new DeleteTagCommandPrivate(*this);
    }

    Scope tag;
};

AKONADI_DECLARE_PRIVATE(DeleteTagCommand)

DeleteTagCommand::DeleteTagCommand()
    : Command(new DeleteTagCommandPrivate)
{
}

DeleteTagCommand::DeleteTagCommand(const Scope &tag)
    : Command(new DeleteTagCommandPrivate(tag))
{
}

DeleteTagCommand::DeleteTagCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::DeleteTag);
}

Scope DeleteTagCommand::tag() const
{
    return d_func()->tag;
}

DataStream &operator<<(DataStream &stream, const DeleteTagCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, DeleteTagCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

DeleteTagResponse::DeleteTagResponse()
    : Response(new ResponsePrivate(Command::DeleteTag))
{
}

DeleteTagResponse::DeleteTagResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::DeleteTag);
}

/****************************************************************************/

class ModifyTagCommandPrivate : public CommandPrivate
{
public:
    ModifyTagCommandPrivate(qint64 tagId = -1)
        : CommandPrivate(Command::ModifyTag)
        , tagId(tagId)
        , parentId(-1)
        , modifiedParts(ModifyTagCommand::None)
    {}
    ModifyTagCommandPrivate(const ModifyTagCommandPrivate &other)
        : CommandPrivate(other)
        , type(other.type)
        , remoteId(other.remoteId)
        , removedAttributes(other.removedAttributes)
        , attributes(other.attributes)
        , tagId(other.tagId)
        , parentId(other.parentId)
        , modifiedParts(other.modifiedParts)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(modifiedParts)
               && COMPARE(parentId)
               && COMPARE(tagId)
               && COMPARE(type)
               && COMPARE(remoteId)
               && COMPARE(removedAttributes);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        CommandPrivate::serialize(stream)
                << tagId
                << modifiedParts;
        if (modifiedParts & ModifyTagCommand::ParentId) {
            stream << parentId;
        }
        if (modifiedParts & ModifyTagCommand::Type) {
            stream << type;
        }
        if (modifiedParts & ModifyTagCommand::RemoteId) {
            stream << remoteId;
        }
        if (modifiedParts & ModifyTagCommand::RemovedAttributes) {
            stream << removedAttributes;
        }
        if (modifiedParts & ModifyTagCommand::Attributes) {
            stream << attributes;
        }
        return stream;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        CommandPrivate::deserialize(stream)
                >> tagId
        >> modifiedParts;
        if (modifiedParts & ModifyTagCommand::ParentId)
        {
            stream >> parentId;
        }
        if (modifiedParts & ModifyTagCommand::Type)
        {
            stream >> type;
        }
        if (modifiedParts & ModifyTagCommand::RemoteId)
        {
            stream >> remoteId;
        }
        if (modifiedParts & ModifyTagCommand::RemovedAttributes)
        {
            stream >> removedAttributes;
        }
        if (modifiedParts & ModifyTagCommand::Attributes)
        {
            stream >> attributes;
        }
        return stream;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        QStringList mps;
        if (modifiedParts & ModifyTagCommand::ParentId) {
            mps << QStringLiteral("Parent ID");
        }
        if (modifiedParts & ModifyTagCommand::Type) {
            mps << QStringLiteral("Type");
        }
        if (modifiedParts & ModifyTagCommand::RemoteId) {
            mps << QStringLiteral("Remote ID");
        }
        if (modifiedParts & ModifyTagCommand::RemovedAttributes) {
            mps << QStringLiteral("Removed Attributes");
        }
        if (modifiedParts & ModifyTagCommand::Attributes) {
            mps << QStringLiteral("Attributes");
        }

        CommandPrivate::debugString(blck);
        blck.write("Tag ID", tagId);
        blck.write("Modified Parts", mps);
        if (modifiedParts & ModifyTagCommand::ParentId) {
            blck.write("Parent ID", parentId);
        }
        if (modifiedParts & ModifyTagCommand::Type) {
            blck.write("Type", type);
        }
        if (modifiedParts & ModifyTagCommand::RemoteId) {
            blck.write("Remote ID", remoteId);
        }
        if (modifiedParts & ModifyTagCommand::RemovedAttributes) {
            blck.write("Removed Attributes", removedAttributes);
        }
        if (modifiedParts & ModifyTagCommand::Attributes) {
            blck.write("Attributes", attributes);
        }
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ModifyTagCommandPrivate(*this);
    }

    QByteArray type;
    QByteArray remoteId;
    QSet<QByteArray> removedAttributes;
    Attributes attributes;
    qint64 tagId;
    qint64 parentId;
    ModifyTagCommand::ModifiedParts modifiedParts;
};

AKONADI_DECLARE_PRIVATE(ModifyTagCommand)

ModifyTagCommand::ModifyTagCommand()
    : Command(new ModifyTagCommandPrivate)
{
}

ModifyTagCommand::ModifyTagCommand(qint64 id)
    : Command(new ModifyTagCommandPrivate(id))
{
}

ModifyTagCommand::ModifyTagCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::ModifyTag);
}

qint64 ModifyTagCommand::tagId() const
{
    return d_func()->tagId;
}

ModifyTagCommand::ModifiedParts ModifyTagCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifyTagCommand::setParentId(qint64 parentId)
{
    d_func()->modifiedParts |= ParentId;
    d_func()->parentId = parentId;
}
qint64 ModifyTagCommand::parentId() const
{
    return d_func()->parentId;
}

void ModifyTagCommand::setType(const QByteArray &type)
{
    d_func()->modifiedParts |= Type;
    d_func()->type = type;
}
QByteArray ModifyTagCommand::type() const
{
    return d_func()->type;
}

void ModifyTagCommand::setRemoteId(const QByteArray &remoteId)
{
    d_func()->modifiedParts |= RemoteId;
    d_func()->remoteId = remoteId;
}
QByteArray ModifyTagCommand::remoteId() const
{
    return d_func()->remoteId;
}

void ModifyTagCommand::setRemovedAttributes(const QSet<QByteArray> &removed)
{
    d_func()->modifiedParts |= RemovedAttributes;
    d_func()->removedAttributes = removed;
}
QSet<QByteArray> ModifyTagCommand::removedAttributes() const
{
    return d_func()->removedAttributes;
}

void ModifyTagCommand::setAttributes(const Protocol::Attributes &attributes)
{
    d_func()->modifiedParts |= Attributes;
    d_func()->attributes = attributes;
}
Attributes ModifyTagCommand::attributes() const
{
    return d_func()->attributes;
}

DataStream &operator<<(DataStream &stream, const ModifyTagCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, ModifyTagCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

ModifyTagResponse::ModifyTagResponse()
    : Response(new ResponsePrivate(Command::ModifyTag))
{
}

ModifyTagResponse::ModifyTagResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::ModifyTag);
}

/****************************************************************************/

class ModifyRelationCommandPrivate : public CommandPrivate
{
public:
    ModifyRelationCommandPrivate(qint64 left = -1, qint64 right = -1,
                                 const QByteArray &type = QByteArray(),
                                 const QByteArray &remoteId = QByteArray())
        : CommandPrivate(Command::ModifyRelation)
        , type(type)
        , remoteId(remoteId)
        , left(left)
        , right(right)
    {}
    ModifyRelationCommandPrivate(const ModifyRelationCommandPrivate &other)
        : CommandPrivate(other)
        , type(other.type)
        , remoteId(other.remoteId)
        , left(other.left)
        , right(other.right)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(left)
               && COMPARE(right)
               && COMPARE(type)
               && COMPARE(remoteId);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << left
               << right
               << type
               << remoteId;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> left
        >> right
        >> type
        >> remoteId;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Left", left);
        blck.write("Right", right);
        blck.write("Type", type);
        blck.write("Remote ID", remoteId);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ModifyRelationCommandPrivate(*this);
    }

    QByteArray type;
    QByteArray remoteId;
    qint64 left;
    qint64 right;
};

AKONADI_DECLARE_PRIVATE(ModifyRelationCommand)

ModifyRelationCommand::ModifyRelationCommand()
    : Command(new ModifyRelationCommandPrivate)
{
}

ModifyRelationCommand::ModifyRelationCommand(qint64 left, qint64 right,
        const QByteArray &type,
        const QByteArray &remoteId)
    : Command(new ModifyRelationCommandPrivate(left, right, type, remoteId))
{
}

ModifyRelationCommand::ModifyRelationCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::ModifyRelation);
}

void ModifyRelationCommand::setLeft(qint64 left)
{
    d_func()->left = left;
}
qint64 ModifyRelationCommand::left() const
{
    return d_func()->left;
}

void ModifyRelationCommand::setRight(qint64 right)
{
    d_func()->right = right;
}
qint64 ModifyRelationCommand::right() const
{
    return d_func()->right;
}

void ModifyRelationCommand::setType(const QByteArray &type)
{
    d_func()->type = type;
}
QByteArray ModifyRelationCommand::type() const
{
    return d_func()->type;
}

void ModifyRelationCommand::setRemoteId(const QByteArray &remoteId)
{
    d_func()->remoteId = remoteId;
}
QByteArray ModifyRelationCommand::remoteId() const
{
    return d_func()->remoteId;
}

DataStream &operator<<(DataStream &stream, const ModifyRelationCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, ModifyRelationCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

ModifyRelationResponse::ModifyRelationResponse()
    : Response(new ResponsePrivate(Command::ModifyRelation))
{
}

ModifyRelationResponse::ModifyRelationResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::ModifyRelation);
}

/****************************************************************************/

class RemoveRelationsCommandPrivate : public CommandPrivate
{
public:
    RemoveRelationsCommandPrivate(qint64 left = -1, qint64 right = -1,
                                  const QByteArray &type = QByteArray())
        : CommandPrivate(Command::RemoveRelations)
        , left(left)
        , right(right)
        , type(type)
    {}
    RemoveRelationsCommandPrivate(const RemoveRelationsCommandPrivate &other)
        : CommandPrivate(other)
        , left(other.left)
        , right(other.right)
        , type(other.type)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(left)
               && COMPARE(right)
               && COMPARE(type);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << left
               << right
               << type;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> left
        >> right
        >> type;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Left", left);
        blck.write("Right", right);
        blck.write("Type", type);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new RemoveRelationsCommandPrivate(*this);
    }

    qint64 left;
    qint64 right;
    QByteArray type;
};

AKONADI_DECLARE_PRIVATE(RemoveRelationsCommand)

RemoveRelationsCommand::RemoveRelationsCommand()
    : Command(new RemoveRelationsCommandPrivate)
{
}

RemoveRelationsCommand::RemoveRelationsCommand(qint64 left, qint64 right, const QByteArray &type)
    : Command(new RemoveRelationsCommandPrivate(left, right, type))
{
}

RemoveRelationsCommand::RemoveRelationsCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::RemoveRelations);
}

void RemoveRelationsCommand::setLeft(qint64 left)
{
    d_func()->left = left;
}
qint64 RemoveRelationsCommand::left() const
{
    return d_func()->left;
}

void RemoveRelationsCommand::setRight(qint64 right)
{
    d_func()->right = right;
}
qint64 RemoveRelationsCommand::right() const
{
    return d_func()->right;
}

void RemoveRelationsCommand::setType(const QByteArray &type)
{
    d_func()->type = type;
}
QByteArray RemoveRelationsCommand::type() const
{
    return d_func()->type;
}

DataStream &operator<<(DataStream &stream, const RemoveRelationsCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, RemoveRelationsCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

RemoveRelationsResponse::RemoveRelationsResponse()
    : Response(new ResponsePrivate(Command::RemoveRelations))
{
}

RemoveRelationsResponse::RemoveRelationsResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::RemoveRelations);
}

/****************************************************************************/

class SelectResourceCommandPrivate : public CommandPrivate
{
public:
    SelectResourceCommandPrivate(const QString &resourceId = QString())
        : CommandPrivate(Command::SelectResource)
        , resourceId(resourceId)
    {}
    SelectResourceCommandPrivate(const SelectResourceCommandPrivate &other)
        : CommandPrivate(other)
        , resourceId(other.resourceId)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(resourceId);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << resourceId;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> resourceId;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Resource ID", resourceId);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new SelectResourceCommandPrivate(*this);
    }

    QString resourceId;
};

AKONADI_DECLARE_PRIVATE(SelectResourceCommand)

SelectResourceCommand::SelectResourceCommand()
    : Command(new SelectResourceCommandPrivate)
{
}

SelectResourceCommand::SelectResourceCommand(const QString &resourceId)
    : Command(new SelectResourceCommandPrivate(resourceId))
{
}

SelectResourceCommand::SelectResourceCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::SelectResource);
}

QString SelectResourceCommand::resourceId() const
{
    return d_func()->resourceId;
}

DataStream &operator<<(DataStream &stream, const SelectResourceCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, SelectResourceCommand &command)
{
    return command.d_func()->deserialize(stream);
}

/****************************************************************************/

SelectResourceResponse::SelectResourceResponse()
    : Response(new ResponsePrivate(Command::SelectResource))
{
}

SelectResourceResponse::SelectResourceResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::SelectResource);
}

/****************************************************************************/

class StreamPayloadCommandPrivate : public CommandPrivate
{
public:
    StreamPayloadCommandPrivate(const QByteArray &name = QByteArray(),
                                StreamPayloadCommand::Request request = StreamPayloadCommand::MetaData,
                                const QString &dest = QString())
        : CommandPrivate(Command::StreamPayload)
        , payloadName(name)
        , dest(dest)
        , request(request)
    {}
    StreamPayloadCommandPrivate(const StreamPayloadCommandPrivate &other)
        : CommandPrivate(other)
        , payloadName(other.payloadName)
        , dest(other.dest)
        , request(other.request)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(request)
               && COMPARE(payloadName)
               && COMPARE(dest);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << payloadName
               << request
               << dest;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> payloadName
        >> request
        >> dest;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        CommandPrivate::debugString(blck);
        blck.write("Payload Name", payloadName);
        blck.write("Request", request);
        blck.write("Destination", dest);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new StreamPayloadCommandPrivate(*this);
    }

    QByteArray payloadName;
    QString dest;
    StreamPayloadCommand::Request request;
};

AKONADI_DECLARE_PRIVATE(StreamPayloadCommand)

StreamPayloadCommand::StreamPayloadCommand()
    : Command(new StreamPayloadCommandPrivate)
{
}

StreamPayloadCommand::StreamPayloadCommand(const QByteArray &name, Request request,
        const QString &dest)
    : Command(new StreamPayloadCommandPrivate(name, request, dest))
{
}

StreamPayloadCommand::StreamPayloadCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::StreamPayload);
}

void StreamPayloadCommand::setPayloadName(const QByteArray &name)
{
    d_func()->payloadName = name;
}
QByteArray StreamPayloadCommand::payloadName() const
{
    return d_func()->payloadName;
}

void StreamPayloadCommand::setRequest(Request request)
{
    d_func()->request = request;
}
StreamPayloadCommand::Request StreamPayloadCommand::request() const
{
    return d_func()->request;
}

void StreamPayloadCommand::setDestination(const QString &dest)
{
    d_func()->dest = dest;
}
QString StreamPayloadCommand::destination() const
{
    return d_func()->dest;
}

DataStream &operator<<(DataStream &stream, const StreamPayloadCommand &command)
{
    return command.d_func()->serialize(stream);
    return stream;
}

DataStream &operator>>(DataStream &stream, StreamPayloadCommand &command)
{
    return command.d_func()->deserialize(stream);
    return stream;
}

/****************************************************************************/

class StreamPayloadResponsePrivate : public ResponsePrivate
{
public:
    StreamPayloadResponsePrivate(const QByteArray &payloadName = QByteArray(),
                                 const PartMetaData &metaData = PartMetaData(),
                                 const QByteArray &data = QByteArray())
        : ResponsePrivate(Command::StreamPayload)
        , payloadName(payloadName)
        , data(data)
        , metaData(metaData)
    {}
    StreamPayloadResponsePrivate(const StreamPayloadResponsePrivate &other)
        : ResponsePrivate(other)
        , payloadName(other.payloadName)
        , data(other.data)
        , metaData(other.metaData)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::compare(other)
               && COMPARE(metaData)
               && COMPARE(payloadName)
               && COMPARE(data);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ResponsePrivate::serialize(stream)
               << payloadName
               << metaData
               << data;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ResponsePrivate::deserialize(stream)
                >> payloadName
        >> metaData
        >> data;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ResponsePrivate::debugString(blck);
        blck.beginBlock("MetaData");
        blck.write("Name", metaData.name());
        blck.write("Size", metaData.size());
        blck.write("Version", metaData.version());
        blck.endBlock();
        blck.write("Data", data);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new StreamPayloadResponsePrivate(*this);
    }

    QByteArray payloadName;
    QByteArray data;
    PartMetaData metaData;
};

AKONADI_DECLARE_PRIVATE(StreamPayloadResponse)

StreamPayloadResponse::StreamPayloadResponse()
    : Response(new StreamPayloadResponsePrivate)
{
}

StreamPayloadResponse::StreamPayloadResponse(const QByteArray &payloadName,
        const PartMetaData &metaData)
    : Response(new StreamPayloadResponsePrivate(payloadName, metaData))
{
}

StreamPayloadResponse::StreamPayloadResponse(const QByteArray &payloadName,
        const QByteArray &data)
    : Response(new StreamPayloadResponsePrivate(payloadName, PartMetaData(), data))
{
}

StreamPayloadResponse::StreamPayloadResponse(const QByteArray &payloadName,
        const PartMetaData &metaData,
        const QByteArray &data)
    : Response(new StreamPayloadResponsePrivate(payloadName, metaData, data))
{
}

StreamPayloadResponse::StreamPayloadResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::StreamPayload);
}

void StreamPayloadResponse::setPayloadName(const QByteArray &payloadName)
{
    d_func()->payloadName = payloadName;
}
QByteArray StreamPayloadResponse::payloadName() const
{
    return d_func()->payloadName;
}
void StreamPayloadResponse::setMetaData(const PartMetaData &metaData)
{
    d_func()->metaData = metaData;
}
PartMetaData StreamPayloadResponse::metaData() const
{
    return d_func()->metaData;
}
void StreamPayloadResponse::setData(const QByteArray &data)
{
    d_func()->data = data;
}
QByteArray StreamPayloadResponse::data() const
{
    return d_func()->data;
}

DataStream &operator<<(DataStream &stream, const StreamPayloadResponse &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, StreamPayloadResponse &command)
{
    return command.d_func()->deserialize(stream);
}

/******************************************************************************/

class ChangeNotificationPrivate : public CommandPrivate
{
public:
    ChangeNotificationPrivate(Command::Type type)
        : CommandPrivate(type)
    {
    }

    ChangeNotificationPrivate(const ChangeNotificationPrivate &other)
        : CommandPrivate(other)
        , sessionId(other.sessionId)
        , metadata(other.metadata)
    {
    }

    virtual bool compare(const Akonadi::Protocol::CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(sessionId);
    }

    virtual DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << sessionId;
    }

    virtual DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> sessionId;
    }

    QByteArray sessionId;

    // For internal use only: Akonadi server can add some additional information
    // that might be useful when evaluating the notification for example, but
    // it is never transferred to clients
    QVector<QByteArray> metadata;
};

AKONADI_DECLARE_PRIVATE(ChangeNotification)

ChangeNotification::ChangeNotification()
    : Command()
{
}

ChangeNotification::ChangeNotification(ChangeNotificationPrivate *dptr)
    : Command(dptr)
{
}

ChangeNotification::ChangeNotification(const Command &other)
    : Command(other)
{
}

bool ChangeNotification::isRemove() const
{
    switch (type()) {
    case Command::Invalid:
        return false;
    case Command::ItemChangeNotification:
        return static_cast<const Protocol::ItemChangeNotification *>(this)->operation() == ItemChangeNotification::Remove;
    case Command::CollectionChangeNotification:
        return static_cast<const Protocol::CollectionChangeNotification *>(this)->operation() == CollectionChangeNotification::Remove;
    case Command::TagChangeNotification:
        return static_cast<const Protocol::TagChangeNotification *>(this)->operation() == TagChangeNotification::Remove;
    case Command::RelationChangeNotification:
        return static_cast<const Protocol::RelationChangeNotification *>(this)->operation() == RelationChangeNotification::Remove;
    case Command::SubscriptionChangeNotification:
        return static_cast<const Protocol::SubscriptionChangeNotification *>(this)->operation() == SubscriptionChangeNotification::Remove;
    case Command::DebugChangeNotification:
        return false;
    default:
        Q_ASSERT_X(false, __FUNCTION__, "Unknown ChangeNotification type");
    }

    return false;
}

bool ChangeNotification::isMove() const
{
    switch (type()) {
    case Command::Invalid:
        return false;
    case Command::ItemChangeNotification:
        return static_cast<const Protocol::ItemChangeNotification *>(this)->operation() == ItemChangeNotification::Move;
    case Command::CollectionChangeNotification:
        return static_cast<const Protocol::CollectionChangeNotification *>(this)->operation() == CollectionChangeNotification::Move;
    case Command::TagChangeNotification:
    case Command::RelationChangeNotification:
    case Command::SubscriptionChangeNotification:
    case Command::DebugChangeNotification:
        return false;
    default:
        Q_ASSERT_X(false, __FUNCTION__, "Unknown ChangeNotification type");
    }

    return false;
}

void ChangeNotification::setSessionId(const QByteArray &sessionId)
{
    d_func()->sessionId = sessionId;
}

QByteArray ChangeNotification::sessionId() const
{
    return d_func()->sessionId;
}

void ChangeNotification::addMetadata(const QByteArray &metadata)
{
    d_func()->metadata.append(metadata);
}

void ChangeNotification::removeMetadata(const QByteArray &metadata)
{
    d_func()->metadata.removeAll(metadata);
}

QVector<QByteArray> ChangeNotification::metadata() const
{
    return d_func()->metadata;
}

class ItemChangeNotificationPrivate : public ChangeNotificationPrivate
{
public:
    ItemChangeNotificationPrivate()
        : ChangeNotificationPrivate(Command::ItemChangeNotification)
        , parentCollection(-1)
        , parentDestCollection(-1)
        , operation(ItemChangeNotification::InvalidOp)
    {}

    ItemChangeNotificationPrivate(const ItemChangeNotificationPrivate &other)
        : ChangeNotificationPrivate(other)
        , items(other.items)
        , resource(other.resource)
        , destResource(other.destResource)
        , parts(other.parts)
        , addedFlags(other.addedFlags)
        , removedFlags(other.removedFlags)
        , addedTags(other.addedTags)
        , removedTags(other.removedTags)
        , addedRelations(other.addedRelations)
        , removedRelations(other.removedRelations)
        , parentCollection(other.parentCollection)
        , parentDestCollection(other.parentDestCollection)
        , operation(other.operation)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::compare(other)
               && COMPARE(operation)
               && COMPARE(parts)
               && COMPARE(addedFlags)
               && COMPARE(removedFlags)
               && COMPARE(addedTags)
               && COMPARE(removedTags)
               && COMPARE(addedRelations)
               && COMPARE(removedRelations)
               && COMPARE(items)
               && COMPARE(resource)
               && COMPARE(destResource)
               && COMPARE(parentCollection)
               && COMPARE(parentDestCollection);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ItemChangeNotificationPrivate(*this);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::serialize(stream)
               << operation
               << items
               << resource
               << destResource
               << parts
               << addedFlags
               << removedFlags
               << addedTags
               << removedTags
               << addedRelations
               << removedRelations
               << parentCollection
               << parentDestCollection;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ChangeNotificationPrivate::deserialize(stream)
                >> operation
        >> items
        >> resource
        >> destResource
        >> parts
        >> addedFlags
        >> removedFlags
        >> addedTags
        >> removedTags
        >> addedRelations
        >> removedRelations
        >> parentCollection
        >> parentDestCollection;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Operation", [this]() -> QString {
            switch (operation)
            {
            case ItemChangeNotification::Add:
                return QStringLiteral("Add");
            case ItemChangeNotification::Modify:
                return QStringLiteral("Modify");
            case ItemChangeNotification::ModifyFlags:
                return QStringLiteral("ModifyFlags");
            case ItemChangeNotification::ModifyTags:
                return QStringLiteral("ModifyTags");
            case ItemChangeNotification::ModifyRelations:
                return QStringLiteral("ModifyRelations");
            case ItemChangeNotification::Move:
                return QStringLiteral("Move");
            case ItemChangeNotification::Remove:
                return QStringLiteral("Remove");
            case ItemChangeNotification::Link:
                return QStringLiteral("Link");
            case ItemChangeNotification::Unlink:
                return QStringLiteral("Unlink");
            case ItemChangeNotification::InvalidOp:
                return QStringLiteral("*INVALID OPERATION*");
            }
            Q_ASSERT(false);
            return QString();
        }());
        blck.beginBlock("Items");
        Q_FOREACH (const ItemChangeNotification::Item &item, items) {
            blck.beginBlock();
            blck.write("ID", item.id);
            blck.write("RemoteID", item.remoteId);
            blck.write("Remote Revision", item.remoteRevision);
            blck.write("Mime Type", item.mimeType);
            blck.endBlock();
        }
        blck.endBlock();
        blck.write("Resource", resource);
        blck.write("Destination Resource", destResource);
        blck.write("Parent Collection", parentCollection);
        blck.write("Parent Destination Collection", parentDestCollection);
        blck.write("Parts", parts);
        blck.write("Added Flags", addedFlags);
        blck.write("Removed Flags", removedFlags);
        blck.write("Added Tags", addedTags);
        blck.write("Removed Tags", removedTags);
        blck.write("Added Relations", addedRelations);
        blck.write("Removed Relations", removedRelations);
    }

    QMap<qint64, ItemChangeNotification::Item> items;
    QByteArray resource;
    QByteArray destResource;
    QSet<QByteArray> parts;
    QSet<QByteArray> addedFlags;
    QSet<QByteArray> removedFlags;
    QSet<qint64> addedTags;
    QSet<qint64> removedTags;
    QSet<ItemChangeNotification::Relation> addedRelations;
    QSet<ItemChangeNotification::Relation> removedRelations;

    qint64 parentCollection;
    qint64 parentDestCollection;
    ItemChangeNotification::Operation operation;
};

AKONADI_DECLARE_PRIVATE(ItemChangeNotification)

ItemChangeNotification::ItemChangeNotification()
    : ChangeNotification(new ItemChangeNotificationPrivate)
{
}

ItemChangeNotification::ItemChangeNotification(const Command &other)
    : ChangeNotification(other)
{
    checkCopyInvariant(Command::ItemChangeNotification);
}

bool ItemChangeNotification::isValid() const
{
    Q_D(const ItemChangeNotification);
    return d->commandType == Command::ItemChangeNotification
           && d->operation != InvalidOp;
}

void ItemChangeNotification::addItem(Id id, const QString &remoteId, const QString &remoteRevision, const QString &mimeType)
{
    d_func()->items.insert(id, Item(id, remoteId, remoteRevision, mimeType));
}

void ItemChangeNotification::setItem(const QVector<Item> &items)
{
    Q_D(ItemChangeNotification);
    clearItems();
    Q_FOREACH (const Item &item, items) {
        d->items.insert(item.id, item);
    }
}

void ItemChangeNotification::clearItems()
{
    d_func()->items.clear();
}

QMap<qint64, ItemChangeNotification::Item> ItemChangeNotification::items() const
{
    return d_func()->items;
}

ItemChangeNotification::Item ItemChangeNotification::item(const Id id) const
{
    return d_func()->items.value(id);
}

QList<qint64> ItemChangeNotification::uids() const
{
    return d_func()->items.keys();
}

ItemChangeNotification::Operation ItemChangeNotification::operation() const
{
    return d_func()->operation;
}

void ItemChangeNotification::setOperation(Operation operation)
{
    d_func()->operation = operation;
}

QByteArray ItemChangeNotification::resource() const
{
    return d_func()->resource;
}

void ItemChangeNotification::setResource(const QByteArray &resource)
{
    d_func()->resource = resource;
}

qint64 ItemChangeNotification::parentCollection() const
{
    return d_func()->parentCollection;
}

qint64 ItemChangeNotification::parentDestCollection() const
{
    return d_func()->parentDestCollection;
}

void ItemChangeNotification::setParentCollection(Id parent)
{
    d_func()->parentCollection = parent;
}

void ItemChangeNotification::setParentDestCollection(Id parent)
{
    d_func()->parentDestCollection = parent;
}

void ItemChangeNotification::setDestinationResource(const QByteArray &destResource)
{
    d_func()->destResource = destResource;
}

QByteArray ItemChangeNotification::destinationResource() const
{
    return d_func()->destResource;
}

QSet<QByteArray> ItemChangeNotification::itemParts() const
{
    return d_func()->parts;
}

void ItemChangeNotification::setItemParts(const QSet<QByteArray> &parts)
{
    d_func()->parts = parts;
}

QSet<QByteArray> ItemChangeNotification::addedFlags() const
{
    return d_func()->addedFlags;
}

void ItemChangeNotification::setAddedFlags(const QSet<QByteArray> &addedFlags)
{
    d_func()->addedFlags = addedFlags;
}

QSet<QByteArray> ItemChangeNotification::removedFlags() const
{
    return d_func()->removedFlags;
}

void ItemChangeNotification::setRemovedFlags(const QSet<QByteArray> &removedFlags)
{
    d_func()->removedFlags = removedFlags;
}

QSet<qint64> ItemChangeNotification::addedTags() const
{
    return d_func()->addedTags;
}

void ItemChangeNotification::setAddedTags(const QSet<qint64> &addedTags)
{
    d_func()->addedTags = addedTags;
}

QSet<qint64> ItemChangeNotification::removedTags() const
{
    return d_func()->removedTags;
}

void ItemChangeNotification::setRemovedTags(const QSet<qint64> &removedTags)
{
    d_func()->removedTags = removedTags;
}

QSet<ItemChangeNotification::Relation> ItemChangeNotification::addedRelations() const
{
    return d_func()->addedRelations;
}

void ItemChangeNotification::setAddedRelations(const QSet<Relation> &relations)
{
    d_func()->addedRelations = relations;
}

QSet<ItemChangeNotification::Relation> ItemChangeNotification::removedRelations() const
{
    return d_func()->removedRelations;
}

void ItemChangeNotification::setRemovedRelations(const QSet<Relation> &relations)
{
    d_func()->removedRelations = relations;
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::ItemChangeNotification::Item &item)
{
    return stream << item.id
           << item.remoteId
           << item.remoteRevision
           << item.mimeType;
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::ItemChangeNotification::Item &item)
{
    return stream >> item.id
           >> item.remoteId
           >> item.remoteRevision
           >> item.mimeType;
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::ItemChangeNotification::Relation &relation)
{
    return stream << relation.leftId
           << relation.rightId
           << relation.type;
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::ItemChangeNotification::Relation &relation)
{
    return stream >> relation.leftId
           >> relation.rightId
           >> relation.type;
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::ItemChangeNotification &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::ItemChangeNotification &command)
{
    return command.d_func()->deserialize(stream);
}

class CollectionChangeNotificationPrivate : public ChangeNotificationPrivate
{
public:
    CollectionChangeNotificationPrivate()
        : ChangeNotificationPrivate(Command::CollectionChangeNotification)
        , id(-1)
        , parentCollection(-1)
        , parentDestCollection(-1)
        , operation(CollectionChangeNotification::InvalidOp)
    {}

    CollectionChangeNotificationPrivate(const CollectionChangeNotificationPrivate &other)
        : ChangeNotificationPrivate(other)
        , resource(other.resource)
        , destResource(other.destResource)
        , changedParts(other.changedParts)
        , remoteId(other.remoteId)
        , remoteRevision(other.remoteRevision)
        , id(other.id)
        , parentCollection(other.parentCollection)
        , parentDestCollection(other.parentDestCollection)
        , operation(other.operation)
    {}

    bool compareWithoutOpAndParts(const CollectionChangeNotificationPrivate *other) const
    {
        return id == other->id
               && remoteId == other->remoteId
               && remoteRevision == other->remoteRevision
               && resource == other->resource
               && destResource == other->destResource
               && parentCollection == other->parentCollection
               && parentDestCollection == other->parentDestCollection;
    }

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::compare(other)
               && COMPARE(operation)
               && COMPARE(changedParts)
               && compareWithoutOpAndParts(static_cast<const CollectionChangeNotificationPrivate *>(other));
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CollectionChangeNotificationPrivate(*this);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::serialize(stream)
               << operation
               << id
               << remoteId
               << remoteRevision
               << resource
               << destResource
               << changedParts
               << parentCollection
               << parentDestCollection;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ChangeNotificationPrivate::deserialize(stream)
                >> operation
        >> id
        >> remoteId
        >> remoteRevision
        >> resource
        >> destResource
        >> changedParts
        >> parentCollection
        >> parentDestCollection;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Operation", [this]() -> QString {
            switch (operation)
            {
            case CollectionChangeNotification::Add:
                return QStringLiteral("Add");
            case CollectionChangeNotification::Modify:
                return QStringLiteral("Modify");
            case CollectionChangeNotification::Move:
                return QStringLiteral("Move");
            case CollectionChangeNotification::Remove:
                return QStringLiteral("Remove");
            case CollectionChangeNotification::Subscribe:
                return QStringLiteral("Subscribe");
            case CollectionChangeNotification::Unsubscribe:
                return QStringLiteral("Unsubscribe");
            case CollectionChangeNotification::InvalidOp:
                return QStringLiteral("*INVALID OPERATION*");
            }
            Q_ASSERT(false);
            return QString();
        }());
        blck.write("Col ID", id);
        blck.write("Col RID", remoteId);
        blck.write("Col RREV", remoteRevision);
        blck.write("Resource", resource);
        blck.write("Destination Resource", destResource);
        blck.write("Parent Collection", parentCollection);
        blck.write("Parent Destination Collection", parentDestCollection);
        blck.write("Changed Parts", changedParts);
    }

    QByteArray resource;
    QByteArray destResource;
    QSet<QByteArray> changedParts;
    QString remoteId;
    QString remoteRevision;
    qint64 id;
    qint64 parentCollection;
    qint64 parentDestCollection;
    CollectionChangeNotification::Operation operation;
};

AKONADI_DECLARE_PRIVATE(CollectionChangeNotification)

CollectionChangeNotification::CollectionChangeNotification()
    : ChangeNotification(new CollectionChangeNotificationPrivate)
{
}

CollectionChangeNotification::CollectionChangeNotification(const Command &other)
    : ChangeNotification(other)
{
    checkCopyInvariant(Command::CollectionChangeNotification);
}

bool CollectionChangeNotification::isValid() const
{
    Q_D(const CollectionChangeNotification);
    return d->commandType == Command::CollectionChangeNotification
           && d->operation != InvalidOp;
}

CollectionChangeNotification::Id CollectionChangeNotification::id() const
{
    return d_func()->id;
}

void CollectionChangeNotification::setId(Id id)
{
    d_func()->id = id;
}

QString CollectionChangeNotification::remoteId() const
{
    return d_func()->remoteId;
}

void CollectionChangeNotification::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}

QString CollectionChangeNotification::remoteRevision() const
{
    return d_func()->remoteRevision;
}

void CollectionChangeNotification::setRemoteRevision(const QString &remoteRevision)
{
    d_func()->remoteRevision = remoteRevision;
}

CollectionChangeNotification::Operation CollectionChangeNotification::operation() const
{
    return d_func()->operation;
}

void CollectionChangeNotification::setOperation(Operation operation)
{
    d_func()->operation = operation;
}

QByteArray CollectionChangeNotification::resource() const
{
    return d_func()->resource;
}

void CollectionChangeNotification::setResource(const QByteArray &resource)
{
    d_func()->resource = resource;
}

qint64 CollectionChangeNotification::parentCollection() const
{
    return d_func()->parentCollection;
}

qint64 CollectionChangeNotification::parentDestCollection() const
{
    return d_func()->parentDestCollection;
}

void CollectionChangeNotification::setParentCollection(Id parent)
{
    d_func()->parentCollection = parent;
}

void CollectionChangeNotification::setParentDestCollection(Id parent)
{
    d_func()->parentDestCollection = parent;
}

void CollectionChangeNotification::setDestinationResource(const QByteArray &destResource)
{
    d_func()->destResource = destResource;
}

QByteArray CollectionChangeNotification::destinationResource() const
{
    return d_func()->destResource;
}

QSet<QByteArray> CollectionChangeNotification::changedParts() const
{
    return d_func()->changedParts;
}

void CollectionChangeNotification::setChangedParts(const QSet<QByteArray> &changedParts)
{
    d_func()->changedParts = changedParts;
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::CollectionChangeNotification &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::CollectionChangeNotification &command)
{
    return command.d_func()->deserialize(stream);
}

bool CollectionChangeNotification::appendAndCompress(ChangeNotification::List &list, const CollectionChangeNotification &msg)
{
    //It is likely that compressable notifications are within the last few notifications, so avoid searching a list that is potentially huge
    static const int maxCompressionSearchLength = 10;
    int searchCounter = 0;
    // There are often multiple Collection Modify notifications in the queue,
    // so we optimize for this case.
    if (msg.operation() == Modify) {
        // We are iterating from end, since there's higher probability of finding
        // matching notification
        for (auto iter = list.end(), begin = list.begin(); iter != begin;) {
            --iter;
            CollectionChangeNotification &it = static_cast<CollectionChangeNotification &>(*iter);
            if (msg.d_func()->compareWithoutOpAndParts(it.d_func())) {
                // both are modifications, merge them together and drop the new one
                if (msg.operation() == Modify && it.operation() == Modify) {
                    it.setChangedParts(it.changedParts() + msg.changedParts());
                    return false;
                }

                // we found Add notification, which means we can drop this modification
                if (it.operation() == Add) {
                    return false;
                }
            }
            searchCounter++;
            if (searchCounter >= maxCompressionSearchLength) {
                break;
            }
        }
    }

    // All other cases are just append, as the compression becomes too expensive in large batches
    list.append(msg);
    return true;
}

class TagChangeNotificationPrivate : public ChangeNotificationPrivate
{
public:
    TagChangeNotificationPrivate()
        : ChangeNotificationPrivate(Command::TagChangeNotification)
        , id(-1)
        , operation(TagChangeNotification::InvalidOp)
    {}

    TagChangeNotificationPrivate(const TagChangeNotificationPrivate &other)
        : ChangeNotificationPrivate(other)
        , resource(other.resource)
        , remoteId(other.remoteId)
        , id(other.id)
        , operation(other.operation)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::compare(other)
               && COMPARE(operation)
               && COMPARE(id)
               && COMPARE(remoteId)
               && COMPARE(resource);
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new TagChangeNotificationPrivate(*this);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::serialize(stream)
               << operation
               << id
               << remoteId
               << resource;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ChangeNotificationPrivate::deserialize(stream)
                >> operation
        >> id
        >> remoteId
        >> resource;
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Operation", [this]() -> QString {
            switch (operation)
            {
            case TagChangeNotification::Add:
                return QStringLiteral("Add");
            case TagChangeNotification::Modify:
                return QStringLiteral("Modify");
            case TagChangeNotification::Remove:
                return QStringLiteral("Remove");
            case ItemChangeNotification::InvalidOp:
                return QStringLiteral("*INVALID OPERATION*");
            }
            Q_ASSERT(false);
            return QString();
        }());
        blck.write("Tag ID", id);
        blck.write("Tag RID", remoteId);
        blck.endBlock();
        blck.write("Resource", resource);
    }

    QByteArray resource;
    QString remoteId;
    TagChangeNotification::Id id;
    TagChangeNotification::Operation operation;
};

AKONADI_DECLARE_PRIVATE(TagChangeNotification)

TagChangeNotification::TagChangeNotification()
    : ChangeNotification(new TagChangeNotificationPrivate)
{
}

TagChangeNotification::TagChangeNotification(const Command &other)
    : ChangeNotification(other)
{
    checkCopyInvariant(Command::TagChangeNotification);
}

bool TagChangeNotification::isValid() const
{
    Q_D(const TagChangeNotification);
    return d->commandType == Command::TagChangeNotification
           && d->operation != InvalidOp;
}

void TagChangeNotification::setId(Id id)
{
    d_func()->id = id;
}

TagChangeNotification::Id TagChangeNotification::id() const
{
    return d_func()->id;
}

void TagChangeNotification::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}

QString TagChangeNotification::remoteId() const
{
    return d_func()->remoteId;
}

TagChangeNotification::Operation TagChangeNotification::operation() const
{
    return d_func()->operation;
}

void TagChangeNotification::setOperation(Operation operation)
{
    d_func()->operation = operation;
}

QByteArray TagChangeNotification::resource() const
{
    return d_func()->resource;
}

void TagChangeNotification::setResource(const QByteArray &resource)
{
    d_func()->resource = resource;
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::TagChangeNotification &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::TagChangeNotification &command)
{
    return command.d_func()->deserialize(stream);
}

class RelationChangeNotificationPrivate : public ChangeNotificationPrivate
{

public:
    RelationChangeNotificationPrivate()
        : ChangeNotificationPrivate(Command::RelationChangeNotification)
        , left(-1)
        , right(-1)
        , operation(RelationChangeNotification::InvalidOp)
    {
    }

    RelationChangeNotificationPrivate(const RelationChangeNotificationPrivate &other)
        : ChangeNotificationPrivate(other)
        , left(other.left)
        , right(other.right)
        , remoteId(other.remoteId)
        , type(other.type)
        , operation(other.operation)
    {
    }

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::compare(other)
               && COMPARE(operation)
               && COMPARE(left)
               && COMPARE(right)
               && COMPARE(remoteId)
               && COMPARE(type);
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Operation", [this]() -> QString {
            switch (operation)
            {
            case RelationChangeNotification::Add:
                return QStringLiteral("Add");
            case RelationChangeNotification::Remove:
                return QStringLiteral("Remove");
            case RelationChangeNotification::InvalidOp:
                return QStringLiteral("*INVALID OPERATION*");
            }
            Q_ASSERT(false);
            return QString();
        }());
        blck.write("Left Item", left);
        blck.write("Right Item", right);
        blck.write("Remote ID", remoteId);
        blck.write("Type", type);
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return ChangeNotificationPrivate::deserialize(stream)
                >> operation
        >> left
        >> right
        >> remoteId
        >> type;
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::serialize(stream)
               << operation
               << left
               << right
               << remoteId
               << type;
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new RelationChangeNotificationPrivate(*this);
    }

    RelationChangeNotification::Id left;
    RelationChangeNotification::Id right;
    QString remoteId;
    QString type;
    RelationChangeNotification::Operation operation;
};

AKONADI_DECLARE_PRIVATE(RelationChangeNotification)

RelationChangeNotification::RelationChangeNotification()
    : ChangeNotification(new RelationChangeNotificationPrivate)
{
}

RelationChangeNotification::RelationChangeNotification(const Command &other)
    : ChangeNotification(other)
{
    checkCopyInvariant(Command::RelationChangeNotification);
}

RelationChangeNotification::Operation RelationChangeNotification::operation() const
{
    return d_func()->operation;
}

void RelationChangeNotification::setOperation(Operation operation)
{
    d_func()->operation = operation;
}

RelationChangeNotification::Id RelationChangeNotification::leftItem() const
{
    return d_func()->left;
}

void RelationChangeNotification::setLeftItem(Id left)
{
    d_func()->left = left;
}

RelationChangeNotification::Id RelationChangeNotification::rightItem() const
{
    return d_func()->right;
}

void RelationChangeNotification::setRightItem(Id right)
{
    d_func()->right = right;
}

QString RelationChangeNotification::remoteId() const
{
    return d_func()->remoteId;
}

void RelationChangeNotification::setRemoteId(const QString &remoteId)
{
    d_func()->remoteId = remoteId;
}

QString RelationChangeNotification::type() const
{
    return d_func()->type;
}

void RelationChangeNotification::setType(const QString &type)
{
    d_func()->type = type;
}

DataStream &operator<<(DataStream &stream, const Akonadi::Protocol::RelationChangeNotification &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, Akonadi::Protocol::RelationChangeNotification &command)
{
    return command.d_func()->deserialize(stream);
}

class CreateSubscriptionCommandPrivate : public CommandPrivate
{
public:
    CreateSubscriptionCommandPrivate(const QByteArray &subscriberName = QByteArray(),
                                     const QByteArray &session = QByteArray())
        : CommandPrivate(Command::CreateSubscription)
        , subscriberName(subscriberName)
        , session(session)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(subscriberName)
               && COMPARE(session);
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Subscriber", subscriberName);
        blck.write("Session", session);
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        return CommandPrivate::deserialize(stream)
                >> subscriberName
        >> session;
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::serialize(stream)
               << subscriberName
               << session;
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new CreateSubscriptionCommandPrivate(subscriberName, session);
    }

    QByteArray subscriberName;
    QByteArray session;
};

AKONADI_DECLARE_PRIVATE(CreateSubscriptionCommand)

CreateSubscriptionCommand::CreateSubscriptionCommand()
    : Command(new CreateSubscriptionCommandPrivate())
{
}

CreateSubscriptionCommand::CreateSubscriptionCommand(const QByteArray &subscriberName,
        const QByteArray &session)
    : Command(new CreateSubscriptionCommandPrivate(subscriberName, session))
{
}

CreateSubscriptionCommand::CreateSubscriptionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::CreateSubscription);
}

void CreateSubscriptionCommand::setSubscriberName(const QByteArray &subscriberName)
{
    d_func()->subscriberName = subscriberName;
}

QByteArray CreateSubscriptionCommand::subscriberName() const
{
    return d_func()->subscriberName;
}

void CreateSubscriptionCommand::setSession(const QByteArray &session)
{
    d_func()->session = session;
}

QByteArray CreateSubscriptionCommand::session() const
{
    return d_func()->session;
}

DataStream &operator<<(DataStream &stream, const CreateSubscriptionCommand &command)
{
    return command.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, CreateSubscriptionCommand &command)
{
    return command.d_func()->deserialize(stream);
}

CreateSubscriptionResponse::CreateSubscriptionResponse()
    : Response(new ResponsePrivate(Command::CreateSubscription))
{
}

CreateSubscriptionResponse::CreateSubscriptionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::CreateSubscription);
}

class ModifySubscriptionCommandPrivate : public CommandPrivate
{
public:
    explicit ModifySubscriptionCommandPrivate(const QByteArray &subscriberName = QByteArray())
        : CommandPrivate(Command::ModifySubscription)
        , subscriberName(subscriberName)
        , modifiedParts(ModifySubscriptionCommand::None)
        , monitorAll(false)
        , exclusive(false)
    {
    }

    ModifySubscriptionCommandPrivate(const ModifySubscriptionCommandPrivate &other)
        : CommandPrivate(other)
        , subscriberName(other.subscriberName)
        , modifiedParts(other.modifiedParts)
        , startCollections(other.startCollections)
        , stopCollections(other.stopCollections)
        , startItems(other.startItems)
        , stopItems(other.stopItems)
        , startTags(other.startTags)
        , stopTags(other.stopTags)
        , startTypes(other.startTypes)
        , stopTypes(other.stopTypes)
        , startResources(other.startResources)
        , stopResources(other.stopResources)
        , startMimeTypes(other.startMimeTypes)
        , stopMimeTypes(other.stopMimeTypes)
        , startSessions(other.startSessions)
        , stopSessions(other.stopSessions)
        , monitorAll(other.monitorAll)
        , exclusive(other.exclusive)
    {
    }

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return CommandPrivate::compare(other)
               && COMPARE(subscriberName)
               && COMPARE(modifiedParts)
               && COMPARE(startCollections)
               && COMPARE(stopCollections)
               && COMPARE(startItems)
               && COMPARE(stopItems)
               && COMPARE(startTags)
               && COMPARE(stopTags)
               && COMPARE(startTypes)
               && COMPARE(stopTypes)
               && COMPARE(startResources)
               && COMPARE(stopResources)
               && COMPARE(startMimeTypes)
               && COMPARE(stopMimeTypes)
               && COMPARE(startSessions)
               && COMPARE(stopSessions)
               && COMPARE(monitorAll)
               && COMPARE(exclusive);
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.write("Subscriber", subscriberName);
        // TODO: Expand the flags
        blck.write("Modified parts", modifiedParts);
        if (modifiedParts & (ModifySubscriptionCommand::Collections | ModifySubscriptionCommand::Add)) {
            blck.write("Start Collections", startCollections);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Collections | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop Collections", stopCollections);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Items | ModifySubscriptionCommand::Add)) {
            blck.write("Start Items", startItems);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Items | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop Items", stopItems);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Tags | ModifySubscriptionCommand::Add)) {
            blck.write("Start Tags", startTags);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Tags | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop Tags", stopTags);
        }
        // TODO: Expand types
        if (modifiedParts & (ModifySubscriptionCommand::Types | ModifySubscriptionCommand::Add)) {
            blck.write("Start types", startTypes);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Types | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop types", stopTypes);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Resources | ModifySubscriptionCommand::Add)) {
            blck.write("Start resources", startResources);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Resources | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop resources", stopResources);
        }
        if (modifiedParts & (ModifySubscriptionCommand::MimeTypes | ModifySubscriptionCommand::Add)) {
            blck.write("Start mimetypes", startMimeTypes);
        }
        if (modifiedParts & (ModifySubscriptionCommand::MimeTypes | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop mimetypes", stopMimeTypes);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Sessions | ModifySubscriptionCommand::Add)) {
            blck.write("Start sessions", startSessions);
        }
        if (modifiedParts & (ModifySubscriptionCommand::Sessions | ModifySubscriptionCommand::Remove)) {
            blck.write("Stop sessions", stopSessions);
        }
        if (modifiedParts & ModifySubscriptionCommand::AllFlag) {
            blck.write("Monitor All", monitorAll);
        }
        if (modifiedParts & ModifySubscriptionCommand::ExclusiveFlag) {
            blck.write("Exclusive", exclusive);
        }
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        CommandPrivate::serialize(stream)
                << subscriberName
                << modifiedParts;
        if (modifiedParts & (ModifySubscriptionCommand::Collections | ModifySubscriptionCommand::Add)) {
            stream << startCollections;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Collections | ModifySubscriptionCommand::Remove)) {
            stream << stopCollections;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Items | ModifySubscriptionCommand::Add)) {
            stream << startItems;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Items | ModifySubscriptionCommand::Remove)) {
            stream << stopItems;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Tags | ModifySubscriptionCommand::Add)) {
            stream << startTags;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Tags | ModifySubscriptionCommand::Remove)) {
            stream << stopTags;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Types | ModifySubscriptionCommand::Add)) {
            stream << startTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Types | ModifySubscriptionCommand::Remove)) {
            stream << stopTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Resources | ModifySubscriptionCommand::Add)) {
            stream << startResources;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Resources | ModifySubscriptionCommand::Remove)) {
            stream << stopResources;
        }
        if (modifiedParts & (ModifySubscriptionCommand::MimeTypes | ModifySubscriptionCommand::Add)) {
            stream << startMimeTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::MimeTypes | ModifySubscriptionCommand::Remove)) {
            stream << stopMimeTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Sessions | ModifySubscriptionCommand::Add)) {
            stream << startSessions;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Sessions | ModifySubscriptionCommand::Remove)) {
            stream << stopSessions;
        }
        if (modifiedParts & ModifySubscriptionCommand::AllFlag) {
            stream << monitorAll;
        }
        if (modifiedParts & ModifySubscriptionCommand::ExclusiveFlag) {
            stream << exclusive;
        }
        return stream;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        CommandPrivate::deserialize(stream)
                >> subscriberName
        >> modifiedParts;
        if (modifiedParts & (ModifySubscriptionCommand::Collections | ModifySubscriptionCommand::Add))
        {
            stream >> startCollections;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Collections | ModifySubscriptionCommand::Remove))
        {
            stream >> stopCollections;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Items | ModifySubscriptionCommand::Add))
        {
            stream >> startItems;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Items | ModifySubscriptionCommand::Remove))
        {
            stream >> stopItems;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Tags | ModifySubscriptionCommand::Add))
        {
            stream >> startTags;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Tags | ModifySubscriptionCommand::Remove))
        {
            stream >> stopTags;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Types | ModifySubscriptionCommand::Add))
        {
            stream >> startTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Types | ModifySubscriptionCommand::Remove))
        {
            stream >> stopTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Resources | ModifySubscriptionCommand::Add))
        {
            stream >> startResources;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Resources | ModifySubscriptionCommand::Remove))
        {
            stream >> stopResources;
        }
        if (modifiedParts & (ModifySubscriptionCommand::MimeTypes | ModifySubscriptionCommand::Add))
        {
            stream >> startMimeTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::MimeTypes | ModifySubscriptionCommand::Remove))
        {
            stream >> stopMimeTypes;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Sessions | ModifySubscriptionCommand::Add))
        {
            stream >> startSessions;
        }
        if (modifiedParts & (ModifySubscriptionCommand::Sessions | ModifySubscriptionCommand::Remove))
        {
            stream >> stopSessions;
        }
        if (modifiedParts & ModifySubscriptionCommand::AllFlag)
        {
            stream >> monitorAll;
        }
        if (modifiedParts & ModifySubscriptionCommand::ExclusiveFlag)
        {
            stream >> exclusive;
        }
        return stream;
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new ModifySubscriptionCommandPrivate(*this);
    }

    QByteArray subscriberName;
    ModifySubscriptionCommand::ModifiedParts modifiedParts;
    // TODO: Don't waste so much space!
    QVector<qint64> startCollections;
    QVector<qint64> stopCollections;
    QVector<qint64> startItems;
    QVector<qint64> stopItems;
    QVector<qint64> startTags;
    QVector<qint64> stopTags;
    QVector<ModifySubscriptionCommand::ChangeType> startTypes;
    QVector<ModifySubscriptionCommand::ChangeType> stopTypes;
    QVector<QByteArray> startResources;
    QVector<QByteArray> stopResources;
    QStringList startMimeTypes;
    QStringList stopMimeTypes;
    QVector<QByteArray> startSessions;
    QVector<QByteArray> stopSessions;
    bool monitorAll;
    bool exclusive;
};

AKONADI_DECLARE_PRIVATE(ModifySubscriptionCommand)

ModifySubscriptionCommand::ModifySubscriptionCommand()
    : Command(new ModifySubscriptionCommandPrivate())
{
}

ModifySubscriptionCommand::ModifySubscriptionCommand(const Command &other)
    : Command(other)
{
    checkCopyInvariant(Command::ModifySubscription);
}

void ModifySubscriptionCommand::setSubscriberName(const QByteArray &subscriberName)
{
    d_func()->subscriberName = subscriberName;
}

QByteArray ModifySubscriptionCommand::subscriberName() const
{
    return d_func()->subscriberName;
}

ModifySubscriptionCommand::ModifiedParts ModifySubscriptionCommand::modifiedParts() const
{
    return d_func()->modifiedParts;
}

void ModifySubscriptionCommand::startMonitoringCollection(qint64 id)
{
    d_func()->stopCollections.removeAll(id);
    d_func()->startCollections << id;
    d_func()->modifiedParts |= ModifiedParts(Collections | Add);
}

QVector<qint64> ModifySubscriptionCommand::startMonitoringCollections() const
{
    return d_func()->startCollections;
}

void ModifySubscriptionCommand::stopMonitoringCollection(qint64 id)
{
    d_func()->startCollections.removeAll(id);
    d_func()->stopCollections << id;
    d_func()->modifiedParts |= ModifiedParts(Collections | Remove);
}

QVector<qint64> ModifySubscriptionCommand::stopMonitoringCollections() const
{
    return d_func()->stopCollections;
}

void ModifySubscriptionCommand::startMonitoringItem(qint64 id)
{
    d_func()->stopItems.removeAll(id);
    d_func()->startItems << id;
    d_func()->modifiedParts |= ModifiedParts(Items | Add);
}

QVector<qint64> ModifySubscriptionCommand::startMonitoringItems() const
{
    return d_func()->startItems;
}

void ModifySubscriptionCommand::stopMonitoringItem(qint64 id)
{
    d_func()->startItems.removeAll(id);
    d_func()->stopItems << id;
    d_func()->modifiedParts |= ModifiedParts(Items | Remove);
}

QVector<qint64> ModifySubscriptionCommand::stopMonitoringItems() const
{
    return d_func()->stopItems;
}

void ModifySubscriptionCommand::startMonitoringTag(qint64 id)
{
    d_func()->stopTags.removeAll(id);
    d_func()->startTags << id;
    d_func()->modifiedParts |= ModifiedParts(Tags | Add);
}

QVector<qint64> ModifySubscriptionCommand::startMonitoringTags() const
{
    return d_func()->startTags;
}

void ModifySubscriptionCommand::stopMonitoringTag(qint64 id)
{
    d_func()->startTags.removeAll(id);
    d_func()->stopTags << id;
    d_func()->modifiedParts |= ModifiedParts(Tags | Remove);
}

QVector<qint64> ModifySubscriptionCommand::stopMonitoringTags() const
{
    return d_func()->stopTags;
}

void ModifySubscriptionCommand::startMonitoringType(ChangeType type)
{
    d_func()->stopTypes.removeAll(type);
    d_func()->startTypes << type;
    d_func()->modifiedParts |= ModifiedParts(Types | Add);
}

QVector<ModifySubscriptionCommand::ChangeType> ModifySubscriptionCommand::startMonitoringTypes() const
{
    return d_func()->startTypes;
}

void ModifySubscriptionCommand::stopMonitoringType(ChangeType type)
{
    d_func()->startTypes.removeAll(type);
    d_func()->stopTypes << type;
    d_func()->modifiedParts |= ModifiedParts(Types | Remove);
}

QVector<ModifySubscriptionCommand::ChangeType> ModifySubscriptionCommand::stopMonitoringTypes() const
{
    return d_func()->stopTypes;
}

void ModifySubscriptionCommand::startMonitoringResource(const QByteArray &resource)
{
    d_func()->stopResources.removeAll(resource);
    d_func()->startResources << resource;
    d_func()->modifiedParts |= ModifiedParts(Resources | Add);
}

QVector<QByteArray> ModifySubscriptionCommand::startMonitoringResources() const
{
    return d_func()->startResources;
}

void ModifySubscriptionCommand::stopMonitoringResource(const QByteArray &resource)
{
    d_func()->startResources.removeAll(resource);
    d_func()->stopResources << resource;
    d_func()->modifiedParts |= ModifiedParts(Resources | Remove);
}

QVector<QByteArray> ModifySubscriptionCommand::stopMonitoringResources() const
{
    return d_func()->stopResources;
}

void ModifySubscriptionCommand::startMonitoringMimeType(const QString &mimeType)
{
    d_func()->stopMimeTypes.removeAll(mimeType);
    d_func()->startMimeTypes << mimeType;
    d_func()->modifiedParts |= ModifiedParts(MimeTypes | Add);
}

QStringList ModifySubscriptionCommand::startMonitoringMimeTypes() const
{
    return d_func()->startMimeTypes;
}

void ModifySubscriptionCommand::stopMonitoringMimeType(const QString &mimeType)
{
    d_func()->startMimeTypes.removeAll(mimeType);
    d_func()->stopMimeTypes << mimeType;
    d_func()->modifiedParts |= ModifiedParts(MimeTypes | Remove);
}

QStringList ModifySubscriptionCommand::stopMonitoringMimeTypes() const
{
    return d_func()->stopMimeTypes;
}

void ModifySubscriptionCommand::setAllMonitored(bool allMonitored)
{
    d_func()->monitorAll = allMonitored;
    d_func()->modifiedParts |= AllFlag;
}

bool ModifySubscriptionCommand::allMonitored() const
{
    return d_func()->monitorAll;
}

void ModifySubscriptionCommand::setExclusive(bool exclusive)
{
    d_func()->exclusive = exclusive;
    d_func()->modifiedParts |= ExclusiveFlag;
}

bool ModifySubscriptionCommand::isExclusive() const
{
    return d_func()->exclusive;
}

void ModifySubscriptionCommand::startIgnoringSession(const QByteArray &session)
{
    d_func()->stopSessions.removeAll(session);
    d_func()->startSessions << session;
    d_func()->modifiedParts |= ModifiedParts(Sessions | Add);
}

QVector<QByteArray> ModifySubscriptionCommand::startIgnoringSessions() const
{
    return d_func()->startSessions;
}

void ModifySubscriptionCommand::stopIgnoringSession(const QByteArray &session)
{
    d_func()->startSessions.removeAll(session);
    d_func()->stopSessions << session;
    d_func()->modifiedParts |= ModifiedParts(Sessions | Remove);
}

QVector<QByteArray> ModifySubscriptionCommand::stopIgnoringSessions() const
{
    return d_func()->stopSessions;
}

DataStream &operator<<(DataStream &stream, const ModifySubscriptionCommand &command)
{
    return command.d_ptr->serialize(stream);
}

DataStream &operator>>(DataStream &stream, ModifySubscriptionCommand &command)
{
    return command.d_ptr->deserialize(stream);
}

ModifySubscriptionResponse::ModifySubscriptionResponse()
    : Response(new ResponsePrivate(Command::ModifySubscription))
{
}

ModifySubscriptionResponse::ModifySubscriptionResponse(const Command &other)
    : Response(other)
{
    checkCopyInvariant(Command::ModifySubscription);
}

class SubscriptionChangeNotificationPrivate : public ChangeNotificationPrivate
{
public:
    SubscriptionChangeNotificationPrivate()
        : ChangeNotificationPrivate(Command::SubscriptionChangeNotification)
        , operation(SubscriptionChangeNotification::InvalidOp)
        , isAllMonitored(false)
        , isExclusive(false)
    {}

    SubscriptionChangeNotificationPrivate(const SubscriptionChangeNotificationPrivate &other)
        : ChangeNotificationPrivate(other)
        , subscriber(other.subscriber)
        , collections(other.collections)
        , items(other.items)
        , tags(other.tags)
        , types(other.types)
        , mimeTypes(other.mimeTypes)
        , resources(other.resources)
        , ignoredSessions(other.ignoredSessions)
        , operation(other.operation)
        , isAllMonitored(other.isAllMonitored)
        , isExclusive(other.isExclusive)
    {}

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::compare(other)
               && COMPARE(operation)
               && COMPARE(subscriber)
               && COMPARE(collections)
               && COMPARE(items)
               && COMPARE(tags)
               && COMPARE(types)
               && COMPARE(mimeTypes)
               && COMPARE(resources)
               && COMPARE(ignoredSessions)
               && COMPARE(isAllMonitored)
               && COMPARE(isExclusive);
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        ChangeNotificationPrivate::debugString(blck);
        blck.write("Subscriber", subscriber);
        blck.write("Operation", operation);
        blck.write("Collections", collections);
        blck.write("Items", items);
        blck.write("Tags", tags);
        blck.write("Types", types);
        blck.write("Mime types", mimeTypes);
        blck.write("Resources", resources);
        blck.write("Ignored sessions", ignoredSessions);
        blck.write("All monitored", isAllMonitored);
        blck.write("Is exclusive", isExclusive);
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        ChangeNotificationPrivate::serialize(stream)
                << subscriber
                << operation
                << collections
                << items
                << tags
                << types
                << mimeTypes
                << resources
                << ignoredSessions
                << isAllMonitored
                << isExclusive;
        return stream;
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        ChangeNotificationPrivate::deserialize(stream)
                >> subscriber
        >> operation
        >> collections
        >> items
        >> tags
        >> types
        >> mimeTypes
        >> resources
        >> ignoredSessions
        >> isAllMonitored
        >> isExclusive;
        return stream;
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new SubscriptionChangeNotificationPrivate(*this);
    }

    QByteArray subscriber;
    QSet<ChangeNotification::Id> collections;
    QSet<ChangeNotification::Id> items;
    QSet<ChangeNotification::Id> tags;
    QSet<ModifySubscriptionCommand::ChangeType> types;
    QSet<QString> mimeTypes;
    QSet<QByteArray> resources;
    QSet<QByteArray> ignoredSessions;
    SubscriptionChangeNotification::Operation operation;
    bool isAllMonitored;
    bool isExclusive;
};

AKONADI_DECLARE_PRIVATE(SubscriptionChangeNotification)

SubscriptionChangeNotification::SubscriptionChangeNotification()
    : ChangeNotification(new SubscriptionChangeNotificationPrivate())
{
}

SubscriptionChangeNotification::SubscriptionChangeNotification(const Command &other)
    : ChangeNotification(other)
{
    checkCopyInvariant(Command::SubscriptionChangeNotification);
}

SubscriptionChangeNotification::Operation SubscriptionChangeNotification::operation() const
{
    return d_func()->operation;
}

void SubscriptionChangeNotification::setOperation(Operation operation)
{
    d_func()->operation = operation;
}

QByteArray SubscriptionChangeNotification::subscriber() const
{
    return d_func()->subscriber;
}

void SubscriptionChangeNotification::setSubscriber(const QByteArray &subscriber)
{
    d_func()->subscriber = subscriber;
}

QSet<ChangeNotification::Id> SubscriptionChangeNotification::collections() const
{
    return d_func()->collections;
}

void SubscriptionChangeNotification::setCollections(const QSet<Id> &cols)
{
    d_func()->collections = cols;
}

QSet<ChangeNotification::Id> SubscriptionChangeNotification::items() const
{
    return d_func()->items;
}

void SubscriptionChangeNotification::setItems(const QSet<Id> &items)
{
    d_func()->items = items;
}

QSet<ChangeNotification::Id> SubscriptionChangeNotification::tags() const
{
    return d_func()->tags;
}

void SubscriptionChangeNotification::setTags(const QSet<Id> &tags)
{
    d_func()->tags = tags;
}

QSet<ModifySubscriptionCommand::ChangeType> SubscriptionChangeNotification::types() const
{
    return d_func()->types;
}

void SubscriptionChangeNotification::setTypes(const QSet<ModifySubscriptionCommand::ChangeType> &types)
{
    d_func()->types = types;
}

QSet<QString> SubscriptionChangeNotification::mimeTypes() const
{
    return d_func()->mimeTypes;
}

void SubscriptionChangeNotification::setMimeTypes(const QSet<QString> &mimetypes)
{
    d_func()->mimeTypes = mimetypes;
}

QSet<QByteArray> SubscriptionChangeNotification::resources() const
{
    return d_func()->resources;
}

void SubscriptionChangeNotification::setResources(const QSet<QByteArray> &resources)
{
    d_func()->resources = resources;
}

QSet<QByteArray> SubscriptionChangeNotification::ignoredSessions() const
{
    return d_func()->ignoredSessions;
}

void SubscriptionChangeNotification::setIgnoredSessions(const QSet<QByteArray> &sessions)
{
    d_func()->ignoredSessions = sessions;
}

bool SubscriptionChangeNotification::isAllMonitored() const
{
    return d_func()->isAllMonitored;
}

void SubscriptionChangeNotification::setAllMonitored(bool allMonitored)
{
    d_func()->isAllMonitored = allMonitored;
}

bool SubscriptionChangeNotification::isExclusive() const
{
    return d_func()->isExclusive;
}

void SubscriptionChangeNotification::setExclusive(bool isExclusive)
{
    d_func()->isExclusive = isExclusive;
}

DataStream &operator<<(DataStream &stream, const SubscriptionChangeNotification &ntf)
{
    return ntf.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, SubscriptionChangeNotification &ntf)
{
    return ntf.d_func()->deserialize(stream);
}

class DebugChangeNotificationPrivate : public ChangeNotificationPrivate
{
public:
    explicit DebugChangeNotificationPrivate()
        : ChangeNotificationPrivate(Command::DebugChangeNotification)
        , timestamp(0)
    {
    }

    DebugChangeNotificationPrivate(const DebugChangeNotificationPrivate &other)
        : ChangeNotificationPrivate(other)
        , notification(other.notification)
        , listeners(other.listeners)
        , timestamp(other.timestamp)
    {
    }

    bool compare(const CommandPrivate *other) const Q_DECL_OVERRIDE
    {
        return ChangeNotificationPrivate::compare(other)
               || COMPARE(notification)
               || COMPARE(listeners)
               || COMPARE(timestamp);
    }

    void debugString(DebugBlock &blck) const Q_DECL_OVERRIDE
    {
        blck.beginBlock("Notification");
        notification.debugString(blck);
        blck.endBlock();
        blck.write("Listeners", listeners);
        blck.write("Timestamp", timestamp);
    }

    DataStream &deserialize(DataStream &stream) Q_DECL_OVERRIDE {
        Command::Type type;
        ChangeNotificationPrivate::deserialize(stream)
                >> type;
        auto ntf = Factory::command(type);
        stream >> ntf
        >> listeners
        >> timestamp;
        notification = ntf;
        return stream;
    }

    DataStream &serialize(DataStream &stream) const Q_DECL_OVERRIDE
    {
        ChangeNotificationPrivate::serialize(stream)
                << notification.type()
                << notification
                << listeners
                << timestamp;
        return stream;
    }

    CommandPrivate *clone() const Q_DECL_OVERRIDE
    {
        return new DebugChangeNotificationPrivate(*this);
    }

    ChangeNotification notification;
    QVector<QByteArray> listeners;
    qint64 timestamp;
};

AKONADI_DECLARE_PRIVATE(DebugChangeNotification)

DebugChangeNotification::DebugChangeNotification()
    : ChangeNotification(new DebugChangeNotificationPrivate)
{
}

DebugChangeNotification::DebugChangeNotification(const Command &other)
    : ChangeNotification(other)
{
    checkCopyInvariant(Command::DebugChangeNotification);
}

ChangeNotification DebugChangeNotification::notification() const
{
    return d_func()->notification;
}

void DebugChangeNotification::setNotification(const ChangeNotification &notification)
{
    d_func()->notification = notification;
}

QVector<QByteArray> DebugChangeNotification::listeners() const
{
    return d_func()->listeners;
}

void DebugChangeNotification::setListeners(const QVector<QByteArray> &listeners)
{
    d_func()->listeners = listeners;
}

qint64 DebugChangeNotification::timestamp() const
{
    return d_func()->timestamp;
}

void DebugChangeNotification::setTimestamp(qint64 timestamp)
{
    d_func()->timestamp = timestamp;
}

DataStream &operator<<(DataStream &stream, const DebugChangeNotification &ntf)
{
    return ntf.d_func()->serialize(stream);
}

DataStream &operator>>(DataStream &stream, DebugChangeNotification &ntf)
{
    return ntf.d_func()->deserialize(stream);
}

} // namespace Protocol
} // namespace Akonadi
