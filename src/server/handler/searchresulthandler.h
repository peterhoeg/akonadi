/*
 * Copyright (C) 2013  Daniel Vrátil <dvratil@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef AKONADI_SEARCHRESULTHANDLER_H_
#define AKONADI_SEARCHRESULTHANDLER_H_

#include "handler.h"

namespace Akonadi
{
namespace Server
{

/**
  @ingroup akonadi_server_handler

  Handler for the search_result command
*/
class SearchResultHandler: public Handler
{
public:
    ~SearchResultHandler() override = default;

    bool parseStream() override;

private:
    bool fail(const QByteArray &searchId, const QString &error);
};

} // namespace Server
} // namespace Akonadi

#endif // AKONADI_SEARCHRESULTHANDLER_H_
