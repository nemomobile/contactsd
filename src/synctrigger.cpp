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

#include "synctrigger.h"
#include "debug.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>

// buteo-syncfw
#include <ProfileEngineDefs.h>
#include <SyncProfile.h>
#include <SyncCommonDefs.h>
#include <ProfileManager.h>

// QtContacts
#include <QContactManager>
#include <QContactId>
#include <QContactDetail>

namespace{
    QContactManager *createManager()
    {
        QMap<QString, QString> parameters;
        parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("false"));
        return new QContactManager(QString::fromLatin1("org.nemomobile.contacts.sqlite"), parameters);
    }
}

namespace Contactsd {

SyncTrigger::SyncTrigger(QDBusConnection *connection)
    : mContactManager(createManager())
    , mDBusConnection(connection)
    , mHaveRegisteredDBus(false)
{
    QObject::connect(mContactManager, &QContactManager::contactsAdded, this, &SyncTrigger::contactDbChangesDetected);
    QObject::connect(mContactManager, &QContactManager::contactsChanged, this, &SyncTrigger::contactDbChangesDetected);
    QObject::connect(mContactManager, &QContactManager::contactsRemoved, this, &SyncTrigger::contactDbChangesDetected);

    mTimer.setInterval(60000); // coalesce local changes within 1 minute
    QObject::connect(&mTimer, &QTimer::timeout, this, &SyncTrigger::coalescedTrigger);
}

SyncTrigger::~SyncTrigger()
{
    delete mContactManager;
    if (mHaveRegisteredDBus) {
        mDBusConnection->unregisterObject(QLatin1String("/SyncTrigger"));
    }
}

bool SyncTrigger::registerTriggerService()
{
    if (mHaveRegisteredDBus) {
        return true;
    }

    if (!mDBusConnection->registerObject(QLatin1String("/SyncTrigger"), this, QDBusConnection::ExportAllContents)) {
        warning() << "Could not register DBus object '/SyncTrigger':" << mDBusConnection->lastError();
        return false;
    }

    mHaveRegisteredDBus = true;
    return true;
}

void SyncTrigger::triggerSync(const QStringList &syncTargets, int syncPolicy, int directionPolicy)
{
    // TODO: in the future, handle AdaptivePolicy by coalescing triggered syncs into time segments.
    debug() << "triggering sync:" << syncTargets << syncPolicy << directionPolicy;

    // We trigger sync with a particular Buteo sync profile if:
    //  - the profile is a per-account (not template) profile
    //  - the profile is enabled
    //  - the profile is for the service corresponding to the sync target
    //  - the profile is a Contacts sync profile
    //  - the profile has two-way directionality (or upsync), or the direction policy permits
    //  - the profile has always-up-to-date set (sync on change), or the sync policy permits
    Buteo::ProfileManager profileManager;
    QList<Buteo::SyncProfile*> syncProfiles = profileManager.allSyncProfiles();
    Q_FOREACH (Buteo::SyncProfile *profile, syncProfiles) {
        if (!profile) {
            continue;
        }

        QString profileId = profile->name();
        bool notTemplate = !profile->key(Buteo::KEY_ACCOUNT_ID, QString()).isEmpty()
                         && profile->key(Buteo::KEY_ACCOUNT_ID, QStringLiteral("0")) != QStringLiteral("0");
        bool isEnabled = profile->isEnabled();
        bool isUpsync = profile->syncDirection() == Buteo::SyncProfile::SYNC_DIRECTION_TWO_WAY
                     || profile->syncDirection() == Buteo::SyncProfile::SYNC_DIRECTION_TO_REMOTE;
        bool alwaysUpToDate = profile->key(Buteo::KEY_SYNC_ALWAYS_UP_TO_DATE, QStringLiteral("false")) == QStringLiteral("true");

        // By convention, the template profile name should be of the form:
        // "syncTarget.dataType" -- eg, "google.Contacts"
        // And per-account profiles should be suffixed with "-accountId"
        bool isContacts = profileId.toLower().contains(QStringLiteral("contacts"));
        bool isTarget = syncTargets.isEmpty();
        if (!isTarget) {
            Q_FOREACH (const QString &syncTarget, syncTargets) {
                if (profileId.toLower().startsWith(syncTarget.toLower())) {
                    isTarget = true;
                    break;
                }
            }
        }

        // in the future, we should inspect the sync profile for deltasync flag
        if (!profileId.toLower().startsWith(QStringLiteral("google"))) {
            isTarget = false; // we currently only support automatic sync with Google
        }

        delete profile;

        if (notTemplate && isEnabled && isTarget && isContacts
                && (isUpsync || directionPolicy == SyncTrigger::AnyDirection)
                && (alwaysUpToDate || syncPolicy == SyncTrigger::ForceSync)) {
            debug() << "sync profile meets trigger criteria, starting sync:" << profileId;
            QDBusMessage message = QDBusMessage::createMethodCall(
                    QStringLiteral("com.meego.msyncd"),
                    QStringLiteral("/synchronizer"),
                    QStringLiteral("com.meego.msyncd"),
                    QStringLiteral("startSync"));
            message.setArguments(QVariantList() << profileId);
            QDBusConnection::sessionBus().asyncCall(message);
        }
    }

    // And we trigger exchange separately if the "mfe" or "exchange" or "activesync" target is specified.
    if (syncTargets.isEmpty()
            || syncTargets.contains(QStringLiteral("mfe"), Qt::CaseInsensitive)
            || syncTargets.contains(QStringLiteral("exchange"), Qt::CaseInsensitive)
            || syncTargets.contains(QStringLiteral("activesync"), Qt::CaseInsensitive)) {
        QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("com.nokia.asdbus"),
                QStringLiteral("/com/nokia/asdbus"),
                QStringLiteral("com.nokia.asdbus"),
                QStringLiteral("syncAll"));
        qint32 dataType = 4; // contacts
        message.setArguments(QVariantList() << dataType);
        QDBusConnection::sessionBus().asyncCall(message);
    }
}

void SyncTrigger::contactDbChangesDetected()
{
    if (mTimer.isActive()) {
        // reset the timer to coalesce detected changes into single sync cycle.
        debug() << "coalescing previously detected change into new change before triggering sync";
        mTimer.stop();
    } else {
        debug() << "detected change, beginning coalesce timer to trigger sync";
    }

    mTimer.start();
}

void SyncTrigger::coalescedTrigger()
{
    debug() << "contact database changes detected, triggering upsyncs";
    triggerSync(QStringList(), SyncTrigger::UpToDateSync, SyncTrigger::UpsyncDirection);
}

}

