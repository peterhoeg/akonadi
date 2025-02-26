/*
 * Copyright (c) 2009  Volker Krause <vkrause@kde.org>
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

#ifndef TESTRUNNER_H
#define TESTRUNNER_H

#include <QObject>
#include <QProcess>
#include <QStringList>

class KProcess;

class TestRunner : public QObject
{
    Q_OBJECT

public:
    explicit TestRunner(const QStringList &args, QObject *parent = nullptr);
    int exitCode() const;
    void terminate();

public Q_SLOTS:
    void run();
    void triggerTermination(int);

Q_SIGNALS:
    void finished();

private Q_SLOTS:
    void processFinished(int exitCode);
    void processError(QProcess::ProcessError error);

private:
    QStringList mArguments;
    int mExitCode;
    KProcess *mProcess = nullptr;
};

#endif // TESTRUNNER_H
