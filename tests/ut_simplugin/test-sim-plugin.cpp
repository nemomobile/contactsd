/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2013 Jolla Ltd.
 **
 ** Contact: Matt Vogt <matthew.vogt@jollamobile.com>
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **/

#include "test-sim-plugin.h"

#include <test-common.h>

#include <QContactDetailFilter>
#include <QContactNickname>
#include <QContactPhoneNumber>
#include <QContactSyncTarget>

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#endif

namespace {

QList<QContact> getAllSimContacts(const QContactManager &m)
{
    QContactDetailFilter stFilter;
    stFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    stFilter.setValue(QStringLiteral("sim-test"));

    return m.contacts(stFilter);
}

}

// In this test, we bypass the contacts daemon entirely, and test the controller
// class which contains all the logic, without involving the real SIM at all.

TestSimPlugin::TestSimPlugin(QObject *parent) :
    QObject(parent)
{
}

void TestSimPlugin::init()
{
}

void TestSimPlugin::initTestCase()
{
    m_controller = new CDSimController;
    m_controller->setSyncTarget(QStringLiteral("sim-test"));
}

void TestSimPlugin::testAdd()
{
    QContactManager &m(m_controller->contactManager());

    QCOMPARE(getAllSimContacts(m).count(), 0);
    QCOMPARE(m_controller->busy(), false);

    // We don't have a modem path configured, so the controller can't query the SIM
    m_controller->simPresenceChanged(true);
    QCOMPARE(m_controller->busy(), false);

    // Normally, a sim-present notification would cause the controller to fetch
    // VCard data from the SIM. Instead, we bypass the first part here, and supply
    // the VCard data directly
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1212\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);

    QTRY_VERIFY(m_controller->busy() == false);

    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).detail<QContactPhoneNumber>().number(), QStringLiteral("(404) 555-1212"));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().contexts().contains(QContactDetail::ContextHome));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().subTypes().contains(QContactPhoneNumber::SubTypeVoice));
}

void TestSimPlugin::testAppend()
{
    QContactManager &m(m_controller->contactManager());

    // Add the Forrest Gump contact manually
    QContact forrest;

    QContactNickname n;
    n.setNickname("Forrest Gump");
    forrest.saveDetail(&n);

    QContactSyncTarget st;
    st.setSyncTarget(QStringLiteral("sim-test"));
    forrest.saveDetail(&st);

    QVERIFY(m.saveContact(&forrest));

    QCOMPARE(getAllSimContacts(m).count(), 1);
    QCOMPARE(m_controller->busy(), false);

    // Process a VCard set containing Forrest Gump and another contact
    m_controller->simPresenceChanged(true);
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1212\n"
"END:VCARD\n"
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Whittaker\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1234\n"
"END:VCARD\n"));
    QTRY_VERIFY(m_controller->busy() == false);

    // Forrest Whittaker should be added
    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 2);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(1).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Whittaker"));
}

void TestSimPlugin::testRemove()
{
    QContactManager &m(m_controller->contactManager());

    // Add two contacts manually
    QContact gump;

    QContactNickname n;
    n.setNickname("Forrest Gump");
    gump.saveDetail(&n);

    QContactSyncTarget st;
    st.setSyncTarget(QStringLiteral("sim-test"));
    gump.saveDetail(&st);

    QContact whittaker;

    n.setNickname("Forrest Whittaker");
    whittaker.saveDetail(&n);

    st.setSyncTarget(QStringLiteral("sim-test"));
    whittaker.saveDetail(&st);

    QVERIFY(m.saveContact(&gump));
    QVERIFY(m.saveContact(&whittaker));

    QCOMPARE(getAllSimContacts(m).count(), 2);
    QCOMPARE(m_controller->busy(), false);

    // Process a VCard set containing only Forrest Whittaker
    m_controller->simPresenceChanged(true);
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Whittaker\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1234\n"
"END:VCARD\n"));
    QTRY_VERIFY(m_controller->busy() == false);

    // Forrest Gump should be removed
    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Whittaker"));
}

void TestSimPlugin::testAddAndRemove()
{
    QContactManager &m(m_controller->contactManager());

    // Add the Forrest Gump contact manually
    QContact forrest;

    QContactNickname n;
    n.setNickname("Forrest Gump");
    forrest.saveDetail(&n);

    QContactSyncTarget st;
    st.setSyncTarget(QStringLiteral("sim-test"));
    forrest.saveDetail(&st);

    QVERIFY(m.saveContact(&forrest));

    QCOMPARE(getAllSimContacts(m).count(), 1);
    QCOMPARE(m_controller->busy(), false);

    // Process a VCard set not containing Forrest Gump but another contact
    m_controller->simPresenceChanged(true);
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Whittaker\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1234\n"
"END:VCARD\n"));
    QTRY_VERIFY(m_controller->busy() == false);

    // Forrest Gump should be removed, Forrest Whittaker added
    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Whittaker"));
}

void TestSimPlugin::testChangedNumber()
{
    QContactManager &m(m_controller->contactManager());

    QCOMPARE(getAllSimContacts(m).count(), 0);
    QCOMPARE(m_controller->busy(), false);

    m_controller->simPresenceChanged(true);
    QCOMPARE(m_controller->busy(), false);

    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1212\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).detail<QContactPhoneNumber>().number(), QStringLiteral("(404) 555-1212"));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().contexts().contains(QContactDetail::ContextHome));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().subTypes().contains(QContactPhoneNumber::SubTypeVoice));

    QContactId existingId = simContacts.at(0).id();

    // Change the number and verify that it is updated in the database, but the contact was not recreated
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=WORK,VIDEO:(404) 555-6789\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    simContacts = getAllSimContacts(m);
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).id(), existingId);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).detail<QContactPhoneNumber>().number(), QStringLiteral("(404) 555-6789"));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().contexts().contains(QContactDetail::ContextWork));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().subTypes().contains(QContactPhoneNumber::SubTypeVideo));

    // Change the context and verify that it is updated in the database
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=HOME,VIDEO:(404) 555-6789\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    simContacts = getAllSimContacts(m);
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).id(), existingId);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).detail<QContactPhoneNumber>().number(), QStringLiteral("(404) 555-6789"));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().contexts().contains(QContactDetail::ContextHome));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().subTypes().contains(QContactPhoneNumber::SubTypeVideo));

    // Change the subtype and verify that it is updated in the database
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=HOME,VOICE,CELL:(404) 555-6789\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    simContacts = getAllSimContacts(m);
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).id(), existingId);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).detail<QContactPhoneNumber>().number(), QStringLiteral("(404) 555-6789"));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().contexts().contains(QContactDetail::ContextHome));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().subTypes().contains(QContactPhoneNumber::SubTypeVoice));
    QVERIFY(simContacts.at(0).detail<QContactPhoneNumber>().subTypes().contains(QContactPhoneNumber::SubTypeMobile));
}

void TestSimPlugin::testMultipleNumbers()
{
    QContactManager &m(m_controller->contactManager());

    QCOMPARE(getAllSimContacts(m).count(), 0);
    QCOMPARE(m_controller->busy(), false);

    m_controller->simPresenceChanged(true);
    QCOMPARE(m_controller->busy(), false);

    // Add a contact with two numbers
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=HOME,VOICE:(404) 555-1212\n"
"TEL;TYPE=WORK,VIDEO:(404) 555-6789\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).details<QContactPhoneNumber>().count(), 2);
    foreach (const QContactPhoneNumber &number, simContacts.at(0).details<QContactPhoneNumber>()) {
        if (number.number() == QStringLiteral("(404) 555-1212")) {
            QVERIFY(number.contexts().contains(QContactDetail::ContextHome));
            QVERIFY(number.subTypes().contains(QContactPhoneNumber::SubTypeVoice));
        } else {
            QCOMPARE(number.number(), QStringLiteral("(404) 555-6789"));
            QVERIFY(number.contexts().contains(QContactDetail::ContextWork));
            QVERIFY(number.subTypes().contains(QContactPhoneNumber::SubTypeVideo));
        }
    }

    QContactId existingId = simContacts.at(0).id();

    // Change the set of numbers
    m_controller->vcardDataAvailable(QStringLiteral(
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"FN:Forrest Gump\n"
"TEL;TYPE=WORK,VIDEO:(404) 555-6789\n"
"TEL;TYPE=HOME,CELL:(404) 555-5555\n"
"TEL;TYPE=WORK,VOICE:(404) 555-4321\n"
"END:VCARD\n"));
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    // Verify that numbers were added and removed, but the contact was not recreated
    simContacts = getAllSimContacts(m);
    QCOMPARE(simContacts.count(), 1);
    QCOMPARE(simContacts.at(0).id(), existingId);
    QCOMPARE(simContacts.at(0).detail<QContactNickname>().nickname(), QStringLiteral("Forrest Gump"));
    QCOMPARE(simContacts.at(0).details<QContactPhoneNumber>().count(), 3);
    foreach (const QContactPhoneNumber &number, simContacts.at(0).details<QContactPhoneNumber>()) {
        if (number.number() == QStringLiteral("(404) 555-6789")) {
            QVERIFY(number.contexts().contains(QContactDetail::ContextWork));
            QVERIFY(number.subTypes().contains(QContactPhoneNumber::SubTypeVideo));
        } else if (number.number() == QStringLiteral("(404) 555-5555")) {
            QVERIFY(number.contexts().contains(QContactDetail::ContextHome));
            QVERIFY(number.subTypes().contains(QContactPhoneNumber::SubTypeMobile));
        } else {
            QCOMPARE(number.number(), QStringLiteral("(404) 555-4321"));
            QVERIFY(number.contexts().contains(QContactDetail::ContextWork));
            QVERIFY(number.subTypes().contains(QContactPhoneNumber::SubTypeVoice));
        }
    }
}

void TestSimPlugin::testEmpty()
{
    QContactManager &m(m_controller->contactManager());

    // Add the Forrest Gump contact manually
    QContact forrest;

    QContactNickname n;
    n.setNickname("Forrest Gump");
    forrest.saveDetail(&n);

    QContactSyncTarget st;
    st.setSyncTarget(QStringLiteral("sim-test"));
    forrest.saveDetail(&st);

    QVERIFY(m.saveContact(&forrest));

    QCOMPARE(getAllSimContacts(m).count(), 1);
    QCOMPARE(m_controller->busy(), false);

    // Process an empty VCard
    m_controller->simPresenceChanged(true);
    m_controller->vcardDataAvailable(QString());
    QCOMPARE(m_controller->busy(), true);

    QTRY_VERIFY(m_controller->busy() == false);

    // No-longer-present contact should be removed
    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 0);
}

void TestSimPlugin::testClear()
{
    QContactManager &m(m_controller->contactManager());

    // Add two contacts manually
    QContact gump;

    QContactNickname n;
    n.setNickname("Forrest Gump");
    gump.saveDetail(&n);

    QContactSyncTarget st;
    st.setSyncTarget(QStringLiteral("sim-test"));
    gump.saveDetail(&st);

    QContact whittaker;

    n.setNickname("Forrest Whittaker");
    whittaker.saveDetail(&n);

    st.setSyncTarget(QStringLiteral("sim-test"));
    whittaker.saveDetail(&st);

    QVERIFY(m.saveContact(&gump));
    QVERIFY(m.saveContact(&whittaker));

    QCOMPARE(getAllSimContacts(m).count(), 2);
    QCOMPARE(m_controller->busy(), false);

    m_controller->simPresenceChanged(true);

    // Report the SIM card removed
    m_controller->simPresenceChanged(false);
    QCOMPARE(m_controller->busy(), true);
    QTRY_VERIFY(m_controller->busy() == false);

    // All sim contacts should be removed
    QList<QContact> simContacts(getAllSimContacts(m));
    QCOMPARE(simContacts.count(), 0);
}

void TestSimPlugin::cleanupTestCase()
{
}

void TestSimPlugin::cleanup()
{
    QContactManager &m(m_controller->contactManager());

    foreach (const QContact &contact, getAllSimContacts(m)) {
        QVERIFY(m.removeContact(contact.id()));
    }
}

CONTACTSD_TEST_MAIN(TestSimPlugin)
