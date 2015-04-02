/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2014 Jolla Ltd
 **
 ** Contact:  Chris Adams (chris.adams@jolla.com)
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **
 ** Other Usage
 ** Alternatively, this file may be used in accordance with the terms and
 ** conditions contained in a signed written agreement between you and Nokia.
 **/

#ifndef SYNCTRIGGER_H
#define SYNCTRIGGER_H

#include <QDBusContext>
#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <QContactManager>
QTCONTACTS_USE_NAMESPACE

namespace Contactsd {

class SyncTrigger : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.nokia.contactsd")

public:
    SyncTrigger(QDBusConnection *connection);
    ~SyncTrigger();
    bool registerTriggerService();

    enum SyncPolicy {
        ForceSync = 0,
        UpToDateSync,
        AdaptiveSync
    };

    enum DirectionPolicy {
        AnyDirection = 0,
        UpsyncDirection
    };

public Q_SLOTS:
    Q_NOREPLY void triggerSync(const QStringList &syncTargets = QStringList(), int syncPolicy = ForceSync, int directionPolicy = AnyDirection);

private Q_SLOTS:
    void contactDbChangesDetected();
    void coalescedTrigger();

private:
    QTimer mTimer;
    QContactManager *mContactManager;
    QDBusConnection *mDBusConnection;
    bool mHaveRegisteredDBus;
};

}

#endif
