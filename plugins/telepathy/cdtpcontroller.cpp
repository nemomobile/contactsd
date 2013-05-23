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

#include <TelepathyQt/Account>
#include <TelepathyQt/AccountPropertyFilter>
#include <TelepathyQt/AndFilter>
#include <TelepathyQt/NotFilter>
#include <TelepathyQt/AccountSet>
#include <TelepathyQt/AccountManager>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/ContactManager>
#include <TelepathyQt/PendingContacts>

#include "buddymanagementadaptor.h"
#include "cdtpcontroller.h"
#include "debug.h"

using namespace Contactsd;

const QLatin1String DBusObjectPath("/telepathy");
static const QString offlineRemovals = QString::fromLatin1("OfflineRemovals");
static const QString offlineInvitations = QString::fromLatin1("OfflineInvitations");

CDTpController::CDTpController(QObject *parent) : QObject(parent)
{
    debug() << "Creating storage";
    mStorage = new CDTpStorage(this);
    mOfflineRosterBuffer = new QSettings(QSettings::IniFormat,
            QSettings::UserScope, QLatin1String("Nokia"),
            QLatin1String("Contactsd"));
    connect(mStorage,
            SIGNAL(error(int, const QString &)),
            SIGNAL(error(int, const QString &)));

    debug() << "Creating account manager";
    const QDBusConnection &bus = QDBusConnection::sessionBus();
    Tp::AccountFactoryPtr accountFactory = Tp::AccountFactory::create(bus,
            Tp::Features() << Tp::Account::FeatureCore
                           << Tp::Account::FeatureAvatar
                           << Tp::Account::FeatureCapabilities);
    Tp::ConnectionFactoryPtr connectionFactory = Tp::ConnectionFactory::create(bus,
            Tp::Features() << Tp::Connection::FeatureConnected
                           << Tp::Connection::FeatureCore
                           << Tp::Connection::FeatureRoster);
    Tp::ChannelFactoryPtr channelFactory = Tp::ChannelFactory::create(bus);
    Tp::ContactFactoryPtr contactFactory = Tp::ContactFactory::create(
            Tp::Features() << Tp::Contact::FeatureAlias
                           << Tp::Contact::FeatureAvatarToken
                           << Tp::Contact::FeatureAvatarData
                           << Tp::Contact::FeatureSimplePresence
                           << Tp::Contact::FeatureInfo
                           << Tp::Contact::FeatureLocation
                           << Tp::Contact::FeatureCapabilities);
    mAM = Tp::AccountManager::create(bus, accountFactory, connectionFactory,
            channelFactory, contactFactory);

    // Wait for AM to become ready
    connect(mAM->becomeReady(Tp::AccountManager::FeatureCore),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onAccountManagerReady(Tp::PendingOperation*)));
    if (registerDBusObject()) {
        (void) new BuddyManagementAdaptor(this);
    }
}

CDTpController::~CDTpController()
{
    QDBusConnection::sessionBus().unregisterObject(DBusObjectPath);
    if (mOfflineRosterBuffer) {
        delete mOfflineRosterBuffer;
    }
}

void CDTpController::onAccountManagerReady(Tp::PendingOperation *op)
{
    if (op->isError()) {
        debug() << "Could not make account manager ready:" <<
            op->errorName() << "-" << op->errorMessage();
        return;
    }

    debug() << "Account manager ready";

    Tp::AccountPropertyFilterPtr propFilter;
    Tp::AccountFilterPtr notFilter;
    QList<Tp::AccountFilterConstPtr> filters;

    propFilter = Tp::AccountPropertyFilter::create();
    propFilter->addProperty(QString::fromLatin1("valid"), true);
    filters << propFilter;

    propFilter = Tp::AccountPropertyFilter::create();
    propFilter->addProperty(QString::fromLatin1("normalizedName"), QString());
    notFilter = Tp::NotFilter<Tp::Account>::create(propFilter);
    filters << notFilter;

    propFilter = Tp::AccountPropertyFilter::create();
    propFilter->addProperty(QString::fromLatin1("cmName"), QLatin1String("ring"));
    notFilter = Tp::NotFilter<Tp::Account>::create(propFilter);
    filters << notFilter;

    propFilter = Tp::AccountPropertyFilter::create();
    propFilter->addProperty(QString::fromLatin1("cmName"), QLatin1String("mmscm"));
    notFilter = Tp::NotFilter<Tp::Account>::create(propFilter);
    filters << notFilter;

    Tp::AccountFilterPtr filter = Tp::AndFilter<Tp::Account>::create(filters);

    mAccountSet = mAM->filterAccounts(filter);
    connect(mAccountSet.data(),
            SIGNAL(accountAdded(const Tp::AccountPtr &)),
            SLOT(onAccountAdded(const Tp::AccountPtr &)));
    connect(mAccountSet.data(),
            SIGNAL(accountRemoved(const Tp::AccountPtr &)),
             SLOT(onAccountRemoved(const Tp::AccountPtr &)));

    Q_FOREACH (const Tp::AccountPtr &account, mAccountSet->accounts()) {
        insertAccount(account, false);
    }

    mStorage->syncAccounts(mAccounts.values());
}

void CDTpController::onAccountAdded(const Tp::AccountPtr &account)
{
    if (mAccounts.contains(account->objectPath())) {
        warning() << "Internal error, account was already in controller";
        return;
    }

    CDTpAccountPtr accountWrapper = insertAccount(account, true);
    mStorage->createAccount(accountWrapper);
}

void CDTpController::onAccountRemoved(const Tp::AccountPtr &account)
{
    CDTpAccountPtr accountWrapper(mAccounts.take(account->objectPath()));
    if (not accountWrapper) {
        warning() << "Internal error, account was not in controller";
        return;
    }
    mStorage->removeAccount(accountWrapper);

    // Drop pending offline operations
    QString accountPath = accountWrapper->account()->objectPath();

    mOfflineRosterBuffer->beginGroup(offlineRemovals);
    mOfflineRosterBuffer->remove(accountPath);
    mOfflineRosterBuffer->endGroup();

    mOfflineRosterBuffer->beginGroup(offlineInvitations);
    mOfflineRosterBuffer->remove(accountPath);
    mOfflineRosterBuffer->endGroup();

    mOfflineRosterBuffer->sync();
}

CDTpAccountPtr CDTpController::insertAccount(const Tp::AccountPtr &account, bool newAccount)
{
    debug() << "Creating wrapper for account" << account->objectPath();

    // Get the list of contact ids waiting to be removed from server
    mOfflineRosterBuffer->beginGroup(offlineRemovals);
    QStringList idsToRemove = mOfflineRosterBuffer->value(account->objectPath()).toStringList();
    mOfflineRosterBuffer->endGroup();

    CDTpAccountPtr accountWrapper = CDTpAccountPtr(new CDTpAccount(account, idsToRemove, newAccount, this));
    mAccounts.insert(account->objectPath(), accountWrapper);

    maybeStartOfflineOperations(accountWrapper);

    // Connect change notifications
    connect(accountWrapper.data(),
            SIGNAL(rosterChanged(CDTpAccountPtr)),
            SLOT(onRosterChanged(CDTpAccountPtr)));
    connect(accountWrapper.data(),
            SIGNAL(changed(CDTpAccountPtr, CDTpAccount::Changes)),
            mStorage,
            SLOT(updateAccount(CDTpAccountPtr, CDTpAccount::Changes)));
    connect(accountWrapper.data(),
            SIGNAL(rosterUpdated(CDTpAccountPtr,
                    const QList<CDTpContactPtr> &,
                    const QList<CDTpContactPtr> &)),
            mStorage,
            SLOT(syncAccountContacts(CDTpAccountPtr,
                    const QList<CDTpContactPtr> &,
                    const QList<CDTpContactPtr> &)));
    connect(accountWrapper.data(),
            SIGNAL(rosterContactChanged(CDTpContactPtr, CDTpContact::Changes)),
            mStorage,
            SLOT(updateContact(CDTpContactPtr, CDTpContact::Changes)));
    connect(accountWrapper.data(),
            SIGNAL(syncStarted(Tp::AccountPtr)),
            SLOT(onSyncStarted(Tp::AccountPtr)));
    connect(accountWrapper.data(),
            SIGNAL(syncEnded(Tp::AccountPtr, int, int)),
            SLOT(onSyncEnded(Tp::AccountPtr, int, int)));

    return accountWrapper;
}

void CDTpController::onSyncStarted(Tp::AccountPtr account)
{
    Q_EMIT importStarted(account->serviceName(), account->objectPath());
}

void CDTpController::onSyncEnded(Tp::AccountPtr account, int contactsAdded, int contactsRemoved)
{
    Q_EMIT importEnded(account->serviceName(), account->objectPath(),
        contactsAdded, contactsRemoved, 0);
}

void CDTpController::onRosterChanged(CDTpAccountPtr accountWrapper)
{
    mStorage->syncAccountContacts(accountWrapper);
    maybeStartOfflineOperations(accountWrapper);
}

void CDTpController::maybeStartOfflineOperations(CDTpAccountPtr accountWrapper)
{
    if (!accountWrapper->hasRoster()) {
        return;
    }

    Tp::AccountPtr account = accountWrapper->account();

    // Start removal operation
    mOfflineRosterBuffer->beginGroup(offlineRemovals);
    QStringList idsToRemove = mOfflineRosterBuffer->value(account->objectPath()).toStringList();
    mOfflineRosterBuffer->endGroup();
    if (!idsToRemove.isEmpty()) {
        CDTpRemovalOperation *op = new CDTpRemovalOperation(accountWrapper, idsToRemove);
        connect(op,
                SIGNAL(finished(Tp::PendingOperation *)),
                SLOT(onRemovalFinished(Tp::PendingOperation *)));
    }

    // Start invitation operation
    mOfflineRosterBuffer->beginGroup(offlineInvitations);
    QStringList idsToInvite = mOfflineRosterBuffer->value(account->objectPath()).toStringList();
    mOfflineRosterBuffer->endGroup();
    if (!idsToInvite.isEmpty()) {
        // FIXME: We should also save the localId for offline operations
        CDTpInvitationOperation *op = new CDTpInvitationOperation(mStorage, accountWrapper, idsToInvite, 0);
        connect(op,
                SIGNAL(finished(Tp::PendingOperation *)),
                SLOT(onInvitationFinished(Tp::PendingOperation *)));
    }
}

void CDTpController::inviteBuddies(const QString &accountPath, const QStringList &imIds)
{
    inviteBuddiesOnContact(accountPath, imIds, 0);
}

void CDTpController::inviteBuddiesOnContact(const QString &accountPath, const QStringList &imIds, uint localId)
{
    debug() << "InviteBuddies:" << accountPath << imIds.join(QLatin1String(", "));

    // Add ids to offlineInvitations, in case operation does not succeed now
    updateOfflineRosterBuffer(offlineInvitations, accountPath, imIds, QStringList());

    CDTpAccountPtr accountWrapper = mAccounts[accountPath];
    if (!accountWrapper) {
        debug() << "Account not found";
        return;
    }

    // Start invitation operation
    if (accountWrapper->hasRoster()) {
        CDTpInvitationOperation *op = new CDTpInvitationOperation(mStorage, accountWrapper, imIds, localId);
        connect(op,
                SIGNAL(finished(Tp::PendingOperation *)),
                SLOT(onInvitationFinished(Tp::PendingOperation *)));
    }
}

void CDTpController::onInvitationFinished(Tp::PendingOperation *op)
{
    // If an error happend, ids stay in the OfflineRosterBuffer and operation
    // will be retried next time account connects.
    if (op->isError()) {
        debug() << "Error" << op->errorName() << ":" << op->errorMessage();
        return;
    }

    CDTpInvitationOperation *iop = qobject_cast<CDTpInvitationOperation *>(op);
    debug() << "Contacts invited:" << iop->contactIds().join(QLatin1String(", "));

    CDTpAccountPtr accountWrapper = iop->accountWrapper();
    const QString accountPath = accountWrapper->account()->objectPath();
    updateOfflineRosterBuffer(offlineInvitations, accountPath, QStringList(), iop->contactIds());
}

void CDTpController::removeBuddies(const QString &accountPath, const QStringList &imIds)
{
    debug() << "RemoveBuddies:" << accountPath << imIds.join(QLatin1String(", "));

    // Add ids to offlineRemovals, in case it does not get removed right now from server
    QStringList currentList = updateOfflineRosterBuffer(offlineRemovals, accountPath, imIds, QStringList());

    CDTpAccountPtr accountWrapper = mAccounts[accountPath];
    if (!accountWrapper) {
        debug() << "Account not found";
        return;
    }

    // Remove ids from storage
    mStorage->removeAccountContacts(accountWrapper, imIds);

    // Add contact to account's avoid list
    accountWrapper->setContactsToAvoid(currentList);

    // Start removal operation
    if (accountWrapper->hasRoster()) {
        CDTpRemovalOperation *op = new CDTpRemovalOperation(accountWrapper, imIds);
        connect(op,
                SIGNAL(finished(Tp::PendingOperation *)),
                SLOT(onRemovalFinished(Tp::PendingOperation *)));
    }
}

void CDTpController::onRemovalFinished(Tp::PendingOperation *op)
{
    // If an error happend, ids stay in the OfflineRosterBuffer and operation
    // will be retried next time account connects.
    if (op->isError()) {
        debug() << "Error" << op->errorName() << ":" << op->errorMessage();
        return;
    }

    CDTpRemovalOperation *rop = qobject_cast<CDTpRemovalOperation *>(op);
    debug() << "Contacts removed from server:" << rop->contactIds().join(QLatin1String(", "));

    CDTpAccountPtr accountWrapper = rop->accountWrapper();
    const QString accountPath = accountWrapper->account()->objectPath();
    QStringList currentList = updateOfflineRosterBuffer(offlineRemovals, accountPath, QStringList(), rop->contactIds());

    // Update account's avoid list, in case they get added back
    accountWrapper->setContactsToAvoid(currentList);
}

QStringList CDTpController::updateOfflineRosterBuffer(const QString group, const QString accountPath,
        const QStringList idsToAdd, const QStringList idsToRemove)
{
    mOfflineRosterBuffer->beginGroup(group);
    QStringList currentList = mOfflineRosterBuffer->value(accountPath).toStringList();
    Q_FOREACH (const QString &id, idsToAdd) {
        if (!currentList.contains(id)) {
            currentList << id;
        }
    }
    Q_FOREACH (const QString id, idsToRemove) {
        currentList.removeOne(id);
    }
    if (currentList.isEmpty()) {
        mOfflineRosterBuffer->remove(accountPath);
    } else {
        mOfflineRosterBuffer->setValue(accountPath, currentList);
    }
    mOfflineRosterBuffer->endGroup();
    mOfflineRosterBuffer->sync();

    return currentList;
}

bool CDTpController::registerDBusObject()
{
    QDBusConnection connection = QDBusConnection::sessionBus();
    if (!connection.isConnected()) {
        warning() << "Could not connect to DBus:" << connection.lastError();
        return false;
    }

    if (!connection.registerObject(DBusObjectPath, this)) {
        warning() << "Could not register DBus object '/':" <<
            connection.lastError();
        return false;
    }
    return true;
}

CDTpRemovalOperation::CDTpRemovalOperation(CDTpAccountPtr accountWrapper,
        const QStringList &contactIds) : PendingOperation(accountWrapper),
        mContactIds(contactIds), mAccountWrapper(accountWrapper)
{
    debug() << "CDTpRemovalOperation: start";

    if (accountWrapper->account()->connection().isNull()) {
        // If the connection is null, we make up an error and emit it
        // setFinishedWithError takes care of going through the event loop
        setFinishedWithError(QString::fromLatin1("nullConnection"),
                             QString::fromLatin1("Account connection is null"));
        return;
    }

    Tp::ContactManagerPtr manager = accountWrapper->account()->connection()->contactManager();
    QList<Tp::ContactPtr> contactsToRemove;
    Q_FOREACH(const QString contactId, mContactIds) {
        Q_FOREACH(const Tp::ContactPtr tpcontact, manager->allKnownContacts()) {
            if (tpcontact->id() == contactId) {
                contactsToRemove << tpcontact;
            }
        }
    }

    Tp::PendingOperation *call = manager->removeContacts(contactsToRemove);
    connect(call,
            SIGNAL(finished(Tp::PendingOperation *)),
            SLOT(onContactsRemoved(Tp::PendingOperation *)));
}

void CDTpRemovalOperation::onContactsRemoved(Tp::PendingOperation *op)
{
    if (op->isError()) {
        setFinishedWithError(op->errorName(), op->errorMessage());
        return;
    }

    setFinished();
}

CDTpInvitationOperation::CDTpInvitationOperation(CDTpStorage *storage,
                                                 CDTpAccountPtr accountWrapper,
                                                 const QStringList &contactIds,
                                                 uint contactLocalId)
    : PendingOperation(accountWrapper)
    , mStorage(storage)
    , mContactIds(contactIds)
    , mAccountWrapper(accountWrapper)
    , mContactLocalId(contactLocalId)
{
    debug() << "CDTpInvitationOperation: start";

    if (accountWrapper->account()->connection().isNull()) {
        // If the connection is null, we make up an error and emit it
        // setFinishedWithError takes care of going through the event loop
        setFinishedWithError(QString::fromLatin1("nullConnection"),
                             QString::fromLatin1("Account connection is null"));
        return;
    }

    Tp::ContactManagerPtr manager = accountWrapper->account()->connection()->contactManager();
    Tp::PendingContacts *call = manager->contactsForIdentifiers(mContactIds);
    connect(call,
            SIGNAL(finished(Tp::PendingOperation *)),
            SLOT(onContactsRetrieved(Tp::PendingOperation *)));
}

void CDTpInvitationOperation::onContactsRetrieved(Tp::PendingOperation *op)
{
    if (op->isError()) {
        // We still create the IMAddress on the contact if the request fails, so
        // that user has a feedback
        if (mContactLocalId != 0) {
            mStorage->createAccountContacts(mAccountWrapper, mContactIds, mContactLocalId);
        }

        setFinishedWithError(op->errorName(), op->errorMessage());
        return;
    }

    Tp::PendingContacts *pcontacts = qobject_cast<Tp::PendingContacts *>(op);

    if (mContactLocalId != 0) {
        QStringList resolvedIds;

        foreach (const Tp::ContactPtr &c, pcontacts->contacts()) {
            resolvedIds.append(c->id());
        }

        // Also create "failed" IMAddresses for invalid identifiers
        foreach (const QString &id, pcontacts->invalidIdentifiers().keys()) {
            resolvedIds.append(id);
        }

        mStorage->createAccountContacts(mAccountWrapper, resolvedIds, mContactLocalId);
    }

    PendingOperation *call = pcontacts->manager()->requestPresenceSubscription(pcontacts->contacts());
    connect(call,
            SIGNAL(finished(Tp::PendingOperation *)),
            SLOT(onPresenceSubscriptionRequested(Tp::PendingOperation *)));
}

void CDTpInvitationOperation::onPresenceSubscriptionRequested(Tp::PendingOperation *op)
{
    if (op->isError()) {
        setFinishedWithError(op->errorName(), op->errorMessage());
        return;
    }

    setFinished();
}
