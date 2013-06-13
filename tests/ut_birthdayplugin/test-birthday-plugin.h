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

#ifndef TEST_BIRTHDAYPLUGIN_H
#define TEST_BIRTHDAYPLUGIN_H

#include <QObject>
#include <QtTest/QtTest>

#include <event.h>

#include <QContact>
#include <QContactManager>

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#else
QTM_USE_NAMESPACE
#endif

class TestBirthdayPlugin : public QObject
{
    Q_OBJECT
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    Q_PLUGIN_METADATA(IID "org.nemomobile.contactsd.test-birthday")
#endif

public:
    explicit TestBirthdayPlugin(QObject *parent = 0);

private Q_SLOTS:
    void init();
    void initTestCase();

    void testAddAndRemoveBirthday();
    void testChangeBirthday();
    void testChangeName();
    void testLocaleChange();

    void testLeapYears_data();
    void testLeapYears();

    void cleanupTestCase();
    void cleanup();

private:
    int countCalendarEvents(const KCalCore::Event::List &eventList,
                            const QContact &contact) const;
    KCalCore::Event::List findCalendarEvents(const KCalCore::Event::List &eventList,
                                             const QContact &contact) const;
    bool saveContact(QContact &contact);

private:
    QContactManager *mManager;
#ifdef USING_QTPIM
    QSet<QContactId> mContactIDs;
#else
    QSet<QContactLocalId> mContactIDs;
#endif
};

#endif // TEST_BIRTHDAYPLUGIN_H
