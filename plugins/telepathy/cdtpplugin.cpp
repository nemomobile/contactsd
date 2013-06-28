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

#include <TelepathyQt/Debug>

#include "cdtpcontroller.h"
#include "cdtpplugin.h"
#include "debug.h"

using namespace Contactsd;

CDTpPlugin::CDTpPlugin()
    : mController(0)
{
}

CDTpPlugin::~CDTpPlugin()
{
    if (mController) {
        delete mController;
    }
}

void CDTpPlugin::init()
{
    debug() << "Initializing contactsd telepathy plugin";

    Tp::registerTypes();
    Tp::enableDebug(isDebugEnabled());
    Tp::enableWarnings(isWarningsEnabled());

    debug() << "Creating controller";
    mController = new CDTpController(this);
    // relay signals
    connect(mController,
            SIGNAL(importStarted(const QString &, const QString &)),
            SIGNAL(importStarted(const QString &, const QString &)));
    connect(mController,
            SIGNAL(importEnded(const QString &, const QString &, int, int, int)),
            SIGNAL(importEnded(const QString &, const QString &, int, int, int)));
    connect(mController,
            SIGNAL(error(int, const QString &)),
            SIGNAL(error(int, const QString &)));
}

CDTpPlugin::MetaData CDTpPlugin::metaData()
{
    MetaData data;
    data[metaDataKeyName]    = QVariant(QString::fromLatin1("telepathy"));
    data[metaDataKeyVersion] = QVariant(QString::fromLatin1("0.2"));
    data[metaDataKeyComment] = QVariant(QString::fromLatin1("contactsd telepathy plugin"));
    return data;
}

