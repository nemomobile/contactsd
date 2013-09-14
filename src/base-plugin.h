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

#ifndef CONTACTSD_BASE_PLUGIN_H
#define CONTACTSD_BASE_PLUGIN_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QStringList>
#include <QMap>
#include <QThreadStorage>
#include <QDir>

namespace Contactsd
{

class Q_DECL_EXPORT BasePlugin : public QObject
{
    Q_OBJECT

public:
    static const QString metaDataKeyVersion;
    static const QString metaDataKeyName;
    static const QString metaDataKeyComment;
    typedef QMap<QString, QVariant> MetaData;

    virtual ~BasePlugin () {}
    virtual void init() = 0;
    virtual MetaData metaData() = 0;

    static QDir cacheDir();
    static QString cacheFileName(const QString &fileName);

Q_SIGNALS:
    // \param service - display name of a service (e.g. Gtalk, MSN)
    // \param account - account id or account path that can uniquely identify an account
    void importStarted(const QString &service, const QString &account);
    void importEnded(const QString &service, const QString &account,
                     int contactsAdded, int contactsRemoved, int contactsMerged);
    void error(int code, const QString &message);
    // Emitted to inform that import timeout should be extended
    void importAlive();
};

} // Contactsd

#endif // CONTACTSD_PLUGININTERFACE_H
