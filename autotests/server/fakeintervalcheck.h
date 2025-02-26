/*
    Copyright (c) 2019 David Faure <faure@kde.org>

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

#ifndef FAKEINTERVALCHECK_H
#define FAKEINTERVALCHECK_H

#include <intervalcheck.h>

#include <QSemaphore>

namespace Akonadi {
namespace Server {

class FakeIntervalCheck : public IntervalCheck
{
    Q_OBJECT
public:
    FakeIntervalCheck(QObject *parent = nullptr);
    void waitForInit();

protected:
    void init() override;

    bool shouldScheduleCollection(const Collection &) override;
    bool hasChanged(const Collection &collection, const Collection &changed) override;
    int collectionScheduleInterval(const Collection &collection) override;
    void collectionExpired(const Collection &collection) override;

private:
    QSemaphore m_initCalled;
};

} // namespace Server
} // namespace Akonadi

#endif
