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

#include <TelepathyQt/AvatarData>
#include <TelepathyQt/ContactCapabilities>
#include <TelepathyQt/ContactManager>
#include <TelepathyQt/ConnectionCapabilities>

#include <qtcontacts-extensions.h>
#include <QContactOriginMetadata>

#include <QContact>
#include <QContactManager>
#include <QContactDetail>
#include <QContactDetailFilter>
#include <QContactIntersectionFilter>
#include <QContactRelationshipFilter>
#include <QContactUnionFilter>
#ifdef USING_QTPIM
#include <QContactIdFilter>
#else
#include <QContactLocalIdFilter>
#endif

#include <QContactAddress>
#include <QContactAvatar>
#include <QContactBirthday>
#include <QContactEmailAddress>
#include <QContactGender>
#include <QContactName>
#include <QContactNickname>
#include <QContactNote>
#include <QContactOnlineAccount>
#include <QContactOrganization>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactSyncTarget>
#include <QContactRelationship>
#include <QContactUrl>

#include "cdtpstorage.h"
#include "cdtpavatarupdate.h"
#include "cdtpplugin.h"
#include "debug.h"

#include <QElapsedTimer>

using namespace Contactsd;

// Uncomment for masses of debug output:
//#define DEBUG_OVERLOAD

// The longer a single batch takes to write, the longer we are locking out other
// writers (readers should be unaffected).  Using a semaphore write mutex, we should
// at least have FIFO semantics on lock release.
#define BATCH_STORE_SIZE 5

#ifdef USING_QTPIM
typedef QContactId ContactIdType;
typedef QList<QContactDetail::DetailType> DetailList;
#else
typedef QContactLocalId ContactIdType;
typedef QStringList DetailList;
#endif

namespace {

template<int N>
const QString &sourceLocation(const char *f)
{
    static const QString tmpl(QString::fromLatin1("%2:%1").arg(N));
    static const QString loc(tmpl.arg(QString::fromLatin1(f)));
    return loc;
}

#define SRC_LOC sourceLocation<__LINE__>(__PRETTY_FUNCTION__)

QString asString(bool f)
{
    return QLatin1String(f ? "true" : "false");
}

QString asString(const Tp::ContactInfoField &field, int i)
{
    if (i >= field.fieldValue.count()) {
        return QLatin1String("");
    }

    return field.fieldValue[i];
}

QStringList asStringList(const Tp::ContactInfoField &field, int i)
{
    QStringList rv;

    while (i < field.fieldValue.count()) {
        rv.append(field.fieldValue[i]);
        ++i;
    }

    return rv;
}

QString asString(CDTpContact::Info::Capability c)
{
    switch (c) {
        case CDTpContact::Info::TextChats:
            return QLatin1String("TextChats");
        case CDTpContact::Info::StreamedMediaCalls:
            return QLatin1String("StreamedMediaCalls");
        case CDTpContact::Info::StreamedMediaAudioCalls:
            return QLatin1String("StreamedMediaAudioCalls");
        case CDTpContact::Info::StreamedMediaAudioVideoCalls:
            return QLatin1String("StreamedMediaAudioVideoCalls");
        case CDTpContact::Info::UpgradingStreamMediaCalls:
            return QLatin1String("UpgradingStreamMediaCalls");
        case CDTpContact::Info::FileTransfers:
            return QLatin1String("FileTransfers");
        case CDTpContact::Info::StreamTubes:
            return QLatin1String("StreamTubes");
        case CDTpContact::Info::DBusTubes:
            return QLatin1String("DBusTubes");
        default:
            break;
    }

    return QString();
}

#ifdef USING_QTPIM
QString asString(const QContactId &id) { return id.toString(); }
#else
QString asString(QContactLocalId id) { return QString::number(id); }
#endif

#ifdef USING_QTPIM
QContactId apiId(const QContact &contact) { return contact.id(); }
#else
QContactLocalId apiId(const QContact &contact) { return contact.localId(); }
#endif

}

template<typename F>
QString stringValue(const QContactDetail &detail, F field)
{
#ifdef USING_QTPIM
    return detail.value<QString>(field);
#else
    return detail.value(field);
#endif
}

namespace {

const int UPDATE_TIMEOUT = 150; // ms
const int UPDATE_THRESHOLD = 50; // contacts

QContactManager *manager()
{
#ifdef USING_QTPIM
    // Temporary override until qtpim supports QTCONTACTS_MANAGER_OVERRIDE
    static QContactManager *manager = new QContactManager(QStringLiteral("org.nemomobile.contacts.sqlite"));
#else
    static QContactManager *manager = new QContactManager;
#endif
    return manager;
}

QContactDetailFilter matchTelepathyFilter()
{
    QContactDetailFilter filter;
#ifdef USING_QTPIM
    filter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
#else
    filter.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);
#endif
    filter.setValue(QLatin1String("telepathy"));
    filter.setMatchFlags(QContactFilter::MatchExactly);
    return filter;
}

template<typename T>
DetailList::value_type detailType()
{
#ifdef USING_QTPIM
    return T::Type;
#else
    return QString::fromLatin1(T::DefinitionName.latin1());
#endif
}

QContactFetchHint contactFetchHint(bool selfContact = false)
{
    QContactFetchHint hint;

    // Relationships are slow and unnecessary here:
    hint.setOptimizationHints(QContactFetchHint::NoRelationships |
                              QContactFetchHint::NoActionPreferences |
                              QContactFetchHint::NoBinaryBlobs);

    if (selfContact) {
        // For the self contact, we only care about accounts/presence/avatars
#ifdef USING_QTPIM
        hint.setDetailTypesHint(DetailList()
#else
        hint.setDetailDefinitionsHint(DetailList()
#endif
            << detailType<QContactOnlineAccount>()
            << detailType<QContactPresence>()
            << detailType<QContactAvatar>());
    }

    return hint;
}

ContactIdType selfContactLocalId()
{
    QContactManager *mgr(manager());

    // Check that there is a self contact
    QContactId selfId;
#ifdef USING_QTPIM
    selfId = mgr->selfContactId();
#else
    selfId.setLocalId(mgr->selfContactId());
#endif

    // Find the telepathy contact aggregated by the real self contact
    QContactRelationshipFilter relationshipFilter;
#ifdef USING_QTPIM
    relationshipFilter.setRelationshipType(QContactRelationship::Aggregates());
    QContact relatedContact;
    relatedContact.setId(selfId);
    relationshipFilter.setRelatedContact(relatedContact);
#else
    relationshipFilter.setRelationshipType(QContactRelationship::Aggregates);
    relationshipFilter.setRelatedContactId(selfId);
#endif
    relationshipFilter.setRelatedContactRole(QContactRelationship::First);

    QContactIntersectionFilter selfFilter;
    selfFilter << matchTelepathyFilter();
    selfFilter << relationshipFilter;

    QList<ContactIdType> selfContactIds = mgr->contactIds(selfFilter);
    if (selfContactIds.count() > 0) {
        if (selfContactIds.count() > 1) {
            warning() << "Invalid number of telepathy self contacts!" << selfContactIds.count();
        }
        return selfContactIds.first();
    }

    // Create a new self contact for telepathy
    debug() << "Creating self contact";
    QContact tpSelf;

    QContactSyncTarget syncTarget;
    syncTarget.setSyncTarget(QLatin1String("telepathy"));

    if (!tpSelf.saveDetail(&syncTarget)) {
        warning() << SRC_LOC << "Unable to add sync target to self contact";
    } else {
        if (!mgr->saveContact(&tpSelf)) {
            warning() << "Unable to save empty contact as self contact - error:" << mgr->error();
        } else {
            // Now connect our contact to the real self contact
            QContactRelationship relationship;
#ifdef USING_QTPIM
            relationship.setRelationshipType(QContactRelationship::Aggregates());
            relatedContact.setId(selfId);
            relationship.setFirst(relatedContact);
            relationship.setSecond(tpSelf);
#else
            relationship.setRelationshipType(QContactRelationship::Aggregates);
            relationship.setFirst(selfId);
            relationship.setSecond(tpSelf.id());
#endif

            if (!mgr->saveRelationship(&relationship)) {
                warning() << "Unable to save relationship for self contact - error:" << mgr->error();
                qFatal("Cannot proceed with invalid self contact!");
            }

            // Find the aggregate contact created by saving our self contact
#ifdef USING_QTPIM
            relationshipFilter.setRelationshipType(QContactRelationship::Aggregates());
            relatedContact.setId(tpSelf.id());
            relationshipFilter.setRelatedContact(relatedContact);
#else
            relationshipFilter.setRelationshipType(QContactRelationship::Aggregates);
            relationshipFilter.setRelatedContactId(tpSelf.id());
#endif
            relationshipFilter.setRelatedContactRole(QContactRelationship::Second);

            foreach (const QContact &aggregator, mgr->contacts(relationshipFilter)) {
                if (aggregator.id() == tpSelf.id())
                    continue;

                // Remove the relationship between these contacts (which removes the childless aggregate)
                QContactRelationship relationship;
#ifdef USING_QTPIM
                relationship.setRelationshipType(QContactRelationship::Aggregates());
                relationship.setFirst(aggregator);
                relationship.setSecond(tpSelf);
#else
                relationship.setRelationshipType(QContactRelationship::Aggregates);
                relationship.setFirst(aggregator.id());
                relationship.setSecond(tpSelf.id());
#endif

                if (!mgr->removeRelationship(relationship)) {
                    warning() << "Unable to remove relationship for self contact - error:" << mgr->error();
                }
            }

            return apiId(tpSelf);
        }
    }

    return ContactIdType();
}

QContact selfContact()
{
    static ContactIdType selfLocalId(selfContactLocalId());
    static QContactFetchHint hint(contactFetchHint(true));

    return manager()->contact(selfLocalId, hint);
}

template<typename Debug>
Debug output(Debug debug, const QContactDetail &detail)
{
#ifdef USING_QTPIM
    const QMap<int, QVariant> &values(detail.values());
    QMap<int, QVariant>::const_iterator it = values.constBegin(), end = values.constEnd();
#else
    const QVariantMap &values(detail.variantValues());
    QVariantMap::const_iterator it = values.constBegin(), end = values.constEnd();
#endif
    for ( ; it != end; ++it) {
        debug << "\n   -" << it.key() << ":" << it.value();
    }
    return debug;
}

#ifdef USING_QTPIM
QContactDetail::DetailType detailType(const QContactDetail &detail) { return detail.type(); }
#else
QString detailType(const QContactDetail &detail) { return detail.definitionName(); }
#endif

template<typename Debug>
Debug output(Debug debug, const QContact &contact)
{
    const QList<QContactDetail> &details(contact.details());
    foreach (const QContactDetail &detail, details) {
        debug << "\n  Detail:" << detailType(detail);
        output(debug, detail);
    }
    return debug;
}

bool storeContactDetail(QContact &contact, QContactDetail &detail, const QString &location)
{
#ifdef DEBUG_OVERLOAD
    debug() << "  Storing" << detailType(detail) << "from:" << location;
    output(debug(), detail);
#endif

    if (!contact.saveDetail(&detail)) {
        debug() << "  Failed storing" << detailType(detail) << "from:" << location;
#ifndef DEBUG_OVERLOAD
        output(debug(), detail);
#endif
        return false;
    }
    return true;
}

DetailList contactChangesList(CDTpContact::Changes changes)
{
    DetailList rv;

    if (changes & CDTpContact::Alias) {
        rv.append(detailType<QContactNickname>());
    }
    if (changes & CDTpContact::Presence) {
        rv.append(detailType<QContactPresence>());
    }
    if (changes & CDTpContact::Capabilities) {
        rv.append(detailType<QContactOnlineAccount>());
        rv.append(detailType<QContactOriginMetadata>());
    }
    if (changes & CDTpContact::Avatar) {
        rv.append(detailType<QContactAvatar>());
    }

    return rv;
}

bool storeContact(QContact &contact, const QString &location, CDTpContact::Changes changes = CDTpContact::All)
{
    QList<QContact> contacts;
    DetailList updates;

    const bool minimizedUpdate((changes != CDTpContact::All) && ((changes & CDTpContact::Information) == 0));
    if (minimizedUpdate) {
        contacts << contact;
        updates = contactChangesList(changes);
    }

#ifdef DEBUG_OVERLOAD
    debug() << "Storing contact" << asString(apiId(contact)) << "from:" << location;
    output(debug(), contact);
#endif

    if (minimizedUpdate) {
        if (!manager()->saveContacts(&contacts, contactChangesList(changes))) {
            warning() << "Failed minimized storing contact" << asString(apiId(contact)) << "from:" << location << "error:" << manager()->error();
#ifndef DEBUG_OVERLOAD
            output(debug(), contact);
#endif
            debug() << "Updates" << updates;
            return false;
        }
    } else {
        if (!manager()->saveContact(&contact)) {
            warning() << "Failed storing contact" << asString(apiId(contact)) << "from:" << location;
#ifndef DEBUG_OVERLOAD
            output(debug(), contact);
#endif
            return false;
        }
    }
    return true;
}

void updateContacts(const QString &location, QList<QContact> *saveList, QList<ContactIdType> *removeList, CDTpContact::Changes changes = CDTpContact::All)
{
    if (saveList && !saveList->isEmpty()) {
        const DetailList detailList(contactChangesList(changes));

        QElapsedTimer t;
        t.start();

        // Try to store contacts in batches
        int storedCount = 0;
        while (storedCount < saveList->count()) {
            QList<QContact> batch(saveList->mid(storedCount, BATCH_STORE_SIZE));
            storedCount += BATCH_STORE_SIZE;

            do {
                bool success;
                QMap<int, QContactManager::Error> errorMap;
                if (changes == CDTpContact::All) {
                    success = manager()->saveContacts(&batch, &errorMap);
                } else {
                    success = manager()->saveContacts(&batch, detailList, &errorMap);
                }
                if (success) {
                    // We could copy the updated contacts back into saveList here, but it doesn't seem warranted
                    break;
                }

                const int errorCount = errorMap.count();
                if (!errorCount) {
                    break;
                }

                // Remove the problematic contacts
                QList<int> indices = errorMap.keys();
                QList<int>::const_iterator begin = indices.begin(), it = begin + errorCount;
                do {
                    int errorIndex = (*--it);
                    const QContact &badContact(batch.at(errorIndex));
                    warning() << "Failed storing contact" << asString(apiId(badContact)) << "from:" << location;
                    output(debug(), badContact);
                    batch.removeAt(errorIndex);
                } while (it != begin);
            } while (true);
        }
        debug() << "Updated" << saveList->count() << "batched contacts - elapsed:" << t.elapsed();
    }

    if (removeList && !removeList->isEmpty()) {
        QElapsedTimer t;
        t.start();

        QList<ContactIdType>::iterator it = removeList->begin(), end = removeList->end();
        for ( ; it != end; ++it) {
            if (!manager()->removeContact(*it)) {
                warning() << "Unable to remove contact";
            }
        }
        debug() << "Removed" << removeList->count() << "individual contacts - elapsed:" << t.elapsed();
    }
}

QList<ContactIdType> findContactIdsForAccount(const QString &accountPath)
{
    QContactIntersectionFilter filter;
    filter << QContactOriginMetadata::matchGroupId(accountPath);
    filter << matchTelepathyFilter();
    return manager()->contactIds(filter);
}

QHash<QString, QContact> findExistingContacts(const QStringList &contactAddresses)
{
    static QContactFetchHint hint(contactFetchHint());

    QHash<QString, QContact> rv;

    // If there is a large number of contacts, do a two-step fetch
    const int maxDirectMatches = 10;
    if (contactAddresses.count() > maxDirectMatches) {
        QList<ContactIdType> ids;
        QSet<QString> addressSet(contactAddresses.toSet());

        // First fetch all telepathy contacts, ID data only
#ifdef USING_QTPIM
        hint.setDetailTypesHint(DetailList() << QContactOriginMetadata::Type);
#else
        hint.setDetailDefinitionsHint(DetailList() << QContactOriginMetadata::DefinitionName);
#endif

        foreach (const QContact &contact, manager()->contacts(matchTelepathyFilter(), QList<QContactSortOrder>(), hint)) {
            const QString &address = stringValue(contact.detail<QContactOriginMetadata>(), QContactOriginMetadata::FieldId);
            if (addressSet.contains(address)) {
                ids.append(apiId(contact));
            }
        }

#ifdef USING_QTPIM
        hint.setDetailTypesHint(DetailList());
#else
        hint.setDetailDefinitionsHint(DetailList());
#endif

        // Now fetch the details of the required contacts by ID
        foreach (const QContact &contact, manager()->contacts(ids, hint)) {
            rv.insert(stringValue(contact.detail<QContactOriginMetadata>(), QContactOriginMetadata::FieldId), contact);
        }
    } else {
        // Just query the ones we need
        QContactIntersectionFilter filter;
        filter << matchTelepathyFilter();

        QContactUnionFilter addressFilter;
        foreach (const QString &address, contactAddresses) {
            addressFilter << QContactOriginMetadata::matchId(address);
        }
        filter << addressFilter;

        foreach (const QContact &contact, manager()->contacts(filter, QList<QContactSortOrder>(), hint)) {
            rv.insert(stringValue(contact.detail<QContactOriginMetadata>(), QContactOriginMetadata::FieldId), contact);
        }
    }

    return rv;
}

QContact findExistingContact(const QString &contactAddress)
{
    static QContactFetchHint hint(contactFetchHint());

    QContactIntersectionFilter filter;
    filter << QContactOriginMetadata::matchId(contactAddress);
    filter << matchTelepathyFilter();

    foreach (const QContact &contact, manager()->contacts(filter, QList<QContactSortOrder>(), hint)) {
        // Return the first match we find (there should be only one)
        return contact;
    }

    debug() << "No matching contact:" << contactAddress;
    return QContact();
}

template<typename T>
T findLinkedDetail(const QContact &owner, const QContactDetail &link)
{
    const QString linkUri(link.detailUri());

    foreach (const T &detail, owner.details<T>()) {
        if (detail.linkedDetailUris().contains(linkUri)) {
            return detail;
        }
    }

    return T();
}

QContactPresence findPresenceForAccount(const QContact &owner, const QContactOnlineAccount &qcoa)
{
    return findLinkedDetail<QContactPresence>(owner, qcoa);
}

QContactAvatar findAvatarForAccount(const QContact &owner, const QContactOnlineAccount &qcoa)
{
    return findLinkedDetail<QContactAvatar>(owner, qcoa);
}

QString imAccount(Tp::AccountPtr account)
{
    return account->objectPath();
}

QString imAccount(CDTpAccountPtr accountWrapper)
{
    return imAccount(accountWrapper->account());
}

QString imAccount(CDTpContactPtr contactWrapper)
{
    return imAccount(contactWrapper->accountWrapper());
}

QString imAddress(const QString &accountPath, const QString &contactId = QString())
{
    static const QString tmpl = QString::fromLatin1("%1!%2");
    return tmpl.arg(accountPath, contactId.isEmpty() ? QLatin1String("self") : contactId);
}

QString imAddress(Tp::AccountPtr account, const QString &contactId = QString())
{
    return imAddress(imAccount(account), contactId);
}

QString imAddress(CDTpAccountPtr accountWrapper, const QString &contactId = QString())
{
    return imAddress(accountWrapper->account(), contactId);
}

QString imAddress(CDTpContactPtr contactWrapper)
{
    return imAddress(contactWrapper->accountWrapper(), contactWrapper->contact()->id());
}

QString imPresence(const QString &accountPath, const QString &contactId = QString())
{
    static const QString tmpl = QString::fromLatin1("%1!%2!presence");
    return tmpl.arg(accountPath, contactId.isEmpty() ? QLatin1String("self") : contactId);
}

QString imPresence(Tp::AccountPtr account, const QString &contactId = QString())
{
    return imPresence(imAccount(account), contactId);
}

QString imPresence(CDTpAccountPtr accountWrapper, const QString &contactId = QString())
{
    return imPresence(accountWrapper->account(), contactId);
}

QString imPresence(CDTpContactPtr contactWrapper)
{
    return imPresence(contactWrapper->accountWrapper(), contactWrapper->contact()->id());
}

QContactPresence::PresenceState qContactPresenceState(Tp::ConnectionPresenceType presenceType)
{
    switch (presenceType) {
    case Tp::ConnectionPresenceTypeOffline:
        return QContactPresence::PresenceOffline;

    case Tp::ConnectionPresenceTypeAvailable:
        return QContactPresence::PresenceAvailable;

    case Tp::ConnectionPresenceTypeAway:
        return QContactPresence::PresenceAway;

    case Tp::ConnectionPresenceTypeExtendedAway:
        return QContactPresence::PresenceExtendedAway;

    case Tp::ConnectionPresenceTypeHidden:
        return QContactPresence::PresenceHidden;

    case Tp::ConnectionPresenceTypeBusy:
        return QContactPresence::PresenceBusy;

    case Tp::ConnectionPresenceTypeUnknown:
    case Tp::ConnectionPresenceTypeUnset:
    case Tp::ConnectionPresenceTypeError:
        break;

    default:
        warning() << "Unknown telepathy presence status" << presenceType;
        break;
    }

    return QContactPresence::PresenceUnknown;
}

bool isOnlinePresence(Tp::ConnectionPresenceType presenceType, Tp::AccountPtr account)
{
    switch (presenceType) {
    // Why??
    case Tp::ConnectionPresenceTypeOffline:
        return account->protocolName() == QLatin1String("skype");

    case Tp::ConnectionPresenceTypeUnset:
    case Tp::ConnectionPresenceTypeUnknown:
    case Tp::ConnectionPresenceTypeError:
        return false;

    default:
        break;
    }

    return true;
}

QStringList currentCapabilites(const Tp::CapabilitiesBase &capabilities, Tp::ConnectionPresenceType presenceType, Tp::AccountPtr account)
{
    QStringList current;

    if (capabilities.textChats()) {
        current << asString(CDTpContact::Info::TextChats);
    }

    if (isOnlinePresence(presenceType, account)) {
        if (capabilities.streamedMediaCalls()) {
            current << asString(CDTpContact::Info::StreamedMediaCalls);
        }
        if (capabilities.streamedMediaAudioCalls()) {
            current << asString(CDTpContact::Info::StreamedMediaAudioCalls);
        }
        if (capabilities.streamedMediaVideoCalls()) {
            current << asString(CDTpContact::Info::StreamedMediaAudioVideoCalls);
        }
        if (capabilities.upgradingStreamedMediaCalls()) {
            current << asString(CDTpContact::Info::UpgradingStreamMediaCalls);
        }
        if (capabilities.fileTransfers()) {
            current << asString(CDTpContact::Info::FileTransfers);
        }
    }

    return current;
}

void updateContactAvatars(QContact &contact, const QString &defaultAvatarPath, const QString &largeAvatarPath, const QContactOnlineAccount &qcoa)
{
    QContactAvatar defaultAvatar;
    QContactAvatar largeAvatar;

    foreach (const QContactAvatar &detail, contact.details<QContactAvatar>()) {
#ifdef USING_QTPIM
        const QList<int> &contexts(detail.contexts());
#else
        const QStringList &contexts(detail.contexts());
#endif
        if (contexts.contains(QContactDetail__ContextDefault)) {
            defaultAvatar = detail;
        } else if (contexts.contains(QContactDetail__ContextLarge)) {
            largeAvatar = detail;
        }
    }

    if (defaultAvatarPath.isEmpty()) {
        if (!defaultAvatar.isEmpty()) {
            if (!contact.removeDetail(&defaultAvatar)) {
                warning() << SRC_LOC << "Unable to remove default avatar from contact:" << contact.id();
            }
        }
    } else {
        defaultAvatar.setImageUrl(QUrl::fromLocalFile(defaultAvatarPath));
        defaultAvatar.setContexts(QContactDetail__ContextDefault);
        defaultAvatar.setLinkedDetailUris(qcoa.detailUri());
        if (!storeContactDetail(contact, defaultAvatar, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save default avatar for contact:" << contact.id();
        }
    }

    if (largeAvatarPath.isEmpty()) {
        if (!largeAvatar.isEmpty()) {
            if (!contact.removeDetail(&largeAvatar)) {
                warning() << SRC_LOC << "Unable to remove large avatar from contact:" << contact.id();
            }
        }
    } else {
        largeAvatar.setImageUrl(QUrl::fromLocalFile(largeAvatarPath));
        largeAvatar.setContexts(QContactDetail__ContextLarge);
        largeAvatar.setLinkedDetailUris(qcoa.detailUri());
        if (!storeContactDetail(contact, largeAvatar, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save large avatar for contact:" << contact.id();
        }
    }
}

QString saveAccountAvatar(CDTpAccountPtr accountWrapper)
{
    const Tp::Avatar &avatar = accountWrapper->account()->avatar();

    if (avatar.avatarData.isEmpty()) {
        return QString();
    }

    const QString avatarDirPath(CDTpPlugin::cacheFileName(QString::fromLatin1("avatars/account")));

    QDir storageDir(avatarDirPath);
    if (!storageDir.exists() && !storageDir.mkpath(QString::fromLatin1("."))) {
        qWarning() << "Unable to create contacts avatar storage directory:" << storageDir.path();
        return QString();
    }

    QString filename = QString::fromLatin1(QCryptographicHash::hash(avatar.avatarData, QCryptographicHash::Md5).toHex());
    filename = avatarDirPath + QDir::separator() + filename + QString::fromLatin1(".jpg");

    QFile avatarFile(filename);
    if (!avatarFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        warning() << "Unable to save account avatar: error opening avatar file" << filename << "for writing";
        return QString();
    }

    avatarFile.write(avatar.avatarData);
    avatarFile.close();

    return filename;
}

void updateFacebookAvatar(QNetworkAccessManager &network, CDTpContactPtr contactWrapper, const QString &facebookId, const QString &avatarType)
{
    const QUrl avatarUrl(QLatin1String("http://graph.facebook.com/") % facebookId %
                         QLatin1String("/picture?type=") % avatarType);

    // CDTpAvatarUpdate keeps a weak reference to CDTpContact, since the contact is
    // also its parent. If we'd pass a CDTpContactPtr to the update, it'd keep a ref that
    // keeps the CDTpContact alive. Then, if the update is the last object to hold
    // a ref to the contact, the refcount of the contact will go to 0 when the update
    // dtor is called (for example from deleteLater). At this point, the update will
    // already be being deleted, but the dtor of CDTpContact will try to delete the
    // update a second time, causing a double free.
    QObject *const update = new CDTpAvatarUpdate(network.get(QNetworkRequest(avatarUrl)),
                                                 contactWrapper.data(),
                                                 avatarType,
                                                 contactWrapper.data());

    QObject::connect(update, SIGNAL(finished()), update, SLOT(deleteLater()));
}

void updateSocialAvatars(QNetworkAccessManager &network, CDTpContactPtr contactWrapper)
{
    if (network.networkAccessible() == QNetworkAccessManager::NotAccessible) {
        return;
    }

    QRegExp facebookIdPattern(QLatin1String("-(\\d+)@chat\\.facebook\\.com"));

    if (not facebookIdPattern.exactMatch(contactWrapper->contact()->id())) {
        return; // only supporting Facebook avatars right now
    }

    const QString socialId = facebookIdPattern.cap(1);

    updateFacebookAvatar(network, contactWrapper, socialId, CDTpAvatarUpdate::Large);
    updateFacebookAvatar(network, contactWrapper, socialId, CDTpAvatarUpdate::Square);
}

CDTpContact::Changes updateAccountDetails(QContact &self, QContactOnlineAccount &qcoa, QContactPresence &presence, CDTpAccountPtr accountWrapper, CDTpAccount::Changes changes)
{
    CDTpContact::Changes selfChanges = 0;

    const QString accountPath(imAccount(accountWrapper));
    debug() << SRC_LOC << "Update account" << accountPath;

    Tp::AccountPtr account = accountWrapper->account();

    if (changes & CDTpAccount::Presence) {
        Tp::Presence tpPresence(account->currentPresence());

        presence.setPresenceState(qContactPresenceState(tpPresence.type()));
        presence.setTimestamp(QDateTime::currentDateTime());
        presence.setCustomMessage(tpPresence.statusMessage());

        selfChanges |= CDTpContact::Presence;
    }
    if ((changes & CDTpAccount::Nickname) ||
        (changes & CDTpAccount::DisplayName)) {
        const QString nickname(account->nickname());
        const QString displayName(account->displayName());

        // Nickname takes precedence (according to test expectations...)
        if (!nickname.isEmpty()) {
            presence.setNickname(nickname);
        } else if (!displayName.isEmpty()) {
            presence.setNickname(displayName);
        } else {
            presence.setNickname(QString());
        }

        selfChanges |= CDTpContact::Presence;
    }
    if (changes & CDTpAccount::Avatar) {
        const QString avatarPath(saveAccountAvatar(accountWrapper));

        QContactAvatar avatar(findAvatarForAccount(self, qcoa));
        if (!avatar.isEmpty()) {
            avatar.setLinkedDetailUris(qcoa.detailUri());
        }

        if (avatarPath.isEmpty()) {
            if (!avatar.isEmpty()) {
                if (!self.removeDetail(&avatar)) {
                    warning() << SRC_LOC << "Unable to remove avatar for account:" << accountPath;
                }
            }
        } else {
            avatar.setImageUrl(QUrl::fromLocalFile(avatarPath));
            avatar.setContexts(QContactDetail__ContextDefault);

            if (!storeContactDetail(self, avatar, SRC_LOC)) {
                warning() << SRC_LOC << "Unable to save avatar for account:" << accountPath;
            }
        }

        selfChanges |= CDTpContact::Avatar;
    }

    if (selfChanges & CDTpContact::Presence) {
        if (!storeContactDetail(self, presence, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save presence for self account:" << accountPath;
        }

        // Presence changes also imply potential capabilities changes
        selfChanges |= CDTpContact::Capabilities;
    }

    if (selfChanges & CDTpContact::Capabilities) {
        // The account has changed
        if (!storeContactDetail(self, qcoa, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save details for self account:" << accountPath;
        }
    }

    return selfChanges;
}

template<typename DetailType>
void deleteContactDetails(QContact &existing)
{
    foreach (DetailType detail, existing.details<DetailType>()) {
        if (!existing.removeDetail(&detail)) {
            warning() << SRC_LOC << "Unable to remove obsolete detail:" << detail.detailUri();
        }
    }
}

#ifdef USING_QTPIM
typedef QHash<QString, int> Dictionary;
#else
typedef QHash<QString, QString> Dictionary;
#endif

Dictionary initPhoneTypes()
{
    Dictionary types;

    types.insert(QLatin1String("bbsl"), QContactPhoneNumber::SubTypeBulletinBoardSystem);
    types.insert(QLatin1String("car"), QContactPhoneNumber::SubTypeCar);
    types.insert(QLatin1String("cell"), QContactPhoneNumber::SubTypeMobile);
    types.insert(QLatin1String("fax"), QContactPhoneNumber::SubTypeFax);
    types.insert(QLatin1String("modem"), QContactPhoneNumber::SubTypeModem);
    types.insert(QLatin1String("pager"), QContactPhoneNumber::SubTypePager);
    types.insert(QLatin1String("video"), QContactPhoneNumber::SubTypeVideo);
    types.insert(QLatin1String("voice"), QContactPhoneNumber::SubTypeVoice);
    // Not sure about these types:
    types.insert(QLatin1String("isdn"), QContactPhoneNumber::SubTypeLandline);
    types.insert(QLatin1String("pcs"), QContactPhoneNumber::SubTypeLandline);

    return types;
}

const Dictionary &phoneTypes()
{
    static Dictionary types(initPhoneTypes());
    return types;
}

Dictionary initAddressTypes()
{
    Dictionary types;

    types.insert(QLatin1String("dom"), QContactAddress::SubTypeDomestic);
    types.insert(QLatin1String("intl"), QContactAddress::SubTypeInternational);
    types.insert(QLatin1String("parcel"), QContactAddress::SubTypeParcel);
    types.insert(QLatin1String("postal"), QContactAddress::SubTypePostal);

    return types;
}

const Dictionary &addressTypes()
{
    static Dictionary types(initAddressTypes());
    return types;
}

Dictionary initGenderTypes()
{
    Dictionary types;

    types.insert(QLatin1String("f"), QContactGender::GenderFemale);
    types.insert(QLatin1String("female"), QContactGender::GenderFemale);
    types.insert(QLatin1String("m"), QContactGender::GenderMale);
    types.insert(QLatin1String("male"), QContactGender::GenderMale);

    return types;
}

const Dictionary &genderTypes()
{
    static Dictionary types(initGenderTypes());
    return types;
}

#ifdef USING_QTPIM
Dictionary initProtocolTypes()
{
    Dictionary types;

    types.insert(QLatin1String("aim"), QContactOnlineAccount::ProtocolAim);
    types.insert(QLatin1String("icq"), QContactOnlineAccount::ProtocolIcq);
    types.insert(QLatin1String("irc"), QContactOnlineAccount::ProtocolIrc);
    types.insert(QLatin1String("jabber"), QContactOnlineAccount::ProtocolJabber);
    types.insert(QLatin1String("msn"), QContactOnlineAccount::ProtocolMsn);
    types.insert(QLatin1String("qq"), QContactOnlineAccount::ProtocolQq);
    types.insert(QLatin1String("skype"), QContactOnlineAccount::ProtocolSkype);
    types.insert(QLatin1String("yahoo"), QContactOnlineAccount::ProtocolYahoo);

    return types;
}

QContactOnlineAccount::Protocol protocolType(const QString &protocol)
{
    static Dictionary types(initProtocolTypes());

    Dictionary::const_iterator it = types.find(protocol.toLower());
    if (it != types.constEnd()) {
        return static_cast<QContactOnlineAccount::Protocol>(*it);
    }

    return QContactOnlineAccount::ProtocolUnknown;
}
#else
const QString &protocolType(const QString &protocol)
{
    return protocol;
}
#endif

void updateContactDetails(QNetworkAccessManager &network, QContact &existing, CDTpContactPtr contactWrapper, CDTpContact::Changes changes)
{
    const QString contactAddress(imAddress(contactWrapper));
    debug() << "Update contact" << contactAddress;

    Tp::ContactPtr contact = contactWrapper->contact();

    // Apply changes
    if (changes & CDTpContact::Alias) {
        QContactNickname nickname = existing.detail<QContactNickname>();
        nickname.setNickname(contact->alias().trimmed());

        if (!storeContactDetail(existing, nickname, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save alias to contact for:" << contactAddress;
        }

        // The alias is also reflected in the presence
        changes |= CDTpContact::Presence;
    }
    if (changes & CDTpContact::Presence) {
        Tp::Presence tpPresence(contact->presence());

        QContactPresence presence = existing.detail<QContactPresence>();
        presence.setPresenceState(qContactPresenceState(tpPresence.type()));
        presence.setTimestamp(QDateTime::currentDateTime());
        presence.setCustomMessage(tpPresence.statusMessage());
        presence.setNickname(contact->alias().trimmed());

        if (!storeContactDetail(existing, presence, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save presence to contact for:" << contactAddress;
        }

        // Since we use static account capabilities as fallback, each presence also implies
        // a capability change. This doesn't fit the pure school of Telepathy, but we really
        // should not drop the static caps fallback at this stage.
        changes |= CDTpContact::Capabilities;
    }
    if (changes & CDTpContact::Capabilities) {
        QContactOnlineAccount qcoa = existing.detail<QContactOnlineAccount>();
        qcoa.setCapabilities(currentCapabilites(contact->capabilities(), contact->presence().type(), contactWrapper->accountWrapper()->account()));

        if (!storeContactDetail(existing, qcoa, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save capabilities to contact for:" << contactAddress;
        }
    }
    if (changes & CDTpContact::Information) {
        if (contactWrapper->isInformationKnown()) {
            // Delete any existing info we have for this contact
            deleteContactDetails<QContactAddress>(existing);
            deleteContactDetails<QContactBirthday>(existing);
            deleteContactDetails<QContactEmailAddress>(existing);
            deleteContactDetails<QContactGender>(existing);
            deleteContactDetails<QContactName>(existing);
            deleteContactDetails<QContactNickname>(existing);
            deleteContactDetails<QContactNote>(existing);
            deleteContactDetails<QContactOrganization>(existing);
            deleteContactDetails<QContactPhoneNumber>(existing);
            deleteContactDetails<QContactUrl>(existing);

            Tp::ContactInfoFieldList listContactInfo = contact->infoFields().allFields();
            if (listContactInfo.count() != 0) {
#ifdef USING_QTPIM
                const int defaultContext(QContactDetail::ContextOther);
                const int homeContext(QContactDetail::ContextHome);
                const int workContext(QContactDetail::ContextWork);
#else
                const QLatin1String defaultContext("Other");
                const QLatin1String homeContext("Home");
                const QLatin1String workContext("Work");
#endif

                QContactOrganization organizationDetail;
                QContactName nameDetail;

                // Add any information reported by telepathy
                foreach (const Tp::ContactInfoField &field, listContactInfo) {
                    if (field.fieldValue.count() == 0) {
                        continue;
                    }

                    // Extract field types
                    QStringList subTypes;
#ifdef USING_QTPIM
                    int detailContext = -1;
                    const int invalidContext = -1;
#else
                    QString detailContext;
                    const QString invalidContext;
#endif

                    foreach (const QString &param, field.parameters) {
                        if (!param.startsWith(QLatin1String("type="))) {
                            continue;
                        }
                        const QString type = param.mid(5);
                        if (type == QLatin1String("home")) {
                            detailContext = homeContext;
                        } else if (type == QLatin1String("work")) {
                            detailContext = workContext;
                        } else if (!subTypes.contains(type)){
                            subTypes << type;
                        }
                    }

                    if (field.fieldName == QLatin1String("tel")) {
#ifdef USING_QTPIM
                        QList<int> selectedTypes;
#else
                        QStringList selectedTypes;
#endif
                        foreach (const QString &type, subTypes) {
                            Dictionary::const_iterator it = phoneTypes().find(type.toLower());
                            if (it != phoneTypes().constEnd()) {
                                selectedTypes.append(*it);
                            }
                        }
                        if (selectedTypes.isEmpty()) {
                            // Assume landline
                            selectedTypes.append(QContactPhoneNumber::SubTypeLandline);
                        }

                        QContactPhoneNumber phoneNumberDetail;
                        phoneNumberDetail.setContexts(detailContext == invalidContext ? defaultContext : detailContext);
                        phoneNumberDetail.setNumber(asString(field, 0));
                        phoneNumberDetail.setSubTypes(selectedTypes);

                        if (!storeContactDetail(existing, phoneNumberDetail, SRC_LOC)) {
                            warning() << SRC_LOC << "Unable to save phone number to contact";
                        }
                    } else if (field.fieldName == QLatin1String("adr")) {
#ifdef USING_QTPIM
                        QList<int> selectedTypes;
#else
                        QStringList selectedTypes;
#endif
                        foreach (const QString &type, subTypes) {
                            Dictionary::const_iterator it = addressTypes().find(type.toLower());
                            if (it != addressTypes().constEnd()) {
                                selectedTypes.append(*it);
                            }
                        }

                        // QContactAddress does not support extended street address, so combine the fields
                        QString streetAddress(asString(field, 1) + QLatin1Char('\n') + asString(field, 2));

                        QContactAddress addressDetail;
                        if (detailContext != invalidContext) {
                            addressDetail.setContexts(detailContext);
                        }
                        if (selectedTypes.isEmpty()) {
                            addressDetail.setSubTypes(selectedTypes);
                        }
                        addressDetail.setPostOfficeBox(asString(field, 0));
                        addressDetail.setStreet(streetAddress);
                        addressDetail.setLocality(asString(field, 3));
                        addressDetail.setRegion(asString(field, 4));
                        addressDetail.setPostcode(asString(field, 5));
                        addressDetail.setCountry(asString(field, 6));

                        if (!storeContactDetail(existing, addressDetail, SRC_LOC)) {
                            warning() << SRC_LOC << "Unable to save address to contact";
                        }
                    } else if (field.fieldName == QLatin1String("email")) {
                        QContactEmailAddress emailDetail;
                        if (detailContext != invalidContext) {
                            emailDetail.setContexts(detailContext);
                        }
                        emailDetail.setEmailAddress(asString(field, 0));

                        if (!storeContactDetail(existing, emailDetail, SRC_LOC)) {
                            warning() << SRC_LOC << "Unable to save email address to contact";
                        }
                    } else if (field.fieldName == QLatin1String("url")) {
                        QContactUrl urlDetail;
                        if (detailContext != invalidContext) {
                            urlDetail.setContexts(detailContext);
                        }
                        urlDetail.setUrl(asString(field, 0));

                        if (!storeContactDetail(existing, urlDetail, SRC_LOC)) {
                            warning() << SRC_LOC << "Unable to save URL to contact";
                        }
                    } else if (field.fieldName == QLatin1String("title")) {
                        organizationDetail.setTitle(asString(field, 0));
                        if (detailContext != invalidContext) {
                            organizationDetail.setContexts(detailContext);
                        }
                    } else if (field.fieldName == QLatin1String("role")) {
                        organizationDetail.setRole(asString(field, 0));
                        if (detailContext != invalidContext) {
                            organizationDetail.setContexts(detailContext);
                        }
                    } else if (field.fieldName == QLatin1String("org")) {
                        organizationDetail.setName(asString(field, 0));
                        organizationDetail.setDepartment(asStringList(field, 1));
                        if (detailContext != invalidContext) {
                            organizationDetail.setContexts(detailContext);
                        }

                        if (!storeContactDetail(existing, organizationDetail, SRC_LOC)) {
                            warning() << SRC_LOC << "Unable to save organization to contact";
                        }

                        // Clear out the stored details
                        organizationDetail = QContactOrganization();
                    } else if (field.fieldName == QLatin1String("n")) {
                        if (detailContext != invalidContext) {
                            nameDetail.setContexts(detailContext);
                        }
                        nameDetail.setLastName(asString(field, 0));
                        nameDetail.setFirstName(asString(field, 1));
                        nameDetail.setMiddleName(asString(field, 2));
                        nameDetail.setPrefix(asString(field, 3));
                        nameDetail.setSuffix(asString(field, 4));
                    } else if (field.fieldName == QLatin1String("fn")) {
                        const QString fn(asString(field, 0));
                        if (!fn.isEmpty()) {
                            if (detailContext != invalidContext) {
                                nameDetail.setContexts(detailContext);
                            }
#ifdef USING_QTPIM
                            nameDetail.setValue(QContactName__FieldCustomLabel, fn);
#else
                            nameDetail.setCustomLabel(fn);
#endif
                        }
                    } else if (field.fieldName == QLatin1String("nickname")) {
                        const QString nickname(asString(field, 0));
                        if (!nickname.isEmpty()) {
                            QContactNickname nicknameDetail;
                            nicknameDetail.setNickname(nickname);
                            if (detailContext != invalidContext) {
                                nicknameDetail.setContexts(detailContext);
                            }

                            if (!storeContactDetail(existing, nicknameDetail, SRC_LOC)) {
                                warning() << SRC_LOC << "Unable to save nickname to contact";
                            }

                            // Use the nickname as the customLabel if we have no 'fn' data
#ifdef USING_QTPIM
                            if (stringValue(nameDetail, QContactName__FieldCustomLabel).isEmpty()) {
                                nameDetail.setValue(QContactName__FieldCustomLabel, nickname);
                            }
#else
                            if (nameDetail.customLabel().isEmpty()) {
                                nameDetail.setCustomLabel(nickname);
                            }
#endif
                        }
                    } else if (field.fieldName == QLatin1String("note") ||
                             field.fieldName == QLatin1String("desc")) {
                        QContactNote noteDetail;
                        if (detailContext != invalidContext) {
                            noteDetail.setContexts(detailContext);
                        }
                        noteDetail.setNote(asString(field, 0));

                        if (!storeContactDetail(existing, noteDetail, SRC_LOC)) {
                            warning() << SRC_LOC << "Unable to save note to contact";
                        }
                    } else if (field.fieldName == QLatin1String("bday")) {
                        /* FIXME: support more date format for compatibility */
                        const QString dateText(asString(field, 0));

                        QDate date = QDate::fromString(dateText, QLatin1String("yyyy-MM-dd"));
                        if (!date.isValid()) {
                            date = QDate::fromString(dateText, QLatin1String("yyyyMMdd"));
                        }
                        if (!date.isValid()) {
                            date = QDate::fromString(dateText, Qt::ISODate);
                        }

                        if (date.isValid()) {
                            QContactBirthday birthdayDetail;
                            birthdayDetail.setDate(date);

                            if (!storeContactDetail(existing, birthdayDetail, SRC_LOC)) {
                                warning() << SRC_LOC << "Unable to save birthday to contact";
                            }
                        } else {
                            debug() << "Unsupported bday format:" << field.fieldValue[0];
                        }
                    } else if (field.fieldName == QLatin1String("x-gender")) {
                        const QString type(field.fieldValue.at(0));

                        Dictionary::const_iterator it = genderTypes().find(type.toLower());
                        if (it != addressTypes().constEnd()) {
                            QContactGender genderDetail;
#ifdef USING_QTPIM
                            genderDetail.setGender(static_cast<QContactGender::GenderField>(*it));
#else
                            genderDetail.setGender(*it);
#endif

                            if (!storeContactDetail(existing, genderDetail, SRC_LOC)) {
                                warning() << SRC_LOC << "Unable to save gender to contact";
                            }
                        } else {
                            debug() << "Unsupported gender type:" << type;
                        }
                    } else {
                        debug() << "Unsupported contact info field" << field.fieldName;
                    }
                }

                if (!nameDetail.isEmpty()) {
                    if (!storeContactDetail(existing, nameDetail, SRC_LOC)) {
                        warning() << SRC_LOC << "Unable to save name details to contact";
                    }
                }
            }
        }
    }
    if (changes & CDTpContact::Avatar) {
        QString defaultAvatarPath = contact->avatarData().fileName;
        if (defaultAvatarPath.isEmpty()) {
            defaultAvatarPath = contactWrapper->squareAvatarPath();
        }

        QContactOnlineAccount qcoa = existing.detail<QContactOnlineAccount>();
        updateContactAvatars(existing, defaultAvatarPath, contactWrapper->largeAvatarPath(), qcoa);
    }
    if (changes & CDTpContact::DefaultAvatar) {
        updateSocialAvatars(network, contactWrapper);
    }
    /* What is this about?
    if (changes & CDTpContact::Authorization) {
        debug() << "  authorization changed";
        g.addPattern(imAddress, nco::imAddressAuthStatusFrom::resource(),
                presenceState(contact->subscriptionState()));
        g.addPattern(imAddress, nco::imAddressAuthStatusTo::resource(),
                presenceState(contact->publishState()));
    }
    */
}

template<typename T, typename R>
QList<R> forEachItem(const QList<T> &list, R (*f)(const T&))
{
    QList<R> rv;
    rv.reserve(list.count());

    foreach (const T &item, list) {
        const R& r = f(item);
        rv.append(r);
    }

    return rv;
}

QString extractAccountPath(const CDTpAccountPtr &accountWrapper)
{
    return imAccount(accountWrapper);
}

void addIconPath(QContactOnlineAccount &qcoa, Tp::AccountPtr account)
{
    QString iconName = account->iconName().trimmed();

    // Ignore any default value returned by telepathy
    if (!iconName.startsWith(QLatin1String("im-"))) {
        qcoa.setValue(QContactOnlineAccount__FieldAccountIconPath, iconName);
    }
}

} // namespace


CDTpStorage::CDTpStorage(QObject *parent) : QObject(parent),
    mUpdateRunning(false)
{
    mUpdateTimer.setInterval(UPDATE_TIMEOUT);
    mUpdateTimer.setSingleShot(true);
    connect(&mUpdateTimer, SIGNAL(timeout()), SLOT(onUpdateQueueTimeout()));
}

CDTpStorage::~CDTpStorage()
{
}

void CDTpStorage::addNewAccount(QContact &self, CDTpAccountPtr accountWrapper)
{
    Tp::AccountPtr account = accountWrapper->account();

    const QString accountPath(imAccount(account));
    const QString accountAddress(imAddress(account));
    const QString accountPresence(imPresence(account));

    debug() << "Creating new self account - account:" << accountPath << "address:" << accountAddress;

    // Create a new QCOA for this account
    QContactOnlineAccount newAccount;

    newAccount.setDetailUri(accountAddress);
    newAccount.setLinkedDetailUris(accountPresence);

    newAccount.setValue(QContactOnlineAccount__FieldAccountPath, accountPath);
    newAccount.setValue(QContactOnlineAccount__FieldEnabled, asString(account->isEnabled()));
    newAccount.setAccountUri(account->normalizedName());
    newAccount.setProtocol(protocolType(account->protocolName()));
    newAccount.setServiceProvider(account->serviceName());

    addIconPath(newAccount, account);

    // Add the new account to the self contact
    if (!storeContactDetail(self, newAccount, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to add account to self contact for:" << accountPath;
        return;
    }

    // Create a presence detail for this account
    QContactPresence presence;

    presence.setDetailUri(accountPresence);
    presence.setLinkedDetailUris(accountAddress);
    presence.setPresenceState(qContactPresenceState(Tp::ConnectionPresenceTypeUnknown));

    if (!storeContactDetail(self, presence, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to add presence to self contact for:" << accountPath;
        return;
    }

    // Store any information from the account
    CDTpContact::Changes selfChanges = updateAccountDetails(self, newAccount, presence, accountWrapper, CDTpAccount::All);

    storeContact(self, SRC_LOC, selfChanges);
}

void CDTpStorage::removeExistingAccount(QContact &self, QContactOnlineAccount &existing)
{
    const QString accountPath(stringValue(existing, QContactOnlineAccount__FieldAccountPath));

    // Remove any contacts derived from this account
    if (!manager()->removeContacts(findContactIdsForAccount(accountPath))) {
        warning() << SRC_LOC << "Unable to remove linked contacts for account:" << accountPath << "error:" << manager()->error();
    }

    // Remove any details linked from the account
    QStringList linkedUris(existing.linkedDetailUris());

    foreach (QContactDetail detail, self.details()) {
        const QString &uri(detail.detailUri());
        if (!uri.isEmpty()) {
            if (linkedUris.contains(uri)) {
                if (!self.removeDetail(&detail)) {
                    warning() << SRC_LOC << "Unable to remove linked detail with URI:" << uri;
                }
            }
        }
    }

    if (!self.removeDetail(&existing)) {
        warning() << SRC_LOC << "Unable to remove obsolete account:" << accountPath;
    }
}

bool CDTpStorage::initializeNewContact(QContact &newContact, CDTpAccountPtr accountWrapper, const QString &contactId)
{
    Tp::AccountPtr account = accountWrapper->account();

    const QString accountPath(imAccount(account));
    const QString contactAddress(imAddress(account, contactId));
    const QString contactPresence(imPresence(account, contactId));

    debug() << "Creating new contact - address:" << contactAddress;

    // This contact is synchronized with telepathy
    QContactSyncTarget syncTarget;
    syncTarget.setSyncTarget(QLatin1String("telepathy"));
    if (!storeContactDetail(newContact, syncTarget, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to add sync target to contact:" << contactAddress;
        return false;
    }

    // Create a metadata field to link the contact with the telepathy data
    QContactOriginMetadata metadata;
    metadata.setId(contactAddress);
    metadata.setGroupId(imAccount(account));
    metadata.setEnabled(true);
    if (!storeContactDetail(newContact, metadata, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to add metadata to contact:" << contactAddress;
        return false;
    }

    // Create a new QCOA for this contact
    QContactOnlineAccount newAccount;

    newAccount.setDetailUri(contactAddress);
    newAccount.setLinkedDetailUris(contactPresence);

    newAccount.setValue(QContactOnlineAccount__FieldAccountPath, accountPath);
    newAccount.setValue(QContactOnlineAccount__FieldEnabled, asString(true));
    newAccount.setAccountUri(contactId);
    newAccount.setProtocol(protocolType(account->protocolName()));
    newAccount.setServiceProvider(account->serviceName());

    addIconPath(newAccount, account);

    // Add the new account to the contact
    if (!storeContactDetail(newContact, newAccount, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to save account to contact for:" << contactAddress;
        return false;
    }

    // Create a presence detail for this contact
    QContactPresence presence;

    presence.setDetailUri(contactPresence);
    presence.setLinkedDetailUris(contactAddress);
    presence.setPresenceState(qContactPresenceState(Tp::ConnectionPresenceTypeUnknown));

    if (!storeContactDetail(newContact, presence, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to save presence to contact for:" << contactAddress;
        return false;
    }
    return true;
}

void CDTpStorage::updateContactChanges(CDTpContactPtr contactWrapper, CDTpContact::Changes changes)
{
    QList<QContact> saveList;
    QList<ContactIdType> removeList;

    QContact existing = findExistingContact(imAddress(contactWrapper));
    updateContactChanges(contactWrapper, changes, existing, &saveList, &removeList);

    updateContacts(SRC_LOC, &saveList, &removeList);
}

void CDTpStorage::updateContactChanges(CDTpContactPtr contactWrapper, CDTpContact::Changes changes, QContact &existing, QList<QContact> *saveList, QList<ContactIdType> *removeList)
{
    const QString accountPath(imAccount(contactWrapper));
    const QString contactAddress(imAddress(contactWrapper));

    if (changes & CDTpContact::Deleted) {
        // This contact has been deleted
        if (!existing.isEmpty()) {
            removeList->append(apiId(existing));
        }
    } else {
        if (existing.isEmpty()) {
            if (!initializeNewContact(existing, contactWrapper->accountWrapper(), contactWrapper->contact()->id())) {
                warning() << SRC_LOC << "Unable to create contact for account:" << accountPath << contactAddress;
                return;
            }
        }

        updateContactDetails(mNetwork, existing, contactWrapper, changes);

        saveList->append(existing);
    }
}

void CDTpStorage::updateAccountChanges(QContact &self, QContactOnlineAccount &qcoa, CDTpAccountPtr accountWrapper, CDTpAccount::Changes changes)
{
    Tp::AccountPtr account = accountWrapper->account();

    const QString accountPath(imAccount(account));
    const QString accountAddress(imAddress(account));

    debug() << "Synchronizing self account - account:" << accountPath << "address:" << accountAddress;

    QContactPresence presence(findPresenceForAccount(self, qcoa));
    if (presence.isEmpty()) {
        warning() << SRC_LOC << "Unable to find presence to match account:" << accountPath;
    }
    CDTpContact::Changes selfChanges = updateAccountDetails(self, qcoa, presence, accountWrapper, changes);

    if (!storeContact(self, SRC_LOC, selfChanges)) {
        warning() << SRC_LOC << "Unable to save self contact - error:" << manager()->error();
    }

    if (account->isEnabled() && accountWrapper->hasRoster()) {
        QHash<QString, CDTpContact::Changes> allChanges;

        // Update all contacts reported in the roster changes of this account
        const QHash<QString, CDTpContact::Changes> changes = accountWrapper->rosterChanges();
        QHash<QString, CDTpContact::Changes>::ConstIterator it = changes.constBegin(), end = changes.constEnd();
        for ( ; it != end; ++it) {
            const QString address = imAddress(accountPath, it.key());

            // We always update contact presence since this method is called after a presence change
            allChanges.insert(address, it.value() | CDTpContact::Presence);
        }

        QStringList contactAddresses;
        foreach (CDTpContactPtr contactWrapper, accountWrapper->contacts()) {
            const QString address = imAddress(accountPath, contactWrapper->contact()->id());
            contactAddresses.append(address);
        }

        // Retrieve the existing contacts in a single batch
        QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

        QList<QContact> saveList;
        QList<ContactIdType> removeList;

        foreach (CDTpContactPtr contactWrapper, accountWrapper->contacts()) {
            const QString address = imAddress(accountPath, contactWrapper->contact()->id());

            QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
            if (existing == existingContacts.end()) {
                warning() << SRC_LOC << "No contact found for address:" << address;
                existing = existingContacts.insert(address, QContact());
            }

            QHash<QString, CDTpContact::Changes>::Iterator changes = allChanges.find(address);
            if (changes == allChanges.end()) {
                warning() << SRC_LOC << "No changes found for contact:" << address;
                continue;
            }

            // If we got a contact without avatar in the roster, and the original
            // had an avatar, then ignore the avatar update (some contact managers
            // send the initial roster with the avatar missing)
            // Contact updates that have a null avatar will clear the avatar though
            if (*changes & CDTpContact::DefaultAvatar) {
                if (*changes != CDTpContact::Added
                  && contactWrapper->contact()->avatarData().fileName.isEmpty()) {
                    *changes ^= CDTpContact::DefaultAvatar;
                }
            }

            updateContactChanges(contactWrapper, *changes, *existing, &saveList, &removeList);
        }

        updateContacts(SRC_LOC, &saveList, &removeList);
    } else {
        QList<QContact> saveList;

        // Set presence to unknown for all contacts of this account
        foreach (const ContactIdType &contactId, findContactIdsForAccount(accountPath)) {
            QContact existing = manager()->contact(contactId);

            QContactPresence presence = existing.detail<QContactPresence>();
            presence.setPresenceState(qContactPresenceState(Tp::ConnectionPresenceTypeUnknown));
            presence.setTimestamp(QDateTime::currentDateTime());

            if (!storeContactDetail(existing, presence, SRC_LOC)) {
                warning() << SRC_LOC << "Unable to save unknown presence to contact for:" << contactId;
            }

            // Also reset the capabilities
            QContactOnlineAccount qcoa = existing.detail<QContactOnlineAccount>();
            qcoa.setCapabilities(currentCapabilites(account->capabilities(), Tp::ConnectionPresenceTypeUnknown, account));

            if (!storeContactDetail(existing, qcoa, SRC_LOC)) {
                warning() << SRC_LOC << "Unable to save capabilities to contact for:" << contactId;
            }

            if (!account->isEnabled()) {
                // Mark the contact as un-enabled also
                QContactOriginMetadata metadata = existing.detail<QContactOriginMetadata>();
                metadata.setEnabled(false);

                if (!storeContactDetail(existing, metadata, SRC_LOC)) {
                    warning() << SRC_LOC << "Unable to un-enable contact for:" << contactId;
                }
            }

            saveList.append(existing);
        }

        updateContacts(SRC_LOC, &saveList, 0, CDTpContact::Presence | CDTpContact::Capabilities);
    }
}

void CDTpStorage::syncAccounts(const QList<CDTpAccountPtr> &accounts)
{
    QContact self(selfContact());
    if (self.isEmpty()) {
        warning() << SRC_LOC << "Unable to retrieve self contact - error:" << manager()->error();
        return;
    }

    // Find the list of paths for the accounts we now have
    QStringList accountPaths = forEachItem(accounts, extractAccountPath);
    
    QSet<int> existingIndices;
    QSet<QString> removalPaths;

    foreach (QContactOnlineAccount existingAccount, self.details<QContactOnlineAccount>()) {
        const QString existingPath(stringValue(existingAccount, QContactOnlineAccount__FieldAccountPath));
        if (existingPath.isEmpty()) {
            warning() << SRC_LOC << "No path for existing account:" << existingPath;
            continue;
        }

        int index = accountPaths.indexOf(existingPath);
        if (index != -1) {
            existingIndices.insert(index);
            updateAccountChanges(self, existingAccount, accounts.at(index), CDTpAccount::All);
        } else {
            debug() << SRC_LOC << "Remove obsolete account:" << existingPath;

            // This account is no longer valid
            removalPaths.insert(existingPath);
        }
    }

    // Remove invalid accounts
    foreach (QContactOnlineAccount existingAccount, self.details<QContactOnlineAccount>()) {
        const QString existingPath(stringValue(existingAccount, QContactOnlineAccount__FieldAccountPath));
        if (removalPaths.contains(existingPath)) {
            removeExistingAccount(self, existingAccount);
        }
    }

    // Add any previously unknown accounts
    for (int i = 0; i < accounts.length(); ++i) {
        if (!existingIndices.contains(i)) {
            addNewAccount(self, accounts.at(i));
        }
    }

    storeContact(self, SRC_LOC);
}

void CDTpStorage::createAccount(CDTpAccountPtr accountWrapper)
{
    QContact self(selfContact());
    if (self.isEmpty()) {
        warning() << SRC_LOC << "Unable to retrieve self contact:" << manager()->error();
        return;
    }

    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Create account:" << accountPath;

    // Ensure this account does not already exist
    foreach (const QContactOnlineAccount &existingAccount, self.details<QContactOnlineAccount>()) {
        const QString existingPath(stringValue(existingAccount, QContactOnlineAccount__FieldAccountPath));
        if (existingPath == accountPath) {
            warning() << SRC_LOC << "Path already exists for create account:" << existingPath;
            return;
        }
    }

    // Add any previously unknown accounts
    addNewAccount(self, accountWrapper);

    QStringList contactAddresses;
    foreach (CDTpContactPtr contactWrapper, accountWrapper->contacts()) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());
        contactAddresses.append(address);
    }

    // Retrieve the existing contacts in a single batch
    QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

    QList<QContact> saveList;
    QList<ContactIdType> removeList;

    // Add any contacts already present for this account
    foreach (CDTpContactPtr contactWrapper, accountWrapper->contacts()) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            existing = existingContacts.insert(address, QContact());
        }

        updateContactChanges(contactWrapper, CDTpContact::All, *existing, &saveList, &removeList);
    }

    updateContacts(SRC_LOC, &saveList, &removeList);
}

void CDTpStorage::updateAccount(CDTpAccountPtr accountWrapper, CDTpAccount::Changes changes)
{
    QContact self(selfContact());
    if (self.isEmpty()) {
        warning() << SRC_LOC << "Unable to retrieve self contact:" << manager()->error();
        return;
    }

    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Update account:" << accountPath;

    foreach (QContactOnlineAccount existingAccount, self.details<QContactOnlineAccount>()) {
        const QString existingPath(stringValue(existingAccount, QContactOnlineAccount__FieldAccountPath));
        if (existingPath == accountPath) {
            updateAccountChanges(self, existingAccount, accountWrapper, changes);
            return;
        }
    }

    warning() << SRC_LOC << "Account not found for update account:" << accountPath;
}

void CDTpStorage::removeAccount(CDTpAccountPtr accountWrapper)
{
    cancelQueuedUpdates(accountWrapper->contacts());

    QContact self(selfContact());
    if (self.isEmpty()) {
        warning() << SRC_LOC << "Unable to retrieve self contact:" << manager()->error();
        return;
    }

    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Remove account:" << accountPath;

    foreach (QContactOnlineAccount existingAccount, self.details<QContactOnlineAccount>()) {
        const QString existingPath(stringValue(existingAccount, QContactOnlineAccount__FieldAccountPath));
        if (existingPath == accountPath) {
            removeExistingAccount(self, existingAccount);

            storeContact(self, SRC_LOC);
            return;
        }
    }

    warning() << SRC_LOC << "Account not found for remove account:" << accountPath;
}

// This is called when account goes online/offline
void CDTpStorage::syncAccountContacts(CDTpAccountPtr accountWrapper)
{
    QContact self(selfContact());
    if (self.isEmpty()) {
        warning() << SRC_LOC << "Unable to retrieve self contact:" << manager()->error();
        return;
    }

    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Sync contacts account:" << accountPath;

    foreach (QContactOnlineAccount existingAccount, self.details<QContactOnlineAccount>()) {
        const QString existingPath(stringValue(existingAccount, QContactOnlineAccount__FieldAccountPath));
        if (existingPath == accountPath) {
            updateAccountChanges(self, existingAccount, accountWrapper, CDTpAccount::Enabled);
            return;
        }
    }

    warning() << SRC_LOC << "Account not found for sync account:" << accountPath;
}

void CDTpStorage::syncAccountContacts(CDTpAccountPtr accountWrapper, const QList<CDTpContactPtr> &contactsAdded, const QList<CDTpContactPtr> &contactsRemoved)
{
    const QString accountPath(imAccount(accountWrapper));

    QStringList contactAddresses;
    foreach (const CDTpContactPtr &contactWrapper, contactsAdded) {
        // This contact must be for the specified account
        if (imAccount(contactWrapper) != accountPath) {
            warning() << SRC_LOC << "Unable to add contact from wrong account:" << imAccount(contactWrapper) << accountPath;
            continue;
        }

        const QString address = imAddress(accountPath, contactWrapper->contact()->id());
        contactAddresses.append(address);
    }
    foreach (const CDTpContactPtr &contactWrapper, contactsRemoved) {
        if (imAccount(contactWrapper) != accountPath) {
            warning() << SRC_LOC << "Unable to remove contact from wrong account:" << imAccount(contactWrapper) << accountPath;
            continue;
        }

        const QString address = imAddress(accountPath, contactWrapper->contact()->id());
        contactAddresses.append(address);
    }

    // Retrieve the existing contacts in a single batch
    QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

    QList<QContact> saveList;
    QList<ContactIdType> removeList;

    foreach (const CDTpContactPtr &contactWrapper, contactsAdded) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            existing = existingContacts.insert(address, QContact());
        }

        updateContactChanges(contactWrapper, CDTpContact::Added | CDTpContact::Information, *existing, &saveList, &removeList);
    }
    foreach (const CDTpContactPtr &contactWrapper, contactsRemoved) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            continue;
        }

        updateContactChanges(contactWrapper, CDTpContact::Deleted, *existing, &saveList, &removeList);
    }

    updateContacts(SRC_LOC, &saveList, &removeList);
}

void CDTpStorage::createAccountContacts(CDTpAccountPtr accountWrapper, const QStringList &imIds, uint localId)
{
    Q_UNUSED(localId) // ???

    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Create contacts account:" << accountPath;

    QList<QContact> saveList;

    foreach (const QString &id, imIds) {
        QContact newContact;
        if (!initializeNewContact(newContact, accountWrapper, id)) {
            warning() << SRC_LOC << "Unable to create contact for account:" << accountPath << id;
        } else {
            saveList.append(newContact);
        }
    }

    updateContacts(SRC_LOC, &saveList, 0);
}

/* Use this only in offline mode - use syncAccountContacts in online mode */
void CDTpStorage::removeAccountContacts(CDTpAccountPtr accountWrapper, const QStringList &contactIds)
{
    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Remove contacts account:" << accountPath;

    QStringList imAddressList;
    foreach (const QString &id, contactIds) {
        imAddressList.append(imAddress(accountPath, id));
    }

    QList<ContactIdType> removeIds;

    // Find any contacts matching the supplied ID list
    foreach (const QContact &existing, manager()->contacts(findContactIdsForAccount(accountPath))) {
        QContactOriginMetadata metadata = existing.detail<QContactOriginMetadata>();
        if (imAddressList.contains(metadata.id())) {
            removeIds.append(apiId(existing));
        }
    }

    if (!manager()->removeContacts(removeIds)) {
        warning() << SRC_LOC << "Unable to remove contacts for account:" << accountPath << "error:" << manager()->error();
    }
}

void CDTpStorage::updateContact(CDTpContactPtr contactWrapper, CDTpContact::Changes changes)
{
    mUpdateQueue[contactWrapper] |= changes;

    if (!mUpdateRunning) {
        // Only update IM contacts in tracker after queuing 50 contacts or after
        // not receiving an update notifiction for 150 ms. This dramatically reduces
        // system but also keeps update latency within acceptable bounds.
        if (!mUpdateTimer.isActive() || mUpdateQueue.count() < UPDATE_THRESHOLD) {
            mUpdateTimer.start();
        }
    }
}

void CDTpStorage::onUpdateQueueTimeout()
{
    debug() << "Update" << mUpdateQueue.count() << "contacts";

    QStringList contactAddresses;

    QHash<CDTpContactPtr, CDTpContact::Changes>::const_iterator it = mUpdateQueue.constBegin(), end = mUpdateQueue.constEnd();
    for ( ; it != end; ++it) {
        CDTpContactPtr contactWrapper = it.key();
        contactAddresses.append(imAddress(contactWrapper));
    }

    // Retrieve the existing contacts in a single batch
    QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

    QList<QContact> saveList;
    QList<ContactIdType> removeList;

    for (it = mUpdateQueue.constBegin(); it != end; ++it) {
        CDTpContactPtr contactWrapper = it.key();

        // Skip the contact in case its account was deleted before this function
        // was invoked
        if (contactWrapper->accountWrapper().isNull()) {
            continue;
        }
        if (!contactWrapper->isVisible()) {
            continue;
        }

        const QString address(imAddress(contactWrapper));
        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            existing = existingContacts.insert(address, QContact());
        }

        updateContactChanges(contactWrapper, it.value(), *existing, &saveList, &removeList);
    }

    mUpdateQueue.clear();

    updateContacts(SRC_LOC, &saveList, &removeList);
}

void CDTpStorage::cancelQueuedUpdates(const QList<CDTpContactPtr> &contacts)
{
    foreach (const CDTpContactPtr &contactWrapper, contacts) {
        mUpdateQueue.remove(contactWrapper);
    }
}

// Instantiate the QContactOriginMetadata functions
#include <qcontactoriginmetadata_impl.h>
