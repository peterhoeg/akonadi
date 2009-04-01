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
#ifndef AKONADIHANDLER_H
#define AKONADIHANDLER_H

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QStringList>

#include "akonadiprivate_export.h"
#include "global.h"
#include "exception.h"

namespace Akonadi {

class Response;
class AkonadiConnection;
class QueryBuilder;
class ImapSet;
class ImapStreamParser;

AKONADI_EXCEPTION_MAKE_INSTANCE( HandlerException );

/**
  \defgroup akonadi_server_handler Command handlers

  All commands supported by the Akonadi server are implemented as sub-classes of Akonadi::Handler.
*/

/**
The handler interfaces describes an entity capable of handling an AkonadiIMAP command.*/
class AKONADIPRIVATE_EXPORT Handler : public QObject {
    Q_OBJECT
public:
    Handler();

    virtual ~Handler();

    /**
     * Set the tag of the command to be processed, and thus of the response
     * generated by this handler.
     * @param tag The command tag, an alphanumerical string, normally.
     */
    void setTag( const QByteArray& tag );

    /**
     * The tag of the command associated with this handler.
     */
    QByteArray tag() const;

    /**
     * Find a handler for a command that is always allowed, like LOGOUT.
     * @param line the command string
     * @return an instance to the handler. The handler is deleted after @see handelLine is executed. The caller needs to delete the handler in case an exception is thrown from handelLine.
     */
    static Handler* findHandlerForCommandAlwaysAllowed( const QByteArray& line );

    /**
     * Find a handler for a command that is allowed when the client is not yet authenticated, like LOGIN.
     * @param line the command string
     * @return an instance to the handler. The handler is deleted after @see handelLine is executed. The caller needs to delete the handler in case an exception is thrown from handelLine.
     */
    static Handler* findHandlerForCommandNonAuthenticated( const QByteArray& line );

    /**
     * Find a handler for a command that is allowed when the client is authenticated, like LIST, FETCH, etc.
     * @param line the command string
     * @return an instance to the handler. The handler is deleted after @see handelLine is executed. The caller needs to delete the handler in case an exception is thrown from handelLine.
     */
    static Handler* findHandlerForCommandAuthenticated( const QByteArray& line );

    void setConnection( AkonadiConnection* connection );
    AkonadiConnection* connection() const;

    /** Send a failure response with the given message. */
    bool failureResponse( const QString& failureMessage );
    /**
      Convenience method to compile with QT_NO_CAST_FROM_ASCII without
      typing too much ;-)
    */
    bool failureResponse( const QByteArray &failureMessage );
    /**
      Convenience method to compile with QT_NO_CAST_FROM_ASCII without
      typing too much ;-)
     */
    bool failureResponse( const char *failureMessage );

    /** Send a success response with the given message. */
    bool successResponse( const char *successMessage );

    /**
     * Assigns the streaming IMAP parser to the handler. Useful only if supportsStreamParser() returns true.
     * @param parser the imap parser object
     */
    void setStreamParser( ImapStreamParser *parser );

    /**
     * Parse and handle the IMAP message using the streaming parser. The implementation MUST leave the trailing newline character(s) in the stream!
     * @return true if parsed successfully, false in case of parse failure
     */
    virtual bool parseStream();

Q_SIGNALS:

    /**
     * Emitted whenever the handler has a response ready for output. There can
     * be several responses per command.
     * @param response The response to be sent to the client.
     */
    void responseAvailable( const Response& response );

    /**
     * Emitted whenever a handler wants the connection to change into a
     * different state. The connection usually honors such requests, but
     * the decision is up to it.
     * @param state The new state the handler suggests to enter.
     */
    void connectionStateChange( ConnectionState state );

private:
    QByteArray m_tag;
    AkonadiConnection* m_connection;

protected:
    ImapStreamParser *m_streamParser;
};

}

#endif
