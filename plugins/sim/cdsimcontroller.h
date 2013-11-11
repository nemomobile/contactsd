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
#include <QContactDetailFilter>

#include <QVersitReader>

#include <qofonophonebook.h>
#include <qofonosimmanager.h>
#include <qofonomessagewaiting.h>

#include <MGConfItem>

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
    explicit CDSimController(QObject *parent = 0, const QString &syncTarget = QString::fromLatin1("sim"));
    ~CDSimController();

    QContactManager &contactManager();

    void setModemPath(const QString &path);

    bool busy() const;

Q_SIGNALS:
    void busyChanged(bool);

public Q_SLOTS:
    void simPresenceChanged(bool present);
    void vcardDataAvailable(const QString &vcardData);
    void vcardReadFailed();
    void readerStateChanged(QVersitReader::State state);
    void voicemailConfigurationChanged();

private:
    void setBusy(bool busy);
    void removeAllSimContacts();
    void ensureSimContactsPresent();
    void updateVoicemailConfiguration();
    QContactDetailFilter simSyncTargetFilter() const;

private:
    QContactManager m_manager;
    QVersitReader m_contactReader;

    QOfonoSimManager m_simManager;
    bool m_simPresent;

    QString m_simSyncTarget;
    QString m_modemPath;
    QOfonoPhonebook m_phonebook;
    QOfonoMessageWaiting m_messageWaiting;

    QList<QContact> m_simContacts;
    bool m_busy;

    MGConfItem *m_voicemailConf;
};

#endif // CDSIMCONTROLLER_H
