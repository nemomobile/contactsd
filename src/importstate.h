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

#ifndef IMPORTSTATE_H_
#define IMPORTSTATE_H_

#include <QMultiHash>
#include <QString>
#include <QStringList>
#include <QSettings>

class Q_DECL_EXPORT ImportState
{
public:
    ImportState();

    bool hasActiveImports();
    // make all account state as finished and reset the state
    void timeout();

    // reset the state
    void reset();

    QStringList activeImportingServices();

    // check if a service still has active importing accounts
    bool serviceHasActiveImports(const QString &service);

    void addImportingAccount(const QString &service, const QString &account);
    // return true if the account is removed successfully,
    // fales if the account doesn't exist
    bool removeImportingAccount(const QString &service, const QString &account,
                                int added, int removed, int merged);

    int contactsAdded();
    int contactsMerged();
    int contactsRemoved();

private:
    // each service may have multiple active importing accounts
    QMultiHash<QString, QString> mService2Accounts;
    // accumlated amount of contacts being added, merged, removed
    int mContactsAdded;
    int mContactsMerged;
    int mContactsRemoved;
    // store each account's import state
    QSettings mStateStore;
};

#endif // IMPORTSTATE_H_
