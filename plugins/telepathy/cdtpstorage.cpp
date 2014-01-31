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
#include "util.h"

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

const int UPDATE_TIMEOUT = 250; // ms
const int UPDATE_MAXIMUM_TIMEOUT = 2000; // ms

QContactManager *manager()
{
    QMap<QString, QString> parameters;
    parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("false"));

    static QContactManager *manager = new QContactManager(QStringLiteral("org.nemomobile.contacts.sqlite"), parameters);
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

    if ((changes & CDTpContact::Information) == 0) {
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
    }

    return rv;
}

bool storeContact(QContact &contact, const QString &location, CDTpContact::Changes changes = CDTpContact::All)
{
    const DetailList updates = contactChangesList(changes);
    const bool minimizedUpdate(!updates.isEmpty());

#ifdef DEBUG_OVERLOAD
    debug() << "Storing contact" << asString(apiId(contact)) << "from:" << location;
    output(debug(), contact);
#endif

    if (minimizedUpdate) {
        QList<QContact> contacts;
        contacts << contact;
        if (!manager()->saveContacts(&contacts, updates)) {
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

void appendContactChange(CDTpStorage::ContactChangeSet *saveSet, const QContact &contact, CDTpContact::Changes changes)
{
    if (changes != 0) {
        if (changes & CDTpContact::Information) {
            // All changes including Information will be full stores, so group them all together
            changes = CDTpContact::All;
        }
        (*saveSet)[changes].append(contact);
    }
}

void updateContacts(const QString &location, CDTpStorage::ContactChangeSet *saveSet, QList<ContactIdType> *removeList)
{
    if (saveSet && !saveSet->isEmpty()) {
        // Each element of the save set is a list of contacts with the same set of changes
        CDTpStorage::ContactChangeSet::iterator sit = saveSet->begin(), send = saveSet->end();
        for ( ; sit != send; ++sit) {
            CDTpContact::Changes changes = sit.key();
            QList<QContact> *saveList = &(sit.value());

            if (saveList && !saveList->isEmpty()) {
                // Restrict the update to only modify the detail types that have changed for these contacts
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
                        if (detailList.isEmpty()) {
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
                            warning() << "Failed storing contact" << asString(apiId(badContact)) << "from:" << location << "error:" << errorMap.value(errorIndex);
                            output(debug(), badContact);
                            batch.removeAt(errorIndex);
                        } while (it != begin);
                    } while (true);
                }
                debug() << "Updated" << saveList->count() << "batched contacts - elapsed:" << t.elapsed() << detailList;
            }
        }
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

QHash<QString, QContact> findExistingContacts(const QSet<QString> &contactAddresses)
{
    return findExistingContacts(contactAddresses.toList());
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
                                                 QString::fromLatin1("%1-picture.jpg").arg(facebookId),
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

    // Ignore the square avatar, we only need the large one
    updateFacebookAvatar(network, contactWrapper, socialId, CDTpAvatarUpdate::Large);
}

CDTpContact::Changes updateAccountDetails(QContact &self, QContactOnlineAccount &qcoa, QContactPresence &presence, CDTpAccountPtr accountWrapper, CDTpAccount::Changes changes)
{
    CDTpContact::Changes selfChanges = 0;

    const QString accountPath(imAccount(accountWrapper));
    debug() << SRC_LOC << "Update account" << accountPath;

    Tp::AccountPtr account = accountWrapper->account();

    if (changes & CDTpAccount::Presence) {
        Tp::Presence tpPresence(account->currentPresence());

        QContactPresence::PresenceState newState(qContactPresenceState(tpPresence.type()));
        const QString newMessage(tpPresence.statusMessage());

        if ((presence.presenceState() != newState) || (presence.customMessage() != newMessage)) {
            presence.setPresenceState(newState);
            presence.setCustomMessage(newMessage);
            presence.setTimestamp(QDateTime::currentDateTime());

            selfChanges |= CDTpContact::Presence;
        }
    }
    if (changes & CDTpAccount::Nickname) {
        const QString nickname(account->nickname());

        if (presence.nickname() != nickname) {
            presence.setNickname(nickname);
            selfChanges |= CDTpContact::Alias;
        }
    }
    if (changes & CDTpAccount::DisplayName) {
        const QString displayName(account->displayName());

        if (qcoa.value(QContactOnlineAccount__FieldAccountDisplayName) != displayName) {
            qcoa.setValue(QContactOnlineAccount__FieldAccountDisplayName, displayName);
            selfChanges |= CDTpContact::Capabilities;
        }
    }
    if (changes & CDTpAccount::StorageInfo) {
        const QString providerDisplayName(accountWrapper->storageInfo().value(QLatin1String("providerDisplayName")).toString());

        if (qcoa.value(QContactOnlineAccount__FieldServiceProviderDisplayName) != providerDisplayName) {
            qcoa.setValue(QContactOnlineAccount__FieldServiceProviderDisplayName, providerDisplayName);
            selfChanges |= CDTpContact::Capabilities;
        }
    }
    if (changes & CDTpAccount::Avatar) {
        const QString avatarPath(saveAccountAvatar(accountWrapper));

        QContactAvatar avatar(findAvatarForAccount(self, qcoa));

        if (avatarPath.isEmpty()) {
            if (!avatar.isEmpty()) {
                if (!self.removeDetail(&avatar)) {
                    warning() << SRC_LOC << "Unable to remove avatar for account:" << accountPath;
                }

                selfChanges |= CDTpContact::Avatar;
            }
        } else {
            QUrl avatarUrl(QUrl::fromLocalFile(avatarPath));
            if (avatarUrl != avatar.imageUrl()) {
                avatar.setImageUrl(avatarUrl);
                avatar.setLinkedDetailUris(qcoa.detailUri());

                if (!storeContactDetail(self, avatar, SRC_LOC)) {
                    warning() << SRC_LOC << "Unable to save avatar for account:" << accountPath;
                }

                selfChanges |= CDTpContact::Avatar;
            }
        }
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

template<typename F1, typename F2>
void replaceNameDetail(F1 getter, F2 setter, QContactName *nameDetail, const QString &value)
{
    if (!value.isEmpty()) {
        (nameDetail->*setter)(value);
    } else {
        // If there is an existing value, remove it
        QString existing((nameDetail->*getter)());
        if (!existing.isEmpty()) {
            (nameDetail->*setter)(value);
        }
    }
}



template<typename T, typename F>
bool detailListsDiffer(const QList<T> &lhs, const QList<T> &rhs, F detailsDiffer)
{
    if (lhs.count() != rhs.count())
        return true;

    typename QList<T>::const_iterator lit = lhs.constBegin(), lend = lhs.constEnd();
    for (typename QList<T>::const_iterator rit = rhs.constBegin(); lit != lend; ++lit, ++rit) {
        if (detailsDiffer(*lit, *rit)) {
            return true;
        }
    }

    return false;
}

template<typename T>
bool replaceDetails(QContact &contact, QList<T> &details, const QString &address, const QString &location)
{
    deleteContactDetails<T>(contact);

    foreach (T detail, details) {
        if (!storeContactDetail(contact, detail, location)) {
            warning() << SRC_LOC << "Unable to save detail to contact:" << address;
        }
    }

    return true;
}

template<typename T>
bool replaceDetails(QContact &contact, T &detail, const QString &address, const QString &location)
{
    return replaceDetails(contact, QList<T>() << detail, address, location);
}

CDTpContact::Changes updateContactDetails(QNetworkAccessManager &network, QContact &existing, CDTpContactPtr contactWrapper, CDTpContact::Changes changes)
{
    const QString contactAddress(imAddress(contactWrapper));
    debug() << "Update contact" << contactAddress;

    Tp::ContactPtr contact = contactWrapper->contact();

    CDTpContact::Changes contactChanges;

    // Apply changes
    if (changes & CDTpContact::Alias) {
        QContactNickname nickname = existing.detail<QContactNickname>();

        const QString newNickname(contact->alias().trimmed());
        if (nickname.nickname() != newNickname) {
            nickname.setNickname(newNickname);

            if (!storeContactDetail(existing, nickname, SRC_LOC)) {
                warning() << SRC_LOC << "Unable to save alias to contact for:" << contactAddress;
            }

            contactChanges |= CDTpContact::Alias;
        }

        // The alias is also reflected in the presence
        changes |= CDTpContact::Presence;
    }
    if (changes & CDTpContact::Presence) {
        Tp::Presence tpPresence(contact->presence());

        QContactPresence presence = existing.detail<QContactPresence>();

        const QContactPresence::PresenceState newState(qContactPresenceState(tpPresence.type()));
        const QString newMessage(tpPresence.statusMessage());
        const QString newNickname(contact->alias().trimmed());

        if (presence.presenceState() != newState || presence.customMessage() != newMessage || presence.nickname() != newNickname) {
            presence.setPresenceState(newState);
            presence.setCustomMessage(newMessage);
            presence.setNickname(newNickname);
            presence.setTimestamp(QDateTime::currentDateTime());

            if (!storeContactDetail(existing, presence, SRC_LOC)) {
                warning() << SRC_LOC << "Unable to save presence to contact for:" << contactAddress;
            }

            contactChanges |= CDTpContact::Presence;
        }

        // Since we use static account capabilities as fallback, each presence also implies
        // a capability change. This doesn't fit the pure school of Telepathy, but we really
        // should not drop the static caps fallback at this stage.
        changes |= CDTpContact::Capabilities;
    }
    if (changes & CDTpContact::Capabilities) {
        QContactOnlineAccount qcoa = existing.detail<QContactOnlineAccount>();
        CDTpAccountPtr accountWrapper = contactWrapper->accountWrapper();
        const QString providerDisplayName(accountWrapper->storageInfo().value(QLatin1String("providerDisplayName")).toString());
        const QStringList newCapabilities(currentCapabilites(contact->capabilities(), contact->presence().type(), contactWrapper->accountWrapper()->account()));

        if (qcoa.capabilities() != newCapabilities ||
            qcoa.value(QContactOnlineAccount__FieldAccountDisplayName) != accountWrapper->account()->displayName() ||
            qcoa.value(QContactOnlineAccount__FieldServiceProviderDisplayName) != providerDisplayName)
        {
            qcoa.setCapabilities(newCapabilities);
            qcoa.setValue(QContactOnlineAccount__FieldAccountDisplayName, accountWrapper->account()->displayName());
            qcoa.setValue(QContactOnlineAccount__FieldServiceProviderDisplayName, providerDisplayName);

            if (!storeContactDetail(existing, qcoa, SRC_LOC)) {
                warning() << SRC_LOC << "Unable to save capabilities to contact for:" << contactAddress;
            }

            contactChanges |= CDTpContact::Capabilities;
        }
    }
    if (changes & CDTpContact::Information) {
        if (contactWrapper->isInformationKnown()) {
            // Extract the current information state from the info fields
            QList<QContactAddress> newAddresses;
            QContactBirthday newBirthday;
            QList<QContactEmailAddress> newEmailAddresses;
            QContactGender newGender;
            QContactName newName;
            QList<QContactNickname> newNicknames;
            QList<QContactNote> newNotes;
            QList<QContactOrganization> newOrganizations;
            QList<QContactPhoneNumber> newPhoneNumbers;
            QList<QContactUrl> newUrls;

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
                QString formattedName;
                bool structuredName = false;

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

                        newPhoneNumbers.append(phoneNumberDetail);
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

                        newAddresses.append(addressDetail);
                    } else if (field.fieldName == QLatin1String("email")) {
                        QContactEmailAddress emailDetail;
                        if (detailContext != invalidContext) {
                            emailDetail.setContexts(detailContext);
                        }
                        emailDetail.setEmailAddress(asString(field, 0));

                        newEmailAddresses.append(emailDetail);
                    } else if (field.fieldName == QLatin1String("url")) {
                        QContactUrl urlDetail;
                        if (detailContext != invalidContext) {
                            urlDetail.setContexts(detailContext);
                        }
                        urlDetail.setUrl(asString(field, 0));

                        newUrls.append(urlDetail);
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

                        newOrganizations.append(organizationDetail);

                        // Clear out the stored details
                        organizationDetail = QContactOrganization();
                    } else if (field.fieldName == QLatin1String("n")) {
                        if (detailContext != invalidContext) {
                            nameDetail.setContexts(detailContext);
                        }

                        replaceNameDetail(&QContactName::lastName, &QContactName::setLastName, &nameDetail, asString(field, 0));
                        replaceNameDetail(&QContactName::firstName, &QContactName::setFirstName, &nameDetail, asString(field, 1));
                        replaceNameDetail(&QContactName::middleName, &QContactName::setMiddleName, &nameDetail, asString(field, 2));
                        replaceNameDetail(&QContactName::prefix, &QContactName::setPrefix, &nameDetail, asString(field, 3));
                        replaceNameDetail(&QContactName::suffix, &QContactName::setSuffix, &nameDetail, asString(field, 4));

                        structuredName = true;
                    } else if (field.fieldName == QLatin1String("fn")) {
                        const QString fn(asString(field, 0));
                        if (!fn.isEmpty()) {
                            if (detailContext != invalidContext) {
                                nameDetail.setContexts(detailContext);
                            }
                            formattedName = fn;
                        }
                    } else if (field.fieldName == QLatin1String("nickname")) {
                        const QString nickname(asString(field, 0));
                        if (!nickname.isEmpty()) {
                            QContactNickname nicknameDetail;
                            nicknameDetail.setNickname(nickname);
                            if (detailContext != invalidContext) {
                                nicknameDetail.setContexts(detailContext);
                            }

                            newNicknames.append(nicknameDetail);

                            // Use the nickname as the customLabel if we have no 'fn' data
                            if (formattedName.isEmpty()) {
                                formattedName = nickname;
                            }
                        }
                    } else if (field.fieldName == QLatin1String("note") ||
                               field.fieldName == QLatin1String("desc")) {
                        QContactNote noteDetail;
                        if (detailContext != invalidContext) {
                            noteDetail.setContexts(detailContext);
                        }
                        noteDetail.setNote(asString(field, 0));

                        newNotes.append(noteDetail);
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

                            newBirthday = birthdayDetail;
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

                            newGender = genderDetail;
                        } else {
                            debug() << "Unsupported gender type:" << type;
                        }
                    } else {
                        debug() << "Unsupported contact info field" << field.fieldName;
                    }
                }

                if (structuredName || !formattedName.isEmpty()) {
                    if (!structuredName) {
                        Util::decomposeNameDetails(formattedName, &nameDetail);
                    }

                    if (!formattedName.isEmpty()) {
#ifdef USING_QTPIM
                        nameDetail.setValue(QContactName__FieldCustomLabel, formattedName);
#else
                        nameDetail.setCustomLabel(formattedName);
#endif
                    }

                    newName = nameDetail;
                }
            }

            // For all detail types, test if there has been any change
            bool changed = false;

            const QList<QContactAddress> oldAddresses = existing.details<QContactAddress>();
            if (detailListsDiffer(oldAddresses, newAddresses,
                [](const QContactAddress &oldAddress, const QContactAddress &newAddress) {
                    if ((oldAddress.contexts() != newAddress.contexts()) ||
                        (oldAddress.subTypes() != newAddress.subTypes()) ||
                        (oldAddress.postOfficeBox() != newAddress.postOfficeBox()) ||
                        (oldAddress.street() != newAddress.street()) ||
                        (oldAddress.locality() != newAddress.locality()) ||
                        (oldAddress.region() != newAddress.region()) ||
                        (oldAddress.postcode() != newAddress.postcode()) ||
                        (oldAddress.country() != newAddress.country())) {
                        return true;
                    }
                    return false;
                })
            ) {
                changed |= replaceDetails(existing, newAddresses, contactAddress, SRC_LOC);
            }

            QContactBirthday oldBirthday = existing.detail<QContactBirthday>();
            if (!oldBirthday.isEmpty() && newBirthday.isEmpty()) {
                deleteContactDetails<QContactBirthday>(existing);
            } else if ((oldBirthday.isEmpty() && !newBirthday.isEmpty()) ||
                       (oldBirthday.date() != newBirthday.date())) {
                changed |= replaceDetails(existing, newBirthday, contactAddress, SRC_LOC);
            }

            const QList<QContactEmailAddress> oldEmailAddresses = existing.details<QContactEmailAddress>();
            if (detailListsDiffer(oldEmailAddresses, newEmailAddresses,
                [](const QContactEmailAddress &oldEmailAddress, const QContactEmailAddress &newEmailAddress) {
                    if ((oldEmailAddress.contexts() != newEmailAddress.contexts()) ||
                        (oldEmailAddress.emailAddress() != newEmailAddress.emailAddress())) {
                        return true;
                    }
                    return false;
                })
            ) {
                changed |= replaceDetails(existing, newEmailAddresses, contactAddress, SRC_LOC);
            }

            QContactGender oldGender = existing.detail<QContactGender>();
            if (!oldGender.isEmpty() && newGender.isEmpty()) {
                deleteContactDetails<QContactGender>(existing);
            } else if ((oldGender.isEmpty() && !newGender.isEmpty()) ||
                       (oldGender.gender() != newGender.gender())) {
                changed |= replaceDetails(existing, newGender, contactAddress, SRC_LOC);
            }

            QContactName oldName = existing.detail<QContactName>();
            if ((oldName.firstName() != newName.firstName()) ||
                (oldName.middleName() != newName.middleName()) ||
                (oldName.lastName() != newName.lastName()) ||
                (oldName.value<QString>(QContactName__FieldCustomLabel) != newName.value<QString>(QContactName__FieldCustomLabel)) ||
                (oldName.prefix() != newName.prefix()) ||
                (oldName.suffix() != newName.suffix())) {
                changed |= replaceDetails(existing, newName, contactAddress, SRC_LOC);
            }

            // Nicknames are different to other list types, since they can come from the presence info as well
            const QList<QContactNickname> oldNicknames = existing.details<QContactNickname>();
            foreach (QContactNickname newNickname, newNicknames) {
                bool found = false;
                foreach (const QContactNickname &oldNickname, oldNicknames) {
                    if ((oldNickname.contexts() == newNickname.contexts()) &&
                        (oldNickname.nickname() == newNickname.nickname())) {
                        // Nickname already present
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    // Add this nickname
                    if (!storeContactDetail(existing, newNickname, SRC_LOC)) {
                        warning() << SRC_LOC << "Unable to save nickname to contact for:" << contactAddress;
                    }
                    changed = true;
                }
            }

            const QList<QContactNote> oldNotes = existing.details<QContactNote>();
            if (detailListsDiffer(oldNotes, newNotes,
                [](const QContactNote &oldNote, const QContactNote &newNote) {
                    if ((oldNote.contexts() != newNote.contexts()) ||
                        (oldNote.note() != newNote.note())) {
                        return true;
                    }
                    return false;
                })
            ) {
                changed |= replaceDetails(existing, newNotes, contactAddress, SRC_LOC);
            }

            const QList<QContactOrganization> oldOrganizations = existing.details<QContactOrganization>();
            if (detailListsDiffer(oldOrganizations, newOrganizations,
                [](const QContactOrganization &oldOrganization, const QContactOrganization &newOrganization) {
                    if ((oldOrganization.contexts() != newOrganization.contexts()) ||
                        (oldOrganization.name() != newOrganization.name()) ||
                        (oldOrganization.department() != newOrganization.department())) {
                        return true;
                    }
                    return false;
                })
            ) {
                changed |= replaceDetails(existing, newOrganizations, contactAddress, SRC_LOC);
            }

            const QList<QContactPhoneNumber> oldPhoneNumbers = existing.details<QContactPhoneNumber>();
            if (detailListsDiffer(oldPhoneNumbers, newPhoneNumbers,
                [](const QContactPhoneNumber &oldPhoneNumber, const QContactPhoneNumber &newPhoneNumber) {
                    if ((oldPhoneNumber.contexts() != newPhoneNumber.contexts()) ||
                        (oldPhoneNumber.subTypes() != newPhoneNumber.subTypes()) ||
                        (oldPhoneNumber.number() != newPhoneNumber.number())) {
                        return true;
                    }
                    return false;
                })
            ) {
                changed |= replaceDetails(existing, newPhoneNumbers, contactAddress, SRC_LOC);
            }

            const QList<QContactUrl> oldUrls = existing.details<QContactUrl>();
            if (detailListsDiffer(oldUrls, newUrls,
                [](const QContactUrl &oldUrl, const QContactUrl &newUrl) {
                    if ((oldUrl.contexts() != newUrl.contexts()) ||
                        (oldUrl.url() != newUrl.url())) {
                        return true;
                    }
                    return false;
                })
            ) {
                changed |= replaceDetails(existing, newUrls, contactAddress, SRC_LOC);
            }

            if (changed) {
                contactChanges |= CDTpContact::Information;
            }
        }
    }
    if (changes & CDTpContact::Avatar) {
        // Prefer the large avatar if available
        QString avatarPath(contactWrapper->largeAvatarPath());
        if (avatarPath.isEmpty()) {
            avatarPath = contact->avatarData().fileName;
        }

        QContactAvatar avatar = existing.detail<QContactAvatar>();

        if (avatarPath.isEmpty()) {
            if (!avatar.isEmpty()) {
                // Remove the avatar detail
                if (!existing.removeDetail(&avatar)) {
                    warning() << SRC_LOC << "Unable to remove avatar from contact:" << contactAddress;
                }

                contactChanges |= CDTpContact::Avatar;
            }
        } else {
            QUrl avatarUrl(QUrl::fromLocalFile(avatarPath));
            if (avatarUrl != avatar.imageUrl()) {
                avatar.setImageUrl(avatarUrl);

                QContactOnlineAccount qcoa = existing.detail<QContactOnlineAccount>();
                avatar.setLinkedDetailUris(qcoa.detailUri());

                if (!storeContactDetail(existing, avatar, SRC_LOC)) {
                    warning() << SRC_LOC << "Unable to save avatar for contact:" << contactAddress;
                }

                contactChanges |= CDTpContact::Avatar;
            }
        }
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

    return contactChanges;
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


CDTpStorage::CDTpStorage(QObject *parent)
    : QObject(parent)
{
    mUpdateTimer.setInterval(UPDATE_TIMEOUT);
    mUpdateTimer.setSingleShot(true);
    connect(&mUpdateTimer, SIGNAL(timeout()), SLOT(onUpdateQueueTimeout()));

    mWaitTimer.invalidate();
}

CDTpStorage::~CDTpStorage()
{
}

/* Set generic account properties of a QContactOnlineAccount. Does not set:
 * detailUri
 * linkedDetailUris (i.e. presence)
 * enabled
 * accountUri
 */
static void updateContactAccount(QContactOnlineAccount &qcoa, CDTpAccountPtr accountWrapper)
{
    Tp::AccountPtr account = accountWrapper->account();

    qcoa.setValue(QContactOnlineAccount__FieldAccountPath, imAccount(account));
    qcoa.setProtocol(protocolType(account->protocolName()));
    qcoa.setServiceProvider(account->serviceName());

    QString providerDisplayName = accountWrapper->storageInfo().value(QLatin1String("providerDisplayName")).toString();
    qcoa.setValue(QContactOnlineAccount__FieldServiceProviderDisplayName, providerDisplayName);
    qcoa.setValue(QContactOnlineAccount__FieldAccountDisplayName, account->displayName());

    addIconPath(qcoa, account);
}

void CDTpStorage::addNewAccount()
{
    CDTpAccountPtr account = CDTpAccountPtr(qobject_cast<CDTpAccount*>(sender()));
    QContact self(selfContact());
    if (!account)
        return;

    debug() << "New account" << imAccount(account) << "is ready, calling delayed addNewAccount";
    addNewAccount(self, account);
}

void CDTpStorage::addNewAccount(QContact &self, CDTpAccountPtr accountWrapper)
{
    Tp::AccountPtr account = accountWrapper->account();

    const QString accountPath(imAccount(account));
    const QString accountAddress(imAddress(account));
    const QString accountPresence(imPresence(account));

    if (!accountWrapper->isReady()) {
        debug() << "Waiting to create new self account" << accountPath << "until ready";
        connect(accountWrapper.data(), SIGNAL(readyChanged()), SLOT(addNewAccount()));
        return;
    }

    debug() << "Creating new self account - account:" << accountPath << "address:" << accountAddress;

    // Create a new QCOA for this account
    QContactOnlineAccount newAccount;
    updateContactAccount(newAccount, accountWrapper);

    newAccount.setDetailUri(accountAddress);
    newAccount.setLinkedDetailUris(accountPresence);
    newAccount.setValue(QContactOnlineAccount__FieldEnabled, asString(account->isEnabled()));
    newAccount.setAccountUri(account->normalizedName());

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

bool CDTpStorage::initializeNewContact(QContact &newContact, CDTpAccountPtr accountWrapper, const QString &contactId, const QString &alias)
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
    updateContactAccount(newAccount, accountWrapper);

    newAccount.setDetailUri(contactAddress);
    newAccount.setLinkedDetailUris(contactPresence);
    newAccount.setValue(QContactOnlineAccount__FieldEnabled, asString(true));
    newAccount.setAccountUri(contactId);

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
    if (!alias.isEmpty()) {
        presence.setNickname(alias);
    }

    if (!storeContactDetail(newContact, presence, SRC_LOC)) {
        warning() << SRC_LOC << "Unable to save presence to contact for:" << contactAddress;
        return false;
    }

    // Initially we will have no name detail - try to extract it from the alias
    if (!alias.isEmpty()) {
        QContactName name;

        Util::decomposeNameDetails(alias, &name);

#ifdef USING_QTPIM
        name.setValue(QContactName__FieldCustomLabel, alias);
#else
        name.setCustomLabel(alias);
#endif

        if (!storeContactDetail(newContact, name, SRC_LOC)) {
            warning() << SRC_LOC << "Unable to save name to contact for:" << contactAddress;
            return false;
        }
    }

    return true;
}

bool CDTpStorage::initializeNewContact(QContact &newContact, CDTpContactPtr contactWrapper)
{
    CDTpAccountPtr accountWrapper = contactWrapper->accountWrapper();
    Tp::ContactPtr contact = contactWrapper->contact();

    const QString id(contact->id());
    const QString alias(contact->alias().trimmed());

    return initializeNewContact(newContact, accountWrapper, id, alias);
}

void CDTpStorage::updateContactChanges(CDTpContactPtr contactWrapper, CDTpContact::Changes changes)
{
    ContactChangeSet saveSet;
    QList<ContactIdType> removeList;

    QContact existing = findExistingContact(imAddress(contactWrapper));
    updateContactChanges(contactWrapper, changes, existing, &saveSet, &removeList);

    updateContacts(SRC_LOC, &saveSet, &removeList);
}

void CDTpStorage::updateContactChanges(CDTpContactPtr contactWrapper, CDTpContact::Changes changes, QContact &existing, ContactChangeSet *saveSet, QList<ContactIdType> *removeList)
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
            if (!initializeNewContact(existing, contactWrapper)) {
                warning() << SRC_LOC << "Unable to create contact for account:" << accountPath << contactAddress;
                return;
            }
            changes |= CDTpContact::All;
        }

        changes = updateContactDetails(mNetwork, existing, contactWrapper, changes);

        appendContactChange(saveSet, existing, changes);
    }
}

QList<CDTpContactPtr> accountContacts(CDTpAccountPtr accountWrapper)
{
    QList<CDTpContactPtr> rv;

    QSet<QString> ids;
    foreach (CDTpContactPtr contactWrapper, accountWrapper->contacts()) {
        const QString id(contactWrapper->contact()->id());
        if (ids.contains(id))
            continue;

        ids.insert(id);
        rv.append(contactWrapper);
    }

    return rv;
}

void CDTpStorage::updateAccount()
{
    CDTpAccount *account = qobject_cast<CDTpAccount*>(sender());
    if (!account)
        return;

    debug() << "Delayed update of account" << account->account()->objectPath() << "is ready";
    updateAccount(CDTpAccountPtr(account), CDTpAccount::All);
}

void CDTpStorage::updateAccountChanges(QContact &self, QContactOnlineAccount &qcoa, CDTpAccountPtr accountWrapper, CDTpAccount::Changes changes)
{
    Tp::AccountPtr account = accountWrapper->account();

    const QString accountPath(imAccount(account));
    const QString accountAddress(imAddress(account));

    if (!accountWrapper->isReady()) {
        debug() << "Delaying update of account" << accountPath << "address" << accountAddress << "until ready";
        connect(accountWrapper.data(), SIGNAL(readyChanged()), SLOT(updateAccount()));
        return;
    }

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
        const QHash<QString, CDTpContact::Changes> rosterChanges = accountWrapper->rosterChanges();
        QHash<QString, CDTpContact::Changes>::ConstIterator it = rosterChanges.constBegin(),
                                                            end = rosterChanges.constEnd();
        for ( ; it != end; ++it) {
            const QString address = imAddress(accountPath, it.key());
            CDTpContact::Changes flags = it.value() | CDTpContact::Presence;

            // If account display name changes, update QCOA of all contacts
            if (changes & CDTpAccount::DisplayName)
                flags |= CDTpContact::Capabilities;

            // We always update contact presence since this method is called after a presence change
            allChanges.insert(address, flags);
        }

        QList<CDTpContactPtr> tpContacts(accountContacts(accountWrapper));

        QStringList contactAddresses;
        foreach (const CDTpContactPtr &contactWrapper, tpContacts) {
            const QString address = imAddress(accountPath, contactWrapper->contact()->id());
            contactAddresses.append(address);
        }

        // Retrieve the existing contacts in a single batch
        QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

        ContactChangeSet saveSet;
        QList<ContactIdType> removeList;

        foreach (const CDTpContactPtr &contactWrapper, tpContacts) {
            const QString address = imAddress(accountPath, contactWrapper->contact()->id());

            QHash<QString, CDTpContact::Changes>::Iterator cit = allChanges.find(address);
            if (cit == allChanges.end()) {
                warning() << SRC_LOC << "No changes found for contact:" << address;
                continue;
            }

            CDTpContact::Changes changes = *cit;

            QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
            if (existing == existingContacts.end()) {
                warning() << SRC_LOC << "No contact found for address:" << address;
                existing = existingContacts.insert(address, QContact());
                changes |= CDTpContact::All;
            }

            // If we got a contact without avatar in the roster, and the original
            // had an avatar, then ignore the avatar update (some contact managers
            // send the initial roster with the avatar missing)
            // Contact updates that have a null avatar will clear the avatar though
            if (changes & CDTpContact::DefaultAvatar) {
                if (((changes & CDTpContact::All) != CDTpContact::All) &&
                    contactWrapper->contact()->avatarData().fileName.isEmpty()) {
                    changes &= ~CDTpContact::DefaultAvatar;
                }
            }

            updateContactChanges(contactWrapper, changes, *existing, &saveSet, &removeList);
        }

        updateContacts(SRC_LOC, &saveSet, &removeList);
    } else {
        ContactChangeSet saveSet;

        const QContactPresence::PresenceState newState(qContactPresenceState(Tp::ConnectionPresenceTypeUnknown));
        const QStringList newCapabilities(currentCapabilites(account->capabilities(), Tp::ConnectionPresenceTypeUnknown, account));

        // Set presence to unknown for all contacts of this account
        foreach (const ContactIdType &contactId, findContactIdsForAccount(accountPath)) {
            QContact existing = manager()->contact(contactId);

            CDTpContact::Changes changes;

            QContactPresence presence = existing.detail<QContactPresence>();

            if (presence.presenceState() != newState) {
                presence.setPresenceState(newState);
                presence.setTimestamp(QDateTime::currentDateTime());

                if (!storeContactDetail(existing, presence, SRC_LOC)) {
                    warning() << SRC_LOC << "Unable to save unknown presence to contact for:" << contactId;
                }

                changes |= CDTpContact::Presence;
            }

            // Also reset the capabilities
            QContactOnlineAccount qcoa = existing.detail<QContactOnlineAccount>();

            if (qcoa.capabilities() != newCapabilities) {
                qcoa.setCapabilities(newCapabilities);

                if (!storeContactDetail(existing, qcoa, SRC_LOC)) {
                    warning() << SRC_LOC << "Unable to save capabilities to contact for:" << contactId;
                }

                changes |= CDTpContact::Capabilities;
            }

            if (!account->isEnabled()) {
                // Mark the contact as un-enabled also
                QContactOriginMetadata metadata = existing.detail<QContactOriginMetadata>();

                if (metadata.enabled()) {
                    metadata.setEnabled(false);

                    if (!storeContactDetail(existing, metadata, SRC_LOC)) {
                        warning() << SRC_LOC << "Unable to un-enable contact for:" << contactId;
                    }

                    changes |= CDTpContact::Capabilities;
                }
            }

            appendContactChange(&saveSet, existing, changes);
        }

        updateContacts(SRC_LOC, &saveSet, 0);
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

    QList<CDTpContactPtr> tpContacts(accountContacts(accountWrapper));

    QStringList contactAddresses;
    foreach (const CDTpContactPtr &contactWrapper, tpContacts) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());
        contactAddresses.append(address);
    }

    // Retrieve the existing contacts in a single batch
    QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

    ContactChangeSet saveSet;
    QList<ContactIdType> removeList;

    // Add any contacts already present for this account
    foreach (const CDTpContactPtr &contactWrapper, tpContacts) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            existing = existingContacts.insert(address, QContact());
        }

        updateContactChanges(contactWrapper, CDTpContact::All, *existing, &saveSet, &removeList);
    }

    updateContacts(SRC_LOC, &saveSet, &removeList);
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
    cancelQueuedUpdates(accountContacts(accountWrapper));

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

    // Ensure there are no duplicates in the list
    QList<CDTpContactPtr> addedContacts(contactsAdded.toSet().toList());
    QList<CDTpContactPtr> removedContacts(contactsRemoved.toSet().toList());

    QSet<QString> contactAddresses;
    foreach (const CDTpContactPtr &contactWrapper, addedContacts) {
        // This contact must be for the specified account
        if (imAccount(contactWrapper) != accountPath) {
            warning() << SRC_LOC << "Unable to add contact from wrong account:" << imAccount(contactWrapper) << accountPath;
            continue;
        }

        const QString address = imAddress(accountPath, contactWrapper->contact()->id());
        contactAddresses.insert(address);
    }
    foreach (const CDTpContactPtr &contactWrapper, removedContacts) {
        if (imAccount(contactWrapper) != accountPath) {
            warning() << SRC_LOC << "Unable to remove contact from wrong account:" << imAccount(contactWrapper) << accountPath;
            continue;
        }

        const QString address = imAddress(accountPath, contactWrapper->contact()->id());
        contactAddresses.insert(address);
    }

    // Retrieve the existing contacts in a single batch
    QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

    ContactChangeSet saveSet;
    QList<ContactIdType> removeList;

    foreach (const CDTpContactPtr &contactWrapper, addedContacts) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());

        CDTpContact::Changes changes = CDTpContact::Information;

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            existing = existingContacts.insert(address, QContact());
            changes |= CDTpContact::All;
        }

        updateContactChanges(contactWrapper, changes, *existing, &saveSet, &removeList);
    }
    foreach (const CDTpContactPtr &contactWrapper, removedContacts) {
        const QString address = imAddress(accountPath, contactWrapper->contact()->id());

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            continue;
        }

        updateContactChanges(contactWrapper, CDTpContact::Deleted, *existing, &saveSet, &removeList);
    }

    updateContacts(SRC_LOC, &saveSet, &removeList);
}

void CDTpStorage::createAccountContacts(CDTpAccountPtr accountWrapper, const QStringList &imIds, uint localId)
{
    Q_UNUSED(localId) // ???

    const QString accountPath(imAccount(accountWrapper));

    debug() << SRC_LOC << "Create contacts account:" << accountPath;

    ContactChangeSet saveSet;

    foreach (const QString &id, imIds) {
        QContact newContact;
        if (!initializeNewContact(newContact, accountWrapper, id, QString())) {
            warning() << SRC_LOC << "Unable to create contact for account:" << accountPath << id;
        } else {
            appendContactChange(&saveSet, newContact, CDTpContact::All);
        }
    }

    updateContacts(SRC_LOC, &saveSet, 0);
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

    // Only update IM contacts after not receiving an update notification for the defined period
    // Also use an upper limit to keep latency within acceptable bounds.
    if (mWaitTimer.isValid()) {
        if (mWaitTimer.elapsed() >= UPDATE_MAXIMUM_TIMEOUT) {
            // Don't prolong the wait any further
            return;
        }
    } else {
        mWaitTimer.start();
    }

    mUpdateTimer.start();
}

void CDTpStorage::onUpdateQueueTimeout()
{
    mWaitTimer.invalidate();

    debug() << "Update" << mUpdateQueue.count() << "contacts";

    QHash<CDTpContactPtr, CDTpContact::Changes> updates;
    QSet<QString> contactAddresses;

    QHash<CDTpContactPtr, CDTpContact::Changes>::const_iterator it = mUpdateQueue.constBegin(), end = mUpdateQueue.constEnd();
    for ( ; it != end; ++it) {
        CDTpContactPtr contactWrapper = it.key();

        // If there are multiple entries for a contact, coalesce the changes
        updates[contactWrapper] |= it.value();
        contactAddresses.insert(imAddress(contactWrapper));
    }

    mUpdateQueue.clear();

    // Retrieve the existing contacts in a single batch
    QHash<QString, QContact> existingContacts = findExistingContacts(contactAddresses);

    ContactChangeSet saveSet;
    QList<ContactIdType> removeList;

    for (it = updates.constBegin(), end = updates.constEnd(); it != end; ++it) {
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
        CDTpContact::Changes changes = it.value();

        QHash<QString, QContact>::Iterator existing = existingContacts.find(address);
        if (existing == existingContacts.end()) {
            warning() << SRC_LOC << "No contact found for address:" << address;
            existing = existingContacts.insert(address, QContact());
            changes |= CDTpContact::All;
        }

        updateContactChanges(contactWrapper, changes, *existing, &saveSet, &removeList);
    }

    updateContacts(SRC_LOC, &saveSet, &removeList);
}

void CDTpStorage::cancelQueuedUpdates(const QList<CDTpContactPtr> &contacts)
{
    foreach (const CDTpContactPtr &contactWrapper, contacts) {
        mUpdateQueue.remove(contactWrapper);
    }
}

// Instantiate the QContactOriginMetadata functions
#include <qcontactoriginmetadata_impl.h>
