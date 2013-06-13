/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2010-2011 Nokia Corporation and/or its subsidiary(-ies).
 **
 ** Contact:  Nokia Corporation (info@qt.nokia.com)
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **
 ** In addition, as a special exception, Nokia gives you certain additional rights.
 ** These rights are described in the Nokia Qt LGPL Exception version 1.1, included
 ** in the file LGPL_EXCEPTION.txt in this package.
 **
 ** Other Usage
 ** Alternatively, this file may be used in accordance with the terms and
 ** conditions contained in a signed written agreement between you and Nokia.
 **/

#ifndef CDBIRTHDAYPLUGIN_H
#define CDBIRTHDAYPLUGIN_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

#include "base-plugin.h"

class CDBirthdayController;

class CDBirthdayPlugin : public Contactsd::BasePlugin
{
    Q_OBJECT
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    Q_PLUGIN_METADATA(IID "org.nemomobile.contactsd.birthday")
#endif

public:
    CDBirthdayPlugin();
    ~CDBirthdayPlugin();

    void init();
    MetaData metaData();

private:
    CDBirthdayController *mController;
};

#endif // CDBIRTHDAYPLUGIN_H
