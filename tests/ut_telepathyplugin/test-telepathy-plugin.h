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

#ifndef TEST_TELEPATHY_PLUGIN_H
#define TEST_TELEPATHY_PLUGIN_H

#include <QObject>
#include <QTest>
#include <QString>

#include <QContactManager>
#include <QContactAbstractRequest>

#include <telepathy-glib/telepathy-glib.h>
#include <TelepathyQt/Contact>

#include "libtelepathy/contacts-conn.h"
#include "libtelepathy/contact-list-manager.h"
#include "libtelepathy/simple-account-manager.h"
#include "libtelepathy/simple-account.h"

#include "test.h"
#include "test-expectation.h"

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#else
QTM_USE_NAMESPACE
#endif

/**
 * Telepathy plugin's unit test
 */
class TestTelepathyPlugin : public Test
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.nemomobile.contactsd.test-telepathy")

public:
#ifdef USING_QTPIM
    typedef QContactId ContactIdType;
#else
    typedef QContactLocalId ContactIdType;
#endif

    TestTelepathyPlugin(QObject *parent = 0);

protected Q_SLOTS:
#ifdef USING_QTPIM
    void contactsAdded(const QList<QContactId>& contactIds);
    void contactsChanged(const QList<QContactId>& contactIds);
    void contactsRemoved(const QList<QContactId>& contactIds);
#else
    void contactsAdded(const QList<QContactLocalId>& contactIds);
    void contactsChanged(const QList<QContactLocalId>& contactIds);
    void contactsRemoved(const QList<QContactLocalId>& contactIds);
#endif
    void onContactsFetched();
    void requestStateChanged(QContactAbstractRequest::State newState);

private Q_SLOTS:
    void initTestCase();
    void init();

    /* Generic tests */
    void testBasicUpdates();
    void testSelfContact();
    void testAuthorization();
    void testContactInfo();
    void testContactPhoneNumber();
    void testRemoveContacts();
    void testRemoveBuddyDBusAPI();
    void testInviteBuddyDBusAPI();
    void testSetOffline();
    void testAvatar();
    void testDisable();

    /* Specific tests */
    void testBug253679();
    void testMergedContact();
    void testBug220851();
    void testIRIEncode();

    /* Benchmark */
    void testBenchmark();

    void cleanup();
    void cleanupTestCase();

private:
    TpHandle ensureHandle(const gchar *id);
    TestExpectationContactPtr createContact(const gchar *id, TpHandle &handle, bool please = false);
    TestExpectationContactPtr createContact(const gchar *id, bool please = false);
    GPtrArray *createContactInfoTel(const gchar *number);
    void verify(Event event, const QList<ContactIdType> &contactIds);
    void runExpectation(TestExpectationPtr expectation);
    void startRequest(QContactAbstractRequest *request);

private:
    QContactManager *mContactManager;
    TpTestsSimpleAccountManager *mAccountManager;
    TpTestsSimpleAccount *mAccount;

    TpBaseConnection *mConnService;
    TpConnection *mConnection;
    TestContactListManager *mListManager;

    QList<ContactIdType> mContactIds;
    int mNOnlyLocalContacts;

    TestExpectationPtr mExpectation;

    bool mCheckLeakedResources;
};

#endif
