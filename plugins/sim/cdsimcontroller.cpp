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
#include "util.h"

#include <QContactDetailFilter>
#include <QContactNickname>
#include <QContactPhoneNumber>
#include <QContactSyncTarget>

#include <QVersitContactImporter>

using namespace Contactsd;

namespace {

QMap<QString, QString> contactManagerParameters()
{
    QMap<QString, QString> rv;
    rv.insert(QStringLiteral("mergePresenceChanges"), QStringLiteral("false"));
    return rv;
}

}

CDSimController::CDSimController(QObject *parent)
    : QObject(parent)
    , m_manager(QStringLiteral("org.nemomobile.contacts.sqlite"), contactManagerParameters())
    , m_simPresent(false)
    , m_transientImport(true)
    , m_busy(false)
    , m_voicemailConf(0)
    , m_transientImportConf(QString::fromLatin1("/org/nemomobile/contacts/sim/transient_import"))
{
    QVariant transientImport = m_transientImportConf.value();
    if (transientImport.isValid())
        m_transientImport = (transientImport.toInt() == 1);

    connect(&m_transientImportConf, SIGNAL(valueChanged()), this, SLOT(transientImportConfigurationChanged()));

    m_fetchIdsRequest.setManager(&m_manager);
    m_fetchRequest.setManager(&m_manager);
    m_saveRequest.setManager(&m_manager);
    m_removeRequest.setManager(&m_manager);

    connect(&m_phonebook, SIGNAL(importReady(const QString &)),
            this, SLOT(vcardDataAvailable(const QString &)));
    connect(&m_phonebook, SIGNAL(importFailed()),
            this, SLOT(vcardReadFailed()));

    connect(&m_messageWaiting, SIGNAL(voicemailMailboxNumberChanged(const QString &)),
            this, SLOT(voicemailConfigurationChanged()));

    connect(&m_contactReader, SIGNAL(stateChanged(QVersitReader::State)),
            this, SLOT(readerStateChanged(QVersitReader::State)));

    connect(&m_fetchIdsRequest, SIGNAL(resultsAvailable()),
            this, SLOT(contactIdsAvailable()));
    connect(&m_fetchIdsRequest, SIGNAL(stateChanged(QContactAbstractRequest::State)),
            this, SLOT(requestStateChanged(QContactAbstractRequest::State)));

    // Fetch only the name, nickname and phone details for imported contacts
    QContactFetchHint hint;
    hint.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactName::Type << QContactNickname::Type << QContactPhoneNumber::Type);
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
    m_messageWaiting.setModemPath(m_modemPath);
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

void CDSimController::performTransientImport()
{
    if (m_syncTarget.isEmpty()) {
        qWarning() << "No sync target is configured";
        return;
    }

    if (m_simPresent && m_transientImport) {
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

void CDSimController::transientImportConfigurationChanged()
{
    bool importEnabled(true);

    QVariant transientImport = m_transientImportConf.value();
    if (transientImport.isValid())
        importEnabled = (transientImport.toInt() == 1);

    if (m_transientImport != importEnabled) {
        m_transientImport = importEnabled;
        performTransientImport();
    }
}

void CDSimController::simPresenceChanged(bool present)
{
    if (m_simPresent != present) {
        qDebug() << "SIM presence changed:" << present;
        m_simPresent = present;
        updateVoicemailConfiguration();
        performTransientImport();
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
        if (m_simPresent && m_transientImport) {
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
        if (m_simPresent && m_transientImport) {
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
        const QString nickname(contact.detail<QContactNickname>().nickname().trimmed());
        existingContacts.insert(nickname, contact);
    }

    // coalesce SIM contacts with the same display label.
    QList<QContact> coalescedSimContacts;
    foreach (const QContact &simContact, m_simContacts) {
        const QContactDisplayLabel &displayLabel = simContact.detail<QContactDisplayLabel>();
        const QList<QContactPhoneNumber> &phoneNumbers = simContact.details<QContactPhoneNumber>();

        // search for a pre-existing match in the coalesced list.
        bool coalescedContactFound = false;
        for (int i = 0; i < coalescedSimContacts.size(); ++i) {
            QContact coalescedContact = coalescedSimContacts.at(i);
            if (coalescedContact.detail<QContactDisplayLabel>().label().trimmed() == displayLabel.label().trimmed()) {
                // found a match.  Coalesce the phone numbers and update the contact in the list.
                QList<QContactPhoneNumber> coalescedPhoneNumbers = coalescedContact.details<QContactPhoneNumber>();
                foreach (QContactPhoneNumber phn, phoneNumbers) {
                    // check to see if the coalesced phone numbers list contains this number
                    bool coalescedNumberFound = false;
                    foreach (const QContactPhoneNumber &cphn, coalescedPhoneNumbers) {
                        if (phn.number() == cphn.number()
                                && phn.contexts() == cphn.contexts()
                                && phn.subTypes() == cphn.subTypes()) {
                            // already exists in the coalesced contact, no need to add it.
                            coalescedNumberFound = true;
                            break;
                        }
                    }

                    // if not, add the number to the coalesced contact and to the coalesced list
                    if (!coalescedNumberFound) {
                        coalescedContact.saveDetail(&phn);
                        coalescedPhoneNumbers.append(phn);
                    }
                }
                coalescedSimContacts.replace(i, coalescedContact);
                coalescedContactFound = true;
                break;
            }
        }

        // no match? add to list.
        if (!coalescedContactFound) {
            coalescedSimContacts.append(simContact);
        }
    }

    QList<QContact> importContacts;

    foreach (QContact simContact, coalescedSimContacts) {
        // SIM imports have their name in the display label
        QContactDisplayLabel displayLabel = simContact.detail<QContactDisplayLabel>();

        // first, remove any duplicate phone number details from the sim contact
        QList<QContactPhoneNumber> existingSimPhoneNumbers;
        foreach (QContactPhoneNumber phoneNumber, simContact.details<QContactPhoneNumber>()) {
            bool foundDuplicateNumber = false;
            for (int i = 0; i < existingSimPhoneNumbers.size(); ++i) {
                if (existingSimPhoneNumbers.at(i).number() == phoneNumber.number()
                        && existingSimPhoneNumbers.at(i).contexts() == phoneNumber.contexts()
                        && existingSimPhoneNumbers.at(i).subTypes() == phoneNumber.subTypes()) {
                    // an exact duplicate of this number already exists in the sim contact.
                    foundDuplicateNumber = true;
                    simContact.removeDetail(&phoneNumber);
                    break;
                }
            }

            if (!foundDuplicateNumber) {
                existingSimPhoneNumbers.append(phoneNumber);
            }
        }

        // then, determine whether this contact is already represented in the device phonebook
        QMap<QString, QContact>::iterator it = existingContacts.find(displayLabel.label().trimmed());
        if (it != existingContacts.end()) {
            // Ensure this contact has the right phone numbers
            QContact &dbContact(*it);

            QList<QContactPhoneNumber> existingNumbers(dbContact.details<QContactPhoneNumber>());
            bool modified = false;
            foreach (QContactPhoneNumber phoneNumber, simContact.details<QContactPhoneNumber>()) {
                bool foundExistingNumber = false;
                for (int i = 0; i < existingNumbers.size(); ++i) {
                    if (existingNumbers.at(i).number() == phoneNumber.number()
                            && existingNumbers.at(i).contexts() == phoneNumber.contexts()
                            && existingNumbers.at(i).subTypes() == phoneNumber.subTypes()) {
                        // this number was not modified.  We don't need to change it.
                        foundExistingNumber = true;
                        existingNumbers.removeAt(i);
                        break;
                    }
                }

                if (!foundExistingNumber) {
                    // this number is new, or modified.  We need to change it.
                    dbContact.saveDetail(&phoneNumber);
                    modified = true;
                }
            }

            // Remove any obsolete numbers
            foreach (QContactPhoneNumber phoneNumber, existingNumbers) {
                dbContact.removeDetail(&phoneNumber);
                modified = true;
            }

            if (modified) {
                // Add the modified contact to the import set
                importContacts.append(dbContact);
            }

            existingContacts.erase(it);
        } else {
            // We need to import this contact

            // Convert the display label to a name, and store the imported
            // display label as the nickname; display label is managed by the backend
            QContactName name = simContact.detail<QContactName>();
            Util::decomposeNameDetails(displayLabel.label().trimmed(), &name);
            simContact.saveDetail(&name);

            QContactNickname nickname = simContact.detail<QContactNickname>();
            nickname.setNickname(displayLabel.label().trimmed());
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

void CDSimController::voicemailConfigurationChanged()
{
    if (!m_voicemailConf || !m_simPresent) {
        // Wait until SIM is present
        return;
    }

    const QString voicemailTarget(QString::fromLatin1("voicemail"));

    QContactDetailFilter syncTargetFilter;
    syncTargetFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    syncTargetFilter.setValue(voicemailTarget);

    QContact voicemailContact;
    foreach (const QContact &contact, m_manager.contacts(syncTargetFilter)) {
        voicemailContact = contact;
        break;
    }

    // If there is a manually configured number, prefer that
    QString voicemailNumber(m_voicemailConf->value().toString());
    if (voicemailNumber.isEmpty()) {
        // Otherwise use the number provided for message waiting
        voicemailNumber = m_messageWaiting.voicemailMailboxNumber();
    }

    if (voicemailNumber.isEmpty()) {
        // Remove the voicemail contact if present
        if (!voicemailContact.id().isNull()) {
            if (!m_manager.removeContact(voicemailContact.id())) {
                qWarning() << "Unable to remove voicemail contact";
            }
        }
    } else {
        // Add/update the voicemail contact if necessary
        QContactPhoneNumber number = voicemailContact.detail<QContactPhoneNumber>();
        if (number.number() == voicemailNumber) {
            // Nothing to change
            return;
        }

        // Update the number
        number.setNumber(voicemailNumber);
        voicemailContact.saveDetail(&number);

        QContactNickname nickname = voicemailContact.detail<QContactNickname>();
        if (nickname.isEmpty()) {
            //: Name for the contact representing the voicemail mailbox
            //% "Voicemail System"
            nickname.setNickname(qtTrId("qtn_sim_voicemail_contact"));
            voicemailContact.saveDetail(&nickname);
        }

        QContactSyncTarget syncTarget = voicemailContact.detail<QContactSyncTarget>();
        if (syncTarget.isEmpty()) {
            syncTarget.setSyncTarget(voicemailTarget);
            voicemailContact.saveDetail(&syncTarget);
        }

        if (!m_manager.saveContact(&voicemailContact)) {
            qWarning() << "Unable to save voicemail contact";
        }
    }
}

void CDSimController::updateVoicemailConfiguration()
{
    QString variablePath(QString::fromLatin1("/sailfish/voicecall/voice_mailbox/"));
    if (m_simPresent) {
        variablePath.append(m_simManager.cardIdentifier());
    } else {
        variablePath.append(QString::fromLatin1("default"));
    }

    if (!m_voicemailConf || m_voicemailConf->key() != variablePath) {
        delete m_voicemailConf;
        m_voicemailConf = new MGConfItem(variablePath);
        connect(m_voicemailConf, SIGNAL(valueChanged()), this, SLOT(voicemailConfigurationChanged()));

        voicemailConfigurationChanged();
    }
}

