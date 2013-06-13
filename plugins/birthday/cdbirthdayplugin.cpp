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

#include "cdbirthdaycontroller.h"
#include "cdbirthdayplugin.h"
#include "debug.h"

using namespace Contactsd;

CDBirthdayPlugin::CDBirthdayPlugin()
    : mController(0)
{
}

CDBirthdayPlugin::~CDBirthdayPlugin()
{
    delete mController;
}

void CDBirthdayPlugin::init()
{
    debug() << "Initializing contactsd birthday plugin";

    mController = new CDBirthdayController(this);
}

CDBirthdayPlugin::MetaData CDBirthdayPlugin::metaData()
{
    MetaData data;
    data[metaDataKeyName]    = QVariant(QLatin1String("birthday"));
    data[metaDataKeyVersion] = QVariant(QLatin1String("0.1"));
    data[metaDataKeyComment] = QVariant(QLatin1String("contactsd birthday plugin"));
    return data;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(birthdayplugin, CDBirthdayPlugin)
#endif
