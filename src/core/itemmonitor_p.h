/*
    Copyright (c) 2007-2008 Tobias Koenig <tokoe@kde.org>

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

#ifndef AKONADI_ITEMMONITOR_P_H
#define AKONADI_ITEMMONITOR_P_H

#include <QObject>

#include "itemfetchjob.h"
#include "monitor.h"

namespace Akonadi
{

/**
 * @internal
 */
class Q_DECL_HIDDEN ItemMonitor::Private : public QObject
{
    Q_OBJECT

public:
    Private(ItemMonitor *parent)
        : QObject(nullptr)
        , mParent(parent)
        , mMonitor(new Monitor())
    {
        mMonitor->setObjectName(QStringLiteral("ItemMonitorMonitor"));
        connect(mMonitor, &Monitor::itemChanged,
                this, &Private::slotItemChanged);
        connect(mMonitor, &Monitor::itemRemoved,
                this, &Private::slotItemRemoved);
    }

    ~Private()
    {
        delete mMonitor;
    }

    ItemMonitor *mParent = nullptr;
    Item mItem;
    Monitor *mMonitor = nullptr;

private Q_SLOTS:
    void slotItemChanged(const Akonadi::Item &item, const QSet<QByteArray> &aSet)
    {
        Q_UNUSED(aSet);
        mItem.apply(item);
        mParent->itemChanged(item);
    }

    void slotItemRemoved(const Akonadi::Item &item)
    {
        Q_UNUSED(item);
        mItem = Item();
        mParent->itemRemoved();
    }
public Q_SLOTS:
    void initialFetchDone(KJob *job)
    {
        if (job->error()) {
            return;
        }

        ItemFetchJob *fetchJob = qobject_cast<ItemFetchJob *>(job);

        if (!fetchJob->items().isEmpty()) {
            mItem = fetchJob->items().at(0);
            mParent->itemChanged(mItem);
        }
    }
};

}

#endif
