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

#ifndef CONTACTSD_H
#define CONTACTSD_H

#include <QObject>
#include <QSocketNotifier>
#include <QStringList>

class ContactsdPluginLoader;

class Q_DECL_EXPORT ContactsDaemon : public QObject
{
    Q_OBJECT

public:
    ContactsDaemon(QObject *parent);
    virtual ~ContactsDaemon();

    void loadPlugins(const QStringList &plugins = QStringList());
    QStringList loadedPlugins() const;

    // UNIX signal handlers
    static void unixSignalHandler(int /* unused */);

private Q_SLOTS:
    // Qt signal handler
    void onUnixSignalReceived();

private:
    ContactsdPluginLoader *mLoader;
    static int sigFd[2];
    QSocketNotifier *mSignalNotifier;
};

#endif // CONTACTSDAEMON_H
