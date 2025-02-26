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

#ifndef COLLECTIONIDENTIFICATIONATTRIBUTE_H
#define COLLECTIONIDENTIFICATIONATTRIBUTE_H

#include <attribute.h>
#include <QByteArray>

namespace Akonadi
{

/**
 * @short Attribute that stores additional information on a collection that can be used for searching.
 *
 * Additional indexed properties that can be used for searching.
 *
 * @author Christian Mollekopf <mollekopf@kolabsys.com>
 * @since 4.15
 */
class AKONADICORE_EXPORT CollectionIdentificationAttribute : public Akonadi::Attribute
{
public:
    explicit CollectionIdentificationAttribute(const QByteArray &identifier = QByteArray(), const QByteArray &folderNamespace = QByteArray(),
            const QByteArray &name = QByteArray(), const QByteArray &organizationUnit = QByteArray(), const QByteArray &mail = QByteArray());
    ~CollectionIdentificationAttribute() override;

    /**
     * Sets an identifier for the collection.
     */
    void setIdentifier(const QByteArray &identifier);
    QByteArray identifier() const;

    void setMail(const QByteArray &);
    QByteArray mail() const;

    void setOu(const QByteArray &);
    QByteArray ou() const;

    void setName(const QByteArray &);
    QByteArray name() const;

    /**
     * Sets a namespace the collection is in.
     *
     * Initially used are:
     * * "person" for a collection shared by a person.
     * * "shared" for a collection shared by a person.
     */
    void setCollectionNamespace(const QByteArray &ns);
    QByteArray collectionNamespace() const;
    QByteArray type() const override;
    Attribute *clone() const override;
    QByteArray serialized() const override;
    void deserialize(const QByteArray &data) override;

private:
    //@cond PRIVATE
    class Private;
    Private *const d;
    //@endcond
};

}

#endif
