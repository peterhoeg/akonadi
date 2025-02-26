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

#ifndef AKSTANDARDDIRS_H
#define AKSTANDARDDIRS_H

#include "akonadiprivate_export.h"

#include <QString>

namespace Akonadi
{

/**
 * Convenience wrappers on top of QStandardPaths that are instance namespace aware.
 * @since 1.7
 */
namespace StandardDirs
{

/**
 * @brief Open mode flags for resource files
 *
 * FileAccessMode is a typedef for QFlags<FileAccessFlag>. It stores
 * a OR combination of FileAccessFlag values
 */
enum FileAccessMode {
    ReadOnly  = 0x1,
    WriteOnly = 0x2,
    ReadWrite = ReadOnly | WriteOnly
};

/**
 * Returns path to the config file @p configFile.
 */
AKONADIPRIVATE_EXPORT QString configFile(const QString &configFile, FileAccessMode openMode = ReadOnly);

/**
 * Returns the full path to the server config file (akonadiserverrc).
 */
AKONADIPRIVATE_EXPORT QString serverConfigFile(FileAccessMode openMode = ReadOnly);

/**
 * Returns the full path to the connection config file (akonadiconnectionrc).
 */
AKONADIPRIVATE_EXPORT QString connectionConfigFile(FileAccessMode openMode = ReadOnly);

/**
 * Returns the full path to the agentsrc config file
 */
AKONADIPRIVATE_EXPORT QString agentsConfigFile(FileAccessMode openMode = ReadOnly);

/**
 * Returns the full path to config file of agent @p identifier.
 *
 * Never returns empty string.
 *
 * @param identifier identifier of the agent (akonadi_foo_resource_0)
 */
AKONADIPRIVATE_EXPORT QString agentConfigFile(const QString &identifier, FileAccessMode openMode = ReadOnly);

/**
 * Instance-aware wrapper for QStandardPaths
 * @note @p relPath does not need to include the "akonadi/" folder.
 */
AKONADIPRIVATE_EXPORT QString saveDir(const char *resource, const QString &relPath = QString());

/**
 * @brief Searches the resource specific directories for a given file
 *
 * Convenience method for finding a given file (with optional relative path)
 * in any of the configured base directories for a given resource type.
 *
 * Will check the user local directory first and then process the system
 * wide path list according to the inherent priority.
 *
 * @param resource a named resource type, e.g. "config"
 * @param relPath relative path of a file to look for, e.g."akonadi/akonadiserverrc"
 *
 * @returns the file path of the first match, or @c QString() if no such relative path
 *          exists in any of the base directories or if a match is not a file
 */
AKONADIPRIVATE_EXPORT QString locateResourceFile(const char *resource, const QString &relPath);

/**
 * Equivalent to QStandardPaths::locateAll() but always includes at least the
 * default Akonadi compile prefix.
 */
AKONADIPRIVATE_EXPORT QStringList locateAllResourceDirs(const QString &relPath);

/**
 * Equivalent to QStandardPaths::findExecutable() but it looks in
 * qApp->applicationDirPath() first.
 */

AKONADIPRIVATE_EXPORT QString findExecutable(const QString &relPath);

}
}

#endif
