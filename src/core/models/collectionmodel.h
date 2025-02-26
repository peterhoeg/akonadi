/*
    Copyright (c) 2006 - 2008 Volker Krause <vkrause@kde.org>

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

#ifndef AKONADI_COLLECTIONMODEL_H
#define AKONADI_COLLECTIONMODEL_H

#include "akonadicore_export.h"
#include "collection.h"

#include <QAbstractItemModel>

namespace Akonadi
{

class CollectionModelPrivate;

/**
 * @short A model for collections.
 *
 * This class provides the interface of QAbstractItemModel for the
 * collection tree of the Akonadi storage.
 *
 * @code
 *
 *   Akonadi::CollectionModel *model = new Akonadi::CollectionModel( this );
 *
 *   QTreeView *view = new QTreeView( this );
 *   view->setModel( model );
 *
 * @endcode
 *
 * If you want to list only collections of a special mime type, use
 * CollectionFilterProxyModel on top of this model.
 *
 * @author Volker Krause <vkrause@kde.org>
 * @deprecated Use Akonadi::EntityTreeModel instead
 */
class AKONADICORE_DEPRECATED_EXPORT CollectionModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /**
     * Describes the roles for collections.
     */
    enum Roles {
        CollectionIdRole = Qt::UserRole + 10, ///< The collection identifier.
        CollectionRole = Qt::UserRole + 11,   ///< The actual collection object.
        UserRole = Qt::UserRole + 42          ///< Role for user extensions.
    };

    /**
     * Creates a new collection model.
     *
     * @param parent The parent object.
     */
    explicit CollectionModel(QObject *parent = nullptr);

    /**
     * Destroys the collection model.
     */
    ~CollectionModel() override;

    /**
     * Sets whether collection statistics information shall be provided
     * by the model.
     *
     * @see CollectionStatistics.
     * @param enable whether to fetch collection statistics
     */
    void fetchCollectionStatistics(bool enable);

    /**
     * Sets whether unsubscribed collections shall be listed in the model.
     */
    void includeUnsubscribed(bool include = true);

    Q_REQUIRED_RESULT int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_REQUIRED_RESULT QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Q_REQUIRED_RESULT QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    Q_REQUIRED_RESULT QModelIndex parent(const QModelIndex &index) const override;
    Q_REQUIRED_RESULT int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_REQUIRED_RESULT QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Q_REQUIRED_RESULT bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role = Qt::EditRole) override;
    Q_REQUIRED_RESULT bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Q_REQUIRED_RESULT Qt::ItemFlags flags(const QModelIndex &index) const override;
    Q_REQUIRED_RESULT Qt::DropActions supportedDropActions() const override;
    Q_REQUIRED_RESULT QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Q_REQUIRED_RESULT bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
    Q_REQUIRED_RESULT QStringList mimeTypes() const override;

protected:
    /**
     * Returns the collection for a given collection @p id.
     */
    Collection collectionForId(Collection::Id id) const;

    //@cond PRIVATE
    Akonadi::CollectionModelPrivate *d_ptr;
    explicit CollectionModel(CollectionModelPrivate *d, QObject *parent = nullptr);
    //@endcond

private:
    Q_DECLARE_PRIVATE(CollectionModel)

    //@cond PRIVATE
    Q_PRIVATE_SLOT(d_func(), void startFirstListJob())
    Q_PRIVATE_SLOT(d_func(), void collectionRemoved(const Akonadi::Collection &))
    Q_PRIVATE_SLOT(d_func(), void collectionChanged(const Akonadi::Collection &))
    Q_PRIVATE_SLOT(d_func(), void updateDone(KJob *))
    Q_PRIVATE_SLOT(d_func(), void collectionStatisticsChanged(Akonadi::Collection::Id,
                   const Akonadi::CollectionStatistics &))
    Q_PRIVATE_SLOT(d_func(), void listDone(KJob *))
    Q_PRIVATE_SLOT(d_func(), void dropResult(KJob *))
    Q_PRIVATE_SLOT(d_func(), void collectionsChanged(const Akonadi::Collection::List &))
    //@endcond
};

}

#endif
