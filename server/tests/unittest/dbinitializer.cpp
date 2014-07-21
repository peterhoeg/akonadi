/*
    Copyright (c) 2014 Christian Mollekopf <mollekopf@kolabsys.com>

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
#include "dbinitializer.h"

#include "akdebug.h"

using namespace Akonadi;
using namespace Akonadi::Server;

DbInitializer::~DbInitializer()
{
    cleanup();
}

Resource DbInitializer::createResource(const char *name)
{
    Resource res;
    qint64 id = -1;
    res.setName(QLatin1String(name));
    bool ret = res.insert(&id);
    Q_ASSERT(ret);
    mResource = res;
    return res;
}

Collection DbInitializer::createCollection(const char *name, const Collection &parent)
{
    Collection col;
    if (parent.isValid()) {
        col.setParent(parent);
    }
    col.setName(QLatin1String(name));
    col.setRemoteId(QLatin1String(name));
    col.setResource(mResource);
    Q_ASSERT(col.insert());
    return col;
}

QByteArray DbInitializer::toByteArray(bool enabled)
{
    if (enabled) {
        return "TRUE";
    }
    return "FALSE";
}

QByteArray DbInitializer::toByteArray(Akonadi::Server::Tristate tristate)
{

    switch (tristate) {
        case Akonadi::Server::Tristate::True:
            return "TRUE";
        case Akonadi::Server::Tristate::False:
            return "FALSE";
        case Akonadi::Server::Tristate::Undefined:
        default:
            break;
    }
    return "DEFAULT";
}

QByteArray DbInitializer::listResponse(const Collection &col)
{
    QByteArray s;
    s = "S: * " + QByteArray::number(col.id()) + " " + QByteArray::number(col.parentId()) + " (NAME \"" + col.name().toLatin1() +
        "\" MIMETYPE () REMOTEID \"" + col.remoteId().toLatin1() +
        "\" REMOTEREVISION \"\" RESOURCE \"" + col.resource().name().toLatin1() +
        "\" VIRTUAL ";
    if (col.isVirtual()) {
        s += "1";
    } else {
        s += "0";
    }
    s += " CACHEPOLICY (INHERIT true INTERVAL -1 CACHETIMEOUT -1 SYNCONDEMAND false LOCALPARTS (ALL))";
    if (col.referenced()) {
        s += " REFERENCED TRUE";
    }
    s += " ENABLED " + toByteArray(col.enabled()) + " DISPLAY " + toByteArray(col.displayPref()) + " SYNC " + toByteArray(col.syncPref()) + " INDEX " + toByteArray(col.indexPref()) +" )";
    return s;
}

Collection DbInitializer::collection(const char *name)
{
    return Collection::retrieveByName(QLatin1String(name));
}

void DbInitializer::cleanup()
{
    Q_FOREACH(Collection col, mResource.collections()) {
        if (!col.isVirtual()) {
            col.remove();
        }
    }
    mResource.remove();
}

