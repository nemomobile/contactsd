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

#ifndef CDBIRTHDAYCALENDAR_H
#define CDBIRTHDAYCALENDAR_H

#include <QObject>
#include <QContact>
#include <QDate>

#include <extendedstorage.h>
#include <extendedcalendar.h>

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#else
QTM_USE_NAMESPACE
#endif

class CalendarBirthday
{
public:
    CalendarBirthday(const QDate &date, const QString &summary)
    : mDate(date), mSummary(summary) {}
    CalendarBirthday() {}

public:
    const QDate & date() const { return mDate; }
    const QString & summary() const { return mSummary; }

private:
    QDate mDate;
    QString mSummary;
};


class CDBirthdayCalendar : public QObject
{
    Q_OBJECT

public:
#ifdef USING_QTPIM
    typedef QContactId ContactIdType;
#else
    typedef QContactLocalId ContactIdType;
#endif

    enum SyncMode {
        KeepOldDB,
        DropOldDB
    };

    //! Constructor.
    explicit CDBirthdayCalendar(SyncMode syncMode, QObject *parent = 0);
    ~CDBirthdayCalendar();

    //! Updates and saves Birthday of \a contact to calendar.
    void updateBirthday(const QContact &contact);

    //! Deletes \a contact birthday from calendar.
    void deleteBirthday(ContactIdType contactId);

    //! Actually save the events in the calendar database
    void save();

    CalendarBirthday birthday(ContactIdType contactId);
    QHash<ContactIdType, CalendarBirthday> birthdays();

private:
    mKCal::Notebook::Ptr createNotebook();

    static ContactIdType localContactId(const QString &calendarEventId);
    static QString calendarEventId(ContactIdType contactId);

    KCalCore::Event::Ptr calendarEvent(ContactIdType contactId);

private Q_SLOTS:
    void onLocaleChanged();

private:
    mKCal::ExtendedCalendar::Ptr mCalendar;
    mKCal::ExtendedStorage::Ptr mStorage;
};

#endif // CDBIRTHDAYCALENDAR_H
