/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2013 Jolla Ltd.
 **
 ** Contact: Matt Vogt <matthew.vogt@jollamobile.com>
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **/

#include "cdsimcontroller.h"
#include "cdsimplugin.h"
#include "debug.h"

#include <qofonomanager.h>

using namespace Contactsd;

CDSimPlugin::CDSimPlugin()
    : mController(0)
{
}

CDSimPlugin::~CDSimPlugin()
{
}

void CDSimPlugin::init()
{
    debug() << "Initializing contactsd sim plugin";

    mController = new CDSimController(this);

    QOfonoManager ofonoManager;
    const QStringList &modems(ofonoManager.modems());
    if (modems.isEmpty()) {
        qWarning() << "No modem available for sim plugin";
    } else {
        // TODO: select the correct modem
        mController->setModemPath(modems.first());
    }
}

CDSimPlugin::MetaData CDSimPlugin::metaData()
{
    MetaData data;
    data[metaDataKeyName]    = QVariant(QLatin1String("sim"));
    data[metaDataKeyVersion] = QVariant(QLatin1String("0.1"));
    data[metaDataKeyComment] = QVariant(QLatin1String("contactsd sim plugin"));
    return data;
}

