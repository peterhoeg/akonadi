/*
    Copyright (c) 2006-2008 Tobias Koenig <tokoe@kde.org>

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

#ifndef AKONADI_AGENTTYPEMODEL_H
#define AKONADI_AGENTTYPEMODEL_H

#include "akonadicore_export.h"

#include <QAbstractItemModel>

namespace Akonadi
{

/**
 * @short Provides a data model for agent types.
 *
 * This class provides the interface of a QAbstractItemModel to
 * access all available agent types: their name, identifier,
 * supported mimetypes and capabilities.
 *
 * @code
 *
 * Akonadi::AgentTypeModel *model = new Akonadi::AgentTypeModel( this );
 *
 * QListView *view = new QListView( this );
 * view->setModel( model );
 *
 * @endcode
 *
 * To show only agent types that match a given mime type or special
 * capabilities, use the AgentFilterProxyModel on top of this model.
 *
 * @author Tobias Koenig <tokoe@kde.org>
 */
class AKONADICORE_EXPORT AgentTypeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /**
     * Describes the roles of this model.
     */
    enum Roles {
        TypeRole = Qt::UserRole + 1,            ///< The agent type itself
        IdentifierRole,                         ///< The identifier of the agent type
        DescriptionRole,                        ///< A description of the agent type
        MimeTypesRole,                          ///< A list of supported mimetypes
        CapabilitiesRole,                       ///< A list of supported capabilities
        UserRole  = Qt::UserRole + 42           ///< Role for user extensions
    };

    /**
     * Creates a new agent type model.
     */
    explicit AgentTypeModel(QObject *parent = nullptr);

    /**
     * Destroys the agent type model.
     */
    ~AgentTypeModel() override;

    Q_REQUIRED_RESULT int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_REQUIRED_RESULT int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_REQUIRED_RESULT QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Q_REQUIRED_RESULT QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    Q_REQUIRED_RESULT QModelIndex parent(const QModelIndex &index) const override;
    Q_REQUIRED_RESULT Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    //@cond PRIVATE
    class Private;
    Private *const d;
    //@endcond
};

}

#endif
