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

#include "cdsimcontroller.h"
#include "cdsimplugin.h"
#include "debug.h"

#include <QContactDetailFilter>
#include <QContactNickname>
#include <QContactPhoneNumber>
#include <QContactSyncTarget>

#include <QVersitContactImporter>

using namespace Contactsd;

CDSimController::CDSimController(QObject *parent)
    : QObject(parent)
    // Temporary override until qtpim supports QTCONTACTS_MANAGER_OVERRIDE
    , m_manager(QStringLiteral("org.nemomobile.contacts.sqlite"))
    , m_simPresent(false)
    , m_busy(false)
{
    m_fetchIdsRequest.setManager(&m_manager);
    m_fetchRequest.setManager(&m_manager);
    m_saveRequest.setManager(&m_manager);
    m_removeRequest.setManager(&m_manager);

    connect(&m_phonebook, SIGNAL(importReady(const QString &)),
            this, SLOT(vcardDataAvailable(const QString &)));
    connect(&m_phonebook, SIGNAL(importFailed()),
            this, SLOT(vcardReadFailed()));

    connect(&m_contactReader, SIGNAL(stateChanged(QVersitReader::State)),
            this, SLOT(readerStateChanged(QVersitReader::State)));

    connect(&m_fetchIdsRequest, SIGNAL(resultsAvailable()),
            this, SLOT(contactIdsAvailable()));
    connect(&m_fetchIdsRequest, SIGNAL(stateChanged(QContactAbstractRequest::State)),
            this, SLOT(requestStateChanged(QContactAbstractRequest::State)));

    // Fetch only the nickname and phone details for imported contacts
    QContactFetchHint hint;
    hint.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactNickname::Type << QContactPhoneNumber::Type);
    hint.setOptimizationHints(QContactFetchHint::NoRelationships | QContactFetchHint::NoActionPreferences | QContactFetchHint::NoBinaryBlobs);
    m_fetchRequest.setFetchHint(hint);

    connect(&m_fetchRequest, SIGNAL(resultsAvailable()),
            this, SLOT(contactsAvailable()));
    connect(&m_fetchRequest, SIGNAL(stateChanged(QContactAbstractRequest::State)),
            this, SLOT(requestStateChanged(QContactAbstractRequest::State)));

    connect(&m_saveRequest, SIGNAL(stateChanged(QContactAbstractRequest::State)),
            this, SLOT(requestStateChanged(QContactAbstractRequest::State)));

    connect(&m_removeRequest, SIGNAL(stateChanged(QContactAbstractRequest::State)),
            this, SLOT(requestStateChanged(QContactAbstractRequest::State)));

    // Resync the contacts list whenever the SIM is inserted/removed
    connect(&m_simManager, SIGNAL(presenceChanged(bool)),
            this, SLOT(simPresenceChanged(bool)));
}

CDSimController::~CDSimController()
{
}

QContactManager &CDSimController::contactManager()
{
    return m_manager;
}

void CDSimController::setSyncTarget(const QString &syncTarget)
{
    m_syncTarget = syncTarget;

    // Search for contacts with this syncTarget
    QContactDetailFilter syncTargetFilter;
    syncTargetFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    syncTargetFilter.setValue(m_syncTarget);
    m_fetchIdsRequest.setFilter(syncTargetFilter);
}

void CDSimController::setModemPath(const QString &path)
{
    qDebug() << "Using modem path:" << path;
    m_modemPath = path;
    m_simManager.setModemPath(m_modemPath);

    // Sync the contacts list with the initial state
    simPresenceChanged(m_simManager.present());
}

bool CDSimController::busy() const
{
    return m_busy;
}

void CDSimController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged(m_busy);
    }
}

void CDSimController::simPresenceChanged(bool present)
{
    if (m_simPresent != present) {
        qDebug() << "SIM presence changed:" << present;
        m_simPresent = present;

        if (m_syncTarget.isEmpty()) {
            qWarning() << "No sync target is configured";
        } else {
            if (m_simPresent) {
                if (m_modemPath.isEmpty()) {
                    qWarning() << "No modem path is configured";
                } else {
                    // Read all contacts from the SIM
                    m_phonebook.setModemPath(m_modemPath);
                    m_phonebook.beginImport();
                    setBusy(true);
                }
            } else {
                // Find any contacts that we need to remove
                if (!m_fetchIdsRequest.isActive()) {
                    m_contactIds.clear();
                    m_contacts.clear();
                    m_fetchIdsRequest.start();
                    setBusy(true);
                }
            }
        }
    }
}

void CDSimController::vcardDataAvailable(const QString &vcardData)
{
    // Create contact records from the SIM VCard data
    m_simContacts.clear();
    m_contactReader.setData(vcardData.toUtf8());
    m_contactReader.startReading();
    setBusy(true);
}

void CDSimController::vcardReadFailed()
{
    qWarning() << "Unable to read VCard data from SIM:" << m_modemPath;
    setBusy(false);
}

void CDSimController::readerStateChanged(QVersitReader::State state)
{
    if (state != QVersitReader::FinishedState)
        return;

    QList<QVersitDocument> results = m_contactReader.results();
    if (results.isEmpty()) {
        qDebug() << "No contacts found in SIM";
    } else {
        QVersitContactImporter importer;
        importer.importDocuments(results);
        m_simContacts = importer.contacts();
        if (m_simContacts.isEmpty()) {
            qDebug() << "No contacts imported from SIM data";
        }
    }

    // Fetch the set of SIM contact IDs
    m_contactIds.clear();
    m_contacts.clear();
    m_fetchIdsRequest.start();
    setBusy(true);
}

void CDSimController::contactIdsAvailable()
{
    // Ensure we don't duplicate any IDs
    foreach (const QContactId &id, m_fetchIdsRequest.ids()) {
        m_contactIds.insert(id);
    }
}

void CDSimController::requestStateChanged(QContactAbstractRequest::State state)
{
    if (state != QContactAbstractRequest::FinishedState)
        return;

    QContactAbstractRequest *request = qobject_cast<QContactAbstractRequest *>(sender());
    if (request->error() != QContactManager::NoError) {
        qWarning() << "Error:" << request->error() << "from request:" << *request;
    }

    if (request == &m_fetchIdsRequest) {
        if (m_simPresent) {
            if (!m_contactIds.isEmpty()) {
                // Fetch the contact details so we can compare with the SIM contacts
                m_fetchRequest.setIds(m_contactIds.toList());
                m_fetchRequest.start();
            } else {
                ensureSimContactsPresent();
            }
        } else {
            removeAllSimContacts();
        }
    } else if (request == &m_fetchRequest) {
        if (m_simPresent) {
            // Compare the imported contacts with the SIM contacts
            ensureSimContactsPresent();
        } else {
            removeAllSimContacts();
        }
    } else {
        if (!m_saveRequest.isActive() && !m_removeRequest.isActive()) {
            setBusy(false);
        }
    }
}

void CDSimController::contactsAvailable()
{
    m_contacts.append(m_fetchRequest.contacts());
}

void CDSimController::removeAllSimContacts()
{
    if (!m_contactIds.isEmpty()) {
        // Remove all SIM contacts from the store
        m_removeRequest.setContactIds(m_contactIds.toList());
        m_removeRequest.start();
    } else {
        setBusy(false);
    }
}

void CDSimController::ensureSimContactsPresent()
{
    // Ensure all contacts from the SIM are present in the store

    QMap<QString, QContact> existingContacts;
    foreach (const QContact &contact, m_contacts) {
        // Identify imported SIM contacts by their nickname record
        const QString nickname(contact.detail<QContactNickname>().nickname());
        existingContacts.insert(nickname, contact);
    }

    QList<QContact> importContacts;

    foreach (QContact simContact, m_simContacts) {
        // SIM imports have their name in the display label
        QContactDisplayLabel displayLabel = simContact.detail<QContactDisplayLabel>();

        QMap<QString, QContact>::iterator it = existingContacts.find(displayLabel.label());
        if (it != existingContacts.end()) {
            // Ensure this contact has the right phone numbers
            QContact &dbContact(*it);

            QMap<QString, QContactPhoneNumber> existingNumbers;
            foreach (const QContactPhoneNumber &phoneNumber, dbContact.details<QContactPhoneNumber>()) {
                existingNumbers.insert(phoneNumber.number(), phoneNumber);
            }

            bool modified = false;

            foreach (QContactPhoneNumber phoneNumber, simContact.details<QContactPhoneNumber>()) {
                QMap<QString, QContactPhoneNumber>::iterator nit = existingNumbers.find(phoneNumber.number());
                if (nit != existingNumbers.end()) {
                    // Ensure the context and sub-type are correct
                    QContactPhoneNumber &existingNumber(*nit);
                    if (existingNumber.contexts() != phoneNumber.contexts()) {
                        existingNumber.setContexts(phoneNumber.contexts());
                        dbContact.saveDetail(&existingNumber);
                        modified = true;
                    }
                    if (existingNumber.subTypes() != phoneNumber.subTypes()) {
                        existingNumber.setSubTypes(phoneNumber.subTypes());
                        dbContact.saveDetail(&existingNumber);
                        modified = true;
                    }

                    existingNumbers.erase(nit);
                } else {
                    // Add this number to the storedContact
                    dbContact.saveDetail(&phoneNumber);
                    modified = true;
                }
            }

            // Remove any obsolete numbers
            foreach (QContactPhoneNumber phoneNumber, dbContact.details<QContactPhoneNumber>()) {
                if (existingNumbers.contains(phoneNumber.number())) {
                    dbContact.removeDetail(&phoneNumber);
                    modified = true;
                }
            }

            if (modified) {
                // Add the modified contact to the import set
                importContacts.append(dbContact);
            }

            existingContacts.erase(it);
        } else {
            // We need to import this contact

            // Convert the display label to a nickname; display label is managed by the backend
            QContactNickname nickname = simContact.detail<QContactNickname>();
            nickname.setNickname(displayLabel.label());
            simContact.saveDetail(&nickname);

            simContact.removeDetail(&displayLabel);

            QContactSyncTarget syncTarget = simContact.detail<QContactSyncTarget>();
            syncTarget.setSyncTarget(m_syncTarget);
            simContact.saveDetail(&syncTarget);

            importContacts.append(simContact);
        }
    }

    if (!importContacts.isEmpty()) {
        // Import any contacts not currently present
        m_saveRequest.setContacts(importContacts);
        m_saveRequest.start();
    }

    if (!existingContacts.isEmpty()) {
        // Remove any imported contacts no longer on the SIM
        QList<QContactId> obsoleteIds;
        foreach (const QContact &contact, existingContacts.values()) {
            obsoleteIds.append(contact.id());
        }

        m_removeRequest.setContactIds(obsoleteIds);
        m_removeRequest.start();
    }

    setBusy(!importContacts.isEmpty() || !existingContacts.isEmpty());
}

