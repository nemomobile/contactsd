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

#ifndef CONTACTSPLUGINLOADER_H_
#define CONTACTSPLUGINLOADER_H_

#include <QObject>
#include <QMap>

#include "base-plugin.h"
#include "importstate.h"

class QTimer;
class QPluginLoader;
class QString;
class QStringList;

class Q_DECL_EXPORT ContactsdPluginLoader : public QObject
{
    Q_OBJECT

public:
    ContactsdPluginLoader();
    ~ContactsdPluginLoader();

    void loadPlugins(const QStringList &plugins);
    void loadPlugins(const QString &pluginsDir, const QStringList &plugins);
    QStringList loadedPlugins() const;
    bool registerNotificationService();

public Q_SLOTS:
    QStringList hasActiveImports();

Q_SIGNALS:
    void importStarted(const QString &service);
    void importStateChanged(const QString &finishedService,
                            const QString &newService);
    void importEnded(int contactsAdded, int contactsRemoved,
                     int contactsMerged);
    void pluginsLoaded();
    void error(int code, const QString &message);

private Q_SLOTS:
    void onPluginImportStarted(const QString &service, const QString &account);
    void onPluginImportEnded(const QString &service, const QString &account,
                             int contactsAdded, int contactsRemoved, int contactsMerged);
    void onImportTimeout();
    void onImportAlive();
    void onCheckAliveTimeout();

private:
    void startImportTimer();
    void stopImportTimer();
    void startCheckAliveTimer();
    void stopCheckAliveTimer();
    QString pluginName(Contactsd::BasePlugin *plugin);

    typedef QMap<QString, Contactsd::BasePlugin*> PluginStore;
    PluginStore mPluginStore;
    ImportState mImportState;

    QTimer *mImportTimer;
    QTimer *mCheckAliveTimer;
};

#endif
