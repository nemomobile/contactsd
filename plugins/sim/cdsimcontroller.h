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

#ifndef CDSIMCONTROLLER_H
#define CDSIMCONTROLLER_H

#include <QtCore>

#include <QContactManager>
#include <QContact>
#include <QContactIdFetchRequest>
#include <QContactFetchByIdRequest>
#include <QContactSaveRequest>
#include <QContactRemoveRequest>

#include <QVersitReader>

#include <qofonophonebook.h>
#include <qofonosimmanager.h>

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
QTVERSIT_USE_NAMESPACE
#else
#error "SIM plugin has not been ported to QtMobility Contacts/Versit"
#endif

class CDSimController : public QObject
{
    Q_OBJECT

public:
    explicit CDSimController(QObject *parent = 0);
    ~CDSimController();

    QContactManager &contactManager();

    void setSyncTarget(const QString &syncTarget);
    void setModemPath(const QString &path);

    bool busy() const;

Q_SIGNALS:
    void busyChanged(bool);

public Q_SLOTS:
    void simPresenceChanged(bool present);
    void vcardDataAvailable(const QString &vcardData);
    void vcardReadFailed();
    void readerStateChanged(QVersitReader::State state);
    void contactIdsAvailable();
    void requestStateChanged(QContactAbstractRequest::State state);
    void contactsAvailable();

private:
    void setBusy(bool busy);
    void removeAllSimContacts();
    void ensureSimContactsPresent();

private:
    QContactManager m_manager;
    QContactIdFetchRequest m_fetchIdsRequest;
    QContactFetchByIdRequest m_fetchRequest;
    QContactSaveRequest m_saveRequest;
    QContactRemoveRequest m_removeRequest;

    QVersitReader m_contactReader;

    QOfonoSimManager m_simManager;
    bool m_simPresent;

    QString m_syncTarget;
    QString m_modemPath;
    QOfonoPhonebook m_phonebook;

    QSet<QContactId> m_contactIds;
    QList<QContact> m_contacts;
    QList<QContact> m_simContacts;
    bool m_busy;
};

#endif // CDSIMCONTROLLER_H
