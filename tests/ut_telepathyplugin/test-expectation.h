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

#ifndef TEST_EXPECTATION_H
#define TEST_EXPECTATION_H

#include <QObject>
#include <QString>
#include <QContactManager>
#include <QContact>

#include <TelepathyQt/SharedPtr>

#include <telepathy-glib/telepathy-glib.h>
#include "libtelepathy/contacts-conn.h"


#define ACCOUNT_PATH TP_ACCOUNT_OBJECT_PATH_BASE "fakecm/fakeproto/fakeaccount"

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
typedef QContactId ContactIdType;
#else
QTM_USE_NAMESPACE
typedef QContactLocalId ContactIdType;
#endif

typedef enum {
    EventAdded,
    EventChanged,
    EventPresenceChanged,
    EventRemoved
} Event;

// --- TestExpectation ---

class TestExpectation : public QObject, public Tp::RefCounted
{
    Q_OBJECT

public:
    void verify(Event event, const QList<ContactIdType> &contactIds);

    void setContactManager(QContactManager *contactManager) { mContactManager = contactManager; };
    QContactManager *contactManager() { return mContactManager; };

Q_SIGNALS:
    void finished();

protected:
    virtual void verify(Event event, const QList<QContact> &contacts);
    virtual void verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error);
    void emitFinished();

private:
    friend class TestFetchContacts;

    QContactManager *mContactManager;
};
typedef Tp::SharedPtr<TestExpectation> TestExpectationPtr;

// --- TestFetchContacts ---

class TestFetchContacts : public QObject
{
    Q_OBJECT

public:
    TestFetchContacts(const QList<ContactIdType> &contactIds, Event event, TestExpectation *exp);

private Q_SLOTS:
    void onContactsFetched();

private:
    QList<ContactIdType> mContactIds;
    Event mEvent;
    TestExpectation *mExp;
};

// --- TestExpectationInit ---

class TestExpectationInit : public TestExpectation
{
    Q_OBJECT

protected:
    void verify(Event event, const QList<QContact> &contacts);
};
typedef Tp::SharedPtr<TestExpectationInit> TestExpectationInitPtr;

// --- TestExpectationCleanup ---

class TestExpectationCleanup : public TestExpectation
{
    Q_OBJECT

public:
    TestExpectationCleanup(int nContacts);

protected:
    void verify(Event event, const QList<QContact> &contacts);
    void verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error);

private:
    void maybeEmitFinished();

private:
    int mNContacts;
    bool mSelfChanged;
};
typedef Tp::SharedPtr<TestExpectationCleanup> TestExpectationCleanupPtr;

// --- TestExpectationContact ---

class TestExpectationContact : public TestExpectation
{
    Q_OBJECT

public:
    TestExpectationContact(Event event, QString accountUri = QString());

    QContact contact() { return mContact; };
    void setEvent(const Event &event) { mEvent = event; };
    void resetVerifyFlags() { mFlags = 0; };

    void verifyAlias(const QString &alias) { mAlias = alias; mFlags |= VerifyAlias; };
    void verifyPresence(TpTestsContactsConnectionPresenceStatusIndex presence) { mPresence = presence; mFlags |= VerifyPresence; };
    void verifyAvatar(const QByteArray &avatarData) { mAvatarData = avatarData; mFlags |= VerifyAvatar; };
    void verifyAuthorization(const QString &subscriptionState, const QString &publishState) { mSubscriptionState = subscriptionState; mPublishState = publishState; mFlags |= VerifyAuthorization; };
    void verifyInfo(GPtrArray *contactInfo) { mContactInfo = contactInfo; mFlags |= VerifyInfo; };
    void verifyContactId(const QContact &contact) {
#ifdef USING_QTPIM
        mContactId = contact.id();
#else
        mContactId = contact.localId();
#endif
        mFlags |= VerifyContactId;
    };
    void verifyGenerator(const QString &generator) { mGenerator = generator; mFlags |= VerifyGenerator; };

    void verify(const QContact &contact);

protected:
    void verify(Event event, const QList<QContact> &contacts);
    void verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error);

private:
    enum VerifyFlags {
        VerifyNone           = 0,
        VerifyAlias          = (1 << 0),
        VerifyPresence       = (1 << 1),
        VerifyAvatar         = (1 << 2),
        VerifyAuthorization  = (1 << 3),
        VerifyInfo           = (1 << 4),
        VerifyContactId      = (1 << 5),
        VerifyGenerator      = (1 << 6),
        VerifyAll            = (1 << 7) - 1
    };

    void verifyContactInfo(const QString name, const QStringList values, const QStringList params) const;

    QString mAccountUri;
    Event mEvent;
    int mFlags;

    QString mAlias;
    TpTestsContactsConnectionPresenceStatusIndex mPresence;
    QByteArray mAvatarData;
    QString mSubscriptionState;
    QString mPublishState;
    GPtrArray *mContactInfo;
    ContactIdType mContactId;
    QString mGenerator;

    QContact mContact;
};
typedef Tp::SharedPtr<TestExpectationContact> TestExpectationContactPtr;

// --- TestExpectationDisconnect ---

class TestExpectationDisconnect : public TestExpectationContact
{
    Q_OBJECT

public:
    TestExpectationDisconnect(int nContacts);

protected:
    void verify(Event event, const QList<QContact> &contacts);

private:
    int mNContacts;
    bool mSelfChanged;
};
typedef Tp::SharedPtr<TestExpectationDisconnect> TestExpectationDisconnectPtr;

// --- TestExpectationMerge ---

class TestExpectationMerge : public TestExpectation
{
    Q_OBJECT

public:
    TestExpectationMerge(const ContactIdType &masterId, const QList<ContactIdType> &mergeIds,
            const QList<TestExpectationContactPtr> expectations = QList<TestExpectationContactPtr>());

protected:
    void verify(Event event, const QList<QContact> &contacts);
    void verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error);

private:
    void maybeEmitFinished();

    ContactIdType mMasterId;
    QList<ContactIdType> mMergeIds;
    bool mGotMergedContact;
    QList<TestExpectationContactPtr> mContactExpectations;
};
typedef Tp::SharedPtr<TestExpectationMerge> TestExpectationMergePtr;

// --- TestExpectationMass ---

class TestExpectationMass : public TestExpectation
{
    Q_OBJECT

public:
    TestExpectationMass(int nAdded, int nChanged, int nRemoved);

protected:
    void verify(Event event, const QList<QContact> &contacts);
    void verify(Event event, const QList<ContactIdType> &contactIds, QContactManager::Error error);

private:
    void maybeEmitFinished();

    int mAdded;
    int mChanged;
    int mRemoved;
};
typedef Tp::SharedPtr<TestExpectationMass> TestExpectationMassPtr;

#endif
