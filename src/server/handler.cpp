/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.             *
 ***************************************************************************/
#include "handler.h"

#include <QtCore/QDebug>
#include <QtCore/QLatin1String>

#include <private/imapset_p.h>
#include <private/protocol_p.h>

#include "connection.h"
#include "response.h"
#include "handler/akappend.h"
#include "handler/copy.h"
#include "handler/colcopy.h"
#include "handler/colmove.h"
#include "handler/create.h"
#include "handler/delete.h"
#include "handler/fetch.h"
#include "handler/link.h"
#include "handler/list.h"
#include "handler/login.h"
#include "handler/logout.h"
#include "handler/modify.h"
#include "handler/move.h"
#include "handler/remove.h"
#include "handler/resourceselect.h"
#include "handler/search.h"
#include "handler/searchpersistent.h"
#include "handler/searchresult.h"
#include "handler/select.h"
#include "handler/status.h"
#include "handler/store.h"
#include "handler/transaction.h"
#include "handler/tagappend.h"
#include "handler/tagfetch.h"
#include "handler/tagremove.h"
#include "handler/tagstore.h"
#include "handler/relationstore.h"
#include "handler/relationremove.h"
#include "handler/relationfetch.h"

#include "storage/querybuilder.h"
#include "imapstreamparser.h"

using namespace Akonadi;
using namespace Akonadi::Server;

Handler::Handler()
    : QObject()
    , m_connection(0)
{
}

Handler::~Handler()
{
}

Handler *Handler::findHandlerForCommandNonAuthenticated(Protocol::Command::Type cmd)
{
    // allowed are LOGIN
    if (cmd == Protocol::Command::Login) {
        return new Login();
    }

    return Q_NULLPTR;
}

Handler *Handler::findHandlerForCommandAlwaysAllowed(Protocol::Command::Type cmd)
{
    // allowed is LOGOUT
    if (cmd == Protocol::Command::Logout) {
        return new Logout();
    }
    return Q_NULLPTR;
}

void Handler::setTag(quint64 tag)
{
    m_tag = tag;
}

quint64 Handler::tag() const
{
    return m_tag;
}

void Handler::setCommand(Protocol::Command::Type cmd)
{
    m_command = cmd;
}

Protocol::Command::Type Handler::command() const
{
    return m_command;
}


Handler *Handler::findHandlerForCommandAuthenticated(Protocol::Command::Type cmd)
{
    switch (cmd)
    {
    case Protocol::Command::Invalid:
        Q_ASSERT_X(cmd != Protocol::Command::Invalid,
                   "Handler::findHandlerForCommandAuthenticated()",
                   "Invalid command is not allowed");
        return Q_NULLPTR;
    case Protocol::Command::Hello:
        Q_ASSERT_X(cmd != Protocol::Command::Hello,
                    "Handler::findHandlerForCommandAuthenticated()",
                    "Hello command is not allowed in this context");
        return Q_NULLPTR;
    case Protocol::Command::Login:
        Q_ASSERT_X(cmd != Protocol::Command::StreamPayload,
                    "Handler::findHandlerForCommandAuthenticated()",
                    "Login command is not allowed in this context");
        return Q_NULLPTR;
    case Protocol::Command::Logout:
        Q_ASSERT_X(cmd != Protocol::Command::StreamPayload,
            "Handler::findHandlerForCommandAuthenticated()",
            "Logout command is not allowed in this context");
        return Q_NULLPTR;

    case Protocol::Command::Transaction:
        return new TransactionHandler();

    case Protocol::Command::CreateItem:
        return new AkAppend();
    case Protocol::Command::CopyItems:
        return new Copy();
    case Protocol::Command::DeleteItems:
        return new Remove();
    case Protocol::Command::FetchItems:
        return new Fetch();
    case Protocol::Command::LinkItems:
        return new Link();
    case Protocol::Command::ModifyItems:
        return new Store();
    case Protocol::Command::MoveItems:
        return new Move();

    case Protocol::Command::CreateCollection:
        return new Create();
    case Protocol::Command::CopyCollection:
        return new ColCopy();
    case Protocol::Command::DeleteCollection:
        return new Delete();
    case Protocol::Command::FetchCollections:
        return new List();
    case Protocol::Command::FetchCollectionStats:
        return new Status();
    case Protocol::Command::ModifyCollection:
        return new Modify();
    case Protocol::Command::MoveCollection:
        return new ColMove();
    case Protocol::Command::SelectCollection:
        return new Select();

    case Protocol::Command::Search:
        return new Search();
    case Protocol::Command::SearchResult:
        return new SearchResult();
    case Protocol::Command::StoreSearch:
        return new SearchPersistent();

    case Protocol::Command::CreateTag:
        return new TagAppend();
    case Protocol::Command::DeleteTag:
        return new TagRemove();
    case Protocol::Command::FetchTags:
        return new TagFetch();
    case Protocol::Command::ModifyTag:
        return new TagStore();

    case Protocol::Command::FetchRelations:
        return new RelationFetch();
    case Protocol::Command::ModifyRelation:
        return new RelationStore();
    case Protocol::Command::RemoveRelations:
        return new RelationRemove();

    case Protocol::Command::SelectResource:
        return new ResourceSelect();

    case Protocol::Command::StreamPayload:
        Q_ASSERT_X(cmd != Protocol::Command::StreamPayload,
                    "Handler::findHandlerForCommandAuthenticated()",
                    "StreamPayload command is not allowed in this context");
        return Q_NULLPTR;
    }

    return Q_NULLPTR;
}

void Handler::setConnection(Connection *connection)
{
    m_connection = connection;
    mInStream.setDevice(m_connection->socket());
    mOutStream.setDevice(m_connection->socket());
}

Connection *Handler::connection() const
{
    return m_connection;
}

bool Handler::failureResponse(const QByteArray &failureMessage)
{
    return failureResponse(QString::fromUtf8(failureMessage));
}

bool Handler::failureResponse(const char *failureMessage)
{
    return failureResponse(QString::fromUtf8(failureMessage));
}

bool Handler::failureResponse(const QString &failureMessage)
{
    Protocol::Response r = Protocol::Factory::response(m_command);
    // FIXME: Error enums?
    r.setError(1, failureMessage);

    sendResponse<Protocol::Response>(r);

    return false;
}

bool Handler::checkScopeConstraints(const Akonadi::Scope &scope, int permittedScopes)
{
    return scope.scope() & permittedScopes;
}


void Handler::sendResponseImpl(const Protocol::Response *response)
{
    // FIXME: This is a dirty hack, really. Ideally we would want to be able
    // to serialize the entire response implementation including all of it's
    // superclasses. At the same time however we use certain responses as parts
    // of different responses, so we need to be able to serialize only the
    // implementations too.

    // Send tag first
    mOutStream << m_tag;
    // Serialize Command baseclass
    mOutStream << *static_cast<const Protocol::Command*>(response);
    // Serialize Response baseclass
    mOutStream << *response;

    // Payload of the actual implementation is serialized in sendResponse() template
}
