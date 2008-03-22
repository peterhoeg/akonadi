/*
    Copyright (c) 2007 Volker Krause <vkrause@kde.org>

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

#include "itemtest.h"
#include "itemtest.moc"

#include <akonadi/item.h>

#include <qtest_kde.h>

QTEST_KDEMAIN( ItemTest, NoGUI )

using namespace Akonadi;

void ItemTest::testMultipart()
{
  Item item;
  item.setMimeType( "application/octet-stream" );

  QStringList parts;
  QCOMPARE( item.availableParts(), parts );

  QByteArray bodyData = "bodydata";
  item.addPart( Item::PartBody, bodyData );
  parts << Item::PartBody;
  QCOMPARE( item.availableParts(), parts );
  QCOMPARE( item.part( Item::PartBody ), bodyData );

  QByteArray myData = "mypartdata";
  parts << "MYPART";

  item.addPart( "MYPART", myData );
  QCOMPARE( item.availableParts(), parts );
  QCOMPARE( item.part( "MYPART" ), myData );
}

void ItemTest::testInheritance()
{
  Item a;

  a.setRemoteId( "Hello World" );

  Item b( a );
  b.setFlag( "\\send" );
}
