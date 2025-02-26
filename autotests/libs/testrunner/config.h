/*
 * Copyright (c) 2008  Igor Trindade Oliveira <igor_trindade@yahoo.com.br>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <QHash>
#include <QString>
#include <QStringList>

class Config
{
public:
    Config();
    ~Config();
    static Config *instance();
    static Config *instance(const QString &pathToConfig);
    static void destroyInstance();
    QString xdgDataHome() const;
    QString xdgConfigHome() const;
    QString basePath() const;
    QStringList backends() const;
    bool setDbBackend(const QString &backend);
    QString dbBackend() const;
    QList<QPair<QString, bool> > agents() const;
    QHash<QString, QString> envVars() const;

protected:
    void setXdgDataHome(const QString &dataHome);
    void setXdgConfigHome(const QString &configHome);
    void setBackends(const QStringList &backends);
    void insertAgent(const QString &agent, bool sync);

private:
    void readConfiguration(const QString &configFile);

private:
    QString mBasePath;
    QString mXdgDataHome;
    QString mXdgConfigHome;
    QStringList mBackends;
    QString mDbBackend;
    QList<QPair<QString, bool> > mAgents;
    QHash<QString, QString> mEnvVars;
};

#endif
