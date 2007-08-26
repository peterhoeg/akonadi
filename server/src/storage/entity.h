/***************************************************************************
 *   Copyright (C) 2006 by Andreas Gungl <a.gungl@gmx.de>                  *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#ifndef ENTITY_H
#define ENTITY_H

#include "countquerybuilder.h"

#include <Qt>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace Akonadi {

/**
  Base class for classes representing database records. It also contains
  low-level data access and manipulation template methods.
*/
class Entity
{
  public:
    int id() const;
    void setId( int id );

    bool isValid() const;

    template <typename T> static QString joinByName( const QList<T> &list, const QString &sep )
    {
      QStringList tmp;
      foreach ( T t, list )
        tmp << t.name();
      return tmp.join( sep );
    }

    /**
      Returns the number of records having @p value in @p column.
      @param column The name of the key column.
      @param value The value used to identify the record.
    */
    template <typename T> inline static int count( const QString &column, const QVariant &value )
    {
      QSqlDatabase db = database();
      if ( !db.isOpen() )
        return -1;

      CountQueryBuilder builder;
      builder.addTable( T::tableName() );
      builder.addValueCondition( column, "=", value );

      if ( !builder.exec() ) {
        qDebug() << "Error during counting records in table" << T::tableName()
            << builder.query().lastError().text();
        return -1;
      }

      return builder.result();
    }

    /**
      Deletes all records having @p value in @p column.
    */
    template <typename T> inline static bool remove( const QString &column, const QVariant &value )
    {
      QSqlDatabase db = database();
      if ( !db.isOpen() )
        return false;

      QueryBuilder builder( QueryBuilder::Delete );
      builder.addTable( T::tableName() );
      builder.addValueCondition( column, "=", value );

      if ( !builder.exec() ) {
        qDebug() << "Error during deleting records from table"
            << T::tableName() << builder.query().lastError().text();
        return false;
      }
      return true;
    }

    /**
      Checks wether an entry in a n:m relation table exists.
      @param leftId Identifier of the left part of the relation.
      @param rightId Identifier of the right part of the relation.
     */
    template <typename T> inline static bool relatesTo( int leftId, int rightId )
    {
      QSqlDatabase db = database();
      if ( !db.isOpen() )
        return false;

      CountQueryBuilder builder;
      builder.addTable( T::tableName() );
      builder.addValueCondition( T::leftColumn(), "=", leftId );
      builder.addValueCondition( T::rightColumn(), "=", rightId );

      if ( !builder.exec() ) {
        qDebug() << "Error during counting records in table" << T::tableName()
            << builder.query().lastError().text();
        return false;
      }

      if ( builder.result() > 0 )
        return true;
      return false;
    }

    /**
      Adds an entry to a n:m relation table (specified by the template parameter).
      @param leftId Identifier of the left part of the relation.
      @param rightId Identifier of the right part of the relation.
    */
    template <typename T> inline static bool addToRelation( int leftId, int rightId )
    {
      QSqlDatabase db = database();
      if ( !db.isOpen() )
        return false;

      QString statement = QLatin1String("INSERT INTO ");
      statement.append( T::tableName() );
      statement.append( QLatin1String(" ( ") );
      statement.append( T::leftColumn() );
      statement.append( QLatin1String(" , ") );
      statement.append( T::rightColumn() );
      statement.append( QLatin1String(" ) VALUES ( :left, :right )") );

      QSqlQuery query( db );
      query.prepare( statement );
      query.bindValue( QLatin1String(":left"), leftId );
      query.bindValue( QLatin1String(":right"), rightId );

      if ( !query.exec() ) {
        qDebug() << "Error during adding a record to table" << T::tableName()
          << query.lastError().text();
        return false;
      }

      return true;
    }

    /**
      Removes an entry from a n:m relation table (specified by the template parameter).
      @param leftId Identifier of the left part of the relation.
      @param rightId Identifier of the right part of the relation.
    */
    template <typename T> inline static bool removeFromRelation( int leftId, int rightId )
    {
      QSqlDatabase db = database();
      if ( !db.isOpen() )
        return false;

      QueryBuilder builder( QueryBuilder::Delete );
      builder.addTable( T::tableName() );
      builder.addValueCondition( T::leftColumn(), "=", leftId );
      builder.addValueCondition( T::rightColumn(), "=", rightId );

      if ( !builder.exec() ) {
        qDebug() << "Error during removing a record from relation table" << T::tableName()
          << builder.query().lastError().text();
        return false;
      }

      return true;
    }

    enum RelationSide {
      Left,
      Right
    };

    /**
      Clears all entries from a n:m relation table (specified by the given template parameter).
      @param id Identifier on the relation side.
      @param side The side of the relation.
    */
    template <typename T> inline static bool clearRelation( int id, RelationSide side = Left )
    {
      QSqlDatabase db = database();
      if ( !db.isOpen() )
        return false;

      QueryBuilder builder( QueryBuilder::Delete );
      builder.addTable( T::tableName() );
      switch ( side ) {
        case Left:
          builder.addValueCondition( T::leftColumn(), "=", id );
          break;
        case Right:
          builder.addValueCondition( T::rightColumn(), "=", id );
          break;
        default:
          qFatal("Invalid enum value");
      }
      if ( !builder.exec() ) {
        qDebug() << "Error during clearing relation table" << T::tableName()
            << "for id" << id << builder.query().lastError().text();
        return false;
      }

      return true;
    }

  protected:
    Entity();
    Entity( int id );


  private:
    static QSqlDatabase database();
    int m_id;
};

}


#endif
