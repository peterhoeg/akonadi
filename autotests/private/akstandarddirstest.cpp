/*
    Copyright (c) 2011 Volker Krause <vkrause@kde.org>

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

#include <private/standarddirs_p.h>
#include <aktest.h>

#include <QObject>
#include <QTest>

#define QL1S(x) QStringLiteral(x)

using namespace Akonadi;

class AkStandardDirsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testCondigFile()
    {
        akTestSetInstanceIdentifier(QString());
        QVERIFY(StandardDirs::agentsConfigFile(StandardDirs::ReadOnly).endsWith(QL1S("agentsrc")));
        QVERIFY(StandardDirs::agentsConfigFile(StandardDirs::ReadWrite).endsWith(QL1S("agentsrc")));
        QVERIFY(!StandardDirs::agentsConfigFile(StandardDirs::ReadWrite).endsWith(QL1S("foo/agentsrc")));

        akTestSetInstanceIdentifier(QL1S("foo"));
        QVERIFY(StandardDirs::agentsConfigFile(StandardDirs::ReadOnly).endsWith(QL1S("agentsrc")));
        QVERIFY(StandardDirs::agentsConfigFile(StandardDirs::ReadWrite).endsWith(QL1S("instance/foo/agentsrc")));
    }

    void testSaveDir()
    {
        akTestSetInstanceIdentifier(QString());
        QVERIFY(StandardDirs::saveDir("data").endsWith(QL1S("/akonadi")));
        QVERIFY(!StandardDirs::saveDir("data").endsWith(QL1S("foo/akonadi")));

        akTestSetInstanceIdentifier(QL1S("foo"));
        QVERIFY(StandardDirs::saveDir("data").endsWith(QL1S("/akonadi/instance/foo")));
    }
};

AKTEST_MAIN(AkStandardDirsTest)

#include "akstandarddirstest.moc"
