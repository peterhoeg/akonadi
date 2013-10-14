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

#ifndef AKONADI_IDLE_H
#define AKONADI_IDLE_H

#include "handler.h"
#include "scope.h"
#include <libs/idle_p.h>

namespace Akonadi {

class IdleClient;

class IdleHandler : public Handler
{
  Q_OBJECT
  public:
    IdleHandler();

    virtual bool parseStream();

  private:
    void startIdle();
    void updateFilter();

    void parseFilter( IdleClient *client );
    void parseFetchScope( IdleClient *client );
    QSet<qint64> parseIdList();
    QSet<Idle::IdleOperation> parseOperationsList();
    QSet<QByteArray> parseStringList();
};

}

#endif // AKONADI_IDLE_H
