/***************************************************************************
 *   Copyright (C) 2006 by Tobias Koenig <tokoe@kde.org>                   *
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

#ifndef AKONADI_DBUSTRACER_H
#define AKONADI_DBUSTRACER_H

#include <QObject>

#include "tracerinterface.h"

namespace Akonadi
{
namespace Server
{

/**
 * A tracer which forwards all tracing information as dbus signals.
 */
class DBusTracer : public QObject, public TracerInterface
{
    Q_OBJECT

public:
    DBusTracer();
    ~DBusTracer() override;

    void beginConnection(const QString &identifier, const QString &msg) override;
    void endConnection(const QString &identifier, const QString &msg) override;
    void connectionInput(const QString &identifier, const QByteArray &msg) override;
    void connectionOutput(const QString &identifier, const QByteArray &msg) override;
    void signal(const QString &signalName, const QString &msg) override;
    void warning(const QString &componentName, const QString &msg) override;
    void error(const QString &componentName, const QString &msg) override;

    TracerInterface::ConnectionFormat connectionFormat() const override {return TracerInterface::Json;}

Q_SIGNALS:
    void connectionStarted(const QString &identifier, const QString &msg);
    void connectionEnded(const QString &identifier, const QString &msg);
    void connectionDataInput(const QString &identifier, const QString &msg);
    void connectionDataOutput(const QString &identifier, const QString &msg);
    void signalEmitted(const QString &signalName, const QString &msg);
    void warningEmitted(const QString &componentName, const QString &msg);
    void errorEmitted(const QString &componentName, const QString &msg);
};

} // namespace Server
} // namespace Akonadi

#endif
