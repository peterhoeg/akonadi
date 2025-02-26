/*
    Copyright (c) 2008 Tobias Koenig <tokoe@kde.org>

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

#include "pluginloader_p.h"
#include "itemserializerplugin.h"

#include <QDebug>
#include <QStringList>
#include <QApplication>

using namespace Akonadi;

int main()
{
    QApplication::setApplicationName(QStringLiteral("pluginloadertest"));

    PluginLoader *loader = PluginLoader::self();

    const QStringList types = loader->names();
    qDebug("Types:");
    for (int i = 0; i < types.count(); ++i) {
        qDebug("%s", qPrintable(types.at(i)));
    }

    QObject *object = loader->createForName(QStringLiteral("text/vcard@KContacts::Addressee"));
    if (qobject_cast<ItemSerializerPlugin *>(object) != nullptr) {
        qDebug("Loaded plugin for mimetype 'text/vcard@KContacts::Addressee' successfully");
    } else {
        qDebug("Unable to load plugin for mimetype 'text/vcard'");
    }

    return 0;
}
