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

#include <QTest>
#include <QFile>

#include <QContactAddress>
#include <QContactAvatar>
#include <QContactEmailAddress>
#include <QContactFetchByIdRequest>
#include <QContactGlobalPresence>
#include <QContactOnlineAccount>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactSyncTarget>
#include <QContactTag>

#include "test-expectation.h"
#include "debug.h"

#ifdef USING_QTPIM
QContactId apiId(const QContact &contact) { return contact.id(); }
#else
QContactLocalId apiId(const QContact &contact) { return contact.localId(); }
#endif
// --- TestExpectation ---

void TestExpectation::verify(Event event, const QList<ContactIdType> &contactIds)
{
     new TestFetchContacts(contactIds, event, this);
}

void TestExpectation::verify(Event event, const QList<QContact> &contacts)
{
    Q_UNUSED(event);
    Q_UNUSED(contacts);

    emitFinished();
}

void TestExpectation::verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error)
{
    Q_UNUSED(event);
    Q_UNUSED(contactIds);
    Q_UNUSED(error);

    QVERIFY2(false, "Error fetching contacts");
    emitFinished();
}

void TestExpectation::emitFinished()
{
    Q_EMIT finished();
}

// --- TestFetchContacts ---

TestFetchContacts::TestFetchContacts(const QList<ContactIdType> &contactIds,
        Event event, TestExpectation *exp) : QObject(exp),
        mContactIds(contactIds), mEvent(event), mExp(exp)
{
    QContactFetchByIdRequest *request = new QContactFetchByIdRequest();
    connect(request,
            SIGNAL(resultsAvailable()),
            SLOT(onContactsFetched()));
    request->setManager(mExp->contactManager());
#ifdef USING_QTPIM
    request->setIds(contactIds);
#else
    request->setLocalIds(contactIds);
#endif
    QVERIFY(request->start());
}

void TestFetchContacts::onContactsFetched()
{
    QContactFetchByIdRequest *req = qobject_cast<QContactFetchByIdRequest *>(sender());
    if (req == 0 || !req->isFinished()) {
        return;
    }

    if (req->error() == QContactManager::NoError) {
        QList<QContact> contacts;
        Q_FOREACH (const QContact &contact, req->contacts()) {
            debug() << "Contact fetched:\n\n" << contact << "\n";

            // For some reason, we get VoiceMail signals sometimes. Ignore them.
            if (contact.detail<QContactTag>().tag() == QLatin1String("voicemail")) {
                debug() << "Ignoring voicemail contact";
                continue;
            }

            contacts << contact;
        }
        if (!contacts.isEmpty()) {
            mExp->verify(mEvent, contacts);
        }
    } else {
        mExp->verify(mEvent, mContactIds, req->error());
    }

    deleteLater();
    req->deleteLater();
}

// --- TestExpectationInit ---

void TestExpectationInit::verify(Event event, const QList<QContact> &contacts)
{
    QCOMPARE(event, EventChanged);
    QCOMPARE(contacts.count(), 1);
    QCOMPARE(apiId(contacts[0]), contactManager()->selfContactId());
    emitFinished();
}

// --- TestExpectationCleanup ---

TestExpectationCleanup::TestExpectationCleanup(int nContacts) :
        mNContacts(nContacts), mSelfChanged(false)
{
}

void TestExpectationCleanup::verify(Event event, const QList<QContact> &contacts)
{
    QCOMPARE(event, EventChanged);

    Q_FOREACH (const QContact &contact, contacts) {
        if (apiId(contact) == contactManager()->selfContactId()) {
            QVERIFY(!mSelfChanged);
            mSelfChanged = true;
            mNContacts--;
            continue;
        }

        QContactSyncTarget detail = contact.detail<QContactSyncTarget>();
        QCOMPARE(detail.syncTarget(), QLatin1String("addressbook"));
        mNContacts--;
    }

    maybeEmitFinished();
}

void TestExpectationCleanup::verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error)
{
    QCOMPARE(event, EventRemoved);
    QCOMPARE(error, QContactManager::DoesNotExistError);
    mNContacts -= contactIds.count();

    maybeEmitFinished();
}

void TestExpectationCleanup::maybeEmitFinished()
{
    QVERIFY(mNContacts >= 0);
    if (mNContacts == 0 && mSelfChanged) {
        emitFinished();
    }
}

// --- TestExpectationContact ---

TestExpectationContact::TestExpectationContact(Event event, QString accountUri):
        mAccountUri(accountUri), mEvent(event), mFlags(0), mPresence(TP_TESTS_CONTACTS_CONNECTION_STATUS_UNSET),
        mContactInfo(0)
{
}

void TestExpectationContact::verify(Event event, const QList<QContact> &contacts)
{
    QCOMPARE(event, mEvent);
    QCOMPARE(contacts.count(), 1);
    mContact = contacts[0];
    verify(contacts[0]);
    emitFinished();
}

void TestExpectationContact::verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error)
{
    QCOMPARE(event, EventRemoved);
    QCOMPARE(contactIds.count(), 1);
    QCOMPARE(error, QContactManager::DoesNotExistError);
    emitFinished();
}

void TestExpectationContact::verify(const QContact &contact)
{
    if (!mAccountUri.isEmpty()) {
#ifdef USING_QTPIM
        // TODO
#else
        const QString uri = QString("telepathy:%1!%2").arg(ACCOUNT_PATH).arg(mAccountUri);
        QList<QContactOnlineAccount> details = contact.details<QContactOnlineAccount>("DetailUri", uri);
        QCOMPARE(details.count(), 1);
        QCOMPARE(details[0].value("AccountPath"), QString(ACCOUNT_PATH));
#endif
    }

    if (mFlags & VerifyAlias) {
#ifdef USING_QTPIM
        QString label = contact.detail<QContactDisplayLabel>().label();
#else
        QString label = contact.displayLabel();
#endif
        QCOMPARE(label, mAlias);
    }

    if (mFlags & VerifyPresence) {
        QContactPresence::PresenceState presence;
        if (mAccountUri.isEmpty()) {
            QContactGlobalPresence presenceDetail = contact.detail<QContactGlobalPresence>();
            presence = presenceDetail.presenceState();
        } else {
#ifdef USING_QTPIM
        // TODO
#else
            const QString uri = QString("presence:%1!%2").arg(ACCOUNT_PATH).arg(mAccountUri);
            QList<QContactPresence> details = contact.details<QContactPresence>("DetailUri", uri);
            QCOMPARE(details.count(), 1);
            presence = details[0].presenceState();
#endif
        }

        switch (mPresence) {
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_AVAILABLE:
            QCOMPARE(presence, QContactPresence::PresenceAvailable);
            break;
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_BUSY:
            QCOMPARE(presence, QContactPresence::PresenceBusy);
            break;
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_AWAY:
            QCOMPARE(presence, QContactPresence::PresenceAway);
            break;
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_OFFLINE:
            QCOMPARE(presence, QContactPresence::PresenceOffline);
            break;
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_UNKNOWN:
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_ERROR:
        case TP_TESTS_CONTACTS_CONNECTION_STATUS_UNSET:
            QCOMPARE(presence, QContactPresence::PresenceUnknown);
            break;
        }
    }

    if (mFlags & VerifyAvatar) {
        QString avatarFileName = contact.detail<QContactAvatar>().imageUrl().path();
        if (mAvatarData.isEmpty()) {
            QVERIFY2(avatarFileName.isEmpty(), "Expected empty avatar filename");
        } else {
            QFile file(avatarFileName);
            file.open(QIODevice::ReadOnly);
            QCOMPARE(file.readAll(), mAvatarData);
            file.close();
        }
    }

    if (mFlags & VerifyAuthorization) {
#ifdef USING_QTPIM
        // TODO
#else
        const QString uri = QString("presence:%1!%2").arg(ACCOUNT_PATH).arg(mAccountUri);
        QList<QContactPresence> details = contact.details<QContactPresence>("DetailUri", uri);
        QCOMPARE(details.count(), 1);
        QCOMPARE(details[0].value("AuthStatusFrom"), mSubscriptionState);
        QCOMPARE(details[0].value("AuthStatusTo"), mPublishState);
#endif
    }

    if (mFlags & VerifyInfo) {
        uint nMatchedField = 0;

        Q_FOREACH (const QContactDetail &detail, contact.details()) {
#ifdef USING_QTPIM
        // TODO
#else
            QStringList params;
            if (detail.contexts().contains("Work")) {
                params << QLatin1String("type=work");
            }
            if (detail.contexts().contains("Home")) {
                params << QLatin1String("type=home");
            }

            if (detail.definitionName() == "PhoneNumber") {
                QContactPhoneNumber phoneNumber = static_cast<QContactPhoneNumber>(detail);

                Q_FOREACH (const QString &type, phoneNumber.subTypes()) {
                    if (type == QLatin1String("Mobile")) {
                        params << QLatin1String("type=cell");
                    } else if (type == QLatin1String("Video")) {
                        params << QLatin1String("type=video");
                    }
                }

                verifyContactInfo("tel", QStringList() << phoneNumber.number(), params);
                nMatchedField++;
            }
            else if (detail.definitionName() == "Address") {
                QContactAddress address = static_cast<QContactAddress>(detail);
                verifyContactInfo("adr", QStringList() << address.postOfficeBox()
                                                       << QString("unmapped") // extended address is not mapped
                                                       << address.street()
                                                       << address.locality()
                                                       << address.region()
                                                       << address.postcode()
                                                       << address.country(),
                                  params);
                nMatchedField++;
            }
            else if (detail.definitionName() == "EmailAddress") {
                QContactEmailAddress emailAddress = static_cast<QContactEmailAddress >(detail);
                verifyContactInfo("email", QStringList() << emailAddress.emailAddress(), params);
                nMatchedField++;
            }
#endif
        }

        if (mContactInfo != NULL) {
            QCOMPARE(nMatchedField, mContactInfo->len);
        }
    }

    if (mFlags & VerifyContactId) {
        QCOMPARE(apiId(contact), mContactId);
    }

    if (mFlags & VerifyGenerator) {
        QContactSyncTarget detail = contact.detail<QContactSyncTarget>();
        QCOMPARE(detail.syncTarget(), mGenerator);
    }
}

void TestExpectationContact::verifyContactInfo(const QString name, const QStringList values,
        const QStringList params) const
{
    QVERIFY2(mContactInfo != NULL, "Found ContactInfo field, was expecting none");

    /* All well known params that must be matched */
    static QStringList knownParams;
    if (knownParams.isEmpty()) {
        knownParams << QLatin1String("type=work");
        knownParams << QLatin1String("type=home");
        knownParams << QLatin1String("type=cell");
        knownParams << QLatin1String("type=video");
    }

    bool found = false;
    for (uint i = 0; i < mContactInfo->len; i++) {
        gchar *c_name;
        gchar **c_parameters;
        gchar **c_values;

        tp_value_array_unpack((GValueArray*) g_ptr_array_index(mContactInfo, i),
            3, &c_name, &c_parameters, &c_values);

        /* if c_values_len < values.count() it could still be OK if all
         * additional values are empty. */
        gint c_values_len = g_strv_length(c_values);
        if (QString(c_name) != name || c_values_len > values.count()) {
            continue;
        }

        /* Verify values match */
        bool match = true;
        for (int j = 0; j < values.count(); j++) {
            if (values[j] == QString("unmapped")) {
                continue;
            }
            if ((j < c_values_len && values[j] != QString(c_values[j])) ||
                (j >= c_values_len && !values[j].isEmpty())) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }

        /* Verify params match */
        QStringList expectedParams = params;
        while (c_parameters && *c_parameters) {
            QLatin1String c_param(*c_parameters);
            if (!expectedParams.removeOne(c_param) &&
                knownParams.contains(c_param)) {
                match = false;
                break;
            }
            c_parameters++;
        }
        if (!expectedParams.isEmpty()) {
            match = false;
        }

        if (match) {
            found = true;
            break;
        }
    }

    QVERIFY2(found, "Unexpected ContactInfo field");
}

// --- TestExpectationDisconnect ---

TestExpectationDisconnect::TestExpectationDisconnect(int nContacts) :
        TestExpectationContact(EventChanged), mNContacts(nContacts), mSelfChanged(false)
{
}

void TestExpectationDisconnect::verify(Event event, const QList<QContact> &contacts)
{
    Q_FOREACH (const QContact contact, contacts) {
        if (apiId(contact) == contactManager()->selfContactId()) {
            QCOMPARE(event, EventChanged);
            verifyPresence(TP_TESTS_CONTACTS_CONNECTION_STATUS_OFFLINE);
            mSelfChanged = true;
            mNContacts--;
        } else {
            // Ignore EventChanged events, we want to check that we had a real
            // tagged update here (for each contact we'll get both an EventChanged
            // and an EventPresenceChanged
            if (event == EventChanged) {
                continue;
            }

            QCOMPARE(event, EventPresenceChanged);
            verifyPresence(TP_TESTS_CONTACTS_CONNECTION_STATUS_UNKNOWN);
            mNContacts--;
        }
        TestExpectationContact::verify(contact);
    }

    QVERIFY(mNContacts >= 0);
    if (mNContacts == 0 && mSelfChanged) {
        emitFinished();
    }
}

// --- TestExpectationMerge ---

TestExpectationMerge::TestExpectationMerge(const ContactIdType &masterId,
        const QList<ContactIdType> &mergeIds, const QList<TestExpectationContactPtr> expectations)
        : mMasterId(masterId), mMergeIds(mergeIds), mGotMergedContact(false),
        mContactExpectations(expectations)
{
}

void TestExpectationMerge::verify(Event event, const QList<QContact> &contacts)
{
    QCOMPARE(event, EventChanged);
    QCOMPARE(contacts.count(), 1);
    QCOMPARE(apiId(contacts[0]), mMasterId);
    mGotMergedContact = true;

    Q_FOREACH (TestExpectationContactPtr exp, mContactExpectations) {
        exp->verify(contacts[0]);
    }

    maybeEmitFinished();
}


void TestExpectationMerge::verify(Event event, const QList<ContactIdType> &contactIds,
        QContactManager::Error error)
{
    QCOMPARE(event, EventRemoved);
    QCOMPARE(error, QContactManager::DoesNotExistError);

    Q_FOREACH (const ContactIdType &id, contactIds) {
        QVERIFY(mMergeIds.contains(id));
        mMergeIds.removeOne(id);
    }

    maybeEmitFinished();
}

void TestExpectationMerge::maybeEmitFinished()
{
    if (mMergeIds.isEmpty() && mGotMergedContact) {
        emitFinished();
    }
}

// --- TestExpectationMass ---

TestExpectationMass::TestExpectationMass(int nAdded, int nChanged, int nRemoved)
        : mAdded(nAdded), mChanged(nChanged), mRemoved(nRemoved)
{
}

void TestExpectationMass::verify(Event event, const QList<QContact> &contacts)
{
    if (event == EventAdded) {
        mAdded -= contacts.count();
        QVERIFY(mAdded >= 0);
    }
    if (event == EventChanged) {
        mChanged -= contacts.count();
        QVERIFY(mChanged >= 0);
    }

    maybeEmitFinished();
}

void TestExpectationMass::verify(Event event, const QList<ContactIdType> &contactIds,
        QContactManager::Error error)
{
    QCOMPARE(event, EventRemoved);
    QCOMPARE(error, QContactManager::DoesNotExistError);
    mRemoved -= contactIds.count();
    QVERIFY(mRemoved >= 0);

    maybeEmitFinished();
}

void TestExpectationMass::maybeEmitFinished()
{
    if (mAdded == 0 && mChanged == 0 && mRemoved == 0) {
        emitFinished();
    }
}
