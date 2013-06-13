/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2011 Nokia Corporation and/or its subsidiary(-ies).
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

#include "test-birthday-plugin.h"

#include <test-common.h>

#include <extendedstorage.h>
#include <extendedcalendar.h>

#include <MGConfItem>
#include <MLocale>

#include <QContactBirthday>
#include <QContactName>

using namespace ML10N;

// A random ID, from plugins/birthday/cdbirthdaycalendar.cpp.
const QLatin1String calNotebookId("b1376da7-5555-1111-2222-227549c4e570");

static const int calendarTimeout = 12000; // ms

static void loopWait(int ms)
{
    QTimer timer;
    timer.setInterval(ms);

    qDebug() << "Waiting for" << (ms/1000) << "seconds";

    QEventLoop loop;
    QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    timer.start();
    loop.exec();
}

#ifdef USING_QTPIM
QContactId apiId(const QContact &contact) { return contact.id(); }
#else
QContactLocalId apiId(const QContact &contact) { return contact.localId(); }
#endif


TestBirthdayPlugin::TestBirthdayPlugin(QObject *parent) :
    QObject(parent),
    mManager(0)
{
}

void TestBirthdayPlugin::init()
{
#ifdef USING_QTPIM
    // Temporary override until qtpim supports QTCONTACTS_MANAGER_OVERRIDE
    mManager = new QContactManager(QStringLiteral("org.nemomobile.contacts.sqlite"));
#else
    mManager = new QContactManager;
#endif
}

void TestBirthdayPlugin::initTestCase()
{
}

void TestBirthdayPlugin::testAddAndRemoveBirthday()
{
    const QString contactID = QUuid::createUuid().toString();
    const QDateTime contactBirthDate = QDateTime::currentDateTime();

    // Add contact with birthday to tracker.
    QContactName contactName;
    contactName.setFirstName(contactID);
    QContactBirthday contactBirthday;
    contactBirthday.setDateTime(contactBirthDate);
    QContact contact;
    QVERIFY(contact.saveDetail(&contactName));
    QVERIFY(contact.saveDetail(&contactBirthday));
    QVERIFY2(mManager->saveContact(&contact), "Error saving contact to tracker");

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Open calendar database, which should have been created by the birthday plugin.
    mKCal::ExtendedCalendar::Ptr calendar =
        mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    mKCal::ExtendedStorage::Ptr storage =
        mKCal::ExtendedCalendar::defaultStorage(calendar);
    storage->open();
    QVERIFY2(not storage->notebook(calNotebookId).isNull(), "No calendar database found");

    // Check calendar database for contact.
    QVERIFY2(storage->loadNotebookIncidences(calNotebookId), "Unable to load events from notebook");
    KCalCore::Event::List eventList = calendar->events();
    QCOMPARE(countCalendarEvents(eventList, contact), 1);

    // Delete the contact and see if the birthday is also deleted.
    QVERIFY2(mManager->removeContact(apiId(contact)), "Unable to delete test contact from tracker database");

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Search for any events in the calendar.
    QVERIFY2(storage->loadNotebookIncidences(calNotebookId), "Unable to load events from notebook");
    eventList = calendar->events();
    QCOMPARE(countCalendarEvents(eventList, contact), 0);

    // Close the calendar.
    QVERIFY2(storage->close(), "Error closing the calendar");
}

void TestBirthdayPlugin::testChangeBirthday()
{
    const QString contactID = QUuid::createUuid().toString();
    const QDateTime contactBirthDate = QDateTime::currentDateTime();

    // Add contact with birthday to tracker.
    QContactName contactName;
    contactName.setFirstName(contactID);
    QContactBirthday contactBirthday;
    contactBirthday.setDateTime(contactBirthDate);
    QContact contact;
    QVERIFY(contact.saveDetail(&contactName));
    QVERIFY(contact.saveDetail(&contactBirthday));
    QVERIFY2(mManager->saveContact(&contact), "Error saving contact to tracker");

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Open calendar database.
    mKCal::ExtendedCalendar::Ptr calendar =
        mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    mKCal::ExtendedStorage::Ptr storage =
        mKCal::ExtendedCalendar::defaultStorage(calendar);
    storage->open();
    QVERIFY2(not storage->notebook(calNotebookId).isNull(), "No calendar database found");

    // Check calendar database for contact.
    QVERIFY2(storage->loadNotebookIncidences(calNotebookId), "Unable to load events from notebook");
    KCalCore::Event::List eventList = calendar->events();
    QCOMPARE(countCalendarEvents(eventList, contact), 1);

    // Change the contact and see if the birthday is updated.
    contactBirthday.setDateTime(contactBirthDate.addDays(-3));
    QVERIFY(contact.saveDetail(&contactBirthday));
    QVERIFY2(mManager->saveContact(&contact), "Unable to update test contact in tracker");

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Search for any events in the calendar.
    QVERIFY2(storage->loadNotebookIncidences(calNotebookId), "Unable to load events from notebook");
    eventList = calendar->events();
    QCOMPARE(countCalendarEvents(eventList, contact), 1);

    // Close the calendar.
    QVERIFY2(storage->close(), "Error closing the calendar");
}

void TestBirthdayPlugin::testChangeName()
{
    const QString contactID = QUuid::createUuid().toString();
    const QDateTime contactBirthDate = QDateTime::currentDateTime();

    // Add contact with birthday to tracker.
    QContactName contactName;
    contactName.setFirstName(contactID);
    QContactBirthday contactBirthday;
    contactBirthday.setDateTime(contactBirthDate);
    QContact contact;
    QVERIFY(contact.saveDetail(&contactName));
    QVERIFY(contact.saveDetail(&contactBirthday));
    QVERIFY2(mManager->saveContact(&contact), "Error saving contact to tracker");

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Open calendar database.
    mKCal::ExtendedCalendar::Ptr calendar =
        mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    mKCal::ExtendedStorage::Ptr storage =
        mKCal::ExtendedCalendar::defaultStorage(calendar);
    storage->open();
    QVERIFY2(not storage->notebook(calNotebookId).isNull(), "No calendar database found");

    // Check calendar database for contact.
    QVERIFY2(storage->loadNotebookIncidences(calNotebookId), "Unable to load events from notebook");
    KCalCore::Event::List eventList = calendar->events();
    QCOMPARE(countCalendarEvents(eventList, contact), 1);

    // Change the contact name and see if the calendar is updated.
    const QString newContactID = QUuid::createUuid().toString();
    contactName.setFirstName(newContactID);
    QVERIFY(contact.saveDetail(&contactName));
    // TODO: Should it be necessary to refetch the contact to get the synthesised displayLabel?
    contact = mManager->contact(apiId(contact));
    QVERIFY2(mManager->saveContact(&contact), "Unable to update test contact in tracker");

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Search for any events in the calendar.
    QVERIFY2(storage->loadNotebookIncidences(calNotebookId), "Unable to load events from notebook");
    eventList = calendar->events();
    QCOMPARE(countCalendarEvents(eventList, contact), 1);

    // Close the calendar.
    QVERIFY2(storage->close(), "Error closing the calendar");
}

void TestBirthdayPlugin::testLocaleChange()
{
    MGConfItem store(QLatin1String("/meegotouch/i18n/language"));
    store.set(QLatin1String("en"));

    // Leave the time to react to locale change
    loopWait(1000);

    // Use the C locale so it can be changed to a different locale later.
    MLocale locale;
    QVERIFY2(locale.isValid(), "Invalid locale");

    if (not locale.isInstalledTrCatalog(QLatin1String("calendar"))) {
        locale.installTrCatalog(QLatin1String("calendar"));
    }

    locale.connectSettings();
    MLocale::setDefault(locale);

    // Open calendar database, which should have been created by the birthday plugin.
    mKCal::ExtendedCalendar::Ptr calendar =
        mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    mKCal::ExtendedStorage::Ptr storage =
        mKCal::ExtendedCalendar::defaultStorage(calendar);
    storage->open();
    QVERIFY2(not storage->notebook(calNotebookId).isNull(), "No calendar database found");

    // Check if locale name for calendar matches calendar name.
    //QVERIFY2(locale.isInstalledTrCatalog(QLatin1String("calendar")), "Calendar locale catalog not installed");
    const QString cLocaleCalendarName = qtTrId("qtn_caln_birthdays");
    QCOMPARE(storage->notebook(calNotebookId)->name(), cLocaleCalendarName);

    // Change locale and check name again.
    store.set(QLatin1String("fi"));

    loopWait(calendarTimeout);

    const QString finnishLocaleCalendarName = qtTrId("qtn_caln_birthdays");

    QVERIFY2(storage->notebook(calNotebookId)->name() != cLocaleCalendarName, "Calendar name was not updated on locale change");
    QCOMPARE(storage->notebook(calNotebookId)->name(), finnishLocaleCalendarName);

    // Close the calendar.
    QVERIFY2(storage->close(), "Error closing the calendar");
}

void TestBirthdayPlugin::testLeapYears_data()
{
    QTest::addColumn<QDate>("contactBirthDate");
    QTest::newRow("leap-day") << QDate(2008, 2, 29);
    QTest::newRow("regular-day") << QDate(2008, 2, 1);
}

void TestBirthdayPlugin::testLeapYears()
{
    const QString contactID = QUuid::createUuid().toString();
    QFETCH(QDate, contactBirthDate);

    // Add contact with birthday to tracker.
    QContactName contactName;
    contactName.setFirstName(contactID);
    QContactBirthday contactBirthday;
    contactBirthday.setDate(contactBirthDate);
    QContact contact;
    QVERIFY(contact.saveDetail(&contactName));
    QVERIFY(contact.saveDetail(&contactBirthday));
    QVERIFY(saveContact(contact));

    // Wait until calendar event gets to calendar.
    loopWait(calendarTimeout);

    // Open calendar database.
    mKCal::ExtendedCalendar::Ptr calendar =
        mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    mKCal::ExtendedStorage::Ptr storage =
        mKCal::ExtendedCalendar::defaultStorage(calendar);

    QVERIFY(storage->open());
    QVERIFY(not storage->notebook(calNotebookId).isNull());

    // Check calendar database for contact.
    QVERIFY(storage->loadNotebookIncidences(calNotebookId));
    const KCalCore::Event::List eventList = findCalendarEvents(calendar->events(), contact);
    QCOMPARE(eventList.count(), 1);

    const KCalCore::Event::Ptr event = eventList.first();
    QCOMPARE(event->summary(), contactID);
    QCOMPARE(event->dtStart().date(), contactBirthDate);
    QCOMPARE(event->allDay(), true);

    // Check number of recurrences and their values.
    const KDateTime first(QDate(2000, 1, 1), QTime(), KDateTime::ClockTime);
    const KDateTime last(QDate(2020, 12, 31), QTime(), KDateTime::ClockTime);
    const KCalCore::DateTimeList instances = event->recurrence()->timesInInterval(first, last);

    QCOMPARE(instances.length(), 13);

    for(int i = 0; i < instances.length(); ++i) {
        QCOMPARE(instances.at(i).date(), contactBirthDate.addYears(i));
    }
}

void TestBirthdayPlugin::cleanupTestCase()
{
}

void TestBirthdayPlugin::cleanup()
{
    // Remove all contacts modified during the test run.
    // This could fail if the contacts were already removed, so the response is ignored.
    mManager->removeContacts(mContactIDs.toList());

    mContactIDs.clear();

    delete mManager;

    mManager = 0;
}

int TestBirthdayPlugin::countCalendarEvents(const KCalCore::Event::List &eventList,
                                            const QContact &contact) const
{
    return findCalendarEvents(eventList, contact).count();
}

KCalCore::Event::List TestBirthdayPlugin::findCalendarEvents(const KCalCore::Event::List &eventList,
                                                             const QContact &contact) const
{
    KCalCore::Event::List matches;

    Q_FOREACH(const KCalCore::Event::Ptr event, eventList) {
        if(event->dtStart().date() == contact.detail<QContactBirthday>().date()) {
#ifdef USING_QTPIM
            if(event->summary() == contact.detail<QContactDisplayLabel>().label()) {
#else
            if(event->summary() == contact.displayLabel()) {
#endif
                matches += event;
            }
        }
    }

    return matches;
}

bool TestBirthdayPlugin::saveContact(QContact &contact)
{
    const bool success = mManager->saveContact(&contact);

    if (success) {
        mContactIDs.insert(apiId(contact));
    }

    return success;
}

CONTACTSD_TEST_MAIN(TestBirthdayPlugin)
