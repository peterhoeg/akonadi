/*
 Copyright (c) 2014  Christian Mollekopf <mollekopf@kolabsys.com>

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

#ifndef TESTSEARCHPLUGIN_H
#define TESTSEARCHPLUGIN_H

#include "../../../src/server/search/abstractsearchplugin.h"
#include <searchquery.h>
#include <QStringList>

class TestSearchPlugin : public QObject, public Akonadi::AbstractSearchPlugin
{
    Q_OBJECT
    Q_INTERFACES(Akonadi::AbstractSearchPlugin)
    Q_PLUGIN_METADATA(IID "org.kde.akonadi.TestSearchPlugin" FILE "akonadi_test_searchplugin.json")
public:
    QSet<qint64> search(const QString &query, const QVector<qint64> &collections, const QStringList &mimeTypes) override;

    static QSet<qint64> parseQuery(const QString &queryString);
};

#endif
