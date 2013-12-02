/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2014 Jolla Ltd.
 ** Contact: Chris Adams <chris.adams@jolla.com>
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **/

#ifndef UTIL_H
#define UTIL_H

#include <QString>

#include <QContact>
#include <QContactName>

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#else
QTM_USE_NAMESPACE
#endif

namespace Contactsd
{
    class Util {
    public:
        Q_DECL_EXPORT static void decomposeNameDetails(const QString &formattedName,
                                                       QContactName *nameDetail);
    };
}

#endif // UTIL_H
