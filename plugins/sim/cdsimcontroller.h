/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2013-2014 Jolla Ltd.
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

#include <qofonomodem.h>
#include <qofonophonebook.h>
#include <qofonosimmanager.h>
#include <qofonomessagewaiting.h>
#include <qofonoextsimsettings.h>

#include <MGConfItem>

QTCONTACTS_USE_NAMESPACE
QTVERSIT_USE_NAMESPACE

class CDSimModemData;
class CDSimController : public QObject
{
    Q_OBJECT

public:
    explicit CDSimController(QObject *parent = 0, const QString &syncTarget = QString::fromLatin1("sim"), bool active = true);
    ~CDSimController();

    QContactManager &contactManager();

    bool busy() const;

Q_SIGNALS:
    void busyChanged(bool);

public Q_SLOTS:
    void setModemPaths(const QStringList &paths);
    void transientImportConfigurationChanged();
    void modemReadyChanged(bool ready);

private:
    void updateBusy();
    void timerEvent(QTimerEvent *event);
    void removeObsoleteSimContacts();

    QContactDetailFilter simSyncTargetFilter() const;

private:
    friend class CDSimModemData;
    friend class TestSimPlugin;

    QContactManager m_manager;
    bool m_transientImport;
    QString m_simSyncTarget;
    bool m_busy;
    bool m_active;
    MGConfItem m_transientImportConf;
    QBasicTimer m_readyTimer;

    QMap<QString, CDSimModemData *> m_modems;
    QSet<QString> m_absentModemPaths;
};

class CDSimModemData : public QObject
{
    Q_OBJECT

public:
    CDSimModemData(CDSimController *controller, const QString &modemPath);
    ~CDSimModemData();

    CDSimController *controller() const;
    QContactManager &manager() const;

    QString modemIdentifier() const;
    QContactFilter modemFilter(const QString &cardIdentifier) const;

    bool ready() const;
    void setReady(bool ready);

    void updateBusy();

Q_SIGNALS:
    void readyChanged(bool ready);

public Q_SLOTS:
    void simStateChanged();
    void vcardDataAvailable(const QString &vcardData);
    void vcardReadFailed();
    void readerStateChanged(QVersitReader::State state);
    void voicemailConfigurationChanged();
    void phonebookValidChanged(bool valid);

public:
    void deactivateAllSimContacts();
    void removeAllSimContacts();
    void ensureSimContactsPresent();
    void updateVoicemailConfiguration();
    void performTransientImport();

    void timerEvent(QTimerEvent *event);

    QString m_modemPath;
    QOfonoSimManager m_simManager;
    QOfonoPhonebook m_phonebook;
    QOfonoMessageWaiting m_messageWaiting;
    QOfonoExtSimSettings m_simSettings;
    MGConfItem *m_voicemailConf;
    QVersitReader m_contactReader;
    QList<QContact> m_simContacts;
    QBasicTimer m_retryTimer;
    bool m_ready;
    int m_retries;
};

#endif // CDSIMCONTROLLER_H
