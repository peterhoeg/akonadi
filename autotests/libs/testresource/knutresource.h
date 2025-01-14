/*
    Copyright (c) 2006 Tobias Koenig <tokoe@kde.org>
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

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

#ifndef KNUTRESOURCE_H
#define KNUTRESOURCE_H

#include <resourcebase.h>
#include <collection.h>
#include <item.h>

#include <xmldocument.h>
#include <agentsearchinterface.h>
#include <searchquery.h>

#include "settings.h"

class QFileSystemWatcher;

class KnutResource : public Akonadi::ResourceBase,
    public Akonadi::AgentBase::ObserverV2,
    public Akonadi::AgentSearchInterface
{
    Q_OBJECT

public:
    using Akonadi::AgentBase::ObserverV2::collectionChanged; // So we don't trigger -Woverloaded-virtual
    explicit KnutResource(const QString &id);
    ~KnutResource() override;

public Q_SLOTS:
    void configure(WId windowId) override;

protected:
    void retrieveCollections() override;
    void retrieveItems(const Akonadi::Collection &collection) override;
#ifdef DO_IT_THE_OLD_WAY
    bool retrieveItem(const Akonadi::Item &item, const QSet<QByteArray> &parts) override;
#endif
    bool retrieveItems(const Akonadi::Item::List &items, const QSet<QByteArray> &parts) override;

    void collectionAdded(const Akonadi::Collection &collection, const Akonadi::Collection &parent) override;
    void collectionChanged(const Akonadi::Collection &collection) override;
    void collectionRemoved(const Akonadi::Collection &collection) override;

    void itemAdded(const Akonadi::Item &item, const Akonadi::Collection &collection) override;
    void itemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts) override;
    void itemRemoved(const Akonadi::Item &ref) override;
    void itemMoved(const Akonadi::Item &item, const Akonadi::Collection &collectionSource, const Akonadi::Collection &collectionDestination) override;

    void search(const QString &query, const Akonadi::Collection &collection) override;
    void addSearch(const QString &query, const QString &queryLanguage, const Akonadi::Collection &resultCollection) override;
    void removeSearch(const Akonadi::Collection &resultCollection) override;

private:
    QDomElement findElementByRid(const QString &rid) const;

    static QSet<qint64> parseQuery(const QString &queryString);

private Q_SLOTS:
    void load();
    void save();

private:
    Akonadi::XmlDocument mDocument;
    QFileSystemWatcher *mWatcher = nullptr;
    KnutSettings *mSettings = nullptr;
};

#endif
