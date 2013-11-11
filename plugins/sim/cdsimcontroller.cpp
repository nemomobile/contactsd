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

CDSimController::CDSimController(QObject *parent, const QString &syncTarget)
    : QObject(parent)
    // Temporary override until qtpim supports QTCONTACTS_MANAGER_OVERRIDE
    , m_manager(QStringLiteral("org.nemomobile.contacts.sqlite"))
    , m_simPresent(false)
    , m_simSyncTarget(syncTarget)
    , m_busy(false)
    , m_voicemailConf(0)
{
    connect(&m_phonebook, SIGNAL(importReady(const QString &)),
            this, SLOT(vcardDataAvailable(const QString &)));
    connect(&m_phonebook, SIGNAL(importFailed()),
            this, SLOT(vcardReadFailed()));

    connect(&m_messageWaiting, SIGNAL(voicemailMailboxNumberChanged(const QString &)),
            this, SLOT(voicemailConfigurationChanged()));

    connect(&m_contactReader, SIGNAL(stateChanged(QVersitReader::State)),
            this, SLOT(readerStateChanged(QVersitReader::State)));

    // Resync the contacts list whenever the SIM is inserted/removed
    connect(&m_simManager, SIGNAL(presenceChanged(bool)),
            this, SLOT(simPresenceChanged(bool)));
}

CDSimController::~CDSimController()
{
}

QContactDetailFilter CDSimController::simSyncTargetFilter() const
{
    QContactDetailFilter syncTargetFilter;
    syncTargetFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    syncTargetFilter.setValue(m_simSyncTarget);
    return syncTargetFilter;
}

QContactManager &CDSimController::contactManager()
{
    return m_manager;
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

void CDSimController::simPresenceChanged(bool present)
{
    if (m_simPresent != present) {
        qDebug() << "SIM presence changed:" << present;
        m_simPresent = present;

        updateVoicemailConfiguration();

        if (m_simSyncTarget.isEmpty()) {
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
                removeAllSimContacts();
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
        m_simContacts.clear();
        removeAllSimContacts();
    } else {
        QVersitContactImporter importer;
        importer.importDocuments(results);
        m_simContacts = importer.contacts();
        if (m_simContacts.isEmpty()) {
            qDebug() << "No contacts imported from SIM data";
            removeAllSimContacts();
        } else {
            // import or remove contacts from local storage as necessary.
            ensureSimContactsPresent();
        }
    }

    setBusy(false);
}

void CDSimController::removeAllSimContacts()
{
    QList<QContactId> doomedIds = m_manager.contactIds(simSyncTargetFilter());
    if (doomedIds.size()) {
        if (!m_manager.removeContacts(doomedIds)) {
            qWarning() << "Error removing sim contacts from device storage";
        }
    }
}

void CDSimController::ensureSimContactsPresent()
{
    // Ensure all contacts from the SIM are present in the store
    QContactFetchHint hint;
    hint.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactNickname::Type << QContactPhoneNumber::Type);
    hint.setOptimizationHints(QContactFetchHint::NoRelationships | QContactFetchHint::NoActionPreferences | QContactFetchHint::NoBinaryBlobs);
    QList<QContact> allSimContacts = m_manager.contacts(simSyncTargetFilter(), QList<QContactSortOrder>(), hint);

    QMap<QString, QContact> existingContacts;
    foreach (const QContact &contact, allSimContacts) {
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
                    foreach (QContactPhoneNumber cphn, coalescedPhoneNumbers) {
                        // note: we don't check metadata (subtypes/contexts)
                        if (phn.number() == cphn.number()) {
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

        QMap<QString, QContact>::iterator it = existingContacts.find(displayLabel.label().trimmed());
        if (it != existingContacts.end()) {
            // Ensure this contact has the right phone numbers
            QContact &dbContact(*it);

            QMap<QString, QContactPhoneNumber> existingNumbers;
            foreach (const QContactPhoneNumber &phoneNumber, dbContact.details<QContactPhoneNumber>()) {
                existingNumbers.insert(phoneNumber.number(), phoneNumber);
            }

            bool modified = false;
            QList<QString> seenBeforeNumber;
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

                    seenBeforeNumber.append(phoneNumber.number());
                    existingNumbers.erase(nit);
                } else if (!seenBeforeNumber.contains(phoneNumber.number())) {
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
            nickname.setNickname(displayLabel.label().trimmed());
            simContact.saveDetail(&nickname);

            simContact.removeDetail(&displayLabel);

            QContactSyncTarget syncTarget = simContact.detail<QContactSyncTarget>();
            syncTarget.setSyncTarget(m_simSyncTarget);
            simContact.saveDetail(&syncTarget);

            importContacts.append(simContact);
        }
    }

    if (!importContacts.isEmpty()) {
        // Import any contacts which were modified or are not currently present
        if (!m_manager.saveContacts(&importContacts)) {
            qWarning() << "Error while saving imported sim contacts";
        }
    }


    if (!existingContacts.isEmpty()) {
        // Remove any imported contacts no longer on the SIM
        QList<QContactId> obsoleteIds;
        foreach (const QContact &contact, existingContacts.values()) {
            obsoleteIds.append(contact.id());
        }

        if (!m_manager.removeContacts(obsoleteIds)) {
            qWarning() << "Error while removing obsolete sim contacts";
        }
    }
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

