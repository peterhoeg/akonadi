/*
    Copyright (c) 2015  Daniel Vrátil <dvratil@redhat.com>

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

#ifndef AKONADI_TAGQUERYHELPER_H
#define AKONADI_TAGQUERYHELPER_H

#include "entities.h"

namespace Akonadi
{

class Scope;

namespace Server
{

class CommandContext;
class QueryBuilder;

/**
  Helper methods to generate WHERE clauses for item queries based on the item set
  used in the protocol.
*/
namespace TagQueryHelper
{

/**
  Add conditions to @p qb for the given remote identifier @p rid.
  The rid context is taken from @p context.
*/
void remoteIdToQuery(const QStringList &rids, CommandContext *context, QueryBuilder &qb);
void gidToQuery(const QStringList &gids, CommandContext *context, QueryBuilder &qb);

/**
  Add conditions to @p qb for the given item operation scope @p scope.
  The rid context is taken from @p context, if none is specified an exception is thrown.
*/
void scopeToQuery(const Scope &scope, CommandContext *context, QueryBuilder &qb);
}

} // namespace Server
} // namespace Akonadi

#endif
