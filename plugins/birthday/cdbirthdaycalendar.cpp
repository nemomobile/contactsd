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

#include <QStringBuilder>

#include <QContactName>
#include <QContactBirthday>

#include <MLocale>

#include <recurrencerule.h>

#include "cdbirthdaycalendar.h"
#include "debug.h"

using namespace Contactsd;
using namespace ML10N;

// A random ID.
const QLatin1String calNotebookId("b1376da7-5555-1111-2222-227549c4e570");
const QLatin1String calNotebookColor("#e00080"); // Pink
const QString calIdExtension = QLatin1String("com.nokia.birthday/");


CDBirthdayCalendar::CDBirthdayCalendar(SyncMode syncMode, QObject *parent) :
    QObject(parent),
    mCalendar(0),
    mStorage(0)
{
    mCalendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    mStorage = mKCal::ExtendedCalendar::defaultStorage(mCalendar);

    MLocale * const locale = new MLocale(this);

    if (not locale->isInstalledTrCatalog(QLatin1String("calendar"))) {
        locale->installTrCatalog(QLatin1String("calendar"));
    }

    locale->connectSettings();
    connect(locale, SIGNAL(settingsChanged()), this, SLOT(onLocaleChanged()));

    MLocale::setDefault(*locale);

    mStorage->open();

    mKCal::Notebook::Ptr notebook = mStorage->notebook(calNotebookId);

    if (notebook.isNull()) {
        notebook = createNotebook();
        mStorage->addNotebook(notebook);
    } else {
        // Clear the calendar database if and only if restoring from a backup.
        switch(syncMode) {
        case KeepOldDB:
            // Force calendar name update, if a locale change happened while contactsd was not running.
            onLocaleChanged();
            break;

        case DropOldDB:
            mStorage->deleteNotebook(notebook);
            notebook = createNotebook();
            mStorage->addNotebook(notebook);
            break;
        }
    }
}

CDBirthdayCalendar::~CDBirthdayCalendar()
{
    if (mStorage) {
        mStorage->close();
    }

    debug() << "Destroyed birthday calendar";
}

mKCal::Notebook::Ptr CDBirthdayCalendar::createNotebook()
{
    return mKCal::Notebook::Ptr(new mKCal::Notebook(calNotebookId,
                                                    qtTrId("qtn_caln_birthdays"),
                                                    QLatin1String(""),
                                                    calNotebookColor,
                                                    false, // Not shared.
                                                    true, // Is master.
                                                    false, // Not synced to Ovi.
                                                    false, // Writable.
                                                    true, // Visible.
                                                    QLatin1String("Birthday-Nokia"),
                                                    QLatin1String(""),
                                                    0));
}

QHash<CDBirthdayCalendar::ContactIdType, CalendarBirthday>
CDBirthdayCalendar::birthdays()
{
    if (not mStorage->loadNotebookIncidences(calNotebookId)) {
        warning() << Q_FUNC_INFO << "Failed to load all incidences";
        return QHash<ContactIdType, CalendarBirthday>();
    }

    QHash<ContactIdType, CalendarBirthday> result;

    foreach(const KCalCore::Event::Ptr event, mCalendar->events()) {
        const QString eventUid = event->uid();
        const ContactIdType contactId = localContactId(eventUid);

#ifdef USING_QTPIM
        if (!contactId.isNull()) {
#else
        if (0 != contactId) {
#endif
            result.insert(contactId, CalendarBirthday(event->dtStart().date(), event->summary()));
        } else {
            warning() << Q_FUNC_INFO << "Birthday event with a bad uid: " << eventUid;
        }
    }

    return result;
}

#ifdef USING_QTPIM
static QContactId contactId(const QContact &contact) { return contact.id(); }
#else
static QContactLocalId contactId(const QContact &contact) { return contact.localId(); }
#endif

void CDBirthdayCalendar::updateBirthday(const QContact &contact)
{
    // Retrieve contact details.
#ifdef USING_QTPIM
    const QString displayLabel = contact.detail<QContactDisplayLabel>().label();
#else
    const QString displayLabel = contact.displayLabel();
#endif
    const QDate contactBirthday = contact.detail<QContactBirthday>().date();

    if (displayLabel.isEmpty() || contactBirthday.isNull()) {
        warning() << Q_FUNC_INFO << "Contact without name or birthday, local ID: "
                  << contactId(contact);
        return;
    }

    // Retrieve birthday event.
    if (not mStorage->isValidNotebook(calNotebookId)) {
        warning() << Q_FUNC_INFO << "Invalid notebook ID: " << calNotebookId;
        return;
    }

    KCalCore::Event::Ptr event = calendarEvent(contactId(contact));

    if (event.isNull()) {
        // Add a new event.
        event = KCalCore::Event::Ptr(new KCalCore::Event());
        event->startUpdates();
        event->setUid(calendarEventId(contactId(contact)));
        event->setAllDay(true);

        // Ensure events appear as birthdays in the calendar, NB#259710.
        event->setCategories(QStringList() << QLatin1String("BIRTHDAY"));

        if (not mCalendar->addEvent(event, calNotebookId)) {
            warning() << Q_FUNC_INFO << "Failed to add event to calendar";
            return;
        }
    } else {
        // Update the existing event.
        event->setReadOnly(false);
        event->startUpdates();
    }

    // Transfer birthday details from contact to calendar event.
    event->setSummary(displayLabel);

    // Event has only date information, no time.
    event->setDtStart(KDateTime(contactBirthday, QTime(), KDateTime::ClockTime));
    event->setDtEnd(KDateTime(contactBirthday.addDays(1), QTime(), KDateTime::ClockTime));

    // Must always set the recurrence as it depends on the event date.
    KCalCore::Recurrence *const recurrence = event->recurrence();

    if (contactBirthday.month() != 2 || contactBirthday.day() < 29) {
        // Simply setup yearly recurrence for trivial dates.
        recurrence->setStartDateTime(event->dtStart());
        recurrence->setYearly(1); /* every year */
    } else {
        // For birthdays on February 29th the event shall occur on the
        // last day of February. This is February 29th in leap years,
        // and February 28th in all other years.
        //
        // NOTE: Actually this recurrence pattern will fail badly for
        // people born on February 29th of the years 2100, 2200, 2300,
        // 2500, ... - but I seriously doubt we care.
        //
        // NOTE2: Using setByYearDays() instead of just setting proper
        // start dates, since libmkcal fails to store the start dates
        // of recurrence rules.
        KCalCore::RecurrenceRule *rule;

        // 1. Include February 29th in leap years.
        rule = new KCalCore::RecurrenceRule;
        rule->setStartDt(event->dtStart());
        rule->setByYearDays(QList<int>() << 60); // Feb 29th
        rule->setRecurrenceType(KCalCore::RecurrenceRule::rYearly);
        rule->setFrequency(4); // every 4th year
        recurrence->addRRule(rule);

        // 2. Include February 28th starting from year after birth.
        rule = new KCalCore::RecurrenceRule;
        rule->setStartDt(event->dtStart());
        rule->setByYearDays(QList<int>() << 59); // Feb 28th
        rule->setRecurrenceType(KCalCore::RecurrenceRule::rYearly);
        rule->setFrequency(1); // every year
        recurrence->addRRule(rule);

        // 3. Exclude February 28th in leap years.
        rule = new KCalCore::RecurrenceRule;
        rule->setStartDt(event->dtStart());
        rule->setByYearDays(QList<int>() << 59); // Feb 28th
        rule->setRecurrenceType(KCalCore::RecurrenceRule::rYearly);
        rule->setFrequency(4); // every 4th year
        recurrence->addExRule(rule);

    }

    // Set the alarms on the event
    event->clearAlarms();
    KCalCore::Alarm::Ptr alarm = event->newAlarm();
    alarm->setType(KCalCore::Alarm::Audio);
    alarm->setEnabled(true);
    alarm->setDisplayAlarm(event->summary());
    alarm->setStartOffset(KCalCore::Duration(-36 * 3600 /* seconds */));

    event->setReadOnly(true);
    event->endUpdates();

    debug() << "Updated birthday event in calendar, local ID: " << contactId(contact);
}

void CDBirthdayCalendar::deleteBirthday(ContactIdType contactId)
{
    KCalCore::Event::Ptr event = calendarEvent(contactId);

    if (event.isNull()) {
        debug() << Q_FUNC_INFO << "Not found in calendar:" << contactId;
        return;
    }

    mCalendar->deleteEvent(event);

    debug() << "Deleted birthday event in calendar, local ID: " << event->uid();
}

void CDBirthdayCalendar::save()
{
    if (not mStorage->save()) {
        warning() << Q_FUNC_INFO << "Failed to update birthdays in calendar";
    }
}

CalendarBirthday CDBirthdayCalendar::birthday(ContactIdType contactId)
{
    KCalCore::Event::Ptr event = calendarEvent(contactId);

    if (event.isNull()) {
        return CalendarBirthday();
    }

    return CalendarBirthday(event->dtStart().date(), event->summary());
}

#ifdef USING_QTPIM
quint32 numericContactId(const QContactId &id)
{
    // Note: only works with the qtcontacts-sqlite backend
    if (!id.isNull()) {
        QStringList components = id.toString().split(QChar::fromLatin1(':'));
        const QString &idComponent = components.isEmpty() ? QString() : components.last();
        if (idComponent.startsWith(QString::fromLatin1("sql-"))) {
            return idComponent.mid(4).toUInt();
        }
    }
    return 0;
}
QContactId fromNumericContactId(quint32 id)
{
    // Note: only works with the qtcontacts-sqlite backend
    static const QString idStr(QStringLiteral("qtcontacts:org.nemomobile.contacts.sqlite::sql-%1"));
    return QContactId::fromString(idStr.arg(id));
}
#else
const QContactLocalId &numericContactId(const QContactLocalId &id)
{
    return id;
}
QContactLocalId fromNumericContactId(quint32 id)
{
    return id;
}
#endif

CDBirthdayCalendar::ContactIdType CDBirthdayCalendar::localContactId(const QString &calendarEventId)
{
    quint32 numericId = 0;

    if (calendarEventId.startsWith(calIdExtension)) {
        numericId = calendarEventId.mid(calIdExtension.length()).toUInt();
    }

    return fromNumericContactId(numericId);
}

QString CDBirthdayCalendar::calendarEventId(ContactIdType contactId)
{
    return calIdExtension + QString::number(numericContactId(contactId));
}

KCalCore::Event::Ptr CDBirthdayCalendar::calendarEvent(ContactIdType contactId)
{
    const QString eventId = calendarEventId(contactId);

    if (not mStorage->load(eventId)) {
        warning() << Q_FUNC_INFO << "Unable to load event from calendar";
        return KCalCore::Event::Ptr();
    }

    KCalCore::Event::Ptr event = mCalendar->event(eventId);

    if (event.isNull()) {
        debug() << Q_FUNC_INFO << "Not found in calendar:" << contactId;
    }

    return event;
}

void CDBirthdayCalendar::onLocaleChanged()
{
    mKCal::Notebook::Ptr notebook = mStorage->notebook(calNotebookId);

    if (notebook.isNull()) {
        warning() << Q_FUNC_INFO << "Calendar not found while changing locale";
        return;
    }

    const QString name = qtTrId("qtn_caln_birthdays");

    debug() << Q_FUNC_INFO << "Updating calendar name to" << name;
    notebook->setName(name);

    if (not mStorage->updateNotebook(notebook)) {
        warning() << Q_FUNC_INFO << "Could not save calendar";
    }
}
