/*
    Copyright (c) 2013 Daniel Vrátil <dvratil@redhat.com>

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

#include "idle.h"
#include "idleclient.h"
#include "idlemanager.h"
#include "fetchhelper.h"
#include "imapstreamparser.h"

#include <libs/protocol_p.h>


using namespace Akonadi;

Idle::Idle()
  : Handler()
{
}

bool Idle::parseStream()
{
  const QByteArray subcmd = m_streamParser->readString();
  if ( subcmd == AKONADI_PARAM_START ) {
    startIdle();
  } else {
    // All subsequent subcommands require active IDLE session
    IdleClient *client = IdleManager::self()->clientForConnection( connection() );
    if ( !client ) {
      throw new HandlerException( "IDLE not active" );
    }

    if ( subcmd == AKONADI_PARAM_FILTER ) {
      updateFilter();
    } else if ( subcmd == AKONADI_PARAM_FREEZE ) {
      if ( client->isFrozen() ) {
        throw new HandlerException( "Already frozen" );
      }
      client->freeze();
    } else if ( subcmd == AKONADI_PARAM_THAW ) {
      if ( !client->isFrozen() ) {
        throw new HandlerException( "Not frozen" );
      }
      client->thaw();
    } else if ( subcmd == AKONADI_PARAM_DONE ) {
      client->done();
    } else if ( subcmd == AKONADI_PARAM_REPLAYED ) {
      const ImapSet set = m_streamParser->readSequenceSet();
      if ( set.isEmpty() ) {
        throw new HandlerException( "Invalid notification set" );
      }
      if ( !client->replayed( set ) ) {
        throw new HandlerException( "No such notification" );
      }
    } else if ( subcmd == AKONADI_PARAM_RECORD ) {
      const ImapSet set = m_streamParser->readSequenceSet();
      if ( set.isEmpty() ) {
        throw new HandlerException( "Invalid notification set" );
      }
      if ( !client->record( set ) ) {
        throw new HandlerException( "Failed to record" );
      }
    } else {
      throw new HandlerException( "Invalid IDLE subcommand" );
    }
  }

  return true;
}

void Idle::startIdle()
{
  QByteArray clientId;
  bool recordChanges = true;

  while ( m_streamParser->hasString() ) {
    const QByteArray arg = m_streamParser->readString();
    if ( arg == AKONADI_PARAM_CLIENTID ) {
      clientId = m_streamParser->readString();
    } else if ( arg == AKONADI_PARAM_RECORDCHANGES ) {
      bool ok = false;
      recordChanges = m_streamParser->readNumber( &ok );
      if ( !ok ) {
        throw new HandlerException( "Invalid value of RECORDCHANGES parameter" );
      }
    }
  }

  if ( clientId.isEmpty() ) {
    throw new HandlerException( "Empty client ID" );
  }

  IdleClient *client = new IdleClient( connection(), clientId );
  client->setRecordChanges( recordChanges );

  if ( !m_streamParser->hasList() ) {
    throw new HandlerException( "Invalid filter" );
  }
  parseFilter( client );

  if ( !m_streamParser->hasList() ) {
    throw new HandlerException( "Invalid scope" );
  }
  parseFetchScope( client );

  IdleManager::self()->registerClient( client );
}

void Idle::updateFilter()
{
  IdleClient *client = IdleManager::self()->clientForConnection( connection() );
  if ( !client ) {
    throw new HandlerException( "No such client");
  }

  if ( m_streamParser->hasList() ) {
    parseFilter( client );
  }

  if ( m_streamParser->hasList() ) {
    parseFetchScope( client );
  }
}

void Idle::parseFilter( IdleClient *client )
{
  m_streamParser->beginList();
  while ( m_streamParser->atListEnd() ) {
    const QByteArray arg = m_streamParser->readString();
    if ( arg == AKONADI_PARAM_ITEMS ) {
      client->setMonitoredItems( parseIdList() );
    } else if ( arg == "+" AKONADI_PARAM_ITEMS ) {
      client->addMonitoredItems( parseIdList() );
    } else if ( arg == "-" AKONADI_PARAM_ITEMS ) {
      client->removeMonitoredItems( parseIdList() );
    } else if ( arg == AKONADI_PARAM_COLLECTIONS ) {
      client->setMonitoredCollections( parseIdList() );
    } else if ( arg == "+" AKONADI_PARAM_COLLECTIONS ) {
      client->addMonitoredCollections( parseIdList() );
    } else if ( arg == "-" AKONADI_PARAM_COLLECTIONS ) {
      client->removeMonitoredCollections( parseIdList() );
    } else if ( arg == AKONADI_PARAM_MIMETYPES ) {
      client->setMonitoredMimeTypes( parseStringList() );
    } else if ( arg == "+" AKONADI_PARAM_MIMETYPES ) {
      client->addMonitoredMimeTypes( parseStringList() );
    } else if ( arg == "-" AKONADI_PARAM_MIMETYPES ) {
      client->removeMonitoredMimeTypes( parseStringList() );
    } else if ( arg == AKONADI_PARAM_RESOURCES ) {
      client->setMonitoredResources( parseStringList() );
    } else if ( arg == "+" AKONADI_PARAM_RESOURCES ) {
      client->addMonitoredResources( parseStringList() );
    } else if ( arg == "-" AKONADI_PARAM_RESOURCES ) {
      client->removeMonitoredResources( parseStringList() );
    } else if ( arg == AKONADI_PARAM_IGNOREDSESSIONS ) {
      client->setIgnoredSessions( parseStringList() );
    } else if ( arg == "+" AKONADI_PARAM_IGNOREDSESSIONS ) {
      client->addIgnoredSessions( parseStringList() );
    } else if ( arg == "-" AKONADI_PARAM_IGNOREDSESSIONS ) {
      client->removeIgnoredSessions( parseStringList() );
    } else if ( arg == AKONADI_PARAM_OPERATIONS ) {
      client->setMonitoredOperations( parseOperationsList() );
    } else if ( arg == "+" AKONADI_PARAM_OPERATIONS ) {
      client->addMonitoredOperations ( parseOperationsList() );
    } else if ( arg == "-" AKONADI_PARAM_OPERATIONS ) {
      client->removeMonitoredOperations( parseOperationsList() );
    } else {
      throw new HandlerException( "Invalid filter" );
    }
  }
}

void Idle::parseFetchScope( IdleClient *client )
{
  m_streamParser->beginList();
  client->setFetchScope( FetchScope( m_streamParser ) );
}


QSet<qint64> Idle::parseIdList()
{
  if ( !m_streamParser->hasList() ) {
    throw new HandlerException( "Invalid filter" );
  }

  QSet<qint64> ids;
  m_streamParser->beginList();
  while ( !m_streamParser->atListEnd() ) {
    bool ok = false;
    ids.insert( m_streamParser->readNumber( &ok ) );
    if ( !ok ) {
      throw new HandlerException( "Invalid filter" );
    }
  }

  return ids;
}

QSet<QByteArray> Idle::parseStringList()
{
  if ( !m_streamParser->hasList() ) {
    throw new HandlerException( "Invalid filter" );
  }

  return m_streamParser->readParenthesizedList().toSet();
}


QSet<QByteArray> Idle::parseOperationsList()
{
  if ( !m_streamParser->hasList() ) {
    throw new HandlerException( "Invalid filter" );
  }

  QSet<QByteArray> operations;
  m_streamParser->beginList();
  while ( !m_streamParser->atListEnd() ) {
    const QByteArray operation = m_streamParser->readString();
    if ( operation == AKONADI_OPERATION_ADD || operation == AKONADI_OPERATION_LINK ||
         operation == AKONADI_OPERATION_MODIFY || operation == AKONADI_OPERATION_MODIFYFLAGS ||
         operation == AKONADI_OPERATION_MOVE || operation == AKONADI_OPERATION_REMOVE ||
         operation == AKONADI_OPERATION_SUBSCRIBE || operation == AKONADI_OPERATION_UNLINK ||
         operation == AKONADI_OPERATION_UNSUBSCRIBE ) {
      operations << operation;
    } else {
      throw new HandlerException( "Invalid filter" );
    }
  }

  return operations;
}
